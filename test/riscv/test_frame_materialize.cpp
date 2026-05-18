#include "riscv/frame_materialize.hpp"
#include "riscv/machine_ir.hpp"
#include "riscv/prologue_epilogue.hpp"
#include "riscv/pretty_print.hpp"
#include "riscv/validate.hpp"

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
    EXPECT_TRUE(fn.needs_frame_pointer);

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

    EXPECT_NO_THROW(validate_module(MachineModule{.functions = {fn}},
                                    ValidationStage::PostFrameMaterialized));
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
    EXPECT_TRUE(fn.needs_frame_pointer);
    EXPECT_EQ(find_frame(fn, 0)->materialized_offset, 16);
    EXPECT_EQ(find_frame(fn, 1)->materialized_offset, 20);
    EXPECT_EQ(find_frame(fn, 2)->callee_save_reg, PhysicalRegister::S0);
    EXPECT_EQ(find_frame(fn, 3)->callee_save_reg, PhysicalRegister::S2);
    EXPECT_EQ(find_frame(fn, 4)->callee_save_reg, PhysicalRegister::S3);
}

TEST(FrameMaterializeTest, RejectsPrePhiInput) {
    MachineFunction fn{
        .symbol = "bad",
        .blocks =
            {
                MachineBlock{
                    .id = 0,
                    .phis =
                        {
                            MachinePhi{
                                .dest = PhysicalRegister::S1,
                                .incoming = {
                                    MachinePhiIncoming{
                                        .pred = 0,
                                        .value = PhysicalRegister::S1,
                                    },
                                },
                            },
                        },
                    .terminator = Return{},
                },
            },
        .entry_block = 0,
    };

    EXPECT_THROW(materialize_frame(fn), FrameMaterializeError);
}

TEST(FrameMaterializeTest, RejectsOpenFrameWithoutPrologueEpiloguePass) {
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

    EXPECT_THROW(materialize_frame(fn), FrameMaterializeError);
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
    EXPECT_NE(text.find("frame: size 16 fp yes"), std::string::npos);
    EXPECT_NE(text.find("offset 0"), std::string::npos);
    EXPECT_NE(text.find("callee_save s0 size 4 align 4 offset 4"), std::string::npos);
}

} // namespace
