#include "riscv/asm_ir.hpp"
#include "riscv/asm_lower.hpp"
#include "riscv/asm_print.hpp"
#include "riscv/machine_ir.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using namespace riscv;

int run_llvm_mc_smoke(const std::string& text, const std::string& stem) {
    const auto dir = std::filesystem::temp_directory_path();
    const auto asm_path = dir / (stem + ".s");
    const auto obj_path = dir / (stem + ".o");

    {
        std::ofstream out(asm_path);
        out << text;
    }

    const auto cmd = "llvm-mc -triple riscv32 -filetype=obj " + asm_path.string() +
                     " -o " + obj_path.string();
    const int status = std::system(cmd.c_str());

    std::error_code ec;
    std::filesystem::remove(asm_path, ec);
    std::filesystem::remove(obj_path, ec);
    return status;
}

TEST(AsmLowerTest, LowersFrameBasedFunctionToStrictAsm) {
    MachineFunction fn{
        .symbol = "strict",
        .frame_objects =
            {
                FrameObject{
                    .id = 0,
                    .kind = FrameObjectKind::LocalSlot,
                    .size = 4,
                    .align = 4,
                    .debug_name = "tmp",
                    .materialized_offset = 0,
                },
                FrameObject{
                    .id = 1,
                    .kind = FrameObjectKind::CalleeSave,
                    .size = 4,
                    .align = 4,
                    .debug_name = "save.ra",
                    .callee_save_reg = PhysicalRegister::Ra,
                    .materialized_offset = 4,
                },
                FrameObject{
                    .id = 2,
                    .kind = FrameObjectKind::CalleeSave,
                    .size = 4,
                    .align = 4,
                    .debug_name = "save.s0",
                    .callee_save_reg = PhysicalRegister::S0,
                    .materialized_offset = 8,
                },
            },
        .blocks =
            {
                MachineBlock{
                    .id = 0,
                    .instructions =
                        {
                            Store{
                                .address = FrameAddress{.frame = 1, .offset = 0},
                                .src = PhysicalRegister::Ra,
                            },
                            Store{
                                .address = FrameAddress{.frame = 2, .offset = 0},
                                .src = PhysicalRegister::S0,
                            },
                            Li{
                                .dest = PhysicalRegister::S1,
                                .value = 5000,
                            },
                            Compare{
                                .dest = PhysicalRegister::S2,
                                .op = CompareOp::Eq,
                                .lhs = PhysicalRegister::S1,
                                .rhs = PhysicalRegister::S1,
                            },
                            Store{
                                .address = FrameAddress{.frame = 0, .offset = 0},
                                .src = PhysicalRegister::S2,
                            },
                            Load{
                                .dest = PhysicalRegister::A0,
                                .address = FrameAddress{.frame = 0, .offset = 0},
                            },
                            Load{
                                .dest = PhysicalRegister::S0,
                                .address = FrameAddress{.frame = 2, .offset = 0},
                            },
                            Load{
                                .dest = PhysicalRegister::Ra,
                                .address = FrameAddress{.frame = 1, .offset = 0},
                            },
                        },
                    .terminator = Return{.value = PhysicalRegister::A0},
                },
            },
        .entry_block = 0,
        .frame_base = FrameBase::S0,
        .frame_size = 16,
    };

    const auto asm_module = lower_to_asm(MachineModule{.functions = {fn}});
    ASSERT_EQ(asm_module.functions.size(), 1u);

    const auto asmir = to_string(asm_module);
    EXPECT_NE(asmir.find("afn @strict frame_size 16"), std::string::npos);
    EXPECT_NE(asmir.find("addi sp, sp, -16"), std::string::npos);
    EXPECT_NE(asmir.find("sw ra, 4(sp)"), std::string::npos);
    EXPECT_NE(asmir.find("sw s0, 8(sp)"), std::string::npos);
    EXPECT_NE(asmir.find("addi s0, sp, 0"), std::string::npos);
    EXPECT_NE(asmir.find("lui s1, 1"), std::string::npos);
    EXPECT_NE(asmir.find("addi s1, s1, 904"), std::string::npos);
    EXPECT_NE(asmir.find("xor s2, s1, s1"), std::string::npos);
    EXPECT_NE(asmir.find("sltiu s2, s2, 1"), std::string::npos);
    EXPECT_NE(asmir.find("lw s0, 8(sp)"), std::string::npos);
    EXPECT_NE(asmir.find("lw ra, 4(sp)"), std::string::npos);
    EXPECT_NE(asmir.find("addi sp, sp, 16"), std::string::npos);
    EXPECT_NE(asmir.find("jalr zero, 0(ra)"), std::string::npos);

    const auto text = to_gnu_as(asm_module);
    EXPECT_NE(text.find(".text"), std::string::npos);
    EXPECT_NE(text.find(".globl strict"), std::string::npos);
    EXPECT_NE(text.find("strict:"), std::string::npos);
}

TEST(AsmLowerTest, UsesFallthroughAwareBranchLowering) {
    MachineFunction fn{
        .symbol = "branchy",
        .blocks =
            {
                MachineBlock{
                    .id = 0,
                    .terminator = BranchNonZero{
                        .condition = PhysicalRegister::S1,
                        .then_block = 1,
                        .else_block = 2,
                    },
                },
                MachineBlock{
                    .id = 1,
                    .terminator = Return{.value = PhysicalRegister::A0},
                },
                MachineBlock{
                    .id = 2,
                    .terminator = Return{.value = PhysicalRegister::A1},
                },
            },
        .entry_block = 0,
    };

    const auto text = to_string(lower_to_asm(MachineModule{.functions = {fn}}));
    EXPECT_NE(text.find("bne s1, zero, .Lbranchy_bb1"), std::string::npos);
    EXPECT_EQ(text.find("jal zero, .Lbranchy_bb2"), std::string::npos);
}

TEST(AsmLowerTest, InjectsInlineBuiltinRuntimeHelpersOnDemand) {
    MachineFunction print_fn{
        .symbol = "caller_print",
        .blocks =
            {
                MachineBlock{
                    .id = 0,
                    .instructions =
                        {
                            Copy{
                                .dest = PhysicalRegister::A0,
                                .src = PhysicalRegister::A1,
                            },
                            Call{.callee = "__rcomp_printlnInt"},
                            Call{.callee = "__rcomp_exit"},
                        },
                    .terminator = Return{},
                },
            },
        .entry_block = 0,
    };

    MachineFunction input_fn{
        .symbol = "caller_input",
        .blocks =
            {
                MachineBlock{
                    .id = 0,
                    .instructions =
                        {
                            Call{.callee = "__rcomp_getInt"},
                        },
                    .terminator = Return{.value = PhysicalRegister::A0},
                },
            },
        .entry_block = 0,
    };

    const auto asm_module = lower_to_asm(MachineModule{.functions = {print_fn, input_fn}});
    const auto text = to_gnu_as(asm_module);

    EXPECT_NE(text.find(".globl __rcomp_printInt"), std::string::npos);
    EXPECT_NE(text.find(".globl __rcomp_printlnInt"), std::string::npos);
    EXPECT_NE(text.find(".globl __rcomp_getInt"), std::string::npos);
    EXPECT_NE(text.find(".globl __rcomp_exit"), std::string::npos);
    EXPECT_NE(text.find("call __rcomp_printInt"), std::string::npos);
    EXPECT_NE(text.find("call putchar"), std::string::npos);
    EXPECT_NE(text.find("call getchar"), std::string::npos);
    EXPECT_NE(text.find("jalr zero, 4(zero)"), std::string::npos);
}

TEST(AsmLowerTest, LowersLargeFrameAdjustmentsWithoutIllegalAddi) {
    MachineFunction fn{
        .symbol = "large_frame",
        .frame_objects =
            {
                FrameObject{
                    .id = 0,
                    .kind = FrameObjectKind::LocalSlot,
                    .size = 4,
                    .align = 4,
                    .debug_name = "tmp",
                    .materialized_offset = 0,
                },
            },
        .blocks =
            {
                MachineBlock{
                    .id = 0,
                    .instructions =
                        {
                            Load{
                                .dest = PhysicalRegister::A0,
                                .address = FrameAddress{.frame = 0, .offset = 0},
                            },
                        },
                    .terminator = Return{.value = PhysicalRegister::A0},
                },
            },
        .entry_block = 0,
        .frame_base = FrameBase::S0,
        .frame_size = 4096,
    };

    const auto asmir = to_string(lower_to_asm(MachineModule{.functions = {fn}}));
    EXPECT_NE(asmir.find("lui t2, 1048575"), std::string::npos);
    EXPECT_NE(asmir.find("add t2, sp, t2"), std::string::npos);
    EXPECT_NE(asmir.find("addi sp, t2, 0"), std::string::npos);
    EXPECT_NE(asmir.find("lui t2, 1"), std::string::npos);
    EXPECT_EQ(asmir.find("addi sp, sp, -4096"), std::string::npos);
    EXPECT_EQ(asmir.find("addi sp, sp, 4096"), std::string::npos);
}

TEST(AsmLowerTest, GeneratedGnuAsParsesWithLlvmMc) {
    if (std::system("llvm-mc --version > /dev/null 2>&1") != 0) {
        GTEST_SKIP() << "llvm-mc not available";
    }

    MachineFunction fn{
        .symbol = "asm_parse_smoke",
        .blocks =
            {
                MachineBlock{
                    .id = 0,
                    .instructions =
                        {
                            Call{.callee = "foo"},
                        },
                    .terminator = Return{},
                },
            },
        .entry_block = 0,
        .frame_size = 4096,
    };

    const auto text = to_gnu_as(lower_to_asm(MachineModule{.functions = {fn}}));
    EXPECT_EQ(run_llvm_mc_smoke(text, "rcomp_test_asm_lower_smoke"), 0);
}

} // namespace
