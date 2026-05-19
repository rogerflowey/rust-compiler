#include "riscv/layout.hpp"
#include "riscv/lower.hpp"
#include "riscv/machine_ir.hpp"
#include "riscv/pretty_print.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/type/type.hpp"

#include <bit>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

semantic::TypeId i32_type() {
    return semantic::get_typeID(semantic::Type{semantic::PrimitiveKind::I32});
}

semantic::TypeId ref_i32_type() {
    return semantic::get_typeID(semantic::Type{
        semantic::ReferenceType{.referenced_type = i32_type(), .is_mutable = false}});
}

semantic::TypeId array3_i32_type() {
    return semantic::get_typeID(
        semantic::Type{semantic::ArrayType{.element_type = i32_type(), .size = 3}});
}

} // namespace

TEST(RiscvLayoutTest, ComputesRv32StructAndArrayLayout) {
    hir::StructDef point_def;
    point_def.name = ast::Identifier{"Point"};
    point_def.fields = {
        semantic::Field{.name = ast::Identifier{"x"}, .type = i32_type()},
        semantic::Field{.name = ast::Identifier{"buf"}, .type = array3_i32_type()},
        semantic::Field{.name = ast::Identifier{"next"}, .type = ref_i32_type()},
    };

    const auto point_type =
        semantic::get_typeID(semantic::Type{semantic::StructType{.symbol = &point_def}});

    EXPECT_EQ(riscv::size_of(i32_type()), 4u);
    EXPECT_EQ(riscv::size_of(ref_i32_type()), 4u);
    EXPECT_EQ(riscv::array_stride(array3_i32_type()), 4u);
    EXPECT_EQ(riscv::field_offset(point_type, 0), 0u);
    EXPECT_EQ(riscv::field_offset(point_type, 1), 4u);
    EXPECT_EQ(riscv::field_offset(point_type, 2), 16u);
    EXPECT_EQ(riscv::size_of(point_type), 20u);
    EXPECT_EQ(riscv::align_of(point_type), 4u);
}

TEST(RiscvMachineIrTest, PrintsSimpleFunction) {
    riscv::MachineFunction function{
        .symbol = "add1",
        .frame_objects = {
            riscv::FrameObject{
                .id = 0,
                .kind = riscv::FrameObjectKind::LocalSlot,
                .size = 4,
                .align = 4,
                .host_type = i32_type(),
                .spill_class = std::nullopt,
                .source_slot = 0,
                .debug_name = "x",
            },
        },
        .blocks = {
            riscv::MachineBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    riscv::Copy{
                        .dest = riscv::VirtualRegister{.id = 0},
                        .src = riscv::PhysicalRegister::A0,
                    },
                    riscv::Li{.dest = riscv::VirtualRegister{.id = 1}, .value = 1},
                    riscv::Binary{
                        .dest = riscv::VirtualRegister{.id = 2},
                        .op = riscv::BinaryOp::Add,
                        .lhs = riscv::VirtualRegister{.id = 0},
                        .rhs = riscv::VirtualRegister{.id = 1},
                    },
                    riscv::Store{
                        .address = riscv::FrameAddress{.frame = 0, .offset = 0},
                        .src = riscv::VirtualRegister{.id = 2},
                    },
                    riscv::Load{
                        .dest = riscv::VirtualRegister{.id = 3},
                        .address = riscv::FrameAddress{.frame = 0, .offset = 0},
                    },
                },
                .terminator =
                    riscv::Return{.value = riscv::VirtualRegister{.id = 3}},
            },
        },
        .entry_block = 0,
        .next_value = 4,
    };

    const riscv::MachineModule module{.functions = {function}};
    const auto text = riscv::to_string(module);
    EXPECT_NE(text.find("mfn @add1"), std::string::npos);
    EXPECT_NE(text.find("fi0: slot i32 size 4 align 4 x"), std::string::npos);
    EXPECT_NE(text.find("v0 = copy a0"), std::string::npos);
    EXPECT_NE(text.find("v2 = add v0, v1"), std::string::npos);
    EXPECT_NE(text.find("store [fi0 + 0], v2"), std::string::npos);
    EXPECT_NE(text.find("ret v3"), std::string::npos);
}

TEST(RiscvMachineIrLoweringTest, LowersScalarFunctionEntryAndReturnAbi) {
    ir3::Function function{
        .symbol = "add",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "a",
                .host_type = i32_type(),
            },
            ir3::Param{
                .value = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                .name = "b",
                .host_type = i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {},
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .phis = {},
                .instructions = {
                    ir3::Binary{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .op = ir3::BinaryOp::SAdd,
                        .lhs = 0,
                        .rhs = 1,
                    },
                },
                .terminator = ir3::Return{.value = 2},
            },
        },
        .entry_block = 0,
        .next_value = 3,
    };

    const auto module = riscv::lower_module(ir3::Module{.functions = {function}});

    ASSERT_EQ(module.functions.size(), 1u);
    const auto text = riscv::to_string(module);
    EXPECT_NE(text.find("v0 = copy a0"), std::string::npos);
    EXPECT_NE(text.find("v1 = copy a1"), std::string::npos);
    EXPECT_NE(text.find("v2 = add v0, v1"), std::string::npos);
    EXPECT_NE(text.find("copy a0, v2"), std::string::npos);
    EXPECT_NE(text.find("ret a0"), std::string::npos);
}

TEST(RiscvMachineIrLoweringTest, AcceptsFullRv32BitPatternConstants) {
    ir3::Function function{
        .symbol = "u32_bits",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {},
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .phis = {},
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 2166136261LL,
                    },
                },
                .terminator = ir3::Return{.value = 0},
            },
        },
        .entry_block = 0,
        .next_value = 1,
    };

    const auto module = riscv::lower_module(ir3::Module{.functions = {function}});
    ASSERT_EQ(module.functions.size(), 1u);
    ASSERT_EQ(module.functions.front().blocks.size(), 1u);
    ASSERT_EQ(module.functions.front().blocks.front().instructions.size(), 2u);

    const auto* li =
        std::get_if<riscv::Li>(&module.functions.front().blocks.front().instructions[0]);
    ASSERT_NE(li, nullptr);
    EXPECT_EQ(li->value, std::bit_cast<std::int32_t>(2166136261u));
}

TEST(RiscvMachineIrLoweringTest, PreservesPhiNodesInSsaMir) {
    // Same CFG as the old EliminatesPhiBySplittingCriticalEdges test:
    //   bb0 --(cond0)--> bb1, bb2
    //   bb1 --(cond1)--> bb3, bb4
    //   bb2 -----------> bb3
    //   bb3: phi(v4) = [bb1: v2, bb2: v3]
    //   bb4: return v2
    // After lowering to MIR SSA form, bb3 must contain a MachinePhi for v4.
    // No critical-edge splitting should happen at this stage.
    ir3::Function function{
        .symbol = "phi_branch",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "cond0",
                .host_type = i32_type(),
            },
            ir3::Param{
                .value = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                .name = "cond1",
                .host_type = i32_type(),
            },
            ir3::Param{
                .value = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                .name = "x",
                .host_type = i32_type(),
            },
            ir3::Param{
                .value = ir3::Value{.id = 3, .klass = ir3::SsaClass::I32},
                .name = "y",
                .host_type = i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {},
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .phis = {},
                .instructions = {},
                .terminator = ir3::Branch{
                    .condition = 0,
                    .then_block = 1,
                    .else_block = 2,
                },
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "bb1",
                .phis = {},
                .instructions = {},
                .terminator = ir3::Branch{
                    .condition = 1,
                    .then_block = 3,
                    .else_block = 4,
                },
            },
            ir3::BasicBlock{
                .id = 2,
                .name = "bb2",
                .phis = {},
                .instructions = {},
                .terminator = ir3::Jump{.target = 3},
            },
            ir3::BasicBlock{
                .id = 3,
                .name = "bb3",
                .phis = {
                    ir3::Phi{
                        .result = ir3::Value{.id = 4, .klass = ir3::SsaClass::I32},
                        .incoming = {
                            ir3::PhiIncoming{.pred = 1, .value = 2},
                            ir3::PhiIncoming{.pred = 2, .value = 3},
                        },
                    },
                },
                .instructions = {},
                .terminator = ir3::Return{.value = 4},
            },
            ir3::BasicBlock{
                .id = 4,
                .name = "bb4",
                .phis = {},
                .instructions = {},
                .terminator = ir3::Return{.value = 2},
            },
        },
        .entry_block = 0,
        .next_value = 5,
    };

    const auto module = riscv::lower_module(ir3::Module{.functions = {function}});
    ASSERT_EQ(module.functions.size(), 1u);
    const auto& lowered = module.functions.front();

    // MIR SSA form: no critical-edge splitting, phi nodes preserved.
    ASSERT_EQ(lowered.blocks.size(), 5u);

    // bb3 (index 3) must have exactly one MachinePhi for v4.
    const auto& bb3 = lowered.blocks[3];
    ASSERT_EQ(bb3.phis.size(), 1u);
    const auto* phi_dest = std::get_if<riscv::VirtualRegister>(&bb3.phis[0].dest);
    ASSERT_NE(phi_dest, nullptr);
    EXPECT_EQ(phi_dest->id, 4u);
    ASSERT_EQ(bb3.phis[0].incoming.size(), 2u);

    // The printed form should contain the phi.
    const auto text = riscv::to_string(module);
    EXPECT_NE(text.find("v4 = phi ["), std::string::npos);
}

TEST(RiscvMachineIrLoweringTest, LowersCallsWithOverflowArguments) {
    ir3::Function function{
        .symbol = "call9",
        .params = {},
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {},
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .phis = {},
                .instructions = {
                    ir3::IConst{.result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                                .value = 0},
                    ir3::IConst{.result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                                .value = 1},
                    ir3::IConst{.result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                                .value = 2},
                    ir3::IConst{.result = ir3::Value{.id = 3, .klass = ir3::SsaClass::I32},
                                .value = 3},
                    ir3::IConst{.result = ir3::Value{.id = 4, .klass = ir3::SsaClass::I32},
                                .value = 4},
                    ir3::IConst{.result = ir3::Value{.id = 5, .klass = ir3::SsaClass::I32},
                                .value = 5},
                    ir3::IConst{.result = ir3::Value{.id = 6, .klass = ir3::SsaClass::I32},
                                .value = 6},
                    ir3::IConst{.result = ir3::Value{.id = 7, .klass = ir3::SsaClass::I32},
                                .value = 7},
                    ir3::IConst{.result = ir3::Value{.id = 8, .klass = ir3::SsaClass::I32},
                                .value = 8},
                    ir3::Call{
                        .result = ir3::Value{.id = 9, .klass = ir3::SsaClass::I32},
                        .callee = "callee9",
                        .args = {0, 1, 2, 3, 4, 5, 6, 7, 8},
                    },
                },
                .terminator = ir3::Return{.value = 9},
            },
        },
        .entry_block = 0,
        .next_value = 10,
    };

    const auto module = riscv::lower_module(ir3::Module{.functions = {function}});
    ASSERT_EQ(module.functions.size(), 1u);
    const auto& lowered = module.functions.front();
    ASSERT_EQ(lowered.frame_objects.size(), 1u);
    EXPECT_EQ(lowered.frame_objects.front().kind, riscv::FrameObjectKind::OutgoingArg);
    EXPECT_EQ(lowered.frame_objects.front().size, 16u);
    EXPECT_EQ(lowered.frame_objects.front().align, 16u);

    const auto text = riscv::to_string(module);
    EXPECT_NE(text.find("copy a0, v0"), std::string::npos);
    EXPECT_NE(text.find("copy a7, v7"), std::string::npos);
    EXPECT_NE(text.find("store [fi0 + 0], v8"), std::string::npos);
    EXPECT_NE(text.find("call @callee9"), std::string::npos);
    EXPECT_NE(text.find("v9 = copy a0"), std::string::npos);
}

TEST(RiscvMachineIrLoweringTest, RejectsUnsupportedReservedBuiltinRuntimeSymbols) {
    ir3::Function function{
        .symbol = "builtin_runtime_error",
        .params = {},
        .return_class = std::nullopt,
        .source_return_type = semantic::get_typeID(semantic::Type{semantic::UnitType{}}),
        .slots = {},
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .phis = {},
                .instructions = {
                    ir3::Call{
                        .result = std::nullopt,
                        .callee = "__rcomp_builtin_print",
                        .args = {},
                    },
                },
                .terminator = ir3::Return{.value = std::nullopt},
            },
        },
        .entry_block = 0,
        .next_value = 0,
    };

    EXPECT_THROW((void)riscv::lower_module(ir3::Module{.functions = {function}}),
                 riscv::LoweringError);
}
