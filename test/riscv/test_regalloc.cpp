#include "riscv/machine_ir.hpp"
#include "riscv/regalloc.hpp"
#include <gtest/gtest.h>

#include <type_traits>

namespace {

using namespace riscv;

VirtualRegister VR(MachineValueId id) {
    return VirtualRegister{.id = id, .reg_class = RegisterClass::Gpr32};
}

bool is_allocatable(PhysicalRegister r) {
    return is_allocatable_register(r);
}

// Returns true if every non-phi instruction operand (def and use) is a PhysicalRegister.
bool no_vregs_in_instructions(const MachineFunction& fn) {
    auto is_vreg = [](const RegisterRef& r) {
        return std::holds_alternative<VirtualRegister>(r);
    };
    for (const auto& block : fn.blocks) {
        for (const auto& inst : block.instructions) {
            bool bad = false;
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, Copy>) {
                    bad |= is_vreg(v.dest) | is_vreg(v.src);
                } else if constexpr (std::is_same_v<T, Li>) {
                    bad |= is_vreg(v.dest);
                } else if constexpr (std::is_same_v<T, Binary> || std::is_same_v<T, Compare>) {
                    bad |= is_vreg(v.dest) | is_vreg(v.lhs) | is_vreg(v.rhs);
                } else if constexpr (std::is_same_v<T, FrameAddr>) {
                    bad |= is_vreg(v.dest);
                } else if constexpr (std::is_same_v<T, Load>) {
                    bad |= is_vreg(v.dest);
                    if (const auto* ra = std::get_if<RegisterAddress>(&v.address)) {
                        bad |= is_vreg(ra->base);
                    }
                } else if constexpr (std::is_same_v<T, Store>) {
                    bad |= is_vreg(v.src);
                    if (const auto* ra = std::get_if<RegisterAddress>(&v.address)) {
                        bad |= is_vreg(ra->base);
                    }
                }
            }, inst);
            if (bad) return false;
        }
        if (block.terminator) {
            bool bad = false;
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, BranchNonZero>) bad |= is_vreg(v.condition);
                else if constexpr (std::is_same_v<T, Return>) {
                    if (v.value) bad |= is_vreg(*v.value);
                }
            }, *block.terminator);
            if (bad) return false;
        }
    }
    return true;
}

bool no_vregs_in_phis(const MachineFunction& fn) {
    for (const auto& block : fn.blocks) {
        for (const auto& phi : block.phis) {
            if (std::holds_alternative<VirtualRegister>(phi.dest)) {
                return false;
            }
            for (const auto& incoming : phi.incoming) {
                if (std::holds_alternative<VirtualRegister>(incoming.value)) {
                    return false;
                }
            }
        }
    }
    return true;
}

std::size_t count_spill_frames(const MachineFunction& fn) {
    std::size_t n = 0;
    for (const auto& fo : fn.frame_objects) {
        if (fo.kind == FrameObjectKind::Spill) ++n;
    }
    return n;
}

// ---- Test 1: No pressure ----
// 3 vregs, all assigned distinct sN regs, no spills.
TEST(RegAllocTest, NoPressureThreeVregs) {
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

    auto stats = allocate_registers(fn);
    EXPECT_EQ(stats.num_spills, 0u);
    EXPECT_EQ(count_spill_frames(fn), 0u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));

    // All instruction dests should be allocatable sN registers.
    for (const auto& inst : fn.blocks[0].instructions) {
        std::visit([](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Li> || std::is_same_v<T, Binary>) {
                const auto* phys = std::get_if<PhysicalRegister>(&v.dest);
                ASSERT_NE(phys, nullptr);
                EXPECT_TRUE(is_allocatable(*phys));
            }
        }, inst);
    }
}

TEST(RegAllocTest, CompareOperandsStayDistinctWhenBothValuesAreLiveAtUse) {
    MachineFunction fn;
    fn.symbol = "cmp_two_values";
    fn.entry_block = 0;
    fn.next_value = 3;

    MachineBlock b;
    b.id = 0;
    b.instructions = {
        Li{.dest = VR(0), .value = 1},
        Li{.dest = VR(1), .value = 2},
        Compare{.dest = VR(2), .op = CompareOp::LtS, .lhs = VR(0), .rhs = VR(1)},
    };
    b.terminator = Return{.value = VR(2)};
    fn.blocks.push_back(b);

    auto stats = allocate_registers(fn);
    EXPECT_EQ(stats.num_spills, 0u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));

    const auto* cmp = std::get_if<Compare>(&fn.blocks[0].instructions[2]);
    ASSERT_NE(cmp, nullptr);
    const auto* lhs = std::get_if<PhysicalRegister>(&cmp->lhs);
    const auto* rhs = std::get_if<PhysicalRegister>(&cmp->rhs);
    ASSERT_NE(lhs, nullptr);
    ASSERT_NE(rhs, nullptr);
    EXPECT_NE(*lhs, *rhs);
}

TEST(RegAllocTest, BranchCarriedCompareValuesStayDistinctAcrossSuccessorCompares) {
    MachineFunction fn;
    fn.symbol = "branch_cmp_chain";
    fn.entry_block = 0;
    fn.next_value = 6;

    {
        MachineBlock b;
        b.id = 0;
        b.instructions = {
            Li{.dest = VR(0), .value = 1},
            Li{.dest = VR(1), .value = 2},
            Li{.dest = VR(2), .value = 3},
            Compare{.dest = VR(3), .op = CompareOp::LtS, .lhs = VR(0), .rhs = VR(1)},
        };
        b.terminator = BranchNonZero{.condition = VR(3), .then_block = 1, .else_block = 2};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b;
        b.id = 1;
        b.instructions = {
            Compare{.dest = VR(4), .op = CompareOp::LtS, .lhs = VR(0), .rhs = VR(2)},
        };
        b.terminator = Return{.value = VR(4)};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b;
        b.id = 2;
        b.instructions = {
            Compare{.dest = VR(5), .op = CompareOp::LtS, .lhs = VR(1), .rhs = VR(2)},
        };
        b.terminator = Return{.value = VR(5)};
        fn.blocks.push_back(b);
    }

    auto stats = allocate_registers(fn);
    EXPECT_EQ(stats.num_spills, 0u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));

    const auto* cmp0 = std::get_if<Compare>(&fn.blocks[0].instructions[3]);
    const auto* cmp1 = std::get_if<Compare>(&fn.blocks[1].instructions[0]);
    const auto* cmp2 = std::get_if<Compare>(&fn.blocks[2].instructions[0]);
    ASSERT_NE(cmp0, nullptr);
    ASSERT_NE(cmp1, nullptr);
    ASSERT_NE(cmp2, nullptr);

    const auto* cmp0_lhs = std::get_if<PhysicalRegister>(&cmp0->lhs);
    const auto* cmp0_rhs = std::get_if<PhysicalRegister>(&cmp0->rhs);
    const auto* cmp1_lhs = std::get_if<PhysicalRegister>(&cmp1->lhs);
    const auto* cmp1_rhs = std::get_if<PhysicalRegister>(&cmp1->rhs);
    const auto* cmp2_lhs = std::get_if<PhysicalRegister>(&cmp2->lhs);
    const auto* cmp2_rhs = std::get_if<PhysicalRegister>(&cmp2->rhs);
    ASSERT_NE(cmp0_lhs, nullptr);
    ASSERT_NE(cmp0_rhs, nullptr);
    ASSERT_NE(cmp1_lhs, nullptr);
    ASSERT_NE(cmp1_rhs, nullptr);
    ASSERT_NE(cmp2_lhs, nullptr);
    ASSERT_NE(cmp2_rhs, nullptr);

    EXPECT_NE(*cmp0_lhs, *cmp0_rhs);
    EXPECT_NE(*cmp1_lhs, *cmp1_rhs);
    EXPECT_NE(*cmp2_lhs, *cmp2_rhs);
}

TEST(RegAllocTest, LoopCarriedPhiValuesDoNotCollapseAcrossRepeatedCoalescing) {
    MachineFunction fn;
    fn.symbol = "loop_phi_chain";
    fn.entry_block = 0;
    fn.next_value = 14;

    {
        MachineBlock b;
        b.id = 0;
        b.instructions = {
            Li{.dest = VR(0), .value = 0},
            Li{.dest = VR(1), .value = 999999},
            Li{.dest = VR(2), .value = -1},
            Li{.dest = VR(3), .value = 0},
            Li{.dest = VR(12), .value = 1},
        };
        b.terminator = Jump{.target = 1};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b;
        b.id = 1;
        b.phis = {
            MachinePhi{
                .dest = VR(4),
                .incoming = {
                    MachinePhiIncoming{.pred = 0, .value = VR(0)},
                    MachinePhiIncoming{.pred = 2, .value = VR(8)},
                },
            },
            MachinePhi{
                .dest = VR(5),
                .incoming = {
                    MachinePhiIncoming{.pred = 0, .value = VR(1)},
                    MachinePhiIncoming{.pred = 2, .value = VR(9)},
                },
            },
            MachinePhi{
                .dest = VR(6),
                .incoming = {
                    MachinePhiIncoming{.pred = 0, .value = VR(2)},
                    MachinePhiIncoming{.pred = 2, .value = VR(10)},
                },
            },
            MachinePhi{
                .dest = VR(7),
                .incoming = {
                    MachinePhiIncoming{.pred = 0, .value = VR(3)},
                    MachinePhiIncoming{.pred = 2, .value = VR(11)},
                },
            },
        };
        b.instructions = {
            Compare{.dest = VR(13), .op = CompareOp::LtS, .lhs = VR(7), .rhs = VR(12)},
        };
        b.terminator = BranchNonZero{.condition = VR(13), .then_block = 2, .else_block = 3};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b;
        b.id = 2;
        b.instructions = {
            Binary{.dest = VR(8), .op = BinaryOp::Add, .lhs = VR(4), .rhs = VR(12)},
            Copy{.dest = VR(9), .src = VR(5)},
            Copy{.dest = VR(10), .src = VR(6)},
            Binary{.dest = VR(11), .op = BinaryOp::Add, .lhs = VR(7), .rhs = VR(12)},
        };
        b.terminator = Jump{.target = 1};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b;
        b.id = 3;
        b.terminator = Return{.value = VR(6)};
        fn.blocks.push_back(b);
    }

    auto stats = allocate_registers(fn);
    EXPECT_EQ(stats.num_spills, 0u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));
    EXPECT_TRUE(no_vregs_in_phis(fn));

    const auto& phis = fn.blocks[1].phis;
    ASSERT_EQ(phis.size(), 4u);
    const auto* pcount = std::get_if<PhysicalRegister>(&phis[0].dest);
    const auto* best_cost = std::get_if<PhysicalRegister>(&phis[1].dest);
    const auto* best_plan = std::get_if<PhysicalRegister>(&phis[2].dest);
    const auto* join_order = std::get_if<PhysicalRegister>(&phis[3].dest);
    ASSERT_NE(pcount, nullptr);
    ASSERT_NE(best_cost, nullptr);
    ASSERT_NE(best_plan, nullptr);
    ASSERT_NE(join_order, nullptr);
    EXPECT_NE(*pcount, *best_cost);
    EXPECT_NE(*pcount, *best_plan);
    EXPECT_NE(*pcount, *join_order);
    EXPECT_NE(*best_cost, *best_plan);
    EXPECT_NE(*best_cost, *join_order);
    EXPECT_NE(*best_plan, *join_order);
}

// ---- Test 2: Phi loop counter, no spill ----
// Loop: entry → header (phi v1=[v0, v2]) → body (v2=add v1, 1) → header or exit
// Both phi dest and back-edge incoming should be allocated to physical regs (no spill).
TEST(RegAllocTest, PhiLoopCounterNoSpill) {
    // Block 0 (entry): v0=li 0, jump 1
    // Block 1 (header): phi(v1=[v0 from 0, v2 from 2]), v3=li 10, v4=cmp.lt.s v1 v3, brnz v4, 2, 3
    // Block 2 (body): v5=li 1, v2=add v1 v5, jump 1
    // Block 3 (exit): ret v1
    MachineFunction fn;
    fn.symbol = "loop"; fn.entry_block = 0; fn.next_value = 6;

    {
        MachineBlock b; b.id = 0;
        b.instructions = {Li{.dest = VR(0), .value = 0}};
        b.terminator = Jump{.target = 1};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 1;
        b.phis = {MachinePhi{
            .dest = VR(1),
            .incoming = {
                MachinePhiIncoming{.pred = 0, .value = VR(0)},
                MachinePhiIncoming{.pred = 2, .value = VR(2)},
            },
        }};
        b.instructions = {
            Li{.dest = VR(3), .value = 10},
            Compare{.dest = VR(4), .op = CompareOp::LtS, .lhs = VR(1), .rhs = VR(3)},
        };
        b.terminator = BranchNonZero{.condition = VR(4), .then_block = 2, .else_block = 3};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 2;
        b.instructions = {
            Li{.dest = VR(5), .value = 1},
            Binary{.dest = VR(2), .op = BinaryOp::Add, .lhs = VR(1), .rhs = VR(5)},
        };
        b.terminator = Jump{.target = 1};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 3; b.terminator = Return{.value = VR(1)};
        fn.blocks.push_back(b);
    }

    auto stats = allocate_registers(fn);
    EXPECT_EQ(stats.num_spills, 0u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));
    EXPECT_TRUE(no_vregs_in_phis(fn));

    // Phi dest should be a physical register after RA.
    const auto& header = fn.blocks[1];
    ASSERT_FALSE(header.phis.empty());
    const auto* phys_dest = std::get_if<PhysicalRegister>(&header.phis[0].dest);
    EXPECT_NE(phys_dest, nullptr) << "phi dest should be a physical register post-RA";
    if (phys_dest) { EXPECT_TRUE(is_allocatable(*phys_dest)); }
}

// ---- Test 3: Phi-heavy pressure still rewrites phis cleanly under spill ----
TEST(RegAllocTest, PhiPressureStillRewritesCleanly) {
    MachineFunction fn;
    fn.symbol = "phi_spill"; fn.entry_block = 0; fn.next_value = 39;

    {
        MachineBlock b; b.id = 0;
        for (int i = 0; i < 20; ++i) {
            b.instructions.push_back(
                Li{.dest = VR(static_cast<MachineValueId>(i)), .value = i + 1});
        }
        b.terminator = Jump{.target = 1};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 1;
        b.phis = {MachinePhi{
            .dest = VR(20),
            .incoming = {
                MachinePhiIncoming{.pred = 0, .value = VR(0)},
                MachinePhiIncoming{.pred = 2, .value = VR(21)},
            },
        }};
        b.instructions = {
            Binary{.dest = VR(22), .op = BinaryOp::Add, .lhs = VR(0), .rhs = VR(1)},
            Binary{.dest = VR(23), .op = BinaryOp::Add, .lhs = VR(2), .rhs = VR(3)},
            Binary{.dest = VR(24), .op = BinaryOp::Add, .lhs = VR(4), .rhs = VR(5)},
            Binary{.dest = VR(25), .op = BinaryOp::Add, .lhs = VR(6), .rhs = VR(7)},
            Binary{.dest = VR(26), .op = BinaryOp::Add, .lhs = VR(8), .rhs = VR(9)},
            Binary{.dest = VR(27), .op = BinaryOp::Add, .lhs = VR(10), .rhs = VR(11)},
            Binary{.dest = VR(28), .op = BinaryOp::Add, .lhs = VR(12), .rhs = VR(13)},
            Binary{.dest = VR(29), .op = BinaryOp::Add, .lhs = VR(14), .rhs = VR(15)},
            Binary{.dest = VR(30), .op = BinaryOp::Add, .lhs = VR(16), .rhs = VR(17)},
            Binary{.dest = VR(31), .op = BinaryOp::Add, .lhs = VR(18), .rhs = VR(19)},
            Binary{.dest = VR(32), .op = BinaryOp::Add, .lhs = VR(22), .rhs = VR(23)},
            Binary{.dest = VR(33), .op = BinaryOp::Add, .lhs = VR(24), .rhs = VR(25)},
            Binary{.dest = VR(34), .op = BinaryOp::Add, .lhs = VR(26), .rhs = VR(27)},
            Binary{.dest = VR(35), .op = BinaryOp::Add, .lhs = VR(28), .rhs = VR(29)},
            Binary{.dest = VR(36), .op = BinaryOp::Add, .lhs = VR(30), .rhs = VR(31)},
            Binary{.dest = VR(37), .op = BinaryOp::Add, .lhs = VR(20), .rhs = VR(32)},
            Binary{.dest = VR(38), .op = BinaryOp::Add, .lhs = VR(37), .rhs = VR(36)},
        };
        b.terminator = BranchNonZero{.condition = VR(38), .then_block = 2, .else_block = 3};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 2;
        b.instructions = {
            Binary{.dest = VR(21), .op = BinaryOp::Add, .lhs = VR(20), .rhs = VR(0)},
        };
        b.terminator = Jump{.target = 1};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 3;
        b.terminator = Return{.value = VR(38)};
        fn.blocks.push_back(b);
    }

    auto stats = allocate_registers(fn);
    EXPECT_GE(stats.num_spills, 1u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));
    EXPECT_TRUE(no_vregs_in_phis(fn));
}

// ---- Test 3: Spill under pressure ----
// 20 simultaneously live vregs → at least 1 spill, instruction rewrite complete.
TEST(RegAllocTest, SpillUnderPressure) {
    // v0..v19: 20 li's all defined before any use, making them simultaneously live.
    MachineFunction fn;
    fn.symbol = "pressure"; fn.entry_block = 0; fn.next_value = 39;
    MachineBlock b; b.id = 0;

    for (int i = 0; i < 20; ++i) {
        b.instructions.push_back(Li{.dest = VR(static_cast<MachineValueId>(i)), .value = i + 1});
    }
    // paired adds
    for (int i = 0; i < 10; ++i) {
        b.instructions.push_back(Binary{
            .dest = VR(static_cast<MachineValueId>(20 + i)),
            .op = BinaryOp::Add,
            .lhs = VR(static_cast<MachineValueId>(2 * i)),
            .rhs = VR(static_cast<MachineValueId>(2 * i + 1)),
        });
    }
    b.instructions.push_back(Binary{.dest = VR(30), .op = BinaryOp::Add, .lhs = VR(20), .rhs = VR(21)});
    b.instructions.push_back(Binary{.dest = VR(31), .op = BinaryOp::Add, .lhs = VR(22), .rhs = VR(23)});
    b.instructions.push_back(Binary{.dest = VR(32), .op = BinaryOp::Add, .lhs = VR(24), .rhs = VR(25)});
    b.instructions.push_back(Binary{.dest = VR(33), .op = BinaryOp::Add, .lhs = VR(26), .rhs = VR(27)});
    b.instructions.push_back(Binary{.dest = VR(34), .op = BinaryOp::Add, .lhs = VR(28), .rhs = VR(29)});
    b.instructions.push_back(Binary{.dest = VR(35), .op = BinaryOp::Add, .lhs = VR(30), .rhs = VR(31)});
    b.instructions.push_back(Binary{.dest = VR(36), .op = BinaryOp::Add, .lhs = VR(32), .rhs = VR(33)});
    b.instructions.push_back(Binary{.dest = VR(37), .op = BinaryOp::Add, .lhs = VR(35), .rhs = VR(36)});
    b.instructions.push_back(Binary{.dest = VR(38), .op = BinaryOp::Add, .lhs = VR(37), .rhs = VR(34)});
    b.terminator = Return{.value = VR(38)};
    fn.blocks.push_back(b);

    auto stats = allocate_registers(fn);
    EXPECT_GE(stats.num_spills, 1u) << "20 live vregs should cause at least one spill";
    EXPECT_GE(count_spill_frames(fn), 1u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));
}

// ---- Test 4: Two-source spilled Binary with spilled dest ----
// Creates pressure where add operands and dest must spill: verifies t0 aliasing.
TEST(RegAllocTest, TwoSourceSpilledBinary) {
    // Strategy: create 20 simultaneous live vregs (v0-v19), then reduce them.
    // This creates spill pressure and forces v12's operands and dest to contend.
    MachineFunction fn;
    fn.symbol = "spilled_binary"; fn.entry_block = 0; fn.next_value = 39;
    MachineBlock b; b.id = 0;

    for (int i = 0; i < 20; ++i) {
        b.instructions.push_back(Li{.dest = VR(static_cast<MachineValueId>(i)), .value = i + 1});
    }
    // 10 paired adds
    for (int i = 0; i < 10; ++i) {
        b.instructions.push_back(Binary{
            .dest = VR(static_cast<MachineValueId>(20 + i)),
            .op = BinaryOp::Add,
            .lhs = VR(static_cast<MachineValueId>(2 * i)),
            .rhs = VR(static_cast<MachineValueId>(2 * i + 1)),
        });
    }
    b.instructions.push_back(Binary{.dest = VR(30), .op = BinaryOp::Add, .lhs = VR(20), .rhs = VR(21)});
    b.instructions.push_back(Binary{.dest = VR(31), .op = BinaryOp::Add, .lhs = VR(22), .rhs = VR(23)});
    b.instructions.push_back(Binary{.dest = VR(32), .op = BinaryOp::Add, .lhs = VR(24), .rhs = VR(25)});
    b.instructions.push_back(Binary{.dest = VR(33), .op = BinaryOp::Add, .lhs = VR(26), .rhs = VR(27)});
    b.instructions.push_back(Binary{.dest = VR(34), .op = BinaryOp::Add, .lhs = VR(28), .rhs = VR(29)});
    b.instructions.push_back(Binary{.dest = VR(35), .op = BinaryOp::Add, .lhs = VR(30), .rhs = VR(31)});
    b.instructions.push_back(Binary{.dest = VR(36), .op = BinaryOp::Add, .lhs = VR(32), .rhs = VR(33)});
    b.instructions.push_back(Binary{.dest = VR(37), .op = BinaryOp::Add, .lhs = VR(35), .rhs = VR(36)});
    b.instructions.push_back(Binary{.dest = VR(38), .op = BinaryOp::Add, .lhs = VR(37), .rhs = VR(34)});
    b.terminator = Return{.value = VR(38)};
    fn.blocks.push_back(b);

    auto stats = allocate_registers(fn);
    EXPECT_GE(stats.num_spills, 1u) << "high live range count should force spilling";
    EXPECT_TRUE(no_vregs_in_instructions(fn));
}

// ---- Test 5: Call across live range ----
// vreg defined before call, used after; it must not get a caller-saved aN color.
TEST(RegAllocTest, CallAcrossLiveRange) {
    // v0=li 42, copy a0=v0, call @foo, v1=copy a0, v2=add v0 v1, ret v2
    // v0 spans the call, so caller-saved colors must be forbidden by RA itself.
    MachineFunction fn;
    fn.symbol = "call_span"; fn.entry_block = 0; fn.next_value = 3;
    MachineBlock b; b.id = 0;
    b.instructions = {
        Li{.dest = VR(0), .value = 42},
        Copy{.dest = PhysicalRegister::A0, .src = VR(0)},
        Call{.callee = "foo"},
        Copy{.dest = VR(1), .src = PhysicalRegister::A0},
        Binary{.dest = VR(2), .op = BinaryOp::Add, .lhs = VR(0), .rhs = VR(1)},
    };
    b.terminator = Return{.value = VR(2)};
    fn.blocks.push_back(b);

    auto stats = allocate_registers(fn);
    EXPECT_EQ(stats.num_spills, 0u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));

    // v0 (first Li) should be allocated to an sN register.
    const auto& li_inst = fn.blocks[0].instructions[0];
    const auto* li = std::get_if<Li>(&li_inst);
    ASSERT_NE(li, nullptr);
    const auto* phys = std::get_if<PhysicalRegister>(&li->dest);
    ASSERT_NE(phys, nullptr);
    EXPECT_TRUE(is_allocatable(*phys));
    EXPECT_FALSE(is_caller_saved_register(*phys))
        << "vreg spanning call must not be colored to a caller-saved register";
}

TEST(RegAllocTest, CallArgumentValuesDoNotCollapseToOneRegister) {
    MachineFunction fn;
    fn.symbol = "call_args"; fn.entry_block = 0; fn.next_value = 2;
    fn.frame_objects.push_back(FrameObject{
        .id = 0,
        .kind = FrameObjectKind::LocalSlot,
        .size = 4,
        .align = 4,
        .host_type = semantic::invalid_type_id,
        .spill_class = std::nullopt,
        .source_slot = std::nullopt,
        .debug_name = "x",
        .saved_reg = std::nullopt,
        .materialized_offset = std::nullopt,
    });

    MachineBlock b; b.id = 0;
    b.instructions = {
        FrameAddr{.dest = VR(0), .frame = 0, .offset = 0},
        Li{.dest = VR(1), .value = 7},
        Copy{.dest = PhysicalRegister::A0, .src = VR(0)},
        Copy{.dest = PhysicalRegister::A1, .src = VR(1)},
        Call{.callee = "build_leaf"},
    };
    b.terminator = Return{};
    fn.blocks.push_back(b);

    auto stats = allocate_registers(fn);
    EXPECT_EQ(stats.num_spills, 0u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));

    const auto* frame_addr = std::get_if<FrameAddr>(&fn.blocks[0].instructions[0]);
    const auto* li = std::get_if<Li>(&fn.blocks[0].instructions[1]);
    ASSERT_NE(frame_addr, nullptr);
    ASSERT_NE(li, nullptr);
    const auto* frame_phys = std::get_if<PhysicalRegister>(&frame_addr->dest);
    const auto* li_phys = std::get_if<PhysicalRegister>(&li->dest);
    ASSERT_NE(frame_phys, nullptr);
    ASSERT_NE(li_phys, nullptr);
    EXPECT_NE(*frame_phys, *li_phys)
        << "distinct live call arguments must not collapse onto one physical register";
}

TEST(RegAllocTest, CallArgumentCopyMayCoalesceAway) {
    MachineFunction fn;
    fn.symbol = "arg_coalesce"; fn.entry_block = 0; fn.next_value = 1;
    MachineBlock b; b.id = 0;
    b.instructions = {
        Li{.dest = VR(0), .value = 5},
        Copy{.dest = PhysicalRegister::A0, .src = VR(0)},
        Call{.callee = "foo"},
    };
    b.terminator = Return{};
    fn.blocks.push_back(b);

    auto stats = allocate_registers(fn);
    EXPECT_EQ(stats.num_spills, 0u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));
    ASSERT_EQ(fn.blocks[0].instructions.size(), 2u);
    const auto* li = std::get_if<Li>(&fn.blocks[0].instructions[0]);
    ASSERT_NE(li, nullptr);
    const auto* phys = std::get_if<PhysicalRegister>(&li->dest);
    ASSERT_NE(phys, nullptr);
    EXPECT_EQ(*phys, PhysicalRegister::A0)
        << "short-lived argument setup should still be able to coalesce into a0";
}

TEST(RegAllocTest, EarlierArgRegisterStaysReservedAcrossOutgoingArgSetup) {
    MachineFunction fn;
    fn.symbol = "arg_bundle_reservation"; fn.entry_block = 0; fn.next_value = 2;
    fn.frame_objects.push_back(FrameObject{
        .id = 0,
        .kind = FrameObjectKind::OutgoingArg,
        .size = 4,
        .align = 4,
        .host_type = semantic::invalid_type_id,
        .spill_class = std::nullopt,
        .source_slot = std::nullopt,
        .debug_name = "",
        .saved_reg = std::nullopt,
        .materialized_offset = std::nullopt,
    });

    MachineBlock b; b.id = 0;
    b.instructions = {
        Li{.dest = VR(0), .value = 11},
        Li{.dest = VR(1), .value = 22},
        Copy{.dest = PhysicalRegister::A0, .src = VR(0)},
        Store{.address = FrameAddress{.frame = 0, .offset = 0}, .src = VR(1)},
        Call{.callee = "foo"},
    };
    b.terminator = Return{};
    fn.blocks.push_back(b);

    auto stats = allocate_registers(fn);
    EXPECT_EQ(stats.num_spills, 0u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));

    const Li* li0 = nullptr;
    const Li* li1 = nullptr;
    const Store* store = nullptr;
    for (const auto& inst : fn.blocks[0].instructions) {
        if (!li0) {
            li0 = std::get_if<Li>(&inst);
            if (li0 && li0->value == 11) {
                continue;
            }
            li0 = nullptr;
        }
        if (!li1) {
            const auto* li = std::get_if<Li>(&inst);
            if (li && li->value == 22) {
                li1 = li;
                continue;
            }
        }
        if (!store) {
            store = std::get_if<Store>(&inst);
        }
    }
    ASSERT_NE(li0, nullptr);
    ASSERT_NE(li1, nullptr);
    ASSERT_NE(store, nullptr);

    const auto* phys0 = std::get_if<PhysicalRegister>(&li0->dest);
    const auto* phys1 = std::get_if<PhysicalRegister>(&li1->dest);
    const auto* store_src = std::get_if<PhysicalRegister>(&store->src);
    ASSERT_NE(phys0, nullptr);
    ASSERT_NE(phys1, nullptr);
    ASSERT_NE(store_src, nullptr);

    EXPECT_EQ(*phys0, PhysicalRegister::A0)
        << "arg0 source should still be allowed to coalesce into a0";
    EXPECT_NE(*store_src, PhysicalRegister::A0)
        << "later outgoing-arg setup must not reuse the already-reserved a0 lane";
}

TEST(RegAllocTest, RewritesUnreachableBlocksToo) {
    MachineFunction fn;
    fn.symbol = "unreachable"; fn.entry_block = 0; fn.next_value = 2;
    {
        MachineBlock b; b.id = 0;
        b.instructions = {Li{.dest = VR(0), .value = 1}};
        b.terminator = Return{.value = VR(0)};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 1;
        b.instructions = {Li{.dest = VR(1), .value = 2}};
        b.terminator = Return{.value = VR(1)};
        fn.blocks.push_back(b);
    }

    auto stats = allocate_registers(fn);
    EXPECT_EQ(stats.num_spills, 0u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));

}

} // namespace
