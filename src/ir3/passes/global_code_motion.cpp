#include "ir3/passes/global_code_motion.hpp"

#include "ir3/analysis/cfg.hpp"
#include "ir3/analysis/dominance.hpp"
#include "ir3/analysis/loop_info.hpp"
#include "ir3/analysis/value_use.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <vector>

namespace ir3 {
namespace {

struct Candidate {
    BlockId block = 0;
    std::size_t instruction = 0;
};

bool is_safe_to_speculate(BinaryOp op) {
    switch (op) {
    case BinaryOp::SDiv:
    case BinaryOp::UDiv:
    case BinaryOp::SRem:
    case BinaryOp::URem:
        return false;
    default:
        return true;
    }
}

std::optional<Value> hoistable_instruction_result(const Instruction& inst) {
    return std::visit(
        [](const auto& value) -> std::optional<Value> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, IConst> || std::is_same_v<T, Unary> ||
                          std::is_same_v<T, Cast>) {
                return value.result;
            } else if constexpr (std::is_same_v<T, Binary>) {
                if (is_safe_to_speculate(value.op)) {
                    return value.result;
                }
            }
            return std::nullopt;
        },
        inst);
}

template <class Fn>
void for_each_operand(const Instruction& inst, Fn&& fn) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Unary>) {
                fn(value.operand);
            } else if constexpr (std::is_same_v<T, Binary>) {
                fn(value.lhs);
                fn(value.rhs);
            } else if constexpr (std::is_same_v<T, Cast>) {
                fn(value.operand);
            }
        },
        inst);
}

std::optional<BlockId> value_def_block(const ValueUseInfo& value_use, ValueId value) {
    const auto& entry = value_use.value(value);
    if (!entry.def) {
        return std::nullopt;
    }

    return std::visit(
        [](const auto& def) -> std::optional<BlockId> {
            using T = std::decay_t<decltype(def)>;
            if constexpr (std::is_same_v<T, PhiDefSite>) {
                return def.block;
            } else if constexpr (std::is_same_v<T, InstructionDefSite>) {
                return def.block;
            }
            return std::nullopt;
        },
        *entry.def);
}

bool block_dominates_loop_latches(const DomTree& dom,
                                  const NaturalLoop& loop,
                                  BlockId block) {
    return std::all_of(loop.latches.begin(), loop.latches.end(), [&](BlockId latch) {
        return dom.dominates(block, latch);
    });
}

bool operands_available_before_loop(const ValueUseInfo& value_use,
                                    const NaturalLoop& loop,
                                    const Instruction& inst) {
    bool available = true;
    for_each_operand(
        inst,
        [&](ValueId operand) {
            if (!available) {
                return;
            }
            const auto def_block = value_def_block(value_use, operand);
            if (def_block && loop.contains(*def_block)) {
                available = false;
            }
        });
    return available;
}

bool is_loop_invariant_candidate(const Function& fn,
                                 const DomTree& dom,
                                 const ValueUseInfo& value_use,
                                 const NaturalLoop& loop,
                                 BlockId block,
                                 std::size_t instruction) {
    const auto& inst = fn.blocks[block].instructions[instruction];
    if (!hoistable_instruction_result(inst)) {
        return false;
    }
    if (!block_dominates_loop_latches(dom, loop, block)) {
        return false;
    }
    return operands_available_before_loop(value_use, loop, inst);
}

std::vector<Candidate> find_loop_candidates(const Function& fn,
                                            const DomTree& dom,
                                            const ValueUseInfo& value_use,
                                            const NaturalLoop& loop) {
    std::vector<Candidate> candidates;
    for (BlockId block : loop.blocks) {
        const auto& instructions = fn.blocks[block].instructions;
        for (std::size_t ii = 0; ii < instructions.size(); ++ii) {
            if (is_loop_invariant_candidate(fn, dom, value_use, loop, block, ii)) {
                candidates.push_back(Candidate{.block = block, .instruction = ii});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [&](const Candidate& lhs, const Candidate& rhs) {
        const auto lhs_rpo = dom.index_of.at(lhs.block);
        const auto rhs_rpo = dom.index_of.at(rhs.block);
        if (lhs_rpo != rhs_rpo) {
            return lhs_rpo < rhs_rpo;
        }
        return lhs.instruction < rhs.instruction;
    });
    return candidates;
}

bool hoist_loop_candidates(Function& fn, const NaturalLoop& loop, std::vector<Candidate> candidates) {
    if (candidates.empty()) {
        return false;
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.block != rhs.block) {
            return lhs.block > rhs.block;
        }
        return lhs.instruction > rhs.instruction;
    });

    std::vector<Instruction> hoisted;
    hoisted.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        auto& instructions = fn.blocks[candidate.block].instructions;
        hoisted.push_back(std::move(instructions[candidate.instruction]));
        instructions.erase(instructions.begin() + static_cast<std::ptrdiff_t>(candidate.instruction));
    }

    std::reverse(hoisted.begin(), hoisted.end());

    auto& preheader_insts = fn.blocks[*loop.preheader].instructions;
    preheader_insts.reserve(preheader_insts.size() + hoisted.size());
    for (auto& inst : hoisted) {
        preheader_insts.push_back(std::move(inst));
    }
    return true;
}

} // namespace

PreservedAnalyses GlobalCodeMotionPass::run(Function& fn, AnalysisManager& am) {
    const auto& cfg = am.get<CfgAnalysis>(fn);
    if (cfg.reachable_rpo.empty()) {
        return PreservedAnalyses::all();
    }

    const auto& dom = am.get<DomTreeAnalysis>(fn);
    const auto& loops = am.get<LoopInfoAnalysis>(fn);
    const auto& value_use = am.get<ValueUseAnalysis>(fn);

    bool changed = false;
    for (const auto& loop : loops.loops) {
        if (!loop.preheader) {
            continue;
        }
        auto candidates = find_loop_candidates(fn, dom, value_use, loop);
        changed = hoist_loop_candidates(fn, loop, std::move(candidates)) || changed;
    }

    if (!changed) {
        return PreservedAnalyses::all();
    }

    auto preserved = PreservedAnalyses::none();
    preserved.preserve<CfgAnalysis>();
    preserved.preserve<DomTreeAnalysis>();
    preserved.preserve<LoopInfoAnalysis>();
    return preserved;
}

} // namespace ir3
