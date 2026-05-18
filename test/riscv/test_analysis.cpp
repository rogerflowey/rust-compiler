#include "riscv/analysis/cfg.hpp"
#include "riscv/analysis/intervals.hpp"
#include "riscv/analysis/liveness.hpp"
#include "riscv/machine_ir.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

using namespace riscv;

VirtualRegister VR(MachineValueId id) {
    return VirtualRegister{.id = id, .reg_class = RegisterClass::Gpr32};
}

// ---- CFG / RPO ----

TEST(CfgTest, StraightLine) {
    MachineFunction fn;
    fn.symbol = "f"; fn.entry_block = 0; fn.next_value = 1;
    MachineBlock b; b.id = 0;
    b.instructions = {Li{.dest = VR(0), .value = 1}};
    b.terminator = Return{.value = VR(0)};
    fn.blocks.push_back(b);

    auto cfg = compute_cfg(fn);
    EXPECT_EQ(cfg.rpo.size(), 1u);
    EXPECT_EQ(fn.blocks[cfg.rpo[0]].id, 0u);
    EXPECT_TRUE(cfg.successors[0].empty());
    EXPECT_TRUE(cfg.predecessors[0].empty());
}

TEST(CfgTest, Diamond) {
    // 0 --brnz--> 1(then), 2(else)  both jump to 3(join)
    MachineFunction fn;
    fn.symbol = "f"; fn.entry_block = 0; fn.next_value = 1;

    auto add_block = [&](BlockId id, std::optional<Terminator> term) {
        MachineBlock b; b.id = id; b.terminator = term;
        fn.blocks.push_back(b);
    };
    add_block(0, BranchNonZero{.condition = VR(0), .then_block = 1, .else_block = 2});
    add_block(1, Jump{.target = 3});
    add_block(2, Jump{.target = 3});
    add_block(3, Return{});

    auto cfg = compute_cfg(fn);
    EXPECT_EQ(cfg.rpo.size(), 4u);
    // entry first, join last
    EXPECT_EQ(fn.blocks[cfg.rpo[0]].id, 0u);
    EXPECT_EQ(fn.blocks[cfg.rpo[3]].id, 3u);
    // join has two predecessors
    EXPECT_EQ(cfg.predecessors[cfg.index_of.at(3)].size(), 2u);
    // entry has two successors
    EXPECT_EQ(cfg.successors[cfg.index_of.at(0)].size(), 2u);
}

TEST(CfgTest, LoopBackEdge) {
    // 0 -> 1(header) ->brnz-> 2(body) -> 1 (back-edge), or -> 3(exit)
    MachineFunction fn;
    fn.symbol = "f"; fn.entry_block = 0; fn.next_value = 1;

    auto add_block = [&](BlockId id, Terminator term) {
        MachineBlock b; b.id = id; b.terminator = term;
        fn.blocks.push_back(b);
    };
    add_block(0, Jump{.target = 1});
    add_block(1, BranchNonZero{.condition = VR(0), .then_block = 2, .else_block = 3});
    add_block(2, Jump{.target = 1});
    add_block(3, Return{});

    auto cfg = compute_cfg(fn);
    EXPECT_EQ(cfg.rpo.size(), 4u);
    // entry must be first in RPO
    EXPECT_EQ(fn.blocks[cfg.rpo[0]].id, 0u);
    // body's successor is header
    ASSERT_EQ(cfg.successors[cfg.index_of.at(2)].size(), 1u);
    EXPECT_EQ(cfg.successors[cfg.index_of.at(2)][0], 1u);
    // header has back-edge from body
    const auto& header_preds = cfg.predecessors[cfg.index_of.at(1)];
    EXPECT_TRUE(std::find(header_preds.begin(), header_preds.end(), BlockId{2}) != header_preds.end());
}

TEST(CfgTest, UnreachableBlockExcludedFromRpo) {
    MachineFunction fn;
    fn.symbol = "f"; fn.entry_block = 0; fn.next_value = 0;

    MachineBlock b0; b0.id = 0; b0.terminator = Return{};
    MachineBlock b1; b1.id = 1; b1.terminator = Jump{.target = 0};
    fn.blocks.push_back(b0);
    fn.blocks.push_back(b1);

    auto cfg = compute_cfg(fn);
    EXPECT_EQ(cfg.rpo.size(), 1u);
    EXPECT_EQ(fn.blocks[cfg.rpo[0]].id, 0u);
}

// ---- Liveness ----

TEST(LivenessTest, SimpleDefUse) {
    // single block: v0=li, v1=li, v2=add v0 v1, ret v2 → all sets empty
    MachineFunction fn;
    fn.symbol = "f"; fn.entry_block = 0; fn.next_value = 3;
    MachineBlock b; b.id = 0;
    b.instructions = {
        Li{.dest = VR(0), .value = 1},
        Li{.dest = VR(1), .value = 2},
        Binary{.dest = VR(2), .op = BinaryOp::Add, .lhs = VR(0), .rhs = VR(1)},
    };
    b.terminator = Return{.value = VR(2)};
    fn.blocks.push_back(b);

    auto cfg = compute_cfg(fn);
    auto live = compute_liveness(fn, cfg);
    EXPECT_TRUE(live.live_in[0].empty());
    EXPECT_TRUE(live.live_out[0].empty());
}

TEST(LivenessTest, BranchOnePathUse) {
    // block 0: v0=li, v1=li, brnz v1, 1, 2
    // block 1: v2=add v0 v0, ret v2   -- uses v0
    // block 2: ret                     -- does not use v0
    // => v0 in live_out[0] and live_in[1], but NOT in live_in[2]
    MachineFunction fn;
    fn.symbol = "f"; fn.entry_block = 0; fn.next_value = 3;

    {
        MachineBlock b; b.id = 0;
        b.instructions = {Li{.dest = VR(0), .value = 1}, Li{.dest = VR(1), .value = 1}};
        b.terminator = BranchNonZero{.condition = VR(1), .then_block = 1, .else_block = 2};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 1;
        b.instructions = {Binary{.dest = VR(2), .op = BinaryOp::Add, .lhs = VR(0), .rhs = VR(0)}};
        b.terminator = Return{.value = VR(2)};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 2; b.terminator = Return{};
        fn.blocks.push_back(b);
    }

    auto cfg = compute_cfg(fn);
    auto live = compute_liveness(fn, cfg);
    std::size_t i0 = cfg.index_of.at(0), i1 = cfg.index_of.at(1), i2 = cfg.index_of.at(2);

    EXPECT_TRUE(live.live_out[i0].contains(MachineValueId{0}));
    EXPECT_TRUE(live.live_in[i1].contains(MachineValueId{0}));
    EXPECT_FALSE(live.live_in[i2].contains(MachineValueId{0}));
    EXPECT_FALSE(live.live_in[i0].contains(MachineValueId{0}));
}

TEST(LivenessTest, LoopLiveThrough) {
    // 0: v0=li, jump 1  |  1(loop): brnz v0, 1, 2  |  2: ret v0
    // v0 must be live_in and live_out of block 1 (back-edge)
    MachineFunction fn;
    fn.symbol = "f"; fn.entry_block = 0; fn.next_value = 1;

    {
        MachineBlock b; b.id = 0;
        b.instructions = {Li{.dest = VR(0), .value = 10}};
        b.terminator = Jump{.target = 1};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 1;
        b.terminator = BranchNonZero{.condition = VR(0), .then_block = 1, .else_block = 2};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 2; b.terminator = Return{.value = VR(0)};
        fn.blocks.push_back(b);
    }

    auto cfg = compute_cfg(fn);
    auto live = compute_liveness(fn, cfg);
    std::size_t i1 = cfg.index_of.at(1);

    EXPECT_TRUE(live.live_in[i1].contains(MachineValueId{0}));
    EXPECT_TRUE(live.live_out[i1].contains(MachineValueId{0}));
}

TEST(LivenessTest, PhiEdgeLiveness) {
    // diamond: 0 -brnz-> 1(then, v1=li), 2(else, v2=li); 3(join): phi v3=[v1,v2], ret v3
    // v1 in live_out[1], v2 in live_out[2], v3 NOT in live_in[3]
    MachineFunction fn;
    fn.symbol = "f"; fn.entry_block = 0; fn.next_value = 4;

    {
        MachineBlock b; b.id = 0;
        b.instructions = {Li{.dest = VR(0), .value = 1}};
        b.terminator = BranchNonZero{.condition = VR(0), .then_block = 1, .else_block = 2};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 1;
        b.instructions = {Li{.dest = VR(1), .value = 10}};
        b.terminator = Jump{.target = 3};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 2;
        b.instructions = {Li{.dest = VR(2), .value = 20}};
        b.terminator = Jump{.target = 3};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 3;
        b.phis = {MachinePhi{
            .dest = VR(3),
            .incoming = {
                MachinePhiIncoming{.pred = 1, .value = VR(1)},
                MachinePhiIncoming{.pred = 2, .value = VR(2)},
            },
        }};
        b.terminator = Return{.value = VR(3)};
        fn.blocks.push_back(b);
    }

    auto cfg = compute_cfg(fn);
    auto live = compute_liveness(fn, cfg);
    std::size_t i1 = cfg.index_of.at(1), i2 = cfg.index_of.at(2), i3 = cfg.index_of.at(3);

    EXPECT_TRUE(live.live_out[i1].contains(MachineValueId{1}));
    EXPECT_TRUE(live.live_out[i2].contains(MachineValueId{2}));
    EXPECT_FALSE(live.live_in[i3].contains(MachineValueId{3}));
}

// ---- Intervals ----

TEST(IntervalsTest, DefAndUseSameBlock) {
    // block 0: [pos0] v0=li, [pos2] v1=li, [pos4] term: ret v1
    MachineFunction fn;
    fn.symbol = "f"; fn.entry_block = 0; fn.next_value = 2;
    MachineBlock b; b.id = 0;
    b.instructions = {Li{.dest = VR(0), .value = 1}, Li{.dest = VR(1), .value = 2}};
    b.terminator = Return{.value = VR(1)};
    fn.blocks.push_back(b);

    auto cfg = compute_cfg(fn);
    auto live = compute_liveness(fn, cfg);
    auto ivs = compute_intervals(fn, cfg, live);

    // intervals sorted by start
    for (std::size_t i = 1; i < ivs.intervals.size(); ++i) {
        EXPECT_LE(ivs.intervals[i - 1].start, ivs.intervals[i].start);
    }

    const LiveInterval* v1 = nullptr;
    for (const auto& iv : ivs.intervals) {
        if (iv.vreg == 1u) v1 = &iv;
    }
    ASSERT_NE(v1, nullptr);
    // v1 defined at pos 2 (second instruction), used at terminator pos 4
    EXPECT_EQ(v1->start, 2u);
    EXPECT_EQ(v1->end, 4u);
    ASSERT_EQ(v1->uses.size(), 1u);
    EXPECT_EQ(v1->uses[0], 4u);
}

TEST(IntervalsTest, PhiIncomingAtPredTerminator) {
    // Same diamond as liveness phi test.
    // v1 (defined in block 1, used via phi in block 3) should have a use recorded
    // at block 1's terminator position.
    MachineFunction fn;
    fn.symbol = "f"; fn.entry_block = 0; fn.next_value = 4;

    {
        MachineBlock b; b.id = 0;
        b.instructions = {Li{.dest = VR(0), .value = 1}};
        b.terminator = BranchNonZero{.condition = VR(0), .then_block = 1, .else_block = 2};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 1;
        b.instructions = {Li{.dest = VR(1), .value = 10}};
        b.terminator = Jump{.target = 3};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 2;
        b.instructions = {Li{.dest = VR(2), .value = 20}};
        b.terminator = Jump{.target = 3};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 3;
        b.phis = {MachinePhi{
            .dest = VR(3),
            .incoming = {
                MachinePhiIncoming{.pred = 1, .value = VR(1)},
                MachinePhiIncoming{.pred = 2, .value = VR(2)},
            },
        }};
        b.terminator = Return{.value = VR(3)};
        fn.blocks.push_back(b);
    }

    auto cfg = compute_cfg(fn);
    auto live = compute_liveness(fn, cfg);
    auto ivs = compute_intervals(fn, cfg, live);

    std::size_t idx1 = cfg.index_of.at(1);
    std::size_t idx2 = cfg.index_of.at(2);
    // terminator position = block_range.second - 2
    Position b1_term = ivs.block_range[idx1].second - 2;
    Position b2_term = ivs.block_range[idx2].second - 2;

    const LiveInterval* v1_iv = nullptr;
    const LiveInterval* v2_iv = nullptr;
    for (const auto& iv : ivs.intervals) {
        if (iv.vreg == 1u) v1_iv = &iv;
        if (iv.vreg == 2u) v2_iv = &iv;
    }
    ASSERT_NE(v1_iv, nullptr);
    ASSERT_NE(v2_iv, nullptr);

    EXPECT_TRUE(std::find(v1_iv->uses.begin(), v1_iv->uses.end(), b1_term) != v1_iv->uses.end())
        << "phi-incoming use of v1 should be at block1 terminator pos " << b1_term;
    EXPECT_TRUE(std::find(v2_iv->uses.begin(), v2_iv->uses.end(), b2_term) != v2_iv->uses.end())
        << "phi-incoming use of v2 should be at block2 terminator pos " << b2_term;
}

} // namespace
