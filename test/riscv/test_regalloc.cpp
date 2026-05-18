#include "riscv/machine_ir.hpp"
#include "riscv/regalloc.hpp"
#include "riscv/validate.hpp"

#include <gtest/gtest.h>

#include <type_traits>

namespace {

using namespace riscv;

VirtualRegister VR(MachineValueId id) {
    return VirtualRegister{.id = id, .reg_class = RegisterClass::Gpr32};
}

bool is_allocatable(PhysicalRegister r) {
    return r >= PhysicalRegister::S1 && r <= PhysicalRegister::S11;
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

std::size_t count_spill_refs_in_phis(const MachineFunction& fn) {
    std::size_t n = 0;
    for (const auto& block : fn.blocks) {
        for (const auto& phi : block.phis) {
            if (std::holds_alternative<SpillRef>(phi.dest)) {
                ++n;
            }
            for (const auto& incoming : phi.incoming) {
                if (std::holds_alternative<SpillRef>(incoming.value)) {
                    ++n;
                }
            }
        }
    }
    return n;
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

// ---- Test 3: Spilled phi operands become SpillRef ----
TEST(RegAllocTest, SpilledPhiUsesSpillRef) {
    MachineFunction fn;
    fn.symbol = "phi_spill"; fn.entry_block = 0; fn.next_value = 24;

    {
        MachineBlock b; b.id = 0;
        for (int i = 0; i < 11; ++i) {
            b.instructions.push_back(
                Li{.dest = VR(static_cast<MachineValueId>(i)), .value = i + 1});
        }
        b.terminator = Jump{.target = 1};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 1;
        b.phis = {MachinePhi{
            .dest = VR(11),
            .incoming = {
                MachinePhiIncoming{.pred = 0, .value = VR(0)},
                MachinePhiIncoming{.pred = 2, .value = VR(12)},
            },
        }};
        b.instructions = {
            Binary{.dest = VR(13), .op = BinaryOp::Add, .lhs = VR(0), .rhs = VR(1)},
            Binary{.dest = VR(14), .op = BinaryOp::Add, .lhs = VR(2), .rhs = VR(3)},
            Binary{.dest = VR(15), .op = BinaryOp::Add, .lhs = VR(4), .rhs = VR(5)},
            Binary{.dest = VR(16), .op = BinaryOp::Add, .lhs = VR(6), .rhs = VR(7)},
            Binary{.dest = VR(17), .op = BinaryOp::Add, .lhs = VR(8), .rhs = VR(9)},
            Binary{.dest = VR(18), .op = BinaryOp::Add, .lhs = VR(13), .rhs = VR(14)},
            Binary{.dest = VR(19), .op = BinaryOp::Add, .lhs = VR(15), .rhs = VR(16)},
            Binary{.dest = VR(20), .op = BinaryOp::Add, .lhs = VR(18), .rhs = VR(19)},
            Binary{.dest = VR(21), .op = BinaryOp::Add, .lhs = VR(20), .rhs = VR(17)},
            Binary{.dest = VR(22), .op = BinaryOp::Add, .lhs = VR(21), .rhs = VR(10)},
            Binary{.dest = VR(23), .op = BinaryOp::Add, .lhs = VR(11), .rhs = VR(22)},
        };
        b.terminator = BranchNonZero{.condition = VR(23), .then_block = 2, .else_block = 3};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 2;
        b.instructions = {
            Binary{.dest = VR(12), .op = BinaryOp::Add, .lhs = VR(11), .rhs = VR(0)},
        };
        b.terminator = Jump{.target = 1};
        fn.blocks.push_back(b);
    }
    {
        MachineBlock b; b.id = 3;
        b.terminator = Return{.value = VR(23)};
        fn.blocks.push_back(b);
    }

    auto stats = allocate_registers(fn);
    EXPECT_GE(stats.num_spills, 1u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));
    EXPECT_TRUE(no_vregs_in_phis(fn));
    EXPECT_GE(count_spill_refs_in_phis(fn), 1u);

    MachineModule mod;
    mod.functions.push_back(fn);
    EXPECT_NO_THROW(validate_module(mod, ValidationStage::PostRegAlloc));
}

// ---- Test 3: Spill under pressure ----
// 12 simultaneously live vregs → at least 1 spill, instruction rewrite complete.
TEST(RegAllocTest, SpillUnderPressure) {
    // v0..v11: 12 li's all defined before any use, making them simultaneously live.
    // Then paired adds: v12=add(v0,v1), v13=add(v2,v3), ..., v17=add(v10,v11)
    // Then chained: v18=add(v12,v13), ..., v22=add(v20,v21)
    MachineFunction fn;
    fn.symbol = "pressure"; fn.entry_block = 0; fn.next_value = 23;
    MachineBlock b; b.id = 0;

    for (int i = 0; i < 12; ++i) {
        b.instructions.push_back(Li{.dest = VR(static_cast<MachineValueId>(i)), .value = i + 1});
    }
    // paired adds
    for (int i = 0; i < 6; ++i) {
        b.instructions.push_back(Binary{
            .dest = VR(static_cast<MachineValueId>(12 + i)),
            .op = BinaryOp::Add,
            .lhs = VR(static_cast<MachineValueId>(2 * i)),
            .rhs = VR(static_cast<MachineValueId>(2 * i + 1)),
        });
    }
    // chained: v18=add(v12,v13), v19=add(v14,v15), v20=add(v16,v17)
    b.instructions.push_back(Binary{.dest = VR(18), .op = BinaryOp::Add, .lhs = VR(12), .rhs = VR(13)});
    b.instructions.push_back(Binary{.dest = VR(19), .op = BinaryOp::Add, .lhs = VR(14), .rhs = VR(15)});
    b.instructions.push_back(Binary{.dest = VR(20), .op = BinaryOp::Add, .lhs = VR(16), .rhs = VR(17)});
    b.instructions.push_back(Binary{.dest = VR(21), .op = BinaryOp::Add, .lhs = VR(18), .rhs = VR(19)});
    b.instructions.push_back(Binary{.dest = VR(22), .op = BinaryOp::Add, .lhs = VR(20), .rhs = VR(21)});
    b.terminator = Return{.value = VR(22)};
    fn.blocks.push_back(b);

    auto stats = allocate_registers(fn);
    EXPECT_GE(stats.num_spills, 1u) << "12 live vregs should cause at least one spill";
    EXPECT_GE(count_spill_frames(fn), 1u);
    EXPECT_TRUE(no_vregs_in_instructions(fn));
}

// ---- Test 4: Two-source spilled Binary with spilled dest ----
// Creates pressure where add operands and dest must spill: verifies t0 aliasing.
TEST(RegAllocTest, TwoSourceSpilledBinary) {
    // Strategy: create 12 simultaneous live vregs (v0-v11), then:
    // v12 = add v0, v1; ... v17 = add v10, v11  (6 adds, each using 2 of 0-11)
    // Then v18 = add v12, v13; ... chained adds to v22.
    // This creates spill pressure and forces v12's operands and dest to contend.
    MachineFunction fn;
    fn.symbol = "spilled_binary"; fn.entry_block = 0; fn.next_value = 23;
    MachineBlock b; b.id = 0;

    for (int i = 0; i < 12; ++i) {
        b.instructions.push_back(Li{.dest = VR(static_cast<MachineValueId>(i)), .value = i + 1});
    }
    // 6 paired adds
    for (int i = 0; i < 6; ++i) {
        b.instructions.push_back(Binary{
            .dest = VR(static_cast<MachineValueId>(12 + i)),
            .op = BinaryOp::Add,
            .lhs = VR(static_cast<MachineValueId>(2 * i)),
            .rhs = VR(static_cast<MachineValueId>(2 * i + 1)),
        });
    }
    // Chained: v18=add(v12,v13), v19=add(v14,v15), v20=add(v16,v17)
    b.instructions.push_back(Binary{.dest = VR(18), .op = BinaryOp::Add, .lhs = VR(12), .rhs = VR(13)});
    b.instructions.push_back(Binary{.dest = VR(19), .op = BinaryOp::Add, .lhs = VR(14), .rhs = VR(15)});
    b.instructions.push_back(Binary{.dest = VR(20), .op = BinaryOp::Add, .lhs = VR(16), .rhs = VR(17)});
    b.instructions.push_back(Binary{.dest = VR(21), .op = BinaryOp::Add, .lhs = VR(18), .rhs = VR(19)});
    b.instructions.push_back(Binary{.dest = VR(22), .op = BinaryOp::Add, .lhs = VR(20), .rhs = VR(21)});
    b.terminator = Return{.value = VR(22)};
    fn.blocks.push_back(b);

    auto stats = allocate_registers(fn);
    EXPECT_GE(stats.num_spills, 1u) << "high live range count should force spilling";
    EXPECT_TRUE(no_vregs_in_instructions(fn));
}

// ---- Test 5: Call across live range ----
// vreg defined before call, used after; must get a callee-save sN, not aN.
// (The allocatable pool is only s1-s11, so this is structurally guaranteed.)
TEST(RegAllocTest, CallAcrossLiveRange) {
    // v0=li 42, copy a0=v0, call @foo, v1=copy a0, v2=add v0 v1, ret v2
    // v0 spans the call; since pool is only s1-s11, it cannot get an aN.
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
    EXPECT_TRUE(is_allocatable(*phys)) << "vreg spanning call should be in callee-save s1-s11";
}

// ---- Test 5: PostRegAlloc validation passes ----
TEST(RegAllocTest, PostRegAllocValidationPasses) {
    // Run each of the above function shapes through RA and verify the
    // validator does not throw.

    // shape 1: no-pressure
    {
        MachineFunction fn;
        fn.symbol = "f1"; fn.entry_block = 0; fn.next_value = 3;
        MachineBlock b; b.id = 0;
        b.instructions = {
            Li{.dest = VR(0), .value = 1},
            Li{.dest = VR(1), .value = 2},
            Binary{.dest = VR(2), .op = BinaryOp::Add, .lhs = VR(0), .rhs = VR(1)},
        };
        b.terminator = Return{.value = VR(2)};
        fn.blocks.push_back(b);
        MachineModule mod; mod.functions.push_back(std::move(fn));
        allocate_registers(mod);
        EXPECT_NO_THROW(validate_module(mod, ValidationStage::PostRegAlloc));
    }

    // shape 2: phi loop (from test 2 above)
    {
        MachineFunction fn;
        fn.symbol = "f2"; fn.entry_block = 0; fn.next_value = 6;
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
        MachineModule mod; mod.functions.push_back(std::move(fn));
        allocate_registers(mod);
        EXPECT_NO_THROW(validate_module(mod, ValidationStage::PostRegAlloc));
    }

    // shape 3: pressure (from test 3 above)
    {
        MachineFunction fn;
        fn.symbol = "f3"; fn.entry_block = 0; fn.next_value = 23;
        MachineBlock b; b.id = 0;
        for (int i = 0; i < 12; ++i) {
            b.instructions.push_back(Li{.dest = VR(static_cast<MachineValueId>(i)), .value = i + 1});
        }
        for (int i = 0; i < 6; ++i) {
            b.instructions.push_back(Binary{
                .dest = VR(static_cast<MachineValueId>(12 + i)),
                .op = BinaryOp::Add,
                .lhs = VR(static_cast<MachineValueId>(2 * i)),
                .rhs = VR(static_cast<MachineValueId>(2 * i + 1)),
            });
        }
        b.instructions.push_back(Binary{.dest = VR(18), .op = BinaryOp::Add, .lhs = VR(12), .rhs = VR(13)});
        b.instructions.push_back(Binary{.dest = VR(19), .op = BinaryOp::Add, .lhs = VR(14), .rhs = VR(15)});
        b.instructions.push_back(Binary{.dest = VR(20), .op = BinaryOp::Add, .lhs = VR(16), .rhs = VR(17)});
        b.instructions.push_back(Binary{.dest = VR(21), .op = BinaryOp::Add, .lhs = VR(18), .rhs = VR(19)});
        b.instructions.push_back(Binary{.dest = VR(22), .op = BinaryOp::Add, .lhs = VR(20), .rhs = VR(21)});
        b.terminator = Return{.value = VR(22)};
        fn.blocks.push_back(b);
        MachineModule mod; mod.functions.push_back(std::move(fn));
        allocate_registers(mod);
        EXPECT_NO_THROW(validate_module(mod, ValidationStage::PostRegAlloc));
    }
}

} // namespace
