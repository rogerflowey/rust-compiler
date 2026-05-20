#include "ir3/passes/dead_code_elim.hpp"

#include "ir3/analysis/cfg.hpp"
#include "ir3/analysis/dominance.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ir3 {
namespace {

struct DefSite {
    std::size_t block = 0;
    std::size_t instruction = 0;
};

void ensure_value_capacity(std::vector<std::size_t>& use_count, ValueId value) {
    if (value >= use_count.size()) {
        use_count.resize(value + 1, 0);
    }
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

std::optional<ValueId> removable_def(const Instruction& inst) {
    return std::visit(
        [](const auto& value) -> std::optional<ValueId> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, IConst> || std::is_same_v<T, Borrow> ||
                          std::is_same_v<T, Unary> || std::is_same_v<T, Binary> ||
                          std::is_same_v<T, Cast>) {
                return value.result.id;
            }
            return std::nullopt;
        },
        inst);
}

} // namespace

PreservedAnalyses DeadCodeEliminationPass::run(Function& fn, AnalysisManager&) {
    std::vector<std::size_t> use_count(fn.next_value, 0);
    std::unordered_map<ValueId, DefSite> def_sites;
    std::vector<std::vector<bool>> erase_mask;
    erase_mask.reserve(fn.blocks.size());

    auto mark_use = [&](ValueId value) {
        ensure_value_capacity(use_count, value);
        ++use_count[value];
    };

    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
        const auto& block = fn.blocks[bi];
        erase_mask.emplace_back(block.instructions.size(), false);

        for (const auto& phi : block.phis) {
            for (const auto& incoming : phi.incoming) {
                mark_use(incoming.value);
            }
        }

        for (std::size_t ii = 0; ii < block.instructions.size(); ++ii) {
            const auto& inst = block.instructions[ii];
            if (auto def = removable_def(inst)) {
                def_sites.emplace(*def, DefSite{.block = bi, .instruction = ii});
            }
            for_each_instruction_use(inst, mark_use);
        }

        if (block.terminator) {
            for_each_terminator_use(*block.terminator, mark_use);
        }
    }

    std::vector<ValueId> worklist;
    worklist.reserve(def_sites.size());
    for (const auto& [value, site] : def_sites) {
        (void)site;
        ensure_value_capacity(use_count, value);
        if (use_count[value] == 0) {
            worklist.push_back(value);
        }
    }

    bool changed = false;
    while (!worklist.empty()) {
        const ValueId value = worklist.back();
        worklist.pop_back();

        const auto it = def_sites.find(value);
        if (it == def_sites.end()) {
            continue;
        }

        const auto site = it->second;
        if (use_count[value] != 0 || erase_mask[site.block][site.instruction]) {
            continue;
        }

        changed = true;
        erase_mask[site.block][site.instruction] = true;
        const auto& inst = fn.blocks[site.block].instructions[site.instruction];
        for_each_instruction_use(
            inst,
            [&](ValueId operand) {
                ensure_value_capacity(use_count, operand);
                if (use_count[operand] == 0) {
                    return;
                }
                --use_count[operand];
                if (use_count[operand] == 0 && def_sites.contains(operand)) {
                    worklist.push_back(operand);
                }
            });
    }

    if (!changed) {
        return PreservedAnalyses::all();
    }

    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
        auto& instructions = fn.blocks[bi].instructions;
        const auto& mask = erase_mask[bi];

        std::vector<Instruction> kept;
        kept.reserve(instructions.size());
        for (std::size_t ii = 0; ii < instructions.size(); ++ii) {
            if (!mask[ii]) {
                kept.push_back(std::move(instructions[ii]));
            }
        }
        instructions = std::move(kept);
    }

    auto preserved = PreservedAnalyses::none();
    preserved.preserve<CfgAnalysis>();
    preserved.preserve<DomTreeAnalysis>();
    return preserved;
}

} // namespace ir3
