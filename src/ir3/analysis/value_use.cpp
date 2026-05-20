#include "ir3/analysis/value_use.hpp"

#include "ir3/analysis/manager.hpp"

#include <cstddef>
#include <string>
#include <stdexcept>
#include <type_traits>

namespace ir3 {
namespace {

[[noreturn]] void value_use_error(const Function& fn, const std::string& message) {
    throw std::runtime_error("IR3 value use analysis failed for @" + fn.symbol +
                             ": " + message);
}

void require_value(const Function& fn, const ValueUseInfo& info, ValueId value) {
    if (value >= info.values.size()) {
        value_use_error(fn, "reference to invalid value %" + std::to_string(value));
    }
}

void define_value(const Function& fn,
                  ValueUseInfo& info,
                  ValueId value,
                  SsaClass klass,
                  ValueDefSite def) {
    require_value(fn, info, value);
    auto& entry = info.values[value];
    if (entry.def.has_value()) {
        value_use_error(fn, "value %" + std::to_string(value) + " has multiple definitions");
    }
    entry.klass = klass;
    entry.def = std::move(def);
}

void add_use(const Function& fn, ValueUseInfo& info, ValueId value, ValueUseSite use) {
    require_value(fn, info, value);
    info.values[value].uses.push_back(std::move(use));
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

template <class Fn>
void for_each_instruction_use(const Instruction& inst, Fn&& fn) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Load>) {
                for_each_place_use(value.source, fn);
            } else if constexpr (std::is_same_v<T, Store>) {
                for_each_place_use(value.dest, fn);
                fn(value.value);
            } else if constexpr (std::is_same_v<T, Copy>) {
                for_each_place_use(value.dest, fn);
                for_each_place_use(value.source, fn);
            } else if constexpr (std::is_same_v<T, Borrow>) {
                for_each_place_use(value.source, fn);
            } else if constexpr (std::is_same_v<T, Unary>) {
                fn(value.operand);
            } else if constexpr (std::is_same_v<T, Binary>) {
                fn(value.lhs);
                fn(value.rhs);
            } else if constexpr (std::is_same_v<T, Cast>) {
                fn(value.operand);
            } else if constexpr (std::is_same_v<T, Call>) {
                for (ValueId arg : value.args) {
                    fn(arg);
                }
            }
        },
        inst);
}

template <class Fn>
void for_each_terminator_use(const Terminator& term, Fn&& fn) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Branch>) {
                fn(value.condition);
            } else if constexpr (std::is_same_v<T, Return>) {
                if (value.value) {
                    fn(*value.value);
                }
            }
        },
        term);
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

} // namespace

ValueUseInfo ValueUseAnalysis::compute(const Function& fn, AnalysisManager&) {
    ValueUseInfo result;
    result.values.resize(fn.next_value);

    for (std::size_t pi = 0; pi < fn.params.size(); ++pi) {
        const auto& param = fn.params[pi];
        define_value(fn, result, param.value.id, param.value.klass, ParamDefSite{.param_index = pi});
    }

    for (const auto& block : fn.blocks) {
        for (std::size_t pi = 0; pi < block.phis.size(); ++pi) {
            const auto& phi = block.phis[pi];
            define_value(fn,
                         result,
                         phi.result.id,
                         phi.result.klass,
                         PhiDefSite{.block = block.id, .phi_index = pi});
        }

        for (std::size_t pi = 0; pi < block.phis.size(); ++pi) {
            const auto& phi = block.phis[pi];
            for (std::size_t ii = 0; ii < phi.incoming.size(); ++ii) {
                add_use(fn,
                        result,
                        phi.incoming[ii].value,
                        PhiUseSite{.block = block.id, .phi_index = pi, .incoming_index = ii});
            }
        }

        for (std::size_t ii = 0; ii < block.instructions.size(); ++ii) {
            const auto& inst = block.instructions[ii];
            if (auto def = instruction_result(inst)) {
                define_value(fn,
                             result,
                             def->id,
                             def->klass,
                             InstructionDefSite{.block = block.id, .instruction_index = ii});
            }
            for_each_instruction_use(
                inst,
                [&](ValueId value) {
                    add_use(fn,
                            result,
                            value,
                            InstructionUseSite{.block = block.id, .instruction_index = ii});
                });
        }

        if (block.terminator) {
            for_each_terminator_use(
                *block.terminator,
                [&](ValueId value) {
                    add_use(fn, result, value, TerminatorUseSite{.block = block.id});
                });
        }
    }

    return result;
}

} // namespace ir3
