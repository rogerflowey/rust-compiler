#include "riscv/analysis/liveness.hpp"

#include <type_traits>
#include <unordered_set>
#include <vector>

namespace riscv {
namespace {

// Collect all RegisterRef uses (virtual only) in an instruction.
template <class Fn>
void for_each_use(const Instruction& inst, Fn&& fn) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            auto use = [&](const RegisterRef& reg) {
                if (const auto* vr = std::get_if<VirtualRegister>(&reg)) {
                    fn(vr->id);
                }
            };
            if constexpr (std::is_same_v<T, Copy>) {
                use(value.src);
            } else if constexpr (std::is_same_v<T, Binary>) {
                use(value.lhs);
                use(value.rhs);
            } else if constexpr (std::is_same_v<T, Compare>) {
                use(value.lhs);
                use(value.rhs);
            } else if constexpr (std::is_same_v<T, Load>) {
                std::visit(
                    [&](const auto& addr) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(addr)>, RegisterAddress>) {
                            use(addr.base);
                        }
                    },
                    value.address);
            } else if constexpr (std::is_same_v<T, Store>) {
                use(value.src);
                std::visit(
                    [&](const auto& addr) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(addr)>, RegisterAddress>) {
                            use(addr.base);
                        }
                    },
                    value.address);
            }
            // Li, FrameAddr, Call: no register uses
        },
        inst);
}

template <class Fn>
void for_each_terminator_use(const Terminator& term, Fn&& fn) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            auto use = [&](const RegisterRef& reg) {
                if (const auto* vr = std::get_if<VirtualRegister>(&reg)) {
                    fn(vr->id);
                }
            };
            if constexpr (std::is_same_v<T, BranchNonZero>) {
                use(value.condition);
            } else if constexpr (std::is_same_v<T, Return>) {
                if (value.value) {
                    use(*value.value);
                }
            }
        },
        term);
}

std::optional<MachineValueId> def_vreg(const Instruction& inst) {
    return std::visit(
        [](const auto& value) -> std::optional<MachineValueId> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Copy>) {
                if (const auto* vr = std::get_if<VirtualRegister>(&value.dest)) {
                    return vr->id;
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, Li> || std::is_same_v<T, Binary> ||
                                 std::is_same_v<T, Compare> || std::is_same_v<T, FrameAddr> ||
                                 std::is_same_v<T, Load>) {
                if (const auto* vr = std::get_if<VirtualRegister>(&value.dest)) {
                    return vr->id;
                }
                return std::nullopt;
            } else {
                return std::nullopt;
            }
        },
        inst);
}

} // namespace

LivenessInfo compute_liveness(const MachineFunction& fn, const CfgInfo& cfg) {
    const std::size_t n = fn.blocks.size();
    LivenessInfo result;
    result.live_in.resize(n);
    result.live_out.resize(n);

    // Precompute upward-exposed uses and kill sets for each block.
    // kill[i]  = vregs defined in block i (phi dests + instruction defs)
    // upward[i] = vregs used before defined in block i (phi incomings excluded)
    std::vector<std::unordered_set<MachineValueId>> upward(n);
    std::vector<std::unordered_set<MachineValueId>> kill(n);

    // phi_edge_uses[i] = vregs used in phi-incomings whose predecessor is block i
    // (these are live at end of block i per the SSA liveness convention)
    std::vector<std::unordered_set<MachineValueId>> phi_edge_uses(n);

    for (std::size_t i = 0; i < n; ++i) {
        const auto& block = fn.blocks[i];

        // Phi dests defined at block entry
        for (const auto& phi : block.phis) {
            if (const auto* vr = std::get_if<VirtualRegister>(&phi.dest)) {
                kill[i].insert(vr->id);
            }
            // Phi incomings: register edge uses at predecessor's exit
            for (const auto& inc : phi.incoming) {
                if (const auto* vr = std::get_if<VirtualRegister>(&inc.value)) {
                    const auto pred_idx = cfg.index_of.at(inc.pred);
                    phi_edge_uses[pred_idx].insert(vr->id);
                }
            }
        }

        // Track what's defined locally (phi dests + instruction defs)
        std::unordered_set<MachineValueId> local_def;
        for (const auto& phi : block.phis) {
            if (const auto* vr = std::get_if<VirtualRegister>(&phi.dest)) {
                local_def.insert(vr->id);
            }
        }

        for (const auto& inst : block.instructions) {
            for_each_use(inst, [&](MachineValueId id) {
                if (!local_def.contains(id)) {
                    upward[i].insert(id);
                }
            });
            if (const auto def = def_vreg(inst)) {
                kill[i].insert(*def);
                local_def.insert(*def);
            }
        }

        if (block.terminator) {
            for_each_terminator_use(*block.terminator, [&](MachineValueId id) {
                if (!local_def.contains(id)) {
                    upward[i].insert(id);
                }
            });
        }
    }

    // Iterative backward dataflow until fixed point.
    bool changed = true;
    while (changed) {
        changed = false;
        // Walk in reverse RPO for faster convergence (backward pass)
        for (int ri = static_cast<int>(cfg.rpo.size()) - 1; ri >= 0; --ri) {
            const std::size_t i = cfg.rpo[static_cast<std::size_t>(ri)];

            // live_out[i] = phi_edge_uses[i] + union(live_in[succ]) for each successor
            std::unordered_set<MachineValueId> new_out = phi_edge_uses[i];
            for (BlockId succ_bid : cfg.successors[i]) {
                const std::size_t succ = cfg.index_of.at(succ_bid);
                for (MachineValueId vid : result.live_in[succ]) {
                    new_out.insert(vid);
                }
            }

            // live_in[i] = upward[i] + (live_out[i] - kill[i])
            std::unordered_set<MachineValueId> new_in = upward[i];
            for (MachineValueId vid : new_out) {
                if (!kill[i].contains(vid)) {
                    new_in.insert(vid);
                }
            }

            if (new_in != result.live_in[i] || new_out != result.live_out[i]) {
                result.live_in[i] = std::move(new_in);
                result.live_out[i] = std::move(new_out);
                changed = true;
            }
        }
    }

    return result;
}

} // namespace riscv
