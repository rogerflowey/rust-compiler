#include "riscv/machine_ir.hpp"
#include "riscv/prologue_epilogue.hpp"
#include "riscv/validate.hpp"

#include <gtest/gtest.h>

namespace {

using namespace riscv;

const FrameObject* find_frame(const MachineFunction& fn, FrameId id) {
    for (const auto& object : fn.frame_objects) {
        if (object.id == id) {
            return &object;
        }
    }
    return nullptr;
}

TEST(PrologueEpilogueTest, AppendsSaveSlotsAndWrapsReturns) {
    MachineFunction fn{
        .symbol = "proepi",
        .frame_objects =
            {
                FrameObject{
                    .id = 0,
                    .kind = FrameObjectKind::LocalSlot,
                    .size = 4,
                    .align = 4,
                    .debug_name = "x",
                },
            },
        .blocks =
            {
                MachineBlock{
                    .id = 0,
                    .instructions =
                        {
                            Li{.dest = PhysicalRegister::S1, .value = 7},
                            Call{.callee = "callee"},
                        },
                    .terminator = Return{.value = PhysicalRegister::A0},
                },
            },
        .entry_block = 0,
    };

    insert_prologue_epilogue(fn);

    ASSERT_TRUE(fn.needs_frame_pointer);
    ASSERT_EQ(fn.frame_objects.size(), 4u);
    EXPECT_EQ(find_frame(fn, 1)->callee_save_reg, PhysicalRegister::Ra);
    EXPECT_EQ(find_frame(fn, 2)->callee_save_reg, PhysicalRegister::S0);
    EXPECT_EQ(find_frame(fn, 3)->callee_save_reg, PhysicalRegister::S1);

    const auto& block = fn.blocks.front();
    ASSERT_EQ(block.instructions.size(), 8u);

    const auto* save_ra = std::get_if<Store>(&block.instructions[0]);
    ASSERT_NE(save_ra, nullptr);
    EXPECT_EQ(std::get<FrameAddress>(save_ra->address).frame, 1u);
    EXPECT_EQ(std::get<PhysicalRegister>(save_ra->src), PhysicalRegister::Ra);

    const auto* save_s0 = std::get_if<Store>(&block.instructions[1]);
    ASSERT_NE(save_s0, nullptr);
    EXPECT_EQ(std::get<FrameAddress>(save_s0->address).frame, 2u);
    EXPECT_EQ(std::get<PhysicalRegister>(save_s0->src), PhysicalRegister::S0);

    const auto* save_s1 = std::get_if<Store>(&block.instructions[2]);
    ASSERT_NE(save_s1, nullptr);
    EXPECT_EQ(std::get<FrameAddress>(save_s1->address).frame, 3u);
    EXPECT_EQ(std::get<PhysicalRegister>(save_s1->src), PhysicalRegister::S1);

    EXPECT_TRUE(std::holds_alternative<Li>(block.instructions[3]));
    EXPECT_TRUE(std::holds_alternative<Call>(block.instructions[4]));

    const auto* restore_s1 = std::get_if<Load>(&block.instructions[5]);
    ASSERT_NE(restore_s1, nullptr);
    EXPECT_EQ(std::get<PhysicalRegister>(restore_s1->dest), PhysicalRegister::S1);
    EXPECT_EQ(std::get<FrameAddress>(restore_s1->address).frame, 3u);

    const auto* restore_s0 = std::get_if<Load>(&block.instructions[6]);
    ASSERT_NE(restore_s0, nullptr);
    EXPECT_EQ(std::get<PhysicalRegister>(restore_s0->dest), PhysicalRegister::S0);
    EXPECT_EQ(std::get<FrameAddress>(restore_s0->address).frame, 2u);

    const auto* restore_ra = std::get_if<Load>(&block.instructions[7]);
    ASSERT_NE(restore_ra, nullptr);
    EXPECT_EQ(std::get<PhysicalRegister>(restore_ra->dest), PhysicalRegister::Ra);
    EXPECT_EQ(std::get<FrameAddress>(restore_ra->address).frame, 1u);

    EXPECT_NO_THROW(validate_module(MachineModule{.functions = {fn}},
                                    ValidationStage::PostPhiElim));
}

TEST(PrologueEpilogueTest, RejectsUnexpectedExistingCalleeSaveSlot) {
    MachineFunction fn{
        .symbol = "bad",
        .frame_objects =
            {
                FrameObject{
                    .id = 0,
                    .kind = FrameObjectKind::CalleeSave,
                    .size = 4,
                    .align = 4,
                    .debug_name = "save.s9",
                    .callee_save_reg = PhysicalRegister::S9,
                },
            },
        .blocks =
            {
                MachineBlock{
                    .id = 0,
                    .terminator = Return{},
                },
            },
        .entry_block = 0,
    };

    EXPECT_THROW(insert_prologue_epilogue(fn), PrologueEpilogueError);
}

} // namespace
