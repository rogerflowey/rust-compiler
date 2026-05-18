#include "riscv/regalloc.hpp"

#include "riscv/analysis/cfg.hpp"
#include "riscv/analysis/intervals.hpp"
#include "riscv/analysis/liveness.hpp"

#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace riscv {
namespace {

// Allocatable pool: s1-s11 (callee-saved, 11 registers)
constexpr PhysicalRegister kAllocatable[] = {
    PhysicalRegister::S1,  PhysicalRegister::S2,  PhysicalRegister::S3,
    PhysicalRegister::S4,  PhysicalRegister::S5,  PhysicalRegister::S6,
    PhysicalRegister::S7,  PhysicalRegister::S8,  PhysicalRegister::S9,
    PhysicalRegister::S10, PhysicalRegister::S11,
};

constexpr PhysicalRegister kScratch0 = PhysicalRegister::T0;
constexpr PhysicalRegister kScratch1 = PhysicalRegister::T1;

struct Allocation {
    std::unordered_map<MachineValueId, PhysicalRegister> assigned;
    std::unordered_map<MachineValueId, FrameId> spilled;
};

// ---- Linear scan ----

Allocation linear_scan(const LiveIntervals& intervals, MachineFunction& fn) {
    Allocation alloc;
    std::vector<PhysicalRegister> free_regs(std::begin(kAllocatable), std::end(kAllocatable));

    struct Active {
        Position end;
        MachineValueId vreg;
        PhysicalRegister reg;
        bool operator<(const Active& o) const {
            return end < o.end || (end == o.end && vreg < o.vreg);
        }
    };
    std::set<Active> active;

    auto expire = [&](Position cur) {
        while (!active.empty() && active.begin()->end < cur) {
            free_regs.push_back(active.begin()->reg);
            active.erase(active.begin());
        }
    };

    auto do_spill = [&](MachineValueId id) {
        FrameId fid = fn.frame_objects.size();
        fn.frame_objects.push_back(FrameObject{
            .id = fid,
            .kind = FrameObjectKind::Spill,
            .size = 4,
            .align = 4,
            .host_type = semantic::invalid_type_id,
            .spill_class = RegisterClass::Gpr32,
            .source_slot = std::nullopt,
            .debug_name = "",
        });
        alloc.spilled.emplace(id, fid);
    };

    // Return the next use position after `after`, or a maximum sentinel for dead defs.
    // The sentinel is std::numeric_limits<Position>::max(), which is always chosen as spill victim.
    auto next_use = [&](const LiveInterval& iv, Position after) -> Position {
        for (Position u : iv.uses) {
            if (u > after) return u;
        }
        // No future use: treat as dead def (will be spilled in preference to live ranges).
        return std::numeric_limits<Position>::max();
    };

    std::unordered_map<MachineValueId, const LiveInterval*> iv_map;
    for (const auto& iv : intervals.intervals) {
        iv_map[iv.vreg] = &iv;
    }

    for (const auto& iv : intervals.intervals) {
        expire(iv.start);

        if (!free_regs.empty()) {
            PhysicalRegister reg = free_regs.back();
            free_regs.pop_back();
            alloc.assigned.emplace(iv.vreg, reg);
            active.insert({iv.end, iv.vreg, reg});
            continue;
        }

        // Farthest-next-use spill heuristic
        Position cur_next = next_use(iv, iv.start);
        Active best_victim;
        bool found_victim = false;

        for (const auto& a : active) {
            Position vn = next_use(*iv_map.at(a.vreg), iv.start);
            if (vn > cur_next) {
                if (!found_victim ||
                    vn > next_use(*iv_map.at(best_victim.vreg), iv.start)) {
                    best_victim = a;
                    found_victim = true;
                }
            }
        }

        if (!found_victim) {
            do_spill(iv.vreg);
        } else {
            active.erase(best_victim);
            do_spill(best_victim.vreg);
            alloc.assigned.erase(best_victim.vreg);
            alloc.assigned.emplace(iv.vreg, best_victim.reg);
            active.insert({iv.end, iv.vreg, best_victim.reg});
        }
    }

    return alloc;
}

// ---- Rewrite helpers ----

// Returns the assigned physical register for a vreg, or nullopt if spilled.
std::optional<PhysicalRegister> lookup(MachineValueId id, const Allocation& alloc) {
    if (const auto it = alloc.assigned.find(id); it != alloc.assigned.end()) {
        return it->second;
    }
    return std::nullopt;
}

SpillRef spilled_ref(const VirtualRegister& reg, const Allocation& alloc) {
    return SpillRef{
        .frame = alloc.spilled.at(reg.id),
        .reg_class = reg.reg_class,
    };
}

// Rewrite a RegisterRef used as a source operand.
// If the vreg is spilled, a load is inserted into `pre` and a scratch register is returned.
// We allocate t0 for the first spilled source, t1 for the second. A third would exceed
// the 2-scratch budget and is an error.
RegisterRef rewrite_src(const RegisterRef& reg,
                        const Allocation& alloc,
                        std::vector<Instruction>& pre,
                        bool& scratch0_used,
                        bool& scratch1_used) {
    if (const auto* phys = std::get_if<PhysicalRegister>(&reg)) {
        return *phys;
    }
    if (std::holds_alternative<SpillRef>(reg)) {
        throw std::runtime_error("unexpected SpillRef during instruction rewrite");
    }
    MachineValueId id = std::get<VirtualRegister>(reg).id;
    if (const auto phys = lookup(id, alloc)) {
        return *phys;
    }
    // Spilled: pick a scratch register
    PhysicalRegister scr;
    if (!scratch0_used) {
        scr = kScratch0;
        scratch0_used = true;
    } else if (!scratch1_used) {
        scr = kScratch1;
        scratch1_used = true;
    } else {
        throw std::runtime_error("Exceeded 2-scratch-register budget in instruction rewrite");
    }
    pre.push_back(Load{
        .dest = scr,
        .address = FrameAddress{.frame = alloc.spilled.at(id), .offset = 0},
    });
    return scr;
}

// Rewrite instruction dest vreg to physical register.
// If spilled, uses kScratch0 for the def and emits a store to `post`.
// IMPORTANT: this always returns kScratch0, which must match the first spilled source
// (if any) to implement the aliasing on RV32IM (e.g., dest=rd, lhs=rs1, both aliased to t0).
// The caller must ensure rewrite_src is called for sources before this for defs.
RegisterRef rewrite_dest(MachineValueId id,
                         const Allocation& alloc,
                         std::vector<Instruction>& post) {
    if (const auto phys = lookup(id, alloc)) {
        return *phys;
    }
    // Spilled: use kScratch0, store after. Any spilled lhs will also use kScratch0 (aliased).
    post.push_back(Store{
        .address = FrameAddress{.frame = alloc.spilled.at(id), .offset = 0},
        .src = kScratch0,
    });
    return kScratch0;
}

// Rewrite address base
Address rewrite_address(const Address& addr,
                        const Allocation& alloc,
                        std::vector<Instruction>& pre,
                        bool& scratch0_used,
                        bool& scratch1_used) {
    return std::visit(
        [&](const auto& a) -> Address {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, RegisterAddress>) {
                RegisterRef new_base = rewrite_src(a.base, alloc, pre, scratch0_used, scratch1_used);
                return RegisterAddress{.base = new_base, .offset = a.offset};
            } else {
                return a; // FrameAddress unchanged
            }
        },
        addr);
}

// Rewrite one instruction, producing pre/post instruction lists for loads/stores.
Instruction rewrite_instruction(const Instruction& inst,
                                const Allocation& alloc,
                                std::vector<Instruction>& pre,
                                std::vector<Instruction>& post) {
    return std::visit(
        [&](const auto& value) -> Instruction {
            using T = std::decay_t<decltype(value)>;
            bool scratch0_used = false, scratch1_used = false;

            if constexpr (std::is_same_v<T, Copy>) {
                RegisterRef new_src = rewrite_src(value.src, alloc, pre, scratch0_used, scratch1_used);
                RegisterRef new_dest;
                if (const auto* phys = std::get_if<PhysicalRegister>(&value.dest)) {
                    new_dest = *phys;
                } else {
                    MachineValueId id = std::get<VirtualRegister>(value.dest).id;
                    new_dest = rewrite_dest(id, alloc, post);
                }
                return Copy{.dest = new_dest, .src = new_src};
            } else if constexpr (std::is_same_v<T, Li>) {
                MachineValueId id = std::get<VirtualRegister>(value.dest).id;
                RegisterRef new_dest = rewrite_dest(id, alloc, post);
                return Li{.dest = new_dest, .value = value.value};
            } else if constexpr (std::is_same_v<T, Binary>) {
                RegisterRef new_lhs = rewrite_src(value.lhs, alloc, pre, scratch0_used, scratch1_used);
                RegisterRef new_rhs = rewrite_src(value.rhs, alloc, pre, scratch0_used, scratch1_used);
                MachineValueId id = std::get<VirtualRegister>(value.dest).id;
                RegisterRef new_dest = rewrite_dest(id, alloc, post);
                return Binary{.dest = new_dest, .op = value.op, .lhs = new_lhs, .rhs = new_rhs};
            } else if constexpr (std::is_same_v<T, Compare>) {
                RegisterRef new_lhs = rewrite_src(value.lhs, alloc, pre, scratch0_used, scratch1_used);
                RegisterRef new_rhs = rewrite_src(value.rhs, alloc, pre, scratch0_used, scratch1_used);
                MachineValueId id = std::get<VirtualRegister>(value.dest).id;
                RegisterRef new_dest = rewrite_dest(id, alloc, post);
                return Compare{
                    .dest = new_dest, .op = value.op, .lhs = new_lhs, .rhs = new_rhs};
            } else if constexpr (std::is_same_v<T, FrameAddr>) {
                MachineValueId id = std::get<VirtualRegister>(value.dest).id;
                RegisterRef new_dest = rewrite_dest(id, alloc, post);
                return FrameAddr{.dest = new_dest, .frame = value.frame, .offset = value.offset};
            } else if constexpr (std::is_same_v<T, Load>) {
                Address new_addr = rewrite_address(value.address, alloc, pre, scratch0_used, scratch1_used);
                MachineValueId id = std::get<VirtualRegister>(value.dest).id;
                RegisterRef new_dest = rewrite_dest(id, alloc, post);
                return Load{.dest = new_dest, .address = new_addr};
            } else if constexpr (std::is_same_v<T, Store>) {
                Address new_addr = rewrite_address(value.address, alloc, pre, scratch0_used, scratch1_used);
                RegisterRef new_src = rewrite_src(value.src, alloc, pre, scratch0_used, scratch1_used);
                return Store{.address = new_addr, .src = new_src};
            } else {
                return value; // Call: no register operands
            }
        },
        inst);
}

// Rewrite a register operand that appears in a terminator (branch condition or return value).
// Delegates to rewrite_src but appends any spill loads directly to the provided vector.
RegisterRef rewrite_register_ref(const RegisterRef& reg, const Allocation& alloc,
                                  std::vector<Instruction>& spill_loads) {
    bool s0 = false, s1 = false;
    return rewrite_src(reg, alloc, spill_loads, s0, s1);
}

RegisterRef rewrite_phi_ref(const RegisterRef& reg, const Allocation& alloc) {
    return std::visit(
        [&](const auto& value) -> RegisterRef {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, PhysicalRegister> ||
                          std::is_same_v<T, SpillRef>) {
                return value;
            } else {
                if (const auto phys = lookup(value.id, alloc)) {
                    return *phys;
                }
                return spilled_ref(value, alloc);
            }
        },
        reg);
}

void rewrite_block(MachineBlock& block, const Allocation& alloc) {
    // Rewrite instructions
    std::vector<Instruction> new_instrs;
    new_instrs.reserve(block.instructions.size() * 2);

    for (const auto& inst : block.instructions) {
        std::vector<Instruction> pre, post;
        Instruction rewritten = rewrite_instruction(inst, alloc, pre, post);
        for (auto& p : pre) new_instrs.push_back(std::move(p));
        new_instrs.push_back(std::move(rewritten));
        for (auto& p : post) new_instrs.push_back(std::move(p));
    }
    block.instructions = std::move(new_instrs);

    // Rewrite terminator
    if (block.terminator) {
        block.terminator = std::visit(
            [&](const auto& term) -> Terminator {
                using T = std::decay_t<decltype(term)>;
                if constexpr (std::is_same_v<T, BranchNonZero>) {
                    std::vector<Instruction> loads;
                    RegisterRef new_cond =
                        rewrite_register_ref(term.condition, alloc, loads);
                    for (auto& l : loads) block.instructions.push_back(std::move(l));
                    return BranchNonZero{.condition = new_cond,
                                        .then_block = term.then_block,
                                        .else_block = term.else_block};
                } else if constexpr (std::is_same_v<T, Return>) {
                    if (!term.value) return term;
                    std::vector<Instruction> loads;
                    RegisterRef new_val =
                        rewrite_register_ref(*term.value, alloc, loads);
                    for (auto& l : loads) block.instructions.push_back(std::move(l));
                    return Return{.value = new_val};
                } else {
                    return term;
                }
            },
            *block.terminator);
    }

    // Rewrite phi operands
    for (auto& phi : block.phis) {
        phi.dest = rewrite_phi_ref(phi.dest, alloc);
        for (auto& inc : phi.incoming) {
            inc.value = rewrite_phi_ref(inc.value, alloc);
        }
    }
}

} // namespace

AllocationStats allocate_registers(MachineFunction& fn) {
    const auto cfg = compute_cfg(fn);
    const auto live = compute_liveness(fn, cfg);
    const auto intervals = compute_intervals(fn, cfg, live);

    Allocation alloc = linear_scan(intervals, fn);

    for (auto& block : fn.blocks) {
        rewrite_block(block, alloc);
    }

    AllocationStats stats;
    stats.num_spills = alloc.spilled.size();
    stats.spill_map = alloc.spilled;
    return stats;
}

void allocate_registers(MachineModule& module) {
    for (auto& fn : module.functions) {
        allocate_registers(fn);
    }
}

} // namespace riscv
