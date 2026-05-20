#include "riscv/regalloc.hpp"

#include "riscv/analysis/cfg.hpp"
#include "riscv/analysis/liveness.hpp"

#include "semantic/type/type.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace riscv {
namespace {

constexpr std::array<PhysicalRegister, 19> kAllocatable = {
    PhysicalRegister::S1,  PhysicalRegister::S2,  PhysicalRegister::S3,
    PhysicalRegister::S4,  PhysicalRegister::S5,  PhysicalRegister::S6,
    PhysicalRegister::S7,  PhysicalRegister::S8,  PhysicalRegister::S9,
    PhysicalRegister::S10, PhysicalRegister::S11, PhysicalRegister::A0,
    PhysicalRegister::A1,  PhysicalRegister::A2,  PhysicalRegister::A3,
    PhysicalRegister::A4,  PhysicalRegister::A5,  PhysicalRegister::A6,
    PhysicalRegister::A7,
};

constexpr PhysicalRegister kScratch0 = PhysicalRegister::T0;
constexpr PhysicalRegister kScratch1 = PhysicalRegister::T1;

struct Allocation {
    std::unordered_map<MachineValueId, PhysicalRegister> assigned;
    std::unordered_map<MachineValueId, FrameId> spilled;
};

struct OriginalNode {
    std::optional<MachineValueId> vreg;
    std::optional<PhysicalRegister> precolor;
    RegisterClass reg_class = RegisterClass::Gpr32;
    std::size_t weight = 0;
};

struct NodeTable {
    std::vector<OriginalNode> nodes;
    std::unordered_map<MachineValueId, int> vreg_nodes;
    std::unordered_map<PhysicalRegister, int> phys_nodes;
};

struct GraphInput {
    NodeTable table;
    std::vector<std::unordered_set<int>> adjacency;
    std::vector<std::pair<int, int>> moves;
};

struct CoalescedNode {
    int rep = -1;
    std::optional<PhysicalRegister> precolor;
    std::size_t weight = 0;
    std::unordered_set<int> neighbors;
    std::vector<MachineValueId> members;
};

struct CoalescedGraph {
    std::unordered_map<int, CoalescedNode> nodes;
};

class Dsu {
public:
    explicit Dsu(std::size_t n) : parent_(n), rank_(n, 0) {
        for (std::size_t i = 0; i < n; ++i) {
            parent_[i] = static_cast<int>(i);
        }
    }

    int find(int value) {
        if (parent_[static_cast<std::size_t>(value)] == value) {
            return value;
        }
        parent_[static_cast<std::size_t>(value)] =
            find(parent_[static_cast<std::size_t>(value)]);
        return parent_[static_cast<std::size_t>(value)];
    }

    int unite(int a, int b, bool prefer_a) {
        a = find(a);
        b = find(b);
        if (a == b) {
            return a;
        }
        if (rank_[static_cast<std::size_t>(a)] < rank_[static_cast<std::size_t>(b)] ||
            (rank_[static_cast<std::size_t>(a)] == rank_[static_cast<std::size_t>(b)] &&
             !prefer_a)) {
            std::swap(a, b);
        }
        parent_[static_cast<std::size_t>(b)] = a;
        if (rank_[static_cast<std::size_t>(a)] == rank_[static_cast<std::size_t>(b)]) {
            ++rank_[static_cast<std::size_t>(a)];
        }
        return a;
    }

private:
    std::vector<int> parent_;
    std::vector<int> rank_;
};

template <class Fn>
void for_each_register_ref(const RegisterRef& reg, Fn&& fn) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, VirtualRegister>) {
                fn(value);
            } else if constexpr (std::is_same_v<T, PhysicalRegister>) {
                fn(value);
            }
        },
        reg);
}

template <class Fn>
void for_each_address_register(const Address& address, Fn&& fn) {
    if (const auto* reg_addr = std::get_if<RegisterAddress>(&address)) {
        for_each_register_ref(reg_addr->base, fn);
    }
}

template <class Fn>
void for_each_instruction_use(const Instruction& inst, Fn&& fn) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Copy>) {
                for_each_register_ref(value.src, fn);
            } else if constexpr (std::is_same_v<T, Binary>) {
                for_each_register_ref(value.lhs, fn);
                for_each_register_ref(value.rhs, fn);
            } else if constexpr (std::is_same_v<T, Compare>) {
                for_each_register_ref(value.lhs, fn);
                for_each_register_ref(value.rhs, fn);
            } else if constexpr (std::is_same_v<T, Load>) {
                for_each_address_register(value.address, fn);
            } else if constexpr (std::is_same_v<T, Store>) {
                for_each_register_ref(value.src, fn);
                for_each_address_register(value.address, fn);
            }
        },
        inst);
}

template <class Fn>
void for_each_instruction_def(const Instruction& inst, Fn&& fn) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Copy> || std::is_same_v<T, Li> ||
                          std::is_same_v<T, Binary> || std::is_same_v<T, Compare> ||
                          std::is_same_v<T, FrameAddr> || std::is_same_v<T, Load>) {
                for_each_register_ref(value.dest, fn);
            }
        },
        inst);
}

template <class Fn>
void for_each_terminator_use(const Terminator& term, Fn&& fn) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, BranchNonZero>) {
                for_each_register_ref(value.condition, fn);
            } else if constexpr (std::is_same_v<T, Return>) {
                if (value.value) {
                    for_each_register_ref(*value.value, fn);
                }
            }
        },
        term);
}

std::optional<int> node_for(const NodeTable& table, const RegisterRef& reg) {
    return std::visit(
        [&](const auto& value) -> std::optional<int> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, VirtualRegister>) {
                return table.vreg_nodes.at(value.id);
            } else if constexpr (std::is_same_v<T, PhysicalRegister>) {
                if (!is_allocatable_register(value)) {
                    return std::nullopt;
                }
                return table.phys_nodes.at(value);
            } else {
                return std::nullopt;
            }
        },
        reg);
}

void ensure_node(NodeTable& table, const RegisterRef& reg) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, VirtualRegister>) {
                if (!table.vreg_nodes.contains(value.id)) {
                    const int id = static_cast<int>(table.nodes.size());
                    table.nodes.push_back(OriginalNode{
                        .vreg = value.id,
                        .precolor = std::nullopt,
                        .reg_class = value.reg_class,
                        .weight = 0,
                    });
                    table.vreg_nodes.emplace(value.id, id);
                }
            } else if constexpr (std::is_same_v<T, PhysicalRegister>) {
                if (!is_allocatable_register(value) || table.phys_nodes.contains(value)) {
                    return;
                }
                const int id = static_cast<int>(table.nodes.size());
                table.nodes.push_back(OriginalNode{
                    .vreg = std::nullopt,
                    .precolor = value,
                    .reg_class = RegisterClass::Gpr32,
                    .weight = std::numeric_limits<std::size_t>::max() / 4,
                });
                table.phys_nodes.emplace(value, id);
            }
        },
        reg);
}

void add_edge(std::vector<std::unordered_set<int>>& adjacency, int a, int b) {
    if (a == b) {
        return;
    }
    adjacency[static_cast<std::size_t>(a)].insert(b);
    adjacency[static_cast<std::size_t>(b)].insert(a);
}

NodeTable collect_nodes(const MachineFunction& fn) {
    NodeTable table;
    for (const auto reg : kAllocatable) {
        ensure_node(table, reg);
    }

    for (const auto& block : fn.blocks) {
        for (const auto& phi : block.phis) {
            ensure_node(table, phi.dest);
            for (const auto& incoming : phi.incoming) {
                ensure_node(table, incoming.value);
            }
        }
        for (const auto& inst : block.instructions) {
            for_each_instruction_def(inst, [&](const auto& reg) { ensure_node(table, reg); });
            for_each_instruction_use(inst, [&](const auto& reg) { ensure_node(table, reg); });
        }
        if (block.terminator) {
            for_each_terminator_use(*block.terminator,
                                    [&](const auto& reg) { ensure_node(table, reg); });
        }
    }
    return table;
}

void bump_weight(NodeTable& table, const RegisterRef& reg) {
    if (const auto* vreg = std::get_if<VirtualRegister>(&reg)) {
        ++table.nodes[static_cast<std::size_t>(table.vreg_nodes.at(vreg->id))].weight;
    }
}

GraphInput build_graph(const MachineFunction& fn) {
    GraphInput input;
    input.table = collect_nodes(fn);
    input.adjacency.resize(input.table.nodes.size());

    const auto cfg = compute_cfg(fn);
    const auto live = compute_liveness(fn, cfg);

    for (const auto& block : fn.blocks) {
        for (const auto& phi : block.phis) {
            bump_weight(input.table, phi.dest);
            for (const auto& incoming : phi.incoming) {
                bump_weight(input.table, incoming.value);
                const auto dest = node_for(input.table, phi.dest);
                const auto src = node_for(input.table, incoming.value);
                if (dest && src) {
                    input.moves.push_back({*dest, *src});
                }
            }
        }
        for (const auto& inst : block.instructions) {
            for_each_instruction_def(inst, [&](const auto& reg) { bump_weight(input.table, reg); });
            for_each_instruction_use(inst, [&](const auto& reg) { bump_weight(input.table, reg); });
            if (const auto* copy = std::get_if<Copy>(&inst)) {
                const auto dest = node_for(input.table, copy->dest);
                const auto src = node_for(input.table, copy->src);
                if (dest && src) {
                    input.moves.push_back({*dest, *src});
                }
            }
        }
        if (block.terminator) {
            for_each_terminator_use(*block.terminator,
                                    [&](const auto& reg) { bump_weight(input.table, reg); });
        }
    }

    auto add_phys_live_edges =
        [&](const std::vector<PhysicalRegister>& phys_regs,
            const std::unordered_set<MachineValueId>& live_vregs) {
            for (const auto phys : phys_regs) {
                if (!is_allocatable_register(phys)) {
                    continue;
                }
                const int phys_node = input.table.phys_nodes.at(phys);
                for (const auto live_vreg : live_vregs) {
                    add_edge(input.adjacency, phys_node, input.table.vreg_nodes.at(live_vreg));
                }
            }
        };

    for (std::size_t block_index = 0; block_index < fn.blocks.size(); ++block_index) {
        const auto& block = fn.blocks[block_index];
        auto current_live = live.live_out[block_index];

        if (block.terminator) {
            std::vector<PhysicalRegister> phys_uses;
            for_each_terminator_use(*block.terminator, [&](const auto& reg) {
                using T = std::decay_t<decltype(reg)>;
                if constexpr (std::is_same_v<T, PhysicalRegister>) {
                    phys_uses.push_back(reg);
                }
            });
            add_phys_live_edges(phys_uses, current_live);
            for_each_terminator_use(*block.terminator, [&](const auto& reg) {
                using T = std::decay_t<decltype(reg)>;
                if constexpr (std::is_same_v<T, VirtualRegister>) {
                    current_live.insert(reg.id);
                }
            });
        }

        for (auto it = block.instructions.rbegin(); it != block.instructions.rend(); ++it) {
            const Instruction& inst = *it;
            std::vector<MachineValueId> defs;
            std::vector<MachineValueId> uses;
            std::vector<PhysicalRegister> phys_defs;
            std::vector<PhysicalRegister> phys_uses;

            for_each_instruction_def(inst, [&](const auto& reg) {
                using T = std::decay_t<decltype(reg)>;
                if constexpr (std::is_same_v<T, VirtualRegister>) {
                    defs.push_back(reg.id);
                } else if constexpr (std::is_same_v<T, PhysicalRegister>) {
                    phys_defs.push_back(reg);
                }
            });
            for_each_instruction_use(inst, [&](const auto& reg) {
                using T = std::decay_t<decltype(reg)>;
                if constexpr (std::is_same_v<T, VirtualRegister>) {
                    uses.push_back(reg.id);
                } else if constexpr (std::is_same_v<T, PhysicalRegister>) {
                    phys_uses.push_back(reg);
                }
            });

            add_phys_live_edges(phys_defs, current_live);
            add_phys_live_edges(phys_uses, current_live);

            std::optional<MachineValueId> copy_src_vreg;
            if (const auto* copy = std::get_if<Copy>(&inst)) {
                if (const auto* src_vreg = std::get_if<VirtualRegister>(&copy->src)) {
                    copy_src_vreg = src_vreg->id;
                }
            }

            for (const auto def : defs) {
                const int def_node = input.table.vreg_nodes.at(def);
                for (const auto live_vreg : current_live) {
                    if (def == live_vreg) {
                        continue;
                    }
                    if (copy_src_vreg && *copy_src_vreg == live_vreg) {
                        continue;
                    }
                    add_edge(input.adjacency, def_node, input.table.vreg_nodes.at(live_vreg));
                }
            }

            for (const auto def : defs) {
                current_live.erase(def);
            }
            for (const auto use : uses) {
                current_live.insert(use);
            }
        }

        for (const auto& phi : block.phis) {
            const auto* dest = std::get_if<VirtualRegister>(&phi.dest);
            if (!dest) {
                continue;
            }
            const int dest_node = input.table.vreg_nodes.at(dest->id);
            for (const auto live_vreg : current_live) {
                if (live_vreg == dest->id) {
                    continue;
                }
                add_edge(input.adjacency, dest_node, input.table.vreg_nodes.at(live_vreg));
            }
        }
    }

    for (const auto& block : fn.blocks) {
        for (const auto& phi : block.phis) {
            for (const auto& incoming : phi.incoming) {
                const auto* phys = std::get_if<PhysicalRegister>(&incoming.value);
                if (!phys || !is_allocatable_register(*phys)) {
                    continue;
                }
                const std::size_t pred_index = cfg.index_of.at(incoming.pred);
                const int phys_node = input.table.phys_nodes.at(*phys);
                for (const auto live_vreg : live.live_out[pred_index]) {
                    add_edge(input.adjacency, phys_node, input.table.vreg_nodes.at(live_vreg));
                }
            }
        }
    }

    return input;
}

CoalescedGraph build_coalesced_graph(const GraphInput& input, Dsu& dsu) {
    CoalescedGraph graph;

    for (std::size_t i = 0; i < input.table.nodes.size(); ++i) {
        const int rep = dsu.find(static_cast<int>(i));
        auto& node = graph.nodes[rep];
        node.rep = rep;
        node.weight += input.table.nodes[i].weight;
        if (input.table.nodes[i].precolor) {
            node.precolor = input.table.nodes[i].precolor;
        }
        if (input.table.nodes[i].vreg) {
            node.members.push_back(*input.table.nodes[i].vreg);
        }
    }

    for (std::size_t i = 0; i < input.adjacency.size(); ++i) {
        const int lhs = dsu.find(static_cast<int>(i));
        for (const int neighbor : input.adjacency[i]) {
            const int rhs = dsu.find(neighbor);
            if (lhs == rhs) {
                continue;
            }
            graph.nodes[lhs].neighbors.insert(rhs);
        }
    }

    return graph;
}

bool can_coalesce(const CoalescedGraph& graph, int lhs, int rhs) {
    if (lhs == rhs) {
        return false;
    }
    const auto& a = graph.nodes.at(lhs);
    const auto& b = graph.nodes.at(rhs);
    if (a.neighbors.contains(rhs)) {
        return false;
    }
    if (a.precolor && b.precolor && *a.precolor != *b.precolor) {
        return false;
    }

    if (a.precolor || b.precolor) {
        const auto& pre = a.precolor ? a : b;
        const auto& other = a.precolor ? b : a;
        for (const int neighbor : other.neighbors) {
            const auto& candidate = graph.nodes.at(neighbor);
            if (candidate.precolor || candidate.neighbors.size() < kAllocatable.size() ||
                candidate.neighbors.contains(pre.rep)) {
                continue;
            }
            return false;
        }
        return true;
    }

    std::unordered_set<int> union_neighbors = a.neighbors;
    union_neighbors.insert(b.neighbors.begin(), b.neighbors.end());
    union_neighbors.erase(lhs);
    union_neighbors.erase(rhs);

    std::size_t high_degree = 0;
    for (const int neighbor : union_neighbors) {
        const auto& candidate = graph.nodes.at(neighbor);
        if (candidate.precolor || candidate.neighbors.size() >= kAllocatable.size()) {
            ++high_degree;
        }
    }
    return high_degree < kAllocatable.size();
}

Allocation color_graph(MachineFunction& fn, const GraphInput& input) {
    Dsu dsu(input.table.nodes.size());

    bool changed = true;
    while (changed) {
        changed = false;
        const auto graph = build_coalesced_graph(input, dsu);
        for (const auto& [lhs0, rhs0] : input.moves) {
            const int lhs = dsu.find(lhs0);
            const int rhs = dsu.find(rhs0);
            if (!can_coalesce(graph, lhs, rhs)) {
                continue;
            }
            const bool prefer_lhs = graph.nodes.at(lhs).precolor.has_value() ||
                                    !graph.nodes.at(rhs).precolor.has_value();
            dsu.unite(lhs, rhs, prefer_lhs);
            changed = true;
        }
    }

    const auto graph = build_coalesced_graph(input, dsu);
    std::unordered_map<int, PhysicalRegister> colors;
    std::unordered_set<int> active;
    std::unordered_map<int, std::size_t> degree;

    for (const auto& [rep, node] : graph.nodes) {
        if (node.precolor) {
            colors.emplace(rep, *node.precolor);
            continue;
        }
        active.insert(rep);
    }
    for (const auto& [rep, node] : graph.nodes) {
        degree.emplace(rep, node.neighbors.size());
    }

    struct StackEntry {
        int rep = -1;
        bool spill_bias = false;
    };
    std::vector<StackEntry> stack;
    stack.reserve(active.size());

    while (!active.empty()) {
        auto low_degree = std::find_if(active.begin(), active.end(), [&](int rep) {
            return degree.at(rep) < kAllocatable.size();
        });

        int chosen = -1;
        bool spill_bias = false;
        if (low_degree != active.end()) {
            chosen = *low_degree;
        } else {
            spill_bias = true;
            chosen = *std::min_element(
                active.begin(),
                active.end(),
                [&](int lhs, int rhs) {
                    const auto& left = graph.nodes.at(lhs);
                    const auto& right = graph.nodes.at(rhs);
                    if (left.weight != right.weight) {
                        return left.weight < right.weight;
                    }
                    if (degree.at(lhs) != degree.at(rhs)) {
                        return degree.at(lhs) > degree.at(rhs);
                    }
                    return lhs < rhs;
                });
        }

        active.erase(chosen);
        stack.push_back({.rep = chosen, .spill_bias = spill_bias});
        for (const int neighbor : graph.nodes.at(chosen).neighbors) {
            if (active.contains(neighbor)) {
                --degree[neighbor];
            }
        }
    }

    std::unordered_set<int> spilled_reps;
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        std::unordered_set<PhysicalRegister> used;
        for (const int neighbor : graph.nodes.at(it->rep).neighbors) {
            if (const auto color = colors.find(neighbor); color != colors.end()) {
                used.insert(color->second);
            }
        }

        auto available = std::find_if(kAllocatable.begin(), kAllocatable.end(), [&](auto reg) {
            return !used.contains(reg);
        });
        if (available == kAllocatable.end()) {
            spilled_reps.insert(it->rep);
            continue;
        }
        colors.emplace(it->rep, *available);
    }

    Allocation alloc;
    for (const auto& [rep, node] : graph.nodes) {
        if (spilled_reps.contains(rep)) {
            for (const auto vreg : node.members) {
                const FrameId frame = fn.frame_objects.size();
                fn.frame_objects.push_back(FrameObject{
                    .id = frame,
                    .kind = FrameObjectKind::Spill,
                    .size = 4,
                    .align = 4,
                    .host_type = semantic::invalid_type_id,
                    .spill_class = RegisterClass::Gpr32,
                    .source_slot = std::nullopt,
                    .debug_name = "",
                    .saved_reg = std::nullopt,
                    .materialized_offset = std::nullopt,
                });
                alloc.spilled.emplace(vreg, frame);
            }
            continue;
        }

        if (!colors.contains(rep)) {
            throw std::runtime_error("graph coloring left an uncolored machine register node");
        }
        for (const auto vreg : node.members) {
            alloc.assigned.emplace(vreg, colors.at(rep));
        }
    }

    return alloc;
}

std::optional<PhysicalRegister> lookup(MachineValueId id, const Allocation& alloc) {
    if (const auto it = alloc.assigned.find(id); it != alloc.assigned.end()) {
        return it->second;
    }
    return std::nullopt;
}

SpillRef spilled_ref(const VirtualRegister& reg, const Allocation& alloc) {
    return SpillRef{.frame = alloc.spilled.at(reg.id), .reg_class = reg.reg_class};
}

RegisterRef rewrite_src(const RegisterRef& reg,
                        const Allocation& alloc,
                        std::vector<Instruction>& pre,
                        bool& scratch0_used,
                        bool& scratch1_used) {
    if (const auto* phys = std::get_if<PhysicalRegister>(&reg)) {
        return *phys;
    }
    if (std::holds_alternative<SpillRef>(reg)) {
        throw std::runtime_error("unexpected SpillRef during instruction rewrite");
    }

    const auto vreg = std::get<VirtualRegister>(reg);
    if (const auto phys = lookup(vreg.id, alloc)) {
        return *phys;
    }

    PhysicalRegister scratch;
    if (!scratch0_used) {
        scratch = kScratch0;
        scratch0_used = true;
    } else if (!scratch1_used) {
        scratch = kScratch1;
        scratch1_used = true;
    } else {
        throw std::runtime_error("Exceeded 2-scratch-register budget in instruction rewrite");
    }

    pre.push_back(Load{
        .dest = scratch,
        .address = FrameAddress{.frame = alloc.spilled.at(vreg.id), .offset = 0},
    });
    return scratch;
}

RegisterRef rewrite_dest(MachineValueId id,
                         const Allocation& alloc,
                         std::vector<Instruction>& post) {
    if (const auto phys = lookup(id, alloc)) {
        return *phys;
    }
    post.push_back(Store{
        .address = FrameAddress{.frame = alloc.spilled.at(id), .offset = 0},
        .src = kScratch0,
    });
    return kScratch0;
}

Address rewrite_address(const Address& addr,
                        const Allocation& alloc,
                        std::vector<Instruction>& pre,
                        bool& scratch0_used,
                        bool& scratch1_used) {
    return std::visit(
        [&](const auto& value) -> Address {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, RegisterAddress>) {
                return RegisterAddress{
                    .base = rewrite_src(value.base, alloc, pre, scratch0_used, scratch1_used),
                    .offset = value.offset,
                };
            }
            return value;
        },
        addr);
}

Instruction rewrite_instruction(const Instruction& inst,
                                const Allocation& alloc,
                                std::vector<Instruction>& pre,
                                std::vector<Instruction>& post) {
    return std::visit(
        [&](const auto& value) -> Instruction {
            using T = std::decay_t<decltype(value)>;
            bool scratch0_used = false;
            bool scratch1_used = false;

            if constexpr (std::is_same_v<T, Copy>) {
                const RegisterRef new_src =
                    rewrite_src(value.src, alloc, pre, scratch0_used, scratch1_used);
                RegisterRef new_dest = value.dest;
                if (const auto* vreg = std::get_if<VirtualRegister>(&value.dest)) {
                    new_dest = rewrite_dest(vreg->id, alloc, post);
                }
                return Copy{.dest = new_dest, .src = new_src};
            } else if constexpr (std::is_same_v<T, Li>) {
                return Li{.dest = rewrite_dest(std::get<VirtualRegister>(value.dest).id, alloc, post),
                          .value = value.value};
            } else if constexpr (std::is_same_v<T, Binary>) {
                return Binary{
                    .dest = rewrite_dest(std::get<VirtualRegister>(value.dest).id, alloc, post),
                    .op = value.op,
                    .lhs = rewrite_src(value.lhs, alloc, pre, scratch0_used, scratch1_used),
                    .rhs = rewrite_src(value.rhs, alloc, pre, scratch0_used, scratch1_used),
                };
            } else if constexpr (std::is_same_v<T, Compare>) {
                return Compare{
                    .dest = rewrite_dest(std::get<VirtualRegister>(value.dest).id, alloc, post),
                    .op = value.op,
                    .lhs = rewrite_src(value.lhs, alloc, pre, scratch0_used, scratch1_used),
                    .rhs = rewrite_src(value.rhs, alloc, pre, scratch0_used, scratch1_used),
                };
            } else if constexpr (std::is_same_v<T, FrameAddr>) {
                return FrameAddr{
                    .dest = rewrite_dest(std::get<VirtualRegister>(value.dest).id, alloc, post),
                    .frame = value.frame,
                    .offset = value.offset,
                };
            } else if constexpr (std::is_same_v<T, Load>) {
                return Load{
                    .dest = rewrite_dest(std::get<VirtualRegister>(value.dest).id, alloc, post),
                    .address =
                        rewrite_address(value.address, alloc, pre, scratch0_used, scratch1_used),
                };
            } else if constexpr (std::is_same_v<T, Store>) {
                return Store{
                    .address =
                        rewrite_address(value.address, alloc, pre, scratch0_used, scratch1_used),
                    .src = rewrite_src(value.src, alloc, pre, scratch0_used, scratch1_used),
                };
            } else {
                return value;
            }
        },
        inst);
}

RegisterRef rewrite_register_ref(const RegisterRef& reg,
                                 const Allocation& alloc,
                                 std::vector<Instruction>& spill_loads) {
    bool scratch0_used = false;
    bool scratch1_used = false;
    return rewrite_src(reg, alloc, spill_loads, scratch0_used, scratch1_used);
}

RegisterRef rewrite_phi_ref(const RegisterRef& reg, const Allocation& alloc) {
    return std::visit(
        [&](const auto& value) -> RegisterRef {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, PhysicalRegister> || std::is_same_v<T, SpillRef>) {
                return value;
            } else {
                if (const auto phys = lookup(value.id, alloc)) {
                    return *phys;
                }
                return spilled_ref(value, alloc);
            }
        },
        reg);
}

void rewrite_block(MachineBlock& block, const Allocation& alloc) {
    std::vector<Instruction> rewritten;
    rewritten.reserve(block.instructions.size() * 2);

    for (const auto& inst : block.instructions) {
        std::vector<Instruction> pre;
        std::vector<Instruction> post;
        Instruction body = rewrite_instruction(inst, alloc, pre, post);
        rewritten.insert(rewritten.end(),
                         std::make_move_iterator(pre.begin()),
                         std::make_move_iterator(pre.end()));
        if (const auto* copy = std::get_if<Copy>(&body);
            !copy || copy->dest != copy->src) {
            rewritten.push_back(std::move(body));
        }
        rewritten.insert(rewritten.end(),
                         std::make_move_iterator(post.begin()),
                         std::make_move_iterator(post.end()));
    }
    block.instructions = std::move(rewritten);

    if (block.terminator) {
        block.terminator = std::visit(
            [&](const auto& term) -> Terminator {
                using T = std::decay_t<decltype(term)>;
                if constexpr (std::is_same_v<T, BranchNonZero>) {
                    std::vector<Instruction> loads;
                    const RegisterRef condition =
                        rewrite_register_ref(term.condition, alloc, loads);
                    block.instructions.insert(block.instructions.end(),
                                              std::make_move_iterator(loads.begin()),
                                              std::make_move_iterator(loads.end()));
                    return BranchNonZero{
                        .condition = condition,
                        .then_block = term.then_block,
                        .else_block = term.else_block,
                    };
                } else if constexpr (std::is_same_v<T, Return>) {
                    if (!term.value) {
                        return term;
                    }
                    std::vector<Instruction> loads;
                    const RegisterRef value = rewrite_register_ref(*term.value, alloc, loads);
                    block.instructions.insert(block.instructions.end(),
                                              std::make_move_iterator(loads.begin()),
                                              std::make_move_iterator(loads.end()));
                    return Return{.value = value};
                }
                return term;
            },
            *block.terminator);
    }

    for (auto& phi : block.phis) {
        phi.dest = rewrite_phi_ref(phi.dest, alloc);
        for (auto& incoming : phi.incoming) {
            incoming.value = rewrite_phi_ref(incoming.value, alloc);
        }
    }
}

} // namespace

AllocationStats allocate_registers(MachineFunction& fn) {
    const auto graph = build_graph(fn);
    Allocation alloc = color_graph(fn, graph);

    for (auto& block : fn.blocks) {
        rewrite_block(block, alloc);
    }

    AllocationStats stats;
    stats.num_spills = alloc.spilled.size();
    stats.spill_map = alloc.spilled;
    return stats;
}

void allocate_registers(MachineModule& module) {
    for (auto& fn : module.functions) {
        allocate_registers(fn);
    }
}

} // namespace riscv
