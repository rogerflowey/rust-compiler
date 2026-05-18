#include "riscv/frame_materialize.hpp"
#include "riscv/machine_ir.hpp"
#include "riscv/prologue_epilogue.hpp"
#include "riscv/pretty_print.hpp"
#include <gtest/gtest.h>

#include <string>

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

TEST(FrameMaterializeTest, AssignsClosedFrameOffsetsInDocumentOrder) {
    MachineFunction fn{
        .symbol = "layout",
        .frame_objects =
            {
                FrameObject{
                    .id = 0,
                    .kind = FrameObjectKind::LocalSlot,
                    .size = 4,
                    .align = 4,
                    .debug_name = "x",
                },
                FrameObject{
                    .id = 1,
                    .kind = FrameObjectKind::OutgoingArg,
                    .size = 16,
                    .align = 16,
                    .debug_name = "outgoing",
                },
                FrameObject{
                    .id = 2,
                    .kind = FrameObjectKind::Spill,
                    .size = 4,
                    .align = 4,
                    .spill_class = RegisterClass::Gpr32,
                    .debug_name = "spill0",
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
                    .terminator = Return{},
                },
            },
        .entry_block = 0,
    };

    insert_prologue_epilogue(fn);
    materialize_frame(fn);

    ASSERT_TRUE(fn.frame_size.has_value());
    EXPECT_EQ(*fn.frame_size, 48u);
    ASSERT_EQ(fn.frame_base, FrameBase::S0);

    ASSERT_NE(find_frame(fn, 1), nullptr);
    ASSERT_NE(find_frame(fn, 0), nullptr);
    ASSERT_NE(find_frame(fn, 2), nullptr);
    EXPECT_EQ(find_frame(fn, 1)->materialized_offset, 0);
    EXPECT_EQ(find_frame(fn, 0)->materialized_offset, 16);
    EXPECT_EQ(find_frame(fn, 2)->materialized_offset, 20);

    ASSERT_EQ(fn.frame_objects.size(), 6u);
    EXPECT_EQ(find_frame(fn, 3)->kind, FrameObjectKind::CalleeSave);
    EXPECT_EQ(find_frame(fn, 3)->callee_save_reg, PhysicalRegister::Ra);
    EXPECT_EQ(find_frame(fn, 3)->materialized_offset, 24);
    EXPECT_EQ(find_frame(fn, 4)->callee_save_reg, PhysicalRegister::S0);
    EXPECT_EQ(find_frame(fn, 4)->materialized_offset, 28);
    EXPECT_EQ(find_frame(fn, 5)->callee_save_reg, PhysicalRegister::S1);
    EXPECT_EQ(find_frame(fn, 5)->materialized_offset, 32);

}

TEST(FrameMaterializeTest, PlacesIncomingArgsAboveAlignedFrame) {
    MachineFunction fn{
        .symbol = "incoming",
        .frame_objects =
            {
                FrameObject{
                    .id = 0,
                    .kind = FrameObjectKind::IncomingArg,
                    .size = 4,
                    .align = 4,
                    .debug_name = "arg8.stack",
                },
                FrameObject{
                    .id = 1,
                    .kind = FrameObjectKind::IncomingArg,
                    .size = 4,
                    .align = 4,
                    .debug_name = "arg9.stack",
                },
            },
        .blocks =
            {
                MachineBlock{
                    .id = 0,
                    .instructions =
                        {
                            Load{
                                .dest = PhysicalRegister::S2,
                                .address = FrameAddress{.frame = 0, .offset = 0},
                            },
                            Load{
                                .dest = PhysicalRegister::S3,
                                .address = FrameAddress{.frame = 1, .offset = 0},
                            },
                        },
                    .terminator = Return{.value = PhysicalRegister::S2},
                },
            },
        .entry_block = 0,
    };

    insert_prologue_epilogue(fn);
    materialize_frame(fn);

    ASSERT_TRUE(fn.frame_size.has_value());
    EXPECT_EQ(*fn.frame_size, 16u);
    ASSERT_EQ(fn.frame_base, FrameBase::S0);
    EXPECT_EQ(find_frame(fn, 0)->materialized_offset, 16);
    EXPECT_EQ(find_frame(fn, 1)->materialized_offset, 20);
    EXPECT_EQ(find_frame(fn, 2)->callee_save_reg, PhysicalRegister::S0);
    EXPECT_EQ(find_frame(fn, 3)->callee_save_reg, PhysicalRegister::S2);
    EXPECT_EQ(find_frame(fn, 4)->callee_save_reg, PhysicalRegister::S3);
}

TEST(FrameMaterializeTest, MaterializesFrameWithoutPrologueEpiloguePass) {
    MachineFunction fn{
        .symbol = "open",
        .frame_objects =
            {
                FrameObject{
                    .id = 0,
                    .kind = FrameObjectKind::LocalSlot,
                    .size = 4,
                    .align = 4,
                    .debug_name = "tmp",
                },
            },
        .blocks =
            {
                MachineBlock{
                    .id = 0,
                    .instructions =
                        {
                            Li{.dest = PhysicalRegister::S1, .value = 1},
                            Call{.callee = "callee"},
                        },
                    .terminator = Return{},
                },
            },
        .entry_block = 0,
    };

    EXPECT_NO_THROW(materialize_frame(fn));
    ASSERT_TRUE(fn.frame_size.has_value());
    EXPECT_EQ(*fn.frame_size, 16u);
    EXPECT_EQ(find_frame(fn, 0)->materialized_offset, 0);
}

TEST(FrameMaterializeTest, OrdersCalleeSaveSlotsByCanonicalRegisterOrder) {
    MachineFunction fn{
        .symbol = "save_order",
        .frame_objects =
            {
                FrameObject{
                    .id = 0,
                    .kind = FrameObjectKind::LocalSlot,
                    .size = 4,
                    .align = 4,
                    .debug_name = "tmp",
                },
                FrameObject{
                    .id = 1,
                    .kind = FrameObjectKind::CalleeSave,
                    .size = 4,
                    .align = 4,
                    .debug_name = "save.s3",
                    .callee_save_reg = PhysicalRegister::S3,
                },
                FrameObject{
                    .id = 2,
                    .kind = FrameObjectKind::CalleeSave,
                    .size = 4,
                    .align = 4,
                    .debug_name = "save.ra",
                    .callee_save_reg = PhysicalRegister::Ra,
                },
                FrameObject{
                    .id = 3,
                    .kind = FrameObjectKind::CalleeSave,
                    .size = 4,
                    .align = 4,
                    .debug_name = "save.s0",
                    .callee_save_reg = PhysicalRegister::S0,
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
        .frame_base = FrameBase::S0,
    };

    materialize_frame(fn);

    ASSERT_TRUE(fn.frame_size.has_value());
    EXPECT_EQ(find_frame(fn, 0)->materialized_offset, 0);
    EXPECT_EQ(find_frame(fn, 2)->materialized_offset, 4);
    EXPECT_EQ(find_frame(fn, 3)->materialized_offset, 8);
    EXPECT_EQ(find_frame(fn, 1)->materialized_offset, 12);
}

TEST(FrameMaterializeTest, PrettyPrintShowsMaterializedOffsets) {
    MachineFunction fn{
        .symbol = "pretty",
        .frame_objects =
            {
                FrameObject{
                    .id = 0,
                    .kind = FrameObjectKind::LocalSlot,
                    .size = 4,
                    .align = 4,
                    .debug_name = "tmp",
                },
            },
        .blocks =
            {
                MachineBlock{
                    .id = 0,
                    .instructions = {
                        Li{.dest = PhysicalRegister::S1, .value = 1},
                    },
                    .terminator = Return{},
                },
            },
        .entry_block = 0,
    };

    insert_prologue_epilogue(fn);
    materialize_frame(fn);
    const auto text = to_string(MachineModule{.functions = {fn}});
    EXPECT_NE(text.find("frame: size 16 base s0"), std::string::npos);
    EXPECT_NE(text.find("offset 0"), std::string::npos);
    EXPECT_NE(text.find("callee_save s0 size 4 align 4 offset 4"), std::string::npos);
}

} // namespace
