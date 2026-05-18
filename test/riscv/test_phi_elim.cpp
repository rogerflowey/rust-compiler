#include "riscv/machine_ir.hpp"
#include "riscv/phi_elim.hpp"
#include "riscv/pretty_print.hpp"
#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace riscv;

PhysicalRegister S(int index) {
    switch (index) {
    case 1:
        return PhysicalRegister::S1;
    case 2:
        return PhysicalRegister::S2;
    case 3:
        return PhysicalRegister::S3;
    case 4:
        return PhysicalRegister::S4;
    case 5:
        return PhysicalRegister::S5;
    case 6:
        return PhysicalRegister::S6;
    default:
        throw std::runtime_error("unsupported S-register index in phi-elim test");
    }
}

SpillRef SP(FrameId frame) {
    return SpillRef{.frame = frame, .reg_class = RegisterClass::Gpr32};
}

FrameObject spill_frame(FrameId id) {
    return FrameObject{
        .id = id,
        .kind = FrameObjectKind::Spill,
        .size = 4,
        .align = 4,
        .host_type = semantic::invalid_type_id,
        .spill_class = RegisterClass::Gpr32,
        .source_slot = std::nullopt,
        .debug_name = "",
        .callee_save_reg = std::nullopt,
        .materialized_offset = std::nullopt,
    };
}

MachineBlock block(BlockId id,
                   Terminator terminator,
                   std::vector<MachinePhi> phis = {},
                   std::vector<Instruction> instructions = {}) {
    return MachineBlock{
        .id = id,
        .name = "",
        .phis = std::move(phis),
        .instructions = std::move(instructions),
        .terminator = std::move(terminator),
    };
}

void eliminate(MachineFunction& fn) {
    MachineModule module{.functions = {fn}};
    eliminate_phis(module);
    fn = std::move(module.functions.front());
}

bool instruction_holds_copy(const Instruction& inst,
                            PhysicalRegister dest,
                            PhysicalRegister src) {
    const auto* copy = std::get_if<Copy>(&inst);
    if (!copy) {
        return false;
    }
    const auto* copy_dest = std::get_if<PhysicalRegister>(&copy->dest);
    const auto* copy_src = std::get_if<PhysicalRegister>(&copy->src);
    return copy_dest && copy_src && *copy_dest == dest && *copy_src == src;
}

bool contains_spill_ref_text(const MachineFunction& fn) {
    return to_string(MachineModule{.functions = {fn}}).find("spill(fi") != std::string::npos;
}

TEST(PhiElimTest, InsertsCopiesOnSingleSuccessorPredecessors) {
    MachineFunction fn;
    fn.symbol = "single_succ";
    fn.entry_block = 0;
    fn.blocks = {
        block(0, BranchNonZero{.condition = S(2), .then_block = 1, .else_block = 2}),
        block(1, Jump{.target = 3}),
        block(2, Jump{.target = 3}),
        block(3,
              Return{.value = S(1)},
              {
                  MachinePhi{
                      .dest = S(1),
                      .incoming = {
                          MachinePhiIncoming{.pred = 1, .value = S(4)},
                          MachinePhiIncoming{.pred = 2, .value = S(5)},
                      },
                  },
              }),
    };

    eliminate(fn);

    ASSERT_TRUE(fn.blocks[3].phis.empty());
    ASSERT_EQ(fn.blocks[1].instructions.size(), 1u);
    ASSERT_EQ(fn.blocks[2].instructions.size(), 1u);
    EXPECT_TRUE(instruction_holds_copy(fn.blocks[1].instructions[0], S(1), S(4)));
    EXPECT_TRUE(instruction_holds_copy(fn.blocks[2].instructions[0], S(1), S(5)));
}

TEST(PhiElimTest, SplitsCriticalEdgesOnlyWhenNeeded) {
    MachineFunction fn;
    fn.symbol = "critical_edge";
    fn.entry_block = 0;
    fn.blocks = {
        block(0, BranchNonZero{.condition = S(1), .then_block = 1, .else_block = 2}),
        block(1, BranchNonZero{.condition = S(2), .then_block = 3, .else_block = 4}),
        block(2, Jump{.target = 3}),
        block(3,
              Return{.value = S(3)},
              {
                  MachinePhi{
                      .dest = S(3),
                      .incoming = {
                          MachinePhiIncoming{.pred = 1, .value = S(4)},
                          MachinePhiIncoming{.pred = 2, .value = S(5)},
                      },
                  },
              }),
        block(4, Return{.value = S(4)}),
    };

    eliminate(fn);

    ASSERT_EQ(fn.blocks.size(), 6u);
    const auto* branch = std::get_if<BranchNonZero>(&*fn.blocks[1].terminator);
    ASSERT_NE(branch, nullptr);
    EXPECT_NE(branch->then_block, 3u);
    EXPECT_EQ(branch->else_block, 4u);

    const auto& edge_block = fn.blocks.back();
    ASSERT_EQ(edge_block.instructions.size(), 1u);
    EXPECT_TRUE(instruction_holds_copy(edge_block.instructions[0], S(3), S(4)));
    const auto* edge_jump = std::get_if<Jump>(&*edge_block.terminator);
    ASSERT_NE(edge_jump, nullptr);
    EXPECT_EQ(edge_jump->target, 3u);

    ASSERT_EQ(fn.blocks[2].instructions.size(), 1u);
    EXPECT_TRUE(instruction_holds_copy(fn.blocks[2].instructions[0], S(3), S(5)));
}

TEST(PhiElimTest, ResolvesRegisterSwapCyclesWithT0) {
    MachineFunction fn;
    fn.symbol = "reg_cycle";
    fn.entry_block = 0;
    fn.blocks = {
        block(0, Jump{.target = 1}),
        block(1,
              Return{.value = S(1)},
              {
                  MachinePhi{
                      .dest = S(1),
                      .incoming = {MachinePhiIncoming{.pred = 0, .value = S(2)}},
                  },
                  MachinePhi{
                      .dest = S(2),
                      .incoming = {MachinePhiIncoming{.pred = 0, .value = S(1)}},
                  },
              }),
    };

    eliminate(fn);

    ASSERT_EQ(fn.blocks[0].instructions.size(), 3u);
    EXPECT_TRUE(
        instruction_holds_copy(fn.blocks[0].instructions[0], PhysicalRegister::T0, S(1)));
    EXPECT_TRUE(instruction_holds_copy(fn.blocks[0].instructions[1], S(1), S(2)));
    EXPECT_TRUE(
        instruction_holds_copy(fn.blocks[0].instructions[2], S(2), PhysicalRegister::T0));
}

TEST(PhiElimTest, LowersSpillOperandsToLoadsAndStores) {
    MachineFunction fn;
    fn.symbol = "spill_mix";
    fn.entry_block = 0;
    fn.frame_objects = {
        spill_frame(0),
        spill_frame(1),
        spill_frame(2),
        spill_frame(3),
    };
    fn.blocks = {
        block(0, Jump{.target = 1}),
        block(1,
              Return{.value = S(1)},
              {
                  MachinePhi{
                      .dest = SP(0),
                      .incoming = {MachinePhiIncoming{.pred = 0, .value = SP(1)}},
                  },
                  MachinePhi{
                      .dest = S(1),
                      .incoming = {MachinePhiIncoming{.pred = 0, .value = SP(2)}},
                  },
                  MachinePhi{
                      .dest = SP(3),
                      .incoming = {MachinePhiIncoming{.pred = 0, .value = S(2)}},
                  },
              }),
    };

    eliminate(fn);

    ASSERT_EQ(fn.blocks[0].instructions.size(), 4u);
    EXPECT_TRUE(std::holds_alternative<Load>(fn.blocks[0].instructions[0]));
    EXPECT_TRUE(std::holds_alternative<Store>(fn.blocks[0].instructions[1]));
    EXPECT_TRUE(std::holds_alternative<Load>(fn.blocks[0].instructions[2]));
    EXPECT_TRUE(std::holds_alternative<Store>(fn.blocks[0].instructions[3]));
    EXPECT_FALSE(contains_spill_ref_text(fn));
}

TEST(PhiElimTest, RemovesIdentityPhiWithoutInsertingCopies) {
    MachineFunction fn;
    fn.symbol = "identity_phi";
    fn.entry_block = 0;
    fn.blocks = {
        block(0, Jump{.target = 1}),
        block(1,
              Return{.value = S(1)},
              {
                  MachinePhi{
                      .dest = S(1),
                      .incoming = {MachinePhiIncoming{.pred = 0, .value = S(1)}},
                  },
              }),
    };

    eliminate(fn);

    EXPECT_TRUE(fn.blocks[1].phis.empty());
    EXPECT_TRUE(fn.blocks[0].instructions.empty());
}

} // namespace
