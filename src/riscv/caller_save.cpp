#include "riscv/caller_save.hpp"

#include "riscv/analysis/cfg.hpp"

#include "semantic/type/type.hpp"

#include <array>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace riscv {
namespace {

struct PhysicalLiveness {
    std::vector<std::unordered_set<PhysicalRegister>> live_in;
    std::vector<std::unordered_set<PhysicalRegister>> live_out;
};

struct BlockInstructionLiveness {
    std::vector<std::unordered_set<PhysicalRegister>> live_before;
    std::vector<std::unordered_set<PhysicalRegister>> live_after;
};

struct CallRegion {
    std::size_t start = 0;
    std::size_t end = 0;
    std::vector<PhysicalRegister> preserved;
};

[[noreturn]] void fail(const MachineFunction& fn, const std::string& message) {
    throw CallerSaveError("Caller-save preservation failed for @" + fn.symbol + ": " + message);
}

template <class Fn>
void for_each_register_ref(const RegisterRef& reg, Fn&& fn) {
    if (const auto* phys = std::get_if<PhysicalRegister>(&reg)) {
        fn(*phys);
    } else if (std::holds_alternative<VirtualRegister>(reg)) {
        throw std::runtime_error("post-RA caller-save pass encountered a virtual register");
    }
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
            } else if constexpr (std::is_same_v<T, Return>) {
                if (value.value) {
                    for_each_register_ref(*value.value, fn);
                }
            }
        },
        term);
}

const FrameObject* find_frame_object(const MachineFunction& fn, FrameId id) {
    for (const auto& object : fn.frame_objects) {
        if (object.id == id) {
            return &object;
        }
    }
    return nullptr;
}

bool is_outgoing_arg_store(const MachineFunction& fn, const Instruction& inst) {
    const auto* store = std::get_if<Store>(&inst);
    if (!store) {
        return false;
    }
    const auto* address = std::get_if<FrameAddress>(&store->address);
    if (!address) {
        return false;
    }
    const FrameObject* object = find_frame_object(fn, address->frame);
    return object && object->kind == FrameObjectKind::OutgoingArg;
}

bool is_arg_setup(const MachineFunction& fn, const Instruction& inst) {
    if (const auto* copy = std::get_if<Copy>(&inst)) {
        const auto* dest = std::get_if<PhysicalRegister>(&copy->dest);
        return dest && is_argument_register(*dest);
    }
    return is_outgoing_arg_store(fn, inst);
}

bool is_result_extract(const Instruction& inst) {
    const auto* copy = std::get_if<Copy>(&inst);
    if (!copy) {
        return false;
    }
    const auto* src = std::get_if<PhysicalRegister>(&copy->src);
    return src && *src == PhysicalRegister::A0;
}

PhysicalLiveness compute_physical_liveness(const MachineFunction& fn, const CfgInfo& cfg) {
    PhysicalLiveness result;
    result.live_in.resize(fn.blocks.size());
    result.live_out.resize(fn.blocks.size());

    std::vector<std::unordered_set<PhysicalRegister>> upward(fn.blocks.size());
    std::vector<std::unordered_set<PhysicalRegister>> kill(fn.blocks.size());
    std::vector<std::unordered_set<PhysicalRegister>> phi_edge_uses(fn.blocks.size());

    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        const auto& block = fn.blocks[i];

        for (const auto& phi : block.phis) {
            if (const auto* dest = std::get_if<PhysicalRegister>(&phi.dest)) {
                kill[i].insert(*dest);
            } else if (std::holds_alternative<VirtualRegister>(phi.dest)) {
                fail(fn, "phi elimination must run after register allocation, but a phi still uses a vreg");
            }

            for (const auto& incoming : phi.incoming) {
                if (const auto* phys = std::get_if<PhysicalRegister>(&incoming.value)) {
                    phi_edge_uses[cfg.index_of.at(incoming.pred)].insert(*phys);
                } else if (std::holds_alternative<VirtualRegister>(incoming.value)) {
                    fail(fn, "caller-save pass encountered a virtual register phi incoming");
                }
            }
        }

        auto local_def = kill[i];
        for (const auto& inst : block.instructions) {
            for_each_instruction_use(inst, [&](PhysicalRegister reg) {
                if (!local_def.contains(reg)) {
                    upward[i].insert(reg);
                }
            });
            for_each_instruction_def(inst, [&](PhysicalRegister reg) {
                kill[i].insert(reg);
                local_def.insert(reg);
            });
        }
        if (block.terminator) {
            for_each_terminator_use(*block.terminator, [&](PhysicalRegister reg) {
                if (!local_def.contains(reg)) {
                    upward[i].insert(reg);
                }
            });
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (int rpo_index = static_cast<int>(cfg.rpo.size()) - 1; rpo_index >= 0; --rpo_index) {
            const std::size_t i = cfg.rpo[static_cast<std::size_t>(rpo_index)];

            std::unordered_set<PhysicalRegister> new_out = phi_edge_uses[i];
            for (const BlockId succ_id : cfg.successors[i]) {
                const std::size_t succ = cfg.index_of.at(succ_id);
                new_out.insert(result.live_in[succ].begin(), result.live_in[succ].end());
            }

            std::unordered_set<PhysicalRegister> new_in = upward[i];
            for (const auto reg : new_out) {
                if (!kill[i].contains(reg)) {
                    new_in.insert(reg);
                }
            }

            if (new_in != result.live_in[i] || new_out != result.live_out[i]) {
                result.live_in[i] = std::move(new_in);
                result.live_out[i] = std::move(new_out);
                changed = true;
            }
        }
    }

    return result;
}

BlockInstructionLiveness compute_block_instruction_liveness(
    const MachineBlock& block,
    const std::unordered_set<PhysicalRegister>& block_live_out) {
    BlockInstructionLiveness info;
    info.live_before.resize(block.instructions.size());
    info.live_after.resize(block.instructions.size());

    auto current = block_live_out;
    if (block.terminator) {
        for_each_terminator_use(*block.terminator,
                                [&](PhysicalRegister reg) { current.insert(reg); });
    }

    for (std::size_t index = block.instructions.size(); index-- > 0;) {
        info.live_after[index] = current;
        for_each_instruction_def(block.instructions[index],
                                 [&](PhysicalRegister reg) { current.erase(reg); });
        for_each_instruction_use(block.instructions[index],
                                 [&](PhysicalRegister reg) { current.insert(reg); });
        info.live_before[index] = current;
    }

    return info;
}

std::unordered_map<PhysicalRegister, FrameId> ensure_caller_save_slots(
    MachineFunction& fn,
    const std::unordered_set<PhysicalRegister>& regs) {
    std::unordered_map<PhysicalRegister, FrameId> slots;

    for (const auto& object : fn.frame_objects) {
        if (object.kind == FrameObjectKind::CallerSave && object.saved_reg) {
            slots.emplace(*object.saved_reg, object.id);
        }
    }

    for (const auto reg : regs) {
        if (slots.contains(reg)) {
            continue;
        }
        const FrameId id = fn.frame_objects.size();
        fn.frame_objects.push_back(FrameObject{
            .id = id,
            .kind = FrameObjectKind::CallerSave,
            .size = 4,
            .align = 4,
            .host_type = semantic::invalid_type_id,
            .spill_class = std::nullopt,
            .source_slot = std::nullopt,
            .debug_name = std::string("caller_save.") + physical_register_name(reg),
            .saved_reg = reg,
            .materialized_offset = std::nullopt,
        });
        slots.emplace(reg, id);
    }

    return slots;
}

std::vector<CallRegion> find_call_regions(const MachineFunction& fn,
                                          const MachineBlock& block,
                                          const BlockInstructionLiveness& liveness) {
    std::vector<CallRegion> regions;

    for (std::size_t i = 0; i < block.instructions.size(); ++i) {
        if (!std::holds_alternative<Call>(block.instructions[i])) {
            continue;
        }

        std::size_t start = i;
        while (start > 0 && is_arg_setup(fn, block.instructions[start - 1])) {
            --start;
        }

        std::size_t end = i;
        if (end + 1 < block.instructions.size() &&
            is_result_extract(block.instructions[end + 1])) {
            ++end;
        }

        const auto& live_before = liveness.live_before[start];
        const auto& live_after = liveness.live_after[end];
        std::vector<PhysicalRegister> preserved;
        for (const auto reg : live_before) {
            if (live_after.contains(reg) && is_caller_saved_register(reg)) {
                preserved.push_back(reg);
            }
        }
        std::sort(preserved.begin(), preserved.end(), [](auto lhs, auto rhs) {
            return static_cast<int>(lhs) < static_cast<int>(rhs);
        });

        if (!preserved.empty()) {
            regions.push_back(CallRegion{
                .start = start,
                .end = end,
                .preserved = std::move(preserved),
            });
        }
    }

    return regions;
}

} // namespace

void preserve_caller_saved(MachineFunction& fn) {
    const auto cfg = compute_cfg(fn);
    const auto block_liveness = compute_physical_liveness(fn, cfg);

    std::vector<std::vector<CallRegion>> regions_by_block(fn.blocks.size());
    std::unordered_set<PhysicalRegister> needed_slots;

    for (std::size_t block_index = 0; block_index < fn.blocks.size(); ++block_index) {
        auto instruction_liveness =
            compute_block_instruction_liveness(fn.blocks[block_index],
                                               block_liveness.live_out[block_index]);
        regions_by_block[block_index] =
            find_call_regions(fn, fn.blocks[block_index], instruction_liveness);
        for (const auto& region : regions_by_block[block_index]) {
            needed_slots.insert(region.preserved.begin(), region.preserved.end());
        }
    }

    if (needed_slots.empty()) {
        return;
    }

    const auto slots = ensure_caller_save_slots(fn, needed_slots);
    for (std::size_t block_index = 0; block_index < fn.blocks.size(); ++block_index) {
        const auto& regions = regions_by_block[block_index];
        if (regions.empty()) {
            continue;
        }

        std::vector<Instruction> rewritten;
        rewritten.reserve(fn.blocks[block_index].instructions.size() + needed_slots.size() * 2);

        std::size_t next_region = 0;
        for (std::size_t i = 0; i < fn.blocks[block_index].instructions.size(); ++i) {
            if (next_region < regions.size() && regions[next_region].start == i) {
                for (const auto reg : regions[next_region].preserved) {
                    rewritten.push_back(Store{
                        .address = FrameAddress{.frame = slots.at(reg), .offset = 0},
                        .src = reg,
                    });
                }
            }

            rewritten.push_back(fn.blocks[block_index].instructions[i]);

            if (next_region < regions.size() && regions[next_region].end == i) {
                for (auto it = regions[next_region].preserved.rbegin();
                     it != regions[next_region].preserved.rend();
                     ++it) {
                    rewritten.push_back(Load{
                        .dest = *it,
                        .address = FrameAddress{.frame = slots.at(*it), .offset = 0},
                    });
                }
                ++next_region;
            }
        }

        fn.blocks[block_index].instructions = std::move(rewritten);
    }
}

void preserve_caller_saved(MachineModule& module) {
    for (auto& fn : module.functions) {
        preserve_caller_saved(fn);
    }
}

} // namespace riscv
