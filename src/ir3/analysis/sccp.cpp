#include "ir3/analysis/sccp.hpp"

#include "ir3/analysis/cfg.hpp"
#include "ir3/analysis/manager.hpp"
#include "ir3/analysis/value_use.hpp"
#include "ir3/fixpoint/lattice.hpp"
#include "ir3/fixpoint/worklist.hpp"

#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace ir3 {
namespace {

[[noreturn]] void sccp_error(const Function& fn, const std::string& message) {
    throw std::runtime_error("IR3 SCCP analysis failed for @" + fn.symbol + ": " +
                             message);
}

bool fits_i32(std::int64_t value) {
    return value >= std::numeric_limits<std::int32_t>::min() &&
           value <= std::numeric_limits<std::int32_t>::max();
}

bool fits_u32(std::int64_t value) {
    return value >= 0 &&
           value <= static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
}

std::uint32_t iconst_bits(const Function& fn, std::int64_t value) {
    if (fits_i32(value)) {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
    }
    if (fits_u32(value)) {
        return static_cast<std::uint32_t>(value);
    }
    sccp_error(fn, "iconst value " + std::to_string(value) + " does not fit in RV32");
}

std::int32_t as_i32(std::uint32_t bits) {
    return std::bit_cast<std::int32_t>(bits);
}

std::uint32_t as_u32(std::int32_t value) {
    return std::bit_cast<std::uint32_t>(value);
}

SccpConst join_consts(const SccpConst& lhs, const SccpConst& rhs) {
    if (lhs.is_overdefined() || rhs.is_overdefined()) {
        return SccpConst::overdefined();
    }
    if (lhs.is_unknown()) {
        return rhs;
    }
    if (rhs.is_unknown()) {
        return lhs;
    }
    if (lhs.bits == rhs.bits) {
        return lhs;
    }
    return SccpConst::overdefined();
}

template <class Fn>
void for_each_place_use(const Place& place, Fn&& fn) {
    std::visit(
        [&](const auto& base) {
            using T = std::decay_t<decltype(base)>;
            if constexpr (std::is_same_v<T, DerefBase>) {
                fn(base.ptr);
            }
        },
        place.base);

    for (const auto& projection : place.projections) {
        if (const auto* index = std::get_if<IndexProjection>(&projection)) {
            fn(index->index);
        }
    }
}

class SccpSolver {
public:
    SccpSolver(const Function& fn, AnalysisManager& am)
        : fn_(fn),
          cfg_(am.get<CfgAnalysis>(fn)),
          value_use_(am.get<ValueUseAnalysis>(fn)),
          block_worklist_(fixpoint::WorklistDiscipline::Lifo),
          value_worklist_(fixpoint::WorklistDiscipline::Lifo) {
        result_.values.resize(fn_.next_value, SccpConst::unknown());
        result_.executable_blocks.resize(fn_.blocks.size(), false);
        result_.executable_edges.resize(fn_.blocks.size());
        for (const auto& block : fn_.blocks) {
            result_.executable_edges[block.id].resize(cfg_.successors[block.id].size(),
                                                      false);
        }
    }

    SccpInfo solve() {
        for (const auto& param : fn_.params) {
            update_value(param.value.id, SccpConst::overdefined());
        }
        mark_block_executable(fn_.entry_block);

        while (!block_worklist_.empty() || !value_worklist_.empty()) {
            while (!block_worklist_.empty()) {
                process_block(block_worklist_.pop());
            }
            while (!value_worklist_.empty()) {
                process_value(value_worklist_.pop());
            }
        }

        return result_;
    }

private:
    void mark_block_executable(BlockId block) {
        if (!result_.executable_blocks[block]) {
            result_.executable_blocks[block] = true;
        }
        block_worklist_.push(block);
    }

    bool mark_edge_executable(BlockId pred, std::size_t succ_index) {
        if (result_.executable_edges[pred][succ_index]) {
            return false;
        }
        result_.executable_edges[pred][succ_index] = true;
        mark_block_executable(cfg_.successors[pred][succ_index]);
        return true;
    }

    bool is_edge_executable(BlockId pred, BlockId succ) const {
        const auto& succs = cfg_.successors[pred];
        const auto& executable = result_.executable_edges[pred];
        for (std::size_t i = 0; i < succs.size(); ++i) {
            if (succs[i] == succ) {
                return executable[i];
            }
        }
        sccp_error(fn_,
                   "phi references non-successor edge bb" + std::to_string(pred) +
                       " -> bb" + std::to_string(succ));
    }

    void process_block(BlockId block) {
        if (!result_.executable_blocks[block]) {
            return;
        }

        const auto& ir_block = fn_.blocks[block];
        for (std::size_t i = 0; i < ir_block.phis.size(); ++i) {
            evaluate_phi(block, i);
        }
        for (std::size_t i = 0; i < ir_block.instructions.size(); ++i) {
            evaluate_instruction(block, i);
        }
        if (ir_block.terminator) {
            evaluate_terminator(block);
        }
    }

    void process_value(ValueId value) {
        for (const auto& use : value_use_.value(value).uses) {
            std::visit(
                [&](const auto& site) {
                    using T = std::decay_t<decltype(site)>;
                    if constexpr (std::is_same_v<T, PhiUseSite>) {
                        if (result_.executable_blocks[site.block]) {
                            evaluate_phi(site.block, site.phi_index);
                        }
                    } else if constexpr (std::is_same_v<T, InstructionUseSite>) {
                        if (result_.executable_blocks[site.block]) {
                            evaluate_instruction(site.block, site.instruction_index);
                        }
                    } else if constexpr (std::is_same_v<T, TerminatorUseSite>) {
                        if (result_.executable_blocks[site.block] &&
                            fn_.blocks[site.block].terminator) {
                            evaluate_terminator(site.block);
                        }
                    }
                },
                use);
        }
    }

    void update_value(ValueId value, const SccpConst& incoming) {
        if (fixpoint::join_assign(result_.values[value], incoming, join_consts)) {
            value_worklist_.push(value);
        }
    }

    void evaluate_phi(BlockId block, std::size_t phi_index) {
        const auto& phi = fn_.blocks[block].phis[phi_index];
        bool saw_executable_pred = false;
        SccpConst state = SccpConst::unknown();

        for (const auto& incoming : phi.incoming) {
            if (!is_edge_executable(incoming.pred, block)) {
                continue;
            }
            saw_executable_pred = true;
            state = join_consts(state, result_.value(incoming.value));
            if (state.is_overdefined()) {
                break;
            }
        }

        if (!saw_executable_pred) {
            return;
        }
        update_value(phi.result.id, state);
    }

    void evaluate_instruction(BlockId block, std::size_t instruction_index) {
        const auto& inst = fn_.blocks[block].instructions[instruction_index];
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, IConst>) {
                    update_value(value.result.id,
                                 SccpConst::constant(iconst_bits(fn_, value.value)));
                } else if constexpr (std::is_same_v<T, Load>) {
                    update_value(value.result.id, SccpConst::overdefined());
                } else if constexpr (std::is_same_v<T, Borrow>) {
                    update_value(value.result.id, SccpConst::overdefined());
                } else if constexpr (std::is_same_v<T, Unary>) {
                    update_value(value.result.id, eval_unary(value));
                } else if constexpr (std::is_same_v<T, Binary>) {
                    update_value(value.result.id, eval_binary(value));
                } else if constexpr (std::is_same_v<T, Cast>) {
                    update_value(value.result.id, eval_cast(value));
                } else if constexpr (std::is_same_v<T, Call>) {
                    if (value.result) {
                        update_value(value.result->id, SccpConst::overdefined());
                    }
                }
            },
            inst);
    }

    void evaluate_terminator(BlockId block) {
        const auto& term = *fn_.blocks[block].terminator;
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    for (std::size_t i = 0; i < cfg_.successors[block].size(); ++i) {
                        if (cfg_.successors[block][i] == value.target) {
                            mark_edge_executable(block, i);
                            return;
                        }
                    }
                    sccp_error(fn_,
                               "jump from bb" + std::to_string(block) +
                                   " references missing successor");
                } else if constexpr (std::is_same_v<T, Branch>) {
                    const auto cond = result_.value(value.condition);
                    if (cond.is_unknown()) {
                        return;
                    }
                    if (cond.is_constant()) {
                        const BlockId target =
                            cond.bits == 0 ? value.else_block : value.then_block;
                        for (std::size_t i = 0; i < cfg_.successors[block].size(); ++i) {
                            if (cfg_.successors[block][i] == target) {
                                mark_edge_executable(block, i);
                                return;
                            }
                        }
                        sccp_error(fn_,
                                   "branch from bb" + std::to_string(block) +
                                       " references missing successor");
                    }
                    for (std::size_t i = 0; i < cfg_.successors[block].size(); ++i) {
                        mark_edge_executable(block, i);
                    }
                }
            },
            term);
    }

    SccpConst eval_unary(const Unary& unary) const {
        const auto operand = result_.value(unary.operand);
        if (operand.is_overdefined()) {
            return SccpConst::overdefined();
        }
        if (!operand.is_constant()) {
            return SccpConst::unknown();
        }

        switch (unary.op) {
        case UnaryOp::SNeg:
        case UnaryOp::UNeg:
            return SccpConst::constant(0u - operand.bits);
        case UnaryOp::BoolNot:
            return SccpConst::constant(operand.bits == 0 ? 1u : 0u);
        case UnaryOp::BitNot:
            return SccpConst::constant(~operand.bits);
        }
        sccp_error(fn_, "unknown unary operator");
    }

    SccpConst eval_binary(const Binary& binary) const {
        const auto lhs = result_.value(binary.lhs);
        const auto rhs = result_.value(binary.rhs);
        if (lhs.is_overdefined() || rhs.is_overdefined()) {
            return SccpConst::overdefined();
        }
        if (!lhs.is_constant() || !rhs.is_constant()) {
            return SccpConst::unknown();
        }

        const std::uint32_t l = lhs.bits;
        const std::uint32_t r = rhs.bits;
        switch (binary.op) {
        case BinaryOp::SAdd:
        case BinaryOp::UAdd:
            return SccpConst::constant(l + r);
        case BinaryOp::SSub:
        case BinaryOp::USub:
            return SccpConst::constant(l - r);
        case BinaryOp::SMul:
        case BinaryOp::UMul:
            return SccpConst::constant(l * r);
        case BinaryOp::SDiv: {
            const auto divisor = as_i32(r);
            const auto dividend = as_i32(l);
            if (divisor == 0 ||
                (dividend == std::numeric_limits<std::int32_t>::min() &&
                 divisor == -1)) {
                return SccpConst::overdefined();
            }
            return SccpConst::constant(as_u32(dividend / divisor));
        }
        case BinaryOp::UDiv:
            if (r == 0) {
                return SccpConst::overdefined();
            }
            return SccpConst::constant(l / r);
        case BinaryOp::SRem: {
            const auto divisor = as_i32(r);
            const auto dividend = as_i32(l);
            if (divisor == 0 ||
                (dividend == std::numeric_limits<std::int32_t>::min() &&
                 divisor == -1)) {
                return SccpConst::overdefined();
            }
            return SccpConst::constant(as_u32(dividend % divisor));
        }
        case BinaryOp::URem:
            if (r == 0) {
                return SccpConst::overdefined();
            }
            return SccpConst::constant(l % r);
        case BinaryOp::BitAnd:
            return SccpConst::constant(l & r);
        case BinaryOp::BitXor:
            return SccpConst::constant(l ^ r);
        case BinaryOp::BitOr:
            return SccpConst::constant(l | r);
        case BinaryOp::SShl:
        case BinaryOp::UShl:
            if (r > 31) {
                return SccpConst::overdefined();
            }
            return SccpConst::constant(l << r);
        case BinaryOp::AShr:
            if (r > 31) {
                return SccpConst::overdefined();
            }
            return SccpConst::constant(as_u32(as_i32(l) >> r));
        case BinaryOp::LShr:
            if (r > 31) {
                return SccpConst::overdefined();
            }
            return SccpConst::constant(l >> r);
        case BinaryOp::Eq:
            return SccpConst::constant(l == r ? 1u : 0u);
        case BinaryOp::Ne:
            return SccpConst::constant(l != r ? 1u : 0u);
        case BinaryOp::SLt:
            return SccpConst::constant(as_i32(l) < as_i32(r) ? 1u : 0u);
        case BinaryOp::ULt:
            return SccpConst::constant(l < r ? 1u : 0u);
        case BinaryOp::SGt:
            return SccpConst::constant(as_i32(l) > as_i32(r) ? 1u : 0u);
        case BinaryOp::UGt:
            return SccpConst::constant(l > r ? 1u : 0u);
        case BinaryOp::SLe:
            return SccpConst::constant(as_i32(l) <= as_i32(r) ? 1u : 0u);
        case BinaryOp::ULe:
            return SccpConst::constant(l <= r ? 1u : 0u);
        case BinaryOp::SGe:
            return SccpConst::constant(as_i32(l) >= as_i32(r) ? 1u : 0u);
        case BinaryOp::UGe:
            return SccpConst::constant(l >= r ? 1u : 0u);
        }
        sccp_error(fn_, "unknown binary operator");
    }

    SccpConst eval_cast(const Cast& cast) const {
        const auto operand = result_.value(cast.operand);
        switch (cast.op) {
        case CastOp::I32ToI32:
            if (operand.is_overdefined()) {
                return SccpConst::overdefined();
            }
            if (operand.is_constant()) {
                return operand;
            }
            return SccpConst::unknown();
        case CastOp::PtrToPtr:
        case CastOp::I32ToPtr:
        case CastOp::PtrToI32:
            return SccpConst::overdefined();
        }
        sccp_error(fn_, "unknown cast operator");
    }

    const Function& fn_;
    const CfgInfo& cfg_;
    const ValueUseInfo& value_use_;
    SccpInfo result_;
    fixpoint::Worklist<BlockId> block_worklist_;
    fixpoint::Worklist<ValueId> value_worklist_;
};

} // namespace

SccpInfo SccpAnalysis::compute(const Function& fn, AnalysisManager& am) {
    return SccpSolver(fn, am).solve();
}

} // namespace ir3
