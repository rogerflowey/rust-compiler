#include "ir3/passes/inlining.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ir3 {
namespace {

struct InliningOptions {
    std::size_t max_blocks = 4;
    std::size_t max_instructions = 16;
    std::size_t max_slots = 8;
    std::size_t max_returns = 4;
    std::size_t max_growth_per_function = 48;
};

struct CallGraphInfo {
    std::vector<std::vector<std::size_t>> edges;
    std::vector<std::size_t> function_scc;
    std::vector<std::vector<std::size_t>> scc_members;
    std::vector<bool> scc_recursive;
    std::vector<std::vector<std::size_t>> scc_edges;
    std::vector<std::size_t> scc_postorder;
};

std::optional<ValueId> defined_value(const Instruction& inst) {
    return std::visit(
        [](const auto& value) -> std::optional<ValueId> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, IConst> || std::is_same_v<T, Load> ||
                          std::is_same_v<T, Borrow> || std::is_same_v<T, Unary> ||
                          std::is_same_v<T, Binary> || std::is_same_v<T, Cast>) {
                return value.result.id;
            } else if constexpr (std::is_same_v<T, Call>) {
                if (value.result) {
                    return value.result->id;
                }
            }
            return std::nullopt;
        },
        inst);
}

std::vector<BlockId> successor_blocks(const Terminator& term) {
    return std::visit(
        [](const auto& value) -> std::vector<BlockId> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Jump>) {
                return {value.target};
            } else if constexpr (std::is_same_v<T, Branch>) {
                if (value.then_block == value.else_block) {
                    return {value.then_block};
                }
                return {value.then_block, value.else_block};
            }
            return {};
        },
        term);
}

std::size_t count_instructions(const Function& fn) {
    std::size_t count = 0;
    for (const auto& block : fn.blocks) {
        count += block.phis.size();
        count += block.instructions.size();
    }
    return count;
}

std::size_t count_returns(const Function& fn) {
    std::size_t count = 0;
    for (const auto& block : fn.blocks) {
        if (block.terminator && std::holds_alternative<Return>(*block.terminator)) {
            ++count;
        }
    }
    return count;
}

bool has_self_edge(const std::vector<std::size_t>& edges, std::size_t node) {
    return std::find(edges.begin(), edges.end(), node) != edges.end();
}

ValueId remap_value(const std::vector<std::optional<ValueId>>& value_map,
                    ValueId old,
                    const std::string& function_name) {
    if (old >= value_map.size() || !value_map[old].has_value()) {
        throw std::runtime_error("IR3 inlining for @" + function_name +
                                 " references unmapped value %" +
                                 std::to_string(old));
    }
    return *value_map[old];
}

SlotId remap_slot(const std::vector<SlotId>& slot_map,
                  SlotId old,
                  const std::string& function_name) {
    if (old >= slot_map.size()) {
        throw std::runtime_error("IR3 inlining for @" + function_name +
                                 " references unmapped slot %" +
                                 std::to_string(old));
    }
    return slot_map[old];
}

BlockId remap_block(const std::vector<BlockId>& block_map,
                    BlockId old,
                    const std::string& function_name) {
    if (old >= block_map.size()) {
        throw std::runtime_error("IR3 inlining for @" + function_name +
                                 " references unmapped block bb" +
                                 std::to_string(old));
    }
    return block_map[old];
}

Place clone_place(const Place& place,
                  const std::vector<std::optional<ValueId>>& value_map,
                  const std::vector<SlotId>& slot_map,
                  const std::string& function_name) {
    Place cloned = place;
    std::visit(
        [&](auto& base) {
            using T = std::decay_t<decltype(base)>;
            if constexpr (std::is_same_v<T, SlotBase>) {
                base.slot = remap_slot(slot_map, base.slot, function_name);
            } else if constexpr (std::is_same_v<T, DerefBase>) {
                base.ptr = remap_value(value_map, base.ptr, function_name);
            }
        },
        cloned.base);

    for (auto& projection : cloned.projections) {
        if (auto* index = std::get_if<IndexProjection>(&projection)) {
            index->index = remap_value(value_map, index->index, function_name);
        }
    }
    return cloned;
}

Instruction clone_instruction(const Instruction& inst,
                              const std::vector<std::optional<ValueId>>& value_map,
                              const std::vector<SlotId>& slot_map,
                              const std::string& function_name) {
    return std::visit(
        [&](const auto& value) -> Instruction {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, IConst>) {
                return IConst{
                    .result = Value{
                        .id = remap_value(value_map, value.result.id, function_name),
                        .klass = value.result.klass,
                    },
                    .value = value.value,
                };
            } else if constexpr (std::is_same_v<T, Load>) {
                return Load{
                    .result = Value{
                        .id = remap_value(value_map, value.result.id, function_name),
                        .klass = value.result.klass,
                    },
                    .source = clone_place(value.source, value_map, slot_map, function_name),
                };
            } else if constexpr (std::is_same_v<T, Store>) {
                return Store{
                    .klass = value.klass,
                    .dest = clone_place(value.dest, value_map, slot_map, function_name),
                    .value = remap_value(value_map, value.value, function_name),
                };
            } else if constexpr (std::is_same_v<T, Copy>) {
                return Copy{
                    .dest = clone_place(value.dest, value_map, slot_map, function_name),
                    .source = clone_place(value.source, value_map, slot_map, function_name),
                };
            } else if constexpr (std::is_same_v<T, Borrow>) {
                return Borrow{
                    .result = Value{
                        .id = remap_value(value_map, value.result.id, function_name),
                        .klass = value.result.klass,
                    },
                    .is_mutable = value.is_mutable,
                    .source = clone_place(value.source, value_map, slot_map, function_name),
                };
            } else if constexpr (std::is_same_v<T, Unary>) {
                return Unary{
                    .result = Value{
                        .id = remap_value(value_map, value.result.id, function_name),
                        .klass = value.result.klass,
                    },
                    .op = value.op,
                    .operand = remap_value(value_map, value.operand, function_name),
                };
            } else if constexpr (std::is_same_v<T, Binary>) {
                return Binary{
                    .result = Value{
                        .id = remap_value(value_map, value.result.id, function_name),
                        .klass = value.result.klass,
                    },
                    .op = value.op,
                    .lhs = remap_value(value_map, value.lhs, function_name),
                    .rhs = remap_value(value_map, value.rhs, function_name),
                };
            } else if constexpr (std::is_same_v<T, Cast>) {
                return Cast{
                    .result = Value{
                        .id = remap_value(value_map, value.result.id, function_name),
                        .klass = value.result.klass,
                    },
                    .operand = remap_value(value_map, value.operand, function_name),
                    .op = value.op,
                };
            } else if constexpr (std::is_same_v<T, Call>) {
                Call cloned = value;
                if (value.result) {
                    cloned.result = Value{
                        .id = remap_value(value_map, value.result->id, function_name),
                        .klass = value.result->klass,
                    };
                }
                for (auto& arg : cloned.args) {
                    arg = remap_value(value_map, arg, function_name);
                }
                return cloned;
            }
            return value;
        },
        inst);
}

Phi clone_phi(const Phi& phi,
              const std::vector<std::optional<ValueId>>& value_map,
              const std::vector<BlockId>& block_map,
              const std::string& function_name) {
    Phi cloned{
        .result = Value{
            .id = remap_value(value_map, phi.result.id, function_name),
            .klass = phi.result.klass,
        },
        .incoming = {},
    };
    cloned.incoming.reserve(phi.incoming.size());
    for (const auto& incoming : phi.incoming) {
        cloned.incoming.push_back(PhiIncoming{
            .pred = remap_block(block_map, incoming.pred, function_name),
            .value = remap_value(value_map, incoming.value, function_name),
        });
    }
    return cloned;
}

void rewrite_value_ref(ValueId& value, ValueId from, ValueId to) {
    if (value == from) {
        value = to;
    }
}

void rewrite_place_value(Place& place, ValueId from, ValueId to) {
    std::visit(
        [&](auto& base) {
            using T = std::decay_t<decltype(base)>;
            if constexpr (std::is_same_v<T, DerefBase>) {
                rewrite_value_ref(base.ptr, from, to);
            }
        },
        place.base);
    for (auto& projection : place.projections) {
        if (auto* index = std::get_if<IndexProjection>(&projection)) {
            rewrite_value_ref(index->index, from, to);
        }
    }
}

void rewrite_instruction_uses(Instruction& inst, ValueId from, ValueId to) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Load>) {
                rewrite_place_value(value.source, from, to);
            } else if constexpr (std::is_same_v<T, Store>) {
                rewrite_place_value(value.dest, from, to);
                rewrite_value_ref(value.value, from, to);
            } else if constexpr (std::is_same_v<T, Copy>) {
                rewrite_place_value(value.dest, from, to);
                rewrite_place_value(value.source, from, to);
            } else if constexpr (std::is_same_v<T, Borrow>) {
                rewrite_place_value(value.source, from, to);
            } else if constexpr (std::is_same_v<T, Unary>) {
                rewrite_value_ref(value.operand, from, to);
            } else if constexpr (std::is_same_v<T, Binary>) {
                rewrite_value_ref(value.lhs, from, to);
                rewrite_value_ref(value.rhs, from, to);
            } else if constexpr (std::is_same_v<T, Cast>) {
                rewrite_value_ref(value.operand, from, to);
            } else if constexpr (std::is_same_v<T, Call>) {
                for (auto& arg : value.args) {
                    rewrite_value_ref(arg, from, to);
                }
            }
        },
        inst);
}

void rewrite_terminator_uses(Terminator& term, ValueId from, ValueId to) {
    std::visit(
        [&](auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Branch>) {
                rewrite_value_ref(value.condition, from, to);
            } else if constexpr (std::is_same_v<T, Return>) {
                if (value.value) {
                    rewrite_value_ref(*value.value, from, to);
                }
            }
        },
        term);
}

void rewrite_all_uses(Function& fn, ValueId from, ValueId to) {
    for (auto& block : fn.blocks) {
        for (auto& phi : block.phis) {
            for (auto& incoming : phi.incoming) {
                rewrite_value_ref(incoming.value, from, to);
            }
        }
        for (auto& inst : block.instructions) {
            rewrite_instruction_uses(inst, from, to);
        }
        if (block.terminator) {
            rewrite_terminator_uses(*block.terminator, from, to);
        }
    }
}

CallGraphInfo build_call_graph(const Module& module) {
    std::unordered_map<std::string, std::size_t> symbol_to_index;
    symbol_to_index.reserve(module.functions.size());
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        symbol_to_index.emplace(module.functions[i].symbol, i);
    }

    CallGraphInfo info;
    info.edges.resize(module.functions.size());
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        for (const auto& block : module.functions[i].blocks) {
            for (const auto& inst : block.instructions) {
                const auto* call = std::get_if<Call>(&inst);
                if (!call) {
                    continue;
                }
                const auto it = symbol_to_index.find(call->callee);
                if (it != symbol_to_index.end()) {
                    info.edges[i].push_back(it->second);
                }
            }
        }
    }

    const std::size_t n = module.functions.size();
    std::vector<int> index(n, -1);
    std::vector<int> lowlink(n, -1);
    std::vector<std::size_t> stack;
    std::vector<bool> on_stack(n, false);
    int next_index = 0;

    auto strongconnect = [&](auto&& self, std::size_t v) -> void {
        index[v] = next_index;
        lowlink[v] = next_index;
        ++next_index;
        stack.push_back(v);
        on_stack[v] = true;

        for (std::size_t succ : info.edges[v]) {
            if (index[succ] == -1) {
                self(self, succ);
                lowlink[v] = std::min(lowlink[v], lowlink[succ]);
            } else if (on_stack[succ]) {
                lowlink[v] = std::min(lowlink[v], index[succ]);
            }
        }

        if (lowlink[v] != index[v]) {
            return;
        }

        std::vector<std::size_t> members;
        while (true) {
            const std::size_t w = stack.back();
            stack.pop_back();
            on_stack[w] = false;
            members.push_back(w);
            if (w == v) {
                break;
            }
        }

        const std::size_t scc_id = info.scc_members.size();
        for (std::size_t member : members) {
            if (info.function_scc.size() < n) {
                info.function_scc.resize(n, 0);
            }
            info.function_scc[member] = scc_id;
        }
        info.scc_members.push_back(std::move(members));
    };

    for (std::size_t i = 0; i < n; ++i) {
        if (index[i] == -1) {
            strongconnect(strongconnect, i);
        }
    }

    info.scc_recursive.resize(info.scc_members.size(), false);
    info.scc_edges.resize(info.scc_members.size());
    for (std::size_t scc = 0; scc < info.scc_members.size(); ++scc) {
        auto& out = info.scc_edges[scc];
        for (std::size_t member : info.scc_members[scc]) {
            if (info.scc_members[scc].size() > 1 || has_self_edge(info.edges[member], member)) {
                info.scc_recursive[scc] = true;
            }
            for (std::size_t succ : info.edges[member]) {
                const std::size_t succ_scc = info.function_scc[succ];
                if (succ_scc == scc) {
                    continue;
                }
                if (std::find(out.begin(), out.end(), succ_scc) == out.end()) {
                    out.push_back(succ_scc);
                }
            }
        }
    }

    std::vector<bool> visited(info.scc_members.size(), false);
    auto visit_scc = [&](auto&& self, std::size_t scc) -> void {
        if (visited[scc]) {
            return;
        }
        visited[scc] = true;
        for (std::size_t succ : info.scc_edges[scc]) {
            self(self, succ);
        }
        info.scc_postorder.push_back(scc);
    };

    for (std::size_t scc = 0; scc < info.scc_members.size(); ++scc) {
        visit_scc(visit_scc, scc);
    }

    return info;
}

bool should_inline_call(const Function& caller,
                        const Call& call,
                        const Function& callee,
                        bool callee_recursive,
                        std::size_t growth_budget_used,
                        const InliningOptions& options) {
    if (callee_recursive) {
        return false;
    }
    if (callee.blocks.empty()) {
        return false;
    }
    if (call.args.size() != callee.params.size()) {
        return false;
    }
    if (callee.blocks.size() > options.max_blocks) {
        return false;
    }
    if (callee.slots.size() > options.max_slots) {
        return false;
    }

    const auto return_count = count_returns(callee);
    if (return_count > options.max_returns) {
        return false;
    }

    if (call.result.has_value() != callee.return_class.has_value()) {
        return false;
    }
    if (call.result && callee.return_class &&
        call.result->klass != *callee.return_class) {
        return false;
    }

    const std::size_t growth =
        count_instructions(callee) + ((call.result && return_count > 1) ? 1u : 0u);
    if (growth > options.max_instructions) {
        return false;
    }
    if (growth_budget_used + growth > options.max_growth_per_function) {
        return false;
    }

    if (caller.symbol == callee.symbol) {
        return false;
    }

    return true;
}

std::size_t estimate_growth(const Function& callee, const Call& call) {
    return count_instructions(callee) +
           ((call.result && count_returns(callee) > 1) ? 1u : 0u);
}

bool inline_callsite(Function& caller,
                     const Function& callee,
                     std::size_t block_index,
                     std::size_t inst_index) {
    if (block_index >= caller.blocks.size()) {
        throw std::runtime_error("IR3 inlining for @" + caller.symbol +
                                 " selected a missing caller block");
    }

    auto& original_block = caller.blocks[block_index];
    if (inst_index >= original_block.instructions.size()) {
        throw std::runtime_error("IR3 inlining for @" + caller.symbol +
                                 " selected a missing instruction");
    }

    const auto* call = std::get_if<Call>(&original_block.instructions[inst_index]);
    if (!call) {
        throw std::runtime_error("IR3 inlining for @" + caller.symbol +
                                 " selected a non-call instruction");
    }
    if (!original_block.terminator) {
        throw std::runtime_error("IR3 inlining for @" + caller.symbol +
                                 " found an unterminated caller block");
    }

    const Call original_call = *call;
    const BlockId original_block_id = original_block.id;
    const std::string original_name = original_block.name;
    Terminator original_terminator = std::move(*original_block.terminator);
    const auto original_successors = successor_blocks(original_terminator);

    std::vector<Instruction> prefix;
    prefix.reserve(inst_index);
    for (std::size_t i = 0; i < inst_index; ++i) {
        prefix.push_back(std::move(original_block.instructions[i]));
    }

    std::vector<Instruction> suffix;
    suffix.reserve(original_block.instructions.size() - inst_index - 1);
    for (std::size_t i = inst_index + 1; i < original_block.instructions.size(); ++i) {
        suffix.push_back(std::move(original_block.instructions[i]));
    }

    const BlockId clone_block_start = caller.blocks.size();
    const BlockId continuation_id =
        clone_block_start + static_cast<BlockId>(callee.blocks.size());

    std::vector<SlotId> slot_map(callee.slots.size());
    for (std::size_t i = 0; i < callee.slots.size(); ++i) {
        auto slot = callee.slots[i];
        slot.id = caller.slots.size();
        if (!slot.debug_name.empty()) {
            slot.debug_name = callee.symbol + "$" + slot.debug_name;
        } else {
            slot.debug_name = callee.symbol + "$slot" + std::to_string(i);
        }
        slot_map[i] = slot.id;
        caller.slots.push_back(std::move(slot));
    }

    std::vector<BlockId> block_map(callee.blocks.size());
    for (std::size_t i = 0; i < callee.blocks.size(); ++i) {
        block_map[i] = clone_block_start + static_cast<BlockId>(i);
    }

    std::vector<std::optional<ValueId>> value_map(callee.next_value);
    for (std::size_t i = 0; i < callee.params.size(); ++i) {
        const auto param_id = callee.params[i].value.id;
        if (param_id >= value_map.size()) {
            throw std::runtime_error("IR3 inlining for @" + caller.symbol +
                                     " saw callee param outside next_value");
        }
        value_map[param_id] = original_call.args[i];
    }

    for (const auto& block : callee.blocks) {
        for (const auto& phi : block.phis) {
            value_map[phi.result.id] = caller.next_value++;
        }
        for (const auto& inst : block.instructions) {
            if (auto def = defined_value(inst)) {
                value_map[*def] = caller.next_value++;
            }
        }
    }

    original_block.instructions = std::move(prefix);
    original_block.terminator = Jump{
        .target = remap_block(block_map, callee.entry_block, callee.symbol),
    };

    std::vector<std::pair<BlockId, std::optional<ValueId>>> return_edges;
    return_edges.reserve(count_returns(callee));

    for (const auto& block : callee.blocks) {
        BasicBlock cloned;
        cloned.id = remap_block(block_map, block.id, callee.symbol);
        if (block.name.empty()) {
            cloned.name = callee.symbol + ".inl";
        } else {
            cloned.name = callee.symbol + ".inl." + block.name;
        }

        cloned.phis.reserve(block.phis.size());
        for (const auto& phi : block.phis) {
            cloned.phis.push_back(clone_phi(phi, value_map, block_map, callee.symbol));
        }

        cloned.instructions.reserve(block.instructions.size());
        for (const auto& inst : block.instructions) {
            cloned.instructions.push_back(
                clone_instruction(inst, value_map, slot_map, callee.symbol));
        }

        if (!block.terminator) {
            throw std::runtime_error("IR3 inlining for @" + caller.symbol +
                                     " found an unterminated callee block in @" +
                                     callee.symbol);
        }

        cloned.terminator = std::visit(
            [&](const auto& term) -> Terminator {
                using T = std::decay_t<decltype(term)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    return Jump{
                        .target = remap_block(block_map, term.target, callee.symbol),
                    };
                } else if constexpr (std::is_same_v<T, Branch>) {
                    return Branch{
                        .condition = remap_value(value_map, term.condition, callee.symbol),
                        .then_block =
                            remap_block(block_map, term.then_block, callee.symbol),
                        .else_block =
                            remap_block(block_map, term.else_block, callee.symbol),
                    };
                } else if constexpr (std::is_same_v<T, Return>) {
                    std::optional<ValueId> returned;
                    if (term.value) {
                        returned = remap_value(value_map, *term.value, callee.symbol);
                    }
                    return_edges.push_back({cloned.id, returned});
                    return Jump{.target = continuation_id};
                }
                return Unreachable{};
            },
            *block.terminator);

        caller.blocks.push_back(std::move(cloned));
    }

    BasicBlock continuation;
    continuation.id = continuation_id;
    continuation.name =
        original_name.empty() ? callee.symbol + ".cont" : original_name + ".cont";
    continuation.instructions = std::move(suffix);
    continuation.terminator = std::move(original_terminator);
    caller.blocks.push_back(std::move(continuation));

    for (BlockId succ : original_successors) {
        if (succ >= caller.blocks.size()) {
            throw std::runtime_error("IR3 inlining for @" + caller.symbol +
                                     " found a missing successor block");
        }
        for (auto& phi : caller.blocks[succ].phis) {
            for (auto& incoming : phi.incoming) {
                if (incoming.pred == original_block_id) {
                    incoming.pred = continuation_id;
                }
            }
        }
    }

    if (original_call.result) {
        if (return_edges.empty()) {
            throw std::runtime_error("IR3 inlining for @" + caller.symbol +
                                     " found no reachable return for call @" +
                                     callee.symbol);
        }

        ValueId replacement = 0;
        if (return_edges.size() == 1) {
            if (!return_edges.front().second) {
                throw std::runtime_error("IR3 inlining for @" + caller.symbol +
                                         " lost scalar return value from @" +
                                         callee.symbol);
            }
            replacement = *return_edges.front().second;
        } else {
            Value phi_result{
                .id = caller.next_value++,
                .klass = original_call.result->klass,
            };
            Phi phi{
                .result = phi_result,
                .incoming = {},
            };
            phi.incoming.reserve(return_edges.size());
            for (const auto& [pred, value] : return_edges) {
                if (!value) {
                    throw std::runtime_error("IR3 inlining for @" + caller.symbol +
                                             " found mixed return kinds in @" +
                                             callee.symbol);
                }
                phi.incoming.push_back(PhiIncoming{
                    .pred = pred,
                    .value = *value,
                });
            }
            caller.blocks[continuation_id].phis.push_back(std::move(phi));
            replacement = phi_result.id;
        }

        rewrite_all_uses(caller, original_call.result->id, replacement);
    }

    return true;
}

void inline_into_function(Function& caller,
                          const Module& module,
                          const std::unordered_map<std::string, std::size_t>& symbol_to_index,
                          const CallGraphInfo& graph,
                          const InliningOptions& options) {
    std::size_t growth_budget_used = 0;

    while (true) {
        bool changed = false;
        for (std::size_t bi = 0; bi < caller.blocks.size() && !changed; ++bi) {
            for (std::size_t ii = 0; ii < caller.blocks[bi].instructions.size(); ++ii) {
                const auto* call = std::get_if<Call>(&caller.blocks[bi].instructions[ii]);
                if (!call) {
                    continue;
                }

                const auto callee_it = symbol_to_index.find(call->callee);
                if (callee_it == symbol_to_index.end()) {
                    continue;
                }

                const std::size_t callee_index = callee_it->second;
                const auto& callee = module.functions[callee_index];
                const bool callee_recursive =
                    graph.scc_recursive[graph.function_scc[callee_index]];
                if (!should_inline_call(caller,
                                        *call,
                                        callee,
                                        callee_recursive,
                                        growth_budget_used,
                                        options)) {
                    continue;
                }

                const std::size_t growth = estimate_growth(callee, *call);
                inline_callsite(caller, callee, bi, ii);
                growth_budget_used += growth;
                changed = true;
                break;
            }
        }

        if (!changed) {
            break;
        }
    }
}

} // namespace

void run_inlining(Module& module) {
    if (module.functions.empty()) {
        return;
    }

    std::unordered_map<std::string, std::size_t> symbol_to_index;
    symbol_to_index.reserve(module.functions.size());
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        symbol_to_index.emplace(module.functions[i].symbol, i);
    }

    const auto graph = build_call_graph(module);
    const InliningOptions options;

    for (std::size_t scc : graph.scc_postorder) {
        for (std::size_t fn_index : graph.scc_members[scc]) {
            inline_into_function(module.functions[fn_index],
                                 module,
                                 symbol_to_index,
                                 graph,
                                 options);
        }
    }
}

} // namespace ir3
