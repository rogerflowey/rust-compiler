#include "ir3/passes/global_value_numbering.hpp"

#include "ir3/analysis/cfg.hpp"
#include "ir3/analysis/dominance.hpp"
#include "ir3/passes/value_rewrite_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ir3 {
namespace {

enum class ExprKind { IConst, Unary, Binary, Cast };

struct ExprKey {
    ExprKind kind = ExprKind::IConst;
    SsaClass result_class = SsaClass::I32;
    std::int64_t const_value = 0;
    UnaryOp unary_op = UnaryOp::SNeg;
    BinaryOp binary_op = BinaryOp::SAdd;
    CastOp cast_op = CastOp::I32ToI32;
    ValueId lhs = 0;
    ValueId rhs = 0;

    auto tie() const {
        return std::tie(kind,
                        result_class,
                        const_value,
                        unary_op,
                        binary_op,
                        cast_op,
                        lhs,
                        rhs);
    }

    bool operator==(const ExprKey& other) const { return tie() == other.tie(); }
};

struct ExprKeyHash {
    std::size_t operator()(const ExprKey& key) const {
        std::size_t seed = 0;
        auto mix = [&](std::size_t value) {
            seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        };

        mix(static_cast<std::size_t>(key.kind));
        mix(static_cast<std::size_t>(key.result_class));
        mix(std::hash<std::int64_t>{}(key.const_value));
        mix(static_cast<std::size_t>(key.unary_op));
        mix(static_cast<std::size_t>(key.binary_op));
        mix(static_cast<std::size_t>(key.cast_op));
        mix(std::hash<ValueId>{}(key.lhs));
        mix(std::hash<ValueId>{}(key.rhs));
        return seed;
    }
};

bool is_commutative(BinaryOp op) {
    switch (op) {
    case BinaryOp::SAdd:
    case BinaryOp::UAdd:
    case BinaryOp::SMul:
    case BinaryOp::UMul:
    case BinaryOp::BitAnd:
    case BinaryOp::BitXor:
    case BinaryOp::BitOr:
    case BinaryOp::Eq:
    case BinaryOp::Ne:
        return true;
    default:
        return false;
    }
}

std::optional<Value> instruction_result(const Instruction& inst) {
    return std::visit(
        [](const auto& value) -> std::optional<Value> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, IConst> || std::is_same_v<T, Unary> ||
                          std::is_same_v<T, Binary> || std::is_same_v<T, Cast>) {
                return value.result;
            }
            return std::nullopt;
        },
        inst);
}

std::optional<ExprKey>
expression_key(const Instruction& inst, const std::unordered_map<ValueId, ValueId>& replacements) {
    return std::visit(
        [&](const auto& value) -> std::optional<ExprKey> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, IConst>) {
                return ExprKey{
                    .kind = ExprKind::IConst,
                    .result_class = value.result.klass,
                    .const_value = value.value,
                };
            } else if constexpr (std::is_same_v<T, Unary>) {
                return ExprKey{
                    .kind = ExprKind::Unary,
                    .result_class = value.result.klass,
                    .unary_op = value.op,
                    .lhs = detail::resolve_replacement(replacements, value.operand),
                };
            } else if constexpr (std::is_same_v<T, Binary>) {
                auto lhs = detail::resolve_replacement(replacements, value.lhs);
                auto rhs = detail::resolve_replacement(replacements, value.rhs);
                if (is_commutative(value.op) && rhs < lhs) {
                    std::swap(lhs, rhs);
                }
                return ExprKey{
                    .kind = ExprKind::Binary,
                    .result_class = value.result.klass,
                    .binary_op = value.op,
                    .lhs = lhs,
                    .rhs = rhs,
                };
            } else if constexpr (std::is_same_v<T, Cast>) {
                return ExprKey{
                    .kind = ExprKind::Cast,
                    .result_class = value.result.klass,
                    .cast_op = value.op,
                    .lhs = detail::resolve_replacement(replacements, value.operand),
                };
            }
            return std::nullopt;
        },
        inst);
}

using ExprTable = std::unordered_map<ExprKey, ValueId, ExprKeyHash>;

struct ScopedExpr {
    ExprKey key;
    std::optional<ValueId> previous;
};

struct GvnState {
    ExprTable available;
    std::vector<ScopedExpr> scope_stack;
    std::unordered_map<ValueId, ValueId> replacements;
};

void push_expr(GvnState& state, ExprKey key, ValueId value) {
    const auto it = state.available.find(key);
    state.scope_stack.push_back(ScopedExpr{
        .key = key,
        .previous = it == state.available.end() ? std::nullopt : std::optional<ValueId>(it->second),
    });
    state.available[std::move(key)] = value;
}

void pop_to_scope(GvnState& state, std::size_t scope_size) {
    while (state.scope_stack.size() > scope_size) {
        auto scoped = std::move(state.scope_stack.back());
        state.scope_stack.pop_back();
        if (scoped.previous) {
            state.available[scoped.key] = *scoped.previous;
        } else {
            state.available.erase(scoped.key);
        }
    }
}

void visit_block(const Function& fn, const DomTree& dom, BlockId block_id, GvnState& state) {
    const auto scope_size = state.scope_stack.size();
    const auto& block = fn.blocks[block_id];

    for (const auto& inst : block.instructions) {
        const auto result = instruction_result(inst);
        if (!result) {
            continue;
        }

        auto key = expression_key(inst, state.replacements);
        if (!key) {
            continue;
        }

        const auto available = state.available.find(*key);
        if (available != state.available.end()) {
            const auto replacement =
                detail::resolve_replacement(state.replacements, available->second);
            if (replacement != result->id) {
                state.replacements[result->id] = replacement;
            }
            continue;
        }

        const auto value = detail::resolve_replacement(state.replacements, result->id);
        push_expr(state, std::move(*key), value);
    }

    for (BlockId child : dom.children[block_id]) {
        visit_block(fn, dom, child, state);
    }

    pop_to_scope(state, scope_size);
}

} // namespace

PreservedAnalyses GlobalValueNumberingPass::run(Function& fn, AnalysisManager& am) {
    const auto& cfg = am.get<CfgAnalysis>(fn);
    if (cfg.reachable_rpo.empty()) {
        return PreservedAnalyses::all();
    }

    const auto& dom = am.get<DomTreeAnalysis>(fn);

    GvnState state;
    visit_block(fn, dom, fn.entry_block, state);

    if (state.replacements.empty()) {
        return PreservedAnalyses::all();
    }

    detail::rewrite_all_uses(fn, state.replacements);

    auto preserved = PreservedAnalyses::none();
    preserved.preserve<CfgAnalysis>();
    preserved.preserve<DomTreeAnalysis>();
    return preserved;
}

} // namespace ir3
