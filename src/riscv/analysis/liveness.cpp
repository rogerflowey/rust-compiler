#include "riscv/analysis/liveness.hpp"

#include <cstdint>
#include <type_traits>
#include <unordered_map>
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
            } else if constexpr (std::is_same_v<T, ShiftImm>) {
                use(value.lhs);
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
            } else if constexpr (std::is_same_v<T, BranchCond>) {
                use(value.lhs);
                use(value.rhs);
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
                                 std::is_same_v<T, ShiftImm> ||
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

    std::unordered_map<MachineValueId, std::size_t> dense_index;
    std::vector<MachineValueId> dense_values;
    auto add_vreg = [&](MachineValueId id) {
        if (dense_index.contains(id)) {
            return;
        }
        dense_index.emplace(id, dense_values.size());
        dense_values.push_back(id);
    };

    for (const auto& block : fn.blocks) {
        for (const auto& phi : block.phis) {
            if (const auto* vr = std::get_if<VirtualRegister>(&phi.dest)) {
                add_vreg(vr->id);
            }
            for (const auto& inc : phi.incoming) {
                if (const auto* vr = std::get_if<VirtualRegister>(&inc.value)) {
                    add_vreg(vr->id);
                }
            }
        }
        for (const auto& inst : block.instructions) {
            for_each_use(inst, add_vreg);
            if (const auto def = def_vreg(inst)) {
                add_vreg(*def);
            }
        }
        if (block.terminator) {
            for_each_terminator_use(*block.terminator, add_vreg);
        }
    }

    using Word = std::uint64_t;
    constexpr std::size_t kWordBits = sizeof(Word) * 8;
    const std::size_t word_count = (dense_values.size() + kWordBits - 1) / kWordBits;

    auto empty_bits = [&] { return std::vector<Word>(word_count, 0); };
    auto set_bit = [&](std::vector<Word>& bits, MachineValueId id) {
        const std::size_t index = dense_index.at(id);
        bits[index / kWordBits] |= Word{1} << (index % kWordBits);
    };
    auto has_bit = [&](const std::vector<Word>& bits, MachineValueId id) {
        const std::size_t index = dense_index.at(id);
        return (bits[index / kWordBits] & (Word{1} << (index % kWordBits))) != 0;
    };
    auto union_into = [](std::vector<Word>& dest, const std::vector<Word>& src) {
        for (std::size_t i = 0; i < dest.size(); ++i) {
            dest[i] |= src[i];
        }
    };
    auto emit_set = [&](const std::vector<Word>& bits,
                        std::unordered_set<MachineValueId>& out) {
        out.clear();
        for (std::size_t i = 0; i < dense_values.size(); ++i) {
            if ((bits[i / kWordBits] & (Word{1} << (i % kWordBits))) != 0) {
                out.insert(dense_values[i]);
            }
        }
    };

    // Precompute upward-exposed uses and kill sets for each block.
    // kill[i]  = vregs defined in block i (phi dests + instruction defs)
    // upward[i] = vregs used before defined in block i (phi incomings excluded)
    std::vector<std::vector<Word>> upward(n, empty_bits());
    std::vector<std::vector<Word>> kill(n, empty_bits());

    // phi_edge_uses[i] = vregs used in phi-incomings whose predecessor is block i
    // (these are live at end of block i per the SSA liveness convention)
    std::vector<std::vector<Word>> phi_edge_uses(n, empty_bits());

    for (std::size_t i = 0; i < n; ++i) {
        const auto& block = fn.blocks[i];

        // Phi dests defined at block entry
        for (const auto& phi : block.phis) {
            if (const auto* vr = std::get_if<VirtualRegister>(&phi.dest)) {
                set_bit(kill[i], vr->id);
            }
            // Phi incomings: register edge uses at predecessor's exit
            for (const auto& inc : phi.incoming) {
                if (const auto* vr = std::get_if<VirtualRegister>(&inc.value)) {
                    const auto pred_idx = cfg.index_of.at(inc.pred);
                    set_bit(phi_edge_uses[pred_idx], vr->id);
                }
            }
        }

        // Track what's defined locally (phi dests + instruction defs)
        auto local_def = empty_bits();
        for (const auto& phi : block.phis) {
            if (const auto* vr = std::get_if<VirtualRegister>(&phi.dest)) {
                set_bit(local_def, vr->id);
            }
        }

        for (const auto& inst : block.instructions) {
            for_each_use(inst, [&](MachineValueId id) {
                if (!has_bit(local_def, id)) {
                    set_bit(upward[i], id);
                }
            });
            if (const auto def = def_vreg(inst)) {
                set_bit(kill[i], *def);
                set_bit(local_def, *def);
            }
        }

        if (block.terminator) {
            for_each_terminator_use(*block.terminator, [&](MachineValueId id) {
                if (!has_bit(local_def, id)) {
                    set_bit(upward[i], id);
                }
            });
        }
    }

    std::vector<std::vector<Word>> live_in_bits(n, empty_bits());
    std::vector<std::vector<Word>> live_out_bits(n, empty_bits());

    // Iterative backward dataflow until fixed point.
    bool changed = true;
    while (changed) {
        changed = false;
        // Walk in reverse RPO for faster convergence (backward pass)
        for (int ri = static_cast<int>(cfg.rpo.size()) - 1; ri >= 0; --ri) {
            const std::size_t i = cfg.rpo[static_cast<std::size_t>(ri)];

            // live_out[i] = phi_edge_uses[i] + union(live_in[succ]) for each successor
            auto new_out = phi_edge_uses[i];
            for (BlockId succ_bid : cfg.successors[i]) {
                const std::size_t succ = cfg.index_of.at(succ_bid);
                union_into(new_out, live_in_bits[succ]);
            }

            // live_in[i] = upward[i] + (live_out[i] - kill[i])
            auto new_in = upward[i];
            for (std::size_t word = 0; word < word_count; ++word) {
                new_in[word] |= new_out[word] & ~kill[i][word];
            }

            if (new_in != live_in_bits[i] || new_out != live_out_bits[i]) {
                live_in_bits[i] = std::move(new_in);
                live_out_bits[i] = std::move(new_out);
                changed = true;
            }
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        emit_set(live_in_bits[i], result.live_in[i]);
        emit_set(live_out_bits[i], result.live_out[i]);
    }

    return result;
}

} // namespace riscv
