#include "riscv/analysis/intervals.hpp"

#include <algorithm>
#include <type_traits>
#include <unordered_map>

namespace riscv {
namespace {

struct IntervalBuilder {
    std::unordered_map<MachineValueId, LiveInterval> map;

    void set_start(MachineValueId id, Position pos) {
        auto& iv = map[id];
        iv.vreg = id;
        iv.start = pos;
        if (iv.end < pos) {
            iv.end = pos;
        }
    }

    void extend_to(MachineValueId id, Position pos) {
        auto& iv = map[id];
        iv.vreg = id;
        if (pos > iv.end) {
            iv.end = pos;
        }
    }

    void record_use(MachineValueId id, Position pos) {
        auto& iv = map[id];
        iv.vreg = id;
        iv.uses.push_back(pos);
        if (pos > iv.end) {
            iv.end = pos;
        }
    }
};

template <class Fn>
void for_each_vreg_use(const Instruction& inst, Fn&& fn) {
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
        },
        inst);
}

template <class Fn>
void for_each_terminator_vreg_use(const Terminator& term, Fn&& fn) {
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

std::optional<MachineValueId> instr_def(const Instruction& inst) {
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

LiveIntervals compute_intervals(const MachineFunction& fn,
                                const CfgInfo& cfg,
                                const LivenessInfo& live) {
    const std::size_t n = fn.blocks.size();
    LiveIntervals result;
    result.block_range.resize(n);

    // Assign positions: each instruction/terminator gets a unique even position.
    // Block entry position = position of first instruction (or terminator if empty).
    // Phi dests live from block entry; phi incomings are uses at predecessor's terminator.
    std::vector<Position> instr_positions; // not stored; used during build
    Position pos = 0;

    // Compute block ranges and assign positions
    std::vector<std::vector<Position>> block_instr_pos(n);
    std::vector<Position> block_term_pos(n);

    for (std::size_t rpo_idx = 0; rpo_idx < cfg.rpo.size(); ++rpo_idx) {
        const std::size_t bi = cfg.rpo[rpo_idx];
        const auto& block = fn.blocks[bi];
        const Position block_start = pos;

        for (std::size_t k = 0; k < block.instructions.size(); ++k) {
            block_instr_pos[bi].push_back(pos);
            pos += 2;
        }
        block_term_pos[bi] = pos;
        pos += 2;
        result.block_range[bi] = {block_start, pos};
    }

    IntervalBuilder builder;

    // Walk blocks in RPO to build intervals
    for (std::size_t rpo_idx = 0; rpo_idx < cfg.rpo.size(); ++rpo_idx) {
        const std::size_t bi = cfg.rpo[rpo_idx];
        const auto& block = fn.blocks[bi];
        const Position block_start = result.block_range[bi].first;
        const Position block_end = block_term_pos[bi]; // terminator position

        // Phi destinations: defined at block entry
        for (const auto& phi : block.phis) {
            if (const auto* vr = std::get_if<VirtualRegister>(&phi.dest)) {
                builder.set_start(vr->id, block_start);
            }
        }

        // Extend live-out vregs through the whole block
        for (MachineValueId vid : live.live_out[bi]) {
            builder.extend_to(vid, block_end);
        }

        // Instructions
        for (std::size_t k = 0; k < block.instructions.size(); ++k) {
            const Position p = block_instr_pos[bi][k];
            for_each_vreg_use(block.instructions[k], [&](MachineValueId id) {
                builder.record_use(id, p);
            });
            if (const auto def = instr_def(block.instructions[k])) {
                builder.set_start(*def, p);
            }
        }

        // Terminator uses
        if (block.terminator) {
            const Position tp = block_term_pos[bi];
            for_each_terminator_vreg_use(*block.terminator, [&](MachineValueId id) {
                builder.record_use(id, tp);
            });
        }

        // Phi-incoming uses: recorded at this block's terminator position
        // (the incoming value must be live until we leave this block)
        for (BlockId succ_bid : cfg.successors[bi]) {
            const std::size_t succ_idx = cfg.index_of.at(succ_bid);
            const auto& succ_block = fn.blocks[succ_idx];
            for (const auto& phi : succ_block.phis) {
                for (const auto& inc : phi.incoming) {
                    if (inc.pred == block.id) {
                        if (const auto* vr = std::get_if<VirtualRegister>(&inc.value)) {
                            builder.record_use(vr->id, block_term_pos[bi]);
                        }
                    }
                }
            }
        }
    }

    // Collect and sort intervals
    result.intervals.reserve(builder.map.size());
    for (auto& [id, iv] : builder.map) {
        // Sort uses
        std::sort(iv.uses.begin(), iv.uses.end());
        // Deduplicate uses
        iv.uses.erase(std::unique(iv.uses.begin(), iv.uses.end()), iv.uses.end());
        result.intervals.push_back(std::move(iv));
    }
    // Sort by start position, with vreg id as tiebreaker for determinism.
    // Linear scan allocation depends on order; unstable sort can produce different results.
    std::sort(result.intervals.begin(), result.intervals.end(),
              [](const LiveInterval& a, const LiveInterval& b) {
                  if (a.start != b.start) return a.start < b.start;
                  return a.vreg < b.vreg;
              });

    return result;
}

} // namespace riscv
