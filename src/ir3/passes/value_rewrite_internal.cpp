#include "ir3/passes/value_rewrite_internal.hpp"

#include <type_traits>

namespace ir3::detail {
namespace {

void rewrite_value_ref(const std::unordered_map<ValueId, ValueId>& replacements, ValueId& value) {
    value = resolve_replacement(replacements, value);
}

void rewrite_place(const std::unordered_map<ValueId, ValueId>& replacements, Place& place) {
    std::visit(
        [&](auto& base) {
            using T = std::decay_t<decltype(base)>;
            if constexpr (std::is_same_v<T, DerefBase>) {
                rewrite_value_ref(replacements, base.ptr);
            }
        },
        place.base);

    for (auto& projection : place.projections) {
        if (auto* index = std::get_if<IndexProjection>(&projection)) {
            rewrite_value_ref(replacements, index->index);
        }
    }
}

void rewrite_instruction_uses(const std::unordered_map<ValueId, ValueId>& replacements,
                              Instruction& inst) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Load>) {
                rewrite_place(replacements, value.source);
            } else if constexpr (std::is_same_v<T, Store>) {
                rewrite_place(replacements, value.dest);
                rewrite_value_ref(replacements, value.value);
            } else if constexpr (std::is_same_v<T, Copy>) {
                rewrite_place(replacements, value.dest);
                rewrite_place(replacements, value.source);
            } else if constexpr (std::is_same_v<T, Borrow>) {
                rewrite_place(replacements, value.source);
            } else if constexpr (std::is_same_v<T, Unary>) {
                rewrite_value_ref(replacements, value.operand);
            } else if constexpr (std::is_same_v<T, Binary>) {
                rewrite_value_ref(replacements, value.lhs);
                rewrite_value_ref(replacements, value.rhs);
            } else if constexpr (std::is_same_v<T, Cast>) {
                rewrite_value_ref(replacements, value.operand);
            } else if constexpr (std::is_same_v<T, Call>) {
                for (auto& arg : value.args) {
                    rewrite_value_ref(replacements, arg);
                }
            }
        },
        inst);
}

void rewrite_terminator_uses(const std::unordered_map<ValueId, ValueId>& replacements,
                             Terminator& term) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Branch>) {
                rewrite_value_ref(replacements, value.condition);
            } else if constexpr (std::is_same_v<T, Return>) {
                if (value.value) {
                    rewrite_value_ref(replacements, *value.value);
                }
            }
        },
        term);
}

} // namespace

ValueId resolve_replacement(const std::unordered_map<ValueId, ValueId>& replacements,
                            ValueId value) {
    ValueId current = value;
    while (true) {
        const auto it = replacements.find(current);
        if (it == replacements.end() || it->second == current) {
            return current;
        }
        current = it->second;
    }
}

void rewrite_all_uses(Function& fn, const std::unordered_map<ValueId, ValueId>& replacements) {
    if (replacements.empty()) {
        return;
    }

    for (auto& block : fn.blocks) {
        for (auto& phi : block.phis) {
            for (auto& incoming : phi.incoming) {
                rewrite_value_ref(replacements, incoming.value);
            }
        }
        for (auto& inst : block.instructions) {
            rewrite_instruction_uses(replacements, inst);
        }
        if (block.terminator) {
            rewrite_terminator_uses(replacements, *block.terminator);
        }
    }
}

} // namespace ir3::detail
