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
constexpr std::size_t kFastSpillVregThreshold = 1200;

struct RematInfo {
    enum class Kind {
        Li,
        FrameAddr,
    };
    Kind kind;
    int32_t imm = 0;
    FrameId frame = 0;
    int32_t offset = 0;
};

struct Allocation {
    std::unordered_map<MachineValueId, PhysicalRegister> assigned;
    std::unordered_map<MachineValueId, FrameId> spilled;
    std::unordered_map<MachineValueId, RematInfo> rematerialized;
};

struct OriginalNode {
    std::optional<MachineValueId> vreg;
    std::optional<PhysicalRegister> precolor;
    RegisterClass reg_class = RegisterClass::Gpr64;
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
    enum class AffinitySource {
        Copy,
        Phi,
    };
    struct AffinityEdge {
        int lhs = -1;
        int rhs = -1;
        AffinitySource source = AffinitySource::Copy;
    };
    std::vector<AffinityEdge> affinity_edges;
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
            } else if constexpr (std::is_same_v<T, ShiftImm>) {
                for_each_register_ref(value.lhs, fn);
            } else if constexpr (std::is_same_v<T, Compare>) {
                for_each_register_ref(value.lhs, fn);
                for_each_register_ref(value.rhs, fn);
            } else if constexpr (std::is_same_v<T, Load>) {
                for_each_address_register(value.address, fn);
            } else if constexpr (std::is_same_v<T, Store>) {
                for_each_register_ref(value.src, fn);
                for_each_address_register(value.address, fn);
            } else if constexpr (std::is_same_v<T, Call>) {
                for (const auto reg : value.uses) {
                    fn(reg);
                }
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
                          std::is_same_v<T, Binary> || std::is_same_v<T, ShiftImm> ||
                          std::is_same_v<T, Compare> || std::is_same_v<T, FrameAddr> ||
                          std::is_same_v<T, Load>) {
                for_each_register_ref(value.dest, fn);
            } else if constexpr (std::is_same_v<T, Call>) {
                for (const auto reg : value.defs) {
                    fn(reg);
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
            if constexpr (std::is_same_v<T, BranchNonZero>) {
                for_each_register_ref(value.condition, fn);
            } else if constexpr (std::is_same_v<T, BranchCond>) {
                for_each_register_ref(value.lhs, fn);
                for_each_register_ref(value.rhs, fn);
            } else if constexpr (std::is_same_v<T, Return>) {
                if (value.value) {
                    for_each_register_ref(*value.value, fn);
                }
            }
        },
        term);
}

std::optional<MachineValueId> copy_src_vreg(const Instruction& inst) {
    const auto* copy = std::get_if<Copy>(&inst);
    if (!copy) {
        return std::nullopt;
    }
    const auto* src = std::get_if<VirtualRegister>(&copy->src);
    if (!src) {
        return std::nullopt;
    }
    return src->id;
}

std::optional<MachineValueId> copy_dest_vreg(const Instruction& inst) {
    const auto* copy = std::get_if<Copy>(&inst);
    if (!copy) {
        return std::nullopt;
    }
    const auto* dest = std::get_if<VirtualRegister>(&copy->dest);
    if (!dest) {
        return std::nullopt;
    }
    return dest->id;
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
                    .reg_class = RegisterClass::Gpr64,
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

void add_vreg_clique_edges(std::vector<std::unordered_set<int>>& adjacency,
                           const NodeTable& table,
                           const std::vector<MachineValueId>& values) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        for (std::size_t j = i + 1; j < values.size(); ++j) {
            if (values[i] == values[j]) {
                continue;
            }
            add_edge(adjacency, table.vreg_nodes.at(values[i]), table.vreg_nodes.at(values[j]));
        }
    }
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

std::unordered_map<MachineValueId, RematInfo> build_remat_map(const MachineFunction& fn) {
    std::unordered_map<MachineValueId, RematInfo> map;
    for (const auto& block : fn.blocks) {
        for (const auto& inst : block.instructions) {
            if (const auto* li = std::get_if<Li>(&inst)) {
                const auto& vreg = std::get<VirtualRegister>(li->dest);
                map[vreg.id] = RematInfo{.kind = RematInfo::Kind::Li, .imm = li->value};
            } else if (const auto* fa = std::get_if<FrameAddr>(&inst)) {
                const auto& vreg = std::get<VirtualRegister>(fa->dest);
                map[vreg.id] = RematInfo{.kind = RematInfo::Kind::FrameAddr,
                                         .frame = fa->frame,
                                         .offset = fa->offset};
            }
        }
    }
    return map;
}

Allocation spill_all_virtual_registers(MachineFunction& fn,
                                       const NodeTable& table,
                                       const std::unordered_map<MachineValueId, RematInfo>& remat_map) {
    Allocation alloc;
    for (const auto& [vreg, _] : table.vreg_nodes) {
        if (const auto rit = remat_map.find(vreg); rit != remat_map.end()) {
            alloc.rematerialized.emplace(vreg, rit->second);
            continue;
        }

        const FrameId frame = fn.frame_objects.size();
        fn.frame_objects.push_back(FrameObject{
            .id = frame,
            .kind = FrameObjectKind::Spill,
            .size = 8,
            .align = 8,
            .host_type = semantic::invalid_type_id,
            .spill_class = RegisterClass::Gpr64,
            .source_slot = std::nullopt,
            .debug_name = "",
            .saved_reg = std::nullopt,
            .materialized_offset = std::nullopt,
        });
        alloc.spilled.emplace(vreg, frame);
    }
    return alloc;
}

void bump_weight(NodeTable& table, const RegisterRef& reg) {
    if (const auto* vreg = std::get_if<VirtualRegister>(&reg)) {
        ++table.nodes[static_cast<std::size_t>(table.vreg_nodes.at(vreg->id))].weight;
    }
}

void add_affinity_edge(GraphInput& input, int lhs, int rhs, GraphInput::AffinitySource source) {
    if (lhs == rhs) {
        return;
    }
    input.affinity_edges.push_back({.lhs = lhs, .rhs = rhs, .source = source});
}

GraphInput build_graph(const MachineFunction& fn) {
    GraphInput input;
    input.table = collect_nodes(fn);
    input.adjacency.resize(input.table.nodes.size());

    for (std::size_t i = 0; i < kAllocatable.size(); ++i) {
        const int lhs = input.table.phys_nodes.at(kAllocatable[i]);
        for (std::size_t j = i + 1; j < kAllocatable.size(); ++j) {
            add_edge(input.adjacency, lhs, input.table.phys_nodes.at(kAllocatable[j]));
        }
    }

    const auto cfg = compute_cfg(fn);
    const auto live = compute_liveness(fn, cfg);

    for (const auto& block : fn.blocks) {
        std::vector<MachineValueId> phi_dests;
        for (const auto& phi : block.phis) {
            bump_weight(input.table, phi.dest);
            if (const auto* dest = std::get_if<VirtualRegister>(&phi.dest)) {
                phi_dests.push_back(dest->id);
            }
            for (const auto& incoming : phi.incoming) {
                bump_weight(input.table, incoming.value);
                const auto dest = node_for(input.table, phi.dest);
                const auto src = node_for(input.table, incoming.value);
                if (dest && src) {
                    add_affinity_edge(input, *dest, *src, GraphInput::AffinitySource::Phi);
                }
            }
        }
        add_vreg_clique_edges(input.adjacency, input.table, phi_dests);

        std::unordered_map<BlockId, std::vector<MachineValueId>> phi_edge_values;
        for (const auto& phi : block.phis) {
            for (const auto& incoming : phi.incoming) {
                if (const auto* value = std::get_if<VirtualRegister>(&incoming.value)) {
                    phi_edge_values[incoming.pred].push_back(value->id);
                }
            }
        }
        for (const auto& [_, values] : phi_edge_values) {
            add_vreg_clique_edges(input.adjacency, input.table, values);
        }

        for (const auto& inst : block.instructions) {
            for_each_instruction_def(inst, [&](const auto& reg) { bump_weight(input.table, reg); });
            for_each_instruction_use(inst, [&](const auto& reg) { bump_weight(input.table, reg); });
            if (const auto* copy = std::get_if<Copy>(&inst)) {
                const auto dest = node_for(input.table, copy->dest);
                const auto src = node_for(input.table, copy->src);
                if (dest && src) {
                    add_affinity_edge(input, *dest, *src, GraphInput::AffinitySource::Copy);
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

    auto add_single_phys_live_edges =
        [&](PhysicalRegister phys,
            const std::unordered_set<MachineValueId>& live_vregs,
            std::optional<MachineValueId> excluded = std::nullopt) {
            if (!is_allocatable_register(phys)) {
                return;
            }
            const int phys_node = input.table.phys_nodes.at(phys);
            for (const auto live_vreg : live_vregs) {
                if (excluded && *excluded == live_vreg) {
                    continue;
                }
                add_edge(input.adjacency, phys_node, input.table.vreg_nodes.at(live_vreg));
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
            if (std::holds_alternative<Copy>(inst)) {
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

                const auto src_vreg = copy_src_vreg(inst);
                const auto dest_vreg = copy_dest_vreg(inst);
                for (const auto phys : phys_defs) {
                    add_single_phys_live_edges(phys, current_live, src_vreg);
                }
                for (const auto phys : phys_uses) {
                    add_single_phys_live_edges(phys, current_live, dest_vreg);
                }

                for (const auto def : defs) {
                    const int def_node = input.table.vreg_nodes.at(def);
                    for (const auto live_vreg : current_live) {
                        if (def == live_vreg) {
                            continue;
                        }
                        if (src_vreg && *src_vreg == live_vreg) {
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
                continue;
            }

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

            add_vreg_clique_edges(input.adjacency, input.table, uses);

            add_phys_live_edges(phys_defs, current_live);
            add_phys_live_edges(phys_uses, current_live);

            for (const auto def : defs) {
                const int def_node = input.table.vreg_nodes.at(def);
                for (const auto live_vreg : current_live) {
                    if (def == live_vreg) {
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

    std::stable_sort(input.affinity_edges.begin(),
                     input.affinity_edges.end(),
                     [](const GraphInput::AffinityEdge& lhs, const GraphInput::AffinityEdge& rhs) {
                         auto priority = [](GraphInput::AffinitySource source) {
                             switch (source) {
                             case GraphInput::AffinitySource::Phi:
                                 return 0;
                             case GraphInput::AffinitySource::Copy:
                                 return 1;
                             }
                             return 3;
                         };
                         return priority(lhs.source) < priority(rhs.source);
                     });

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

void merge_coalesced_nodes(CoalescedGraph& graph, int lhs, int rhs, int rep) {
    const int merged = (rep == lhs) ? rhs : lhs;
    if (rep == merged) {
        return;
    }

    auto rep_it = graph.nodes.find(rep);
    auto merged_it = graph.nodes.find(merged);
    if (rep_it == graph.nodes.end() || merged_it == graph.nodes.end()) {
        throw std::runtime_error("coalesced graph lost a node during incremental merge");
    }

    auto& rep_node = rep_it->second;
    CoalescedNode merged_node = std::move(merged_it->second);
    graph.nodes.erase(merged_it);

    rep_node.weight += merged_node.weight;
    if (!rep_node.precolor && merged_node.precolor) {
        rep_node.precolor = merged_node.precolor;
    }
    rep_node.members.insert(rep_node.members.end(),
                            std::make_move_iterator(merged_node.members.begin()),
                            std::make_move_iterator(merged_node.members.end()));

    rep_node.neighbors.erase(rep);
    rep_node.neighbors.erase(merged);

    for (const int neighbor : merged_node.neighbors) {
        if (neighbor == rep) {
            continue;
        }
        auto neighbor_it = graph.nodes.find(neighbor);
        if (neighbor_it == graph.nodes.end()) {
            throw std::runtime_error("coalesced graph lost a neighbor during incremental merge");
        }
        auto& neighbor_node = neighbor_it->second;
        neighbor_node.neighbors.erase(merged);
        neighbor_node.neighbors.insert(rep);
        rep_node.neighbors.insert(neighbor);
    }
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

Allocation color_graph(MachineFunction& fn,
                       const GraphInput& input,
                       const std::unordered_map<MachineValueId, RematInfo>& remat_map) {
    Dsu dsu(input.table.nodes.size());
    CoalescedGraph graph = build_coalesced_graph(input, dsu);

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& edge : input.affinity_edges) {
            const int lhs = dsu.find(edge.lhs);
            const int rhs = dsu.find(edge.rhs);
            if (!can_coalesce(graph, lhs, rhs)) {
                continue;
            }
            const bool prefer_lhs = graph.nodes.at(lhs).precolor.has_value() ||
                                    !graph.nodes.at(rhs).precolor.has_value();
            const int rep = dsu.unite(lhs, rhs, prefer_lhs);
            merge_coalesced_nodes(graph, lhs, rhs, rep);
            changed = true;
        }
    }
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
                if (const auto rit = remat_map.find(vreg); rit != remat_map.end()) {
                    alloc.rematerialized.emplace(vreg, rit->second);
                } else {
                    const FrameId frame = fn.frame_objects.size();
                    fn.frame_objects.push_back(FrameObject{
                        .id = frame,
                        .kind = FrameObjectKind::Spill,
                        .size = 8,
                        .align = 8,
                        .host_type = semantic::invalid_type_id,
                        .spill_class = RegisterClass::Gpr64,
                        .source_slot = std::nullopt,
                        .debug_name = "",
                        .saved_reg = std::nullopt,
                        .materialized_offset = std::nullopt,
                    });
                    alloc.spilled.emplace(vreg, frame);
                }
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

    if (const auto rit = alloc.rematerialized.find(vreg.id); rit != alloc.rematerialized.end()) {
        PhysicalRegister scratch;
        if (!scratch0_used) {
            scratch = kScratch0;
            scratch0_used = true;
        } else if (!scratch1_used) {
            scratch = kScratch1;
            scratch1_used = true;
        } else {
            throw std::runtime_error(
                "Exceeded 2-scratch-register budget in instruction rewrite");
        }

        if (rit->second.kind == RematInfo::Kind::Li) {
            pre.push_back(Li{.dest = scratch, .value = rit->second.imm});
        } else {
            pre.push_back(FrameAddr{
                .dest = scratch, .frame = rit->second.frame, .offset = rit->second.offset});
        }
        return scratch;
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
        .width = MachineWidth::XLen,
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
    if (alloc.rematerialized.contains(id)) {
        return kScratch0;
    }
    post.push_back(Store{
        .address = FrameAddress{.frame = alloc.spilled.at(id), .offset = 0},
        .width = MachineWidth::XLen,
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

std::optional<Instruction> rewrite_instruction(const Instruction& inst,
                                             const Allocation& alloc,
                                             std::vector<Instruction>& pre,
                                             std::vector<Instruction>& post) {
    return std::visit(
        [&](const auto& value) -> std::optional<Instruction> {
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
                const auto vreg_id = std::get<VirtualRegister>(value.dest).id;
                if (alloc.rematerialized.contains(vreg_id)) {
                    return std::nullopt;
                }
                return Li{.dest = rewrite_dest(vreg_id, alloc, post),
                          .value = value.value};
            } else if constexpr (std::is_same_v<T, Binary>) {
                return Binary{
                    .dest = rewrite_dest(std::get<VirtualRegister>(value.dest).id, alloc, post),
                    .op = value.op,
                    .width = value.width,
                    .lhs = rewrite_src(value.lhs, alloc, pre, scratch0_used, scratch1_used),
                    .rhs = rewrite_src(value.rhs, alloc, pre, scratch0_used, scratch1_used),
                };
            } else if constexpr (std::is_same_v<T, ShiftImm>) {
                return ShiftImm{
                    .dest = rewrite_dest(std::get<VirtualRegister>(value.dest).id, alloc, post),
                    .op = value.op,
                    .width = value.width,
                    .lhs = rewrite_src(value.lhs, alloc, pre, scratch0_used, scratch1_used),
                    .amount = value.amount,
                };
            } else if constexpr (std::is_same_v<T, Compare>) {
                return Compare{
                    .dest = rewrite_dest(std::get<VirtualRegister>(value.dest).id, alloc, post),
                    .op = value.op,
                    .lhs = rewrite_src(value.lhs, alloc, pre, scratch0_used, scratch1_used),
                    .rhs = rewrite_src(value.rhs, alloc, pre, scratch0_used, scratch1_used),
                };
            } else if constexpr (std::is_same_v<T, FrameAddr>) {
                const auto vreg_id = std::get<VirtualRegister>(value.dest).id;
                if (alloc.rematerialized.contains(vreg_id)) {
                    return std::nullopt;
                }
                return FrameAddr{
                    .dest = rewrite_dest(vreg_id, alloc, post),
                    .frame = value.frame,
                    .offset = value.offset,
                };
            } else if constexpr (std::is_same_v<T, Load>) {
                return Load{
                    .dest = rewrite_dest(std::get<VirtualRegister>(value.dest).id, alloc, post),
                    .width = value.width,
                    .address =
                        rewrite_address(value.address, alloc, pre, scratch0_used, scratch1_used),
                };
            } else if constexpr (std::is_same_v<T, Store>) {
                return Store{
                    .address =
                        rewrite_address(value.address, alloc, pre, scratch0_used, scratch1_used),
                    .width = value.width,
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

std::pair<RegisterRef, RegisterRef> rewrite_register_ref_pair(
    const RegisterRef& lhs,
    const RegisterRef& rhs,
    const Allocation& alloc,
    std::vector<Instruction>& spill_loads) {
    bool scratch0_used = false;
    bool scratch1_used = false;
    const RegisterRef new_lhs = rewrite_src(lhs, alloc, spill_loads, scratch0_used, scratch1_used);
    const RegisterRef new_rhs = rewrite_src(rhs, alloc, spill_loads, scratch0_used, scratch1_used);
    return {new_lhs, new_rhs};
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
        std::optional<Instruction> body = rewrite_instruction(inst, alloc, pre, post);
        rewritten.insert(rewritten.end(),
                         std::make_move_iterator(pre.begin()),
                         std::make_move_iterator(pre.end()));
        if (body) {
            if (const auto* copy = std::get_if<Copy>(&*body);
                !copy || copy->dest != copy->src) {
                rewritten.push_back(std::move(*body));
            }
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
                } else if constexpr (std::is_same_v<T, BranchCond>) {
                    std::vector<Instruction> loads;
                    const auto [lhs, rhs] =
                        rewrite_register_ref_pair(term.lhs, term.rhs, alloc, loads);
                    block.instructions.insert(block.instructions.end(),
                                               std::make_move_iterator(loads.begin()),
                                               std::make_move_iterator(loads.end()));
                    return BranchCond{
                        .op = term.op,
                        .lhs = lhs,
                        .rhs = rhs,
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

void fixup_remat_phi_operands(MachineFunction& fn, const Allocation& alloc) {
    if (alloc.rematerialized.empty()) {
        return;
    }

    std::unordered_map<BlockId, std::size_t> block_index;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        block_index[fn.blocks[i].id] = i;
    }

    struct RematKey {
        RematInfo::Kind kind;
        int32_t imm = 0;
        FrameId frame = 0;
        int32_t offset = 0;
        bool operator==(const RematKey& other) const {
            return kind == other.kind && imm == other.imm && frame == other.frame &&
                   offset == other.offset;
        }
    };
    struct RematKeyHash {
        std::size_t operator()(const RematKey& key) const {
            auto h = std::hash<int>{}(static_cast<int>(key.kind));
            h ^= std::hash<int32_t>{}(key.imm) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<FrameId>{}(key.frame) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int32_t>{}(key.offset) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_map<BlockId,
                       std::unordered_map<RematKey, PhysicalRegister, RematKeyHash>>
        pred_remat_scratch;

    for (auto& block : fn.blocks) {
        for (auto& phi : block.phis) {
            for (auto& incoming : phi.incoming) {
                const auto* vreg = std::get_if<VirtualRegister>(&incoming.value);
                if (!vreg) {
                    continue;
                }
                const auto rit = alloc.rematerialized.find(vreg->id);
                if (rit == alloc.rematerialized.end()) {
                    continue;
                }

                const RematKey key{.kind = rit->second.kind,
                                   .imm = rit->second.imm,
                                   .frame = rit->second.frame,
                                   .offset = rit->second.offset};
                auto& pred_map = pred_remat_scratch[incoming.pred];

                PhysicalRegister scratch;
                if (auto sit = pred_map.find(key); sit != pred_map.end()) {
                    scratch = sit->second;
                } else {
                    if (pred_map.size() >= 2) {
                        throw std::runtime_error(
                            "Exceeded 2-scratch-register budget in remat phi fixup");
                    }
                    scratch = pred_map.empty() ? kScratch0 : kScratch1;
                    pred_map[key] = scratch;

                    auto& pred_block = fn.blocks[block_index.at(incoming.pred)];
                    if (rit->second.kind == RematInfo::Kind::Li) {
                        pred_block.instructions.push_back(
                            Li{.dest = scratch, .value = rit->second.imm});
                    } else {
                        pred_block.instructions.push_back(
                            FrameAddr{.dest = scratch,
                                      .frame = rit->second.frame,
                                      .offset = rit->second.offset});
                    }
                }

                incoming.value = scratch;
            }
        }
    }
}

} // namespace

AllocationStats allocate_registers(MachineFunction& fn) {
    const auto table = collect_nodes(fn);
    const auto remat_map = build_remat_map(fn);
    Allocation alloc;
    if (table.vreg_nodes.size() > kFastSpillVregThreshold) {
        alloc = spill_all_virtual_registers(fn, table, remat_map);
    } else {
        auto graph = build_graph(fn);
        alloc = color_graph(fn, graph, remat_map);
    }

    fixup_remat_phi_operands(fn, alloc);

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
