#include "ir3/passes/sccp_value_rewrite.hpp"

#include <bit>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

namespace ir3 {
namespace {

std::int64_t materialize_iconst(std::uint32_t bits) {
    return static_cast<std::int64_t>(std::bit_cast<std::int32_t>(bits));
}

std::optional<Value> instruction_result(const Instruction& inst) {
    return std::visit(
        [](const auto& value) -> std::optional<Value> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, IConst> || std::is_same_v<T, Load> ||
                          std::is_same_v<T, Borrow> || std::is_same_v<T, Unary> ||
                          std::is_same_v<T, Binary> || std::is_same_v<T, Cast>) {
                return value.result;
            } else if constexpr (std::is_same_v<T, Call>) {
                return value.result;
            }
            return std::nullopt;
        },
        inst);
}

bool is_same_iconst(const Instruction& inst, Value result, std::uint32_t bits) {
    const auto* iconst = std::get_if<IConst>(&inst);
    return iconst && iconst->result.id == result.id &&
           iconst->result.klass == result.klass &&
           iconst->value == materialize_iconst(bits);
}

} // namespace

bool SccpValueRewriter::run(Function& fn, const SccpInfo& sccp) const {
    bool changed = false;

    for (auto& block : fn.blocks) {
        std::vector<Instruction> prefix;
        std::vector<Phi> kept_phis;
        kept_phis.reserve(block.phis.size());

        for (const auto& phi : block.phis) {
            const auto state = sccp.value(phi.result.id);
            if (!state.is_constant()) {
                kept_phis.push_back(phi);
                continue;
            }
            prefix.push_back(IConst{
                .result = phi.result,
                .value = materialize_iconst(state.bits),
            });
            changed = true;
        }

        if (kept_phis.size() != block.phis.size()) {
            block.phis = std::move(kept_phis);
        }

        for (auto& inst : block.instructions) {
            const auto result = instruction_result(inst);
            if (!result) {
                continue;
            }
            const auto state = sccp.value(result->id);
            if (!state.is_constant() || is_same_iconst(inst, *result, state.bits)) {
                continue;
            }

            inst = IConst{
                .result = *result,
                .value = materialize_iconst(state.bits),
            };
            changed = true;
        }

        if (!prefix.empty()) {
            std::vector<Instruction> rewritten;
            rewritten.reserve(prefix.size() + block.instructions.size());
            for (auto& inst : prefix) {
                rewritten.push_back(std::move(inst));
            }
            for (auto& inst : block.instructions) {
                rewritten.push_back(std::move(inst));
            }
            block.instructions = std::move(rewritten);
        }
    }

    return changed;
}

} // namespace ir3
