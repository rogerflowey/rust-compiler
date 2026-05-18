#include "riscv/phi_elim.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace riscv {
namespace {

constexpr PhysicalRegister kCycleScratch = PhysicalRegister::T0;
constexpr PhysicalRegister kSpillScratch = PhysicalRegister::T1;

struct CopyLocation {
    std::variant<PhysicalRegister, FrameId> value;

    static CopyLocation reg(PhysicalRegister reg) {
        return CopyLocation{.value = reg};
    }

    static CopyLocation spill(FrameId frame) {
        return CopyLocation{.value = frame};
    }

    bool operator==(const CopyLocation& other) const {
        return value == other.value;
    }
};

struct EdgeMove {
    CopyLocation dest;
    CopyLocation src;
};

using EdgeKey = std::pair<BlockId, BlockId>;

CopyLocation lower_location(const RegisterRef& reg) {
    return std::visit(
        [](const auto& value) -> CopyLocation {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, PhysicalRegister>) {
                if (value == kCycleScratch || value == kSpillScratch) {
                    throw std::runtime_error(
                        "phi elimination requires t0/t1 to stay reserved scratch registers");
                }
                return CopyLocation::reg(value);
            } else if constexpr (std::is_same_v<T, SpillRef>) {
                return CopyLocation::spill(value.frame);
            } else {
                throw std::runtime_error(
                    "phi elimination expects post-RA Machine IR without virtual registers");
            }
        },
        reg);
}

bool is_register(const CopyLocation& location) {
    return std::holds_alternative<PhysicalRegister>(location.value);
}

PhysicalRegister as_register(const CopyLocation& location) {
    return std::get<PhysicalRegister>(location.value);
}

FrameId as_spill_frame(const CopyLocation& location) {
    return std::get<FrameId>(location.value);
}

std::size_t find_block_index(const MachineFunction& fn, BlockId id) {
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        if (fn.blocks[i].id == id) {
            return i;
        }
    }
    throw std::runtime_error("phi elimination references undefined block bb" +
                             std::to_string(id));
}

std::size_t successor_count(const Terminator& terminator) {
    return std::visit(
        [](const auto& term) -> std::size_t {
            using T = std::decay_t<decltype(term)>;
            if constexpr (std::is_same_v<T, Jump>) {
                return 1;
            } else if constexpr (std::is_same_v<T, BranchNonZero>) {
                return term.then_block == term.else_block ? 1u : 2u;
            } else {
                return 0;
            }
        },
        terminator);
}

void rewrite_edge_target(Terminator& terminator, BlockId old_target, BlockId new_target) {
    bool rewritten = false;
    std::visit(
        [&](auto& term) {
            using T = std::decay_t<decltype(term)>;
            if constexpr (std::is_same_v<T, Jump>) {
                if (term.target == old_target) {
                    term.target = new_target;
                    rewritten = true;
                }
            } else if constexpr (std::is_same_v<T, BranchNonZero>) {
                if (term.then_block == old_target) {
                    term.then_block = new_target;
                    rewritten = true;
                }
                if (term.else_block == old_target) {
                    term.else_block = new_target;
                    rewritten = true;
                }
            }
        },
        terminator);

    if (!rewritten) {
        throw std::runtime_error("phi elimination could not rewrite predecessor edge target");
    }
}

void emit_move(std::vector<Instruction>& out,
               const CopyLocation& dest,
               const CopyLocation& src,
               PhysicalRegister spill_scratch) {
    if (dest == src) {
        return;
    }

    if (is_register(dest) && is_register(src)) {
        out.push_back(Copy{.dest = as_register(dest), .src = as_register(src)});
        return;
    }

    if (is_register(dest) && !is_register(src)) {
        out.push_back(Load{
            .dest = as_register(dest),
            .address = FrameAddress{.frame = as_spill_frame(src), .offset = 0},
        });
        return;
    }

    if (!is_register(dest) && is_register(src)) {
        out.push_back(Store{
            .address = FrameAddress{.frame = as_spill_frame(dest), .offset = 0},
            .src = as_register(src),
        });
        return;
    }

    out.push_back(Load{
        .dest = spill_scratch,
        .address = FrameAddress{.frame = as_spill_frame(src), .offset = 0},
    });
    out.push_back(Store{
        .address = FrameAddress{.frame = as_spill_frame(dest), .offset = 0},
        .src = spill_scratch,
    });
}

void save_cycle_value(std::vector<Instruction>& out, const CopyLocation& location) {
    if (is_register(location)) {
        out.push_back(Copy{.dest = kCycleScratch, .src = as_register(location)});
        return;
    }

    out.push_back(Load{
        .dest = kCycleScratch,
        .address = FrameAddress{.frame = as_spill_frame(location), .offset = 0},
    });
}

bool any_source_uses(const std::vector<EdgeMove>& pending, const CopyLocation& location) {
    return std::any_of(pending.begin(),
                       pending.end(),
                       [&](const EdgeMove& move) { return move.src == location; });
}

std::optional<std::size_t> ready_move_index(const std::vector<EdgeMove>& pending) {
    for (std::size_t i = 0; i < pending.size(); ++i) {
        bool blocked = false;
        for (std::size_t j = 0; j < pending.size(); ++j) {
            if (i == j) {
                continue;
            }
            if (pending[i].dest == pending[j].src) {
                blocked = true;
                break;
            }
        }
        if (!blocked) {
            return i;
        }
    }
    return std::nullopt;
}

std::vector<Instruction> resolve_parallel_copies(std::vector<EdgeMove> moves) {
    std::vector<Instruction> out;
    bool cycle_scratch_live = false;

    while (!moves.empty()) {
        moves.erase(std::remove_if(moves.begin(),
                                   moves.end(),
                                   [](const EdgeMove& move) { return move.dest == move.src; }),
                    moves.end());
        if (moves.empty()) {
            break;
        }

        if (const auto ready = ready_move_index(moves)) {
            const EdgeMove move = moves[*ready];
            emit_move(out,
                      move.dest,
                      move.src,
                      cycle_scratch_live ? kSpillScratch : kCycleScratch);
            moves.erase(moves.begin() + static_cast<std::ptrdiff_t>(*ready));

            if (cycle_scratch_live &&
                !any_source_uses(moves, CopyLocation::reg(kCycleScratch))) {
                cycle_scratch_live = false;
            }
            continue;
        }

        if (cycle_scratch_live) {
            throw std::runtime_error(
                "phi elimination encountered nested cycle pressure beyond the t0/t1 budget");
        }

        const CopyLocation saved = moves.front().dest;
        save_cycle_value(out, saved);
        for (auto& move : moves) {
            if (move.src == saved) {
                move.src = CopyLocation::reg(kCycleScratch);
            }
        }
        cycle_scratch_live = true;
    }

    return out;
}

MachineBlock make_edge_block(BlockId id,
                             BlockId succ,
                             const std::vector<Instruction>& copies,
                             const std::string& pred_name,
                             const std::string& succ_name) {
    MachineBlock block;
    block.id = id;
    if (!pred_name.empty() || !succ_name.empty()) {
        block.name = (pred_name.empty() ? "bb" + std::to_string(id) : pred_name) +
                     "_to_" +
                     (succ_name.empty() ? "bb" + std::to_string(succ) : succ_name);
    }
    block.instructions = copies;
    block.terminator = Jump{.target = succ};
    return block;
}

} // namespace

void eliminate_phis(MachineFunction& fn) {
    std::map<EdgeKey, std::vector<EdgeMove>> edge_moves;

    for (const auto& block : fn.blocks) {
        for (const auto& phi : block.phis) {
            const CopyLocation dest = lower_location(phi.dest);
            for (const auto& incoming : phi.incoming) {
                const CopyLocation src = lower_location(incoming.value);
                if (dest != src) {
                    edge_moves[{incoming.pred, block.id}].push_back(EdgeMove{
                        .dest = dest,
                        .src = src,
                    });
                }
            }
        }
    }

    BlockId next_block_id = 0;
    for (const auto& block : fn.blocks) {
        next_block_id = std::max(next_block_id, block.id + 1);
    }

    for (const auto& [edge, moves] : edge_moves) {
        if (moves.empty()) {
            continue;
        }

        const BlockId pred_id = edge.first;
        const BlockId succ_id = edge.second;
        const std::vector<Instruction> copies = resolve_parallel_copies(moves);
        if (copies.empty()) {
            continue;
        }

        const std::size_t pred_index = find_block_index(fn, pred_id);
        MachineBlock& pred = fn.blocks[pred_index];
        if (!pred.terminator) {
            throw std::runtime_error("phi elimination found predecessor without terminator");
        }

        if (successor_count(*pred.terminator) <= 1) {
            pred.instructions.insert(pred.instructions.end(), copies.begin(), copies.end());
            continue;
        }

        const std::size_t succ_index = find_block_index(fn, succ_id);
        MachineBlock edge_block =
            make_edge_block(next_block_id++,
                            succ_id,
                            copies,
                            pred.name,
                            fn.blocks[succ_index].name);
        rewrite_edge_target(*pred.terminator, succ_id, edge_block.id);
        fn.blocks.push_back(std::move(edge_block));
    }

    for (auto& block : fn.blocks) {
        block.phis.clear();
    }
}

void eliminate_phis(MachineModule& module) {
    for (auto& fn : module.functions) {
        eliminate_phis(fn);
    }
}

} // namespace riscv
