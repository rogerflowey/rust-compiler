#include "riscv/passes/large_frame_base_hoist.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace riscv {
namespace {

constexpr std::int32_t kLargeFrameObjectThreshold = 2048;
constexpr std::int32_t kHoistUseThreshold = 2;

std::optional<MachineValueId> vreg_id(const RegisterRef& reg) {
    if (const auto* vreg = std::get_if<VirtualRegister>(&reg)) {
        return vreg->id;
    }
    return std::nullopt;
}

void note_reg_id(MachineFunction& fn, const RegisterRef& reg) {
    if (const auto id = vreg_id(reg)) {
        fn.next_value = std::max(fn.next_value, *id + 1);
    }
}

void refresh_next_value(MachineFunction& fn) {
    for (const auto& block : fn.blocks) {
        for (const auto& phi : block.phis) {
            note_reg_id(fn, phi.dest);
            for (const auto& incoming : phi.incoming) {
                note_reg_id(fn, incoming.value);
            }
        }
        for (const auto& inst : block.instructions) {
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, Copy>) {
                        note_reg_id(fn, value.dest);
                        note_reg_id(fn, value.src);
                    } else if constexpr (std::is_same_v<T, Li>) {
                        note_reg_id(fn, value.dest);
                    } else if constexpr (std::is_same_v<T, Binary>) {
                        note_reg_id(fn, value.dest);
                        note_reg_id(fn, value.lhs);
                        note_reg_id(fn, value.rhs);
                    } else if constexpr (std::is_same_v<T, ShiftImm>) {
                        note_reg_id(fn, value.dest);
                        note_reg_id(fn, value.lhs);
                    } else if constexpr (std::is_same_v<T, Compare>) {
                        note_reg_id(fn, value.dest);
                        note_reg_id(fn, value.lhs);
                        note_reg_id(fn, value.rhs);
                    } else if constexpr (std::is_same_v<T, FrameAddr>) {
                        note_reg_id(fn, value.dest);
                    } else if constexpr (std::is_same_v<T, Load>) {
                        note_reg_id(fn, value.dest);
                        if (const auto* reg_addr = std::get_if<RegisterAddress>(&value.address)) {
                            note_reg_id(fn, reg_addr->base);
                        }
                    } else if constexpr (std::is_same_v<T, Store>) {
                        note_reg_id(fn, value.src);
                        if (const auto* reg_addr = std::get_if<RegisterAddress>(&value.address)) {
                            note_reg_id(fn, reg_addr->base);
                        }
                    }
                },
                inst);
        }
        if (!block.terminator) {
            continue;
        }
        std::visit(
            [&](const auto& term) {
                using T = std::decay_t<decltype(term)>;
                if constexpr (std::is_same_v<T, BranchNonZero>) {
                    note_reg_id(fn, term.condition);
                } else if constexpr (std::is_same_v<T, BranchCond>) {
                    note_reg_id(fn, term.lhs);
                    note_reg_id(fn, term.rhs);
                } else if constexpr (std::is_same_v<T, Return>) {
                    if (term.value) {
                        note_reg_id(fn, *term.value);
                    }
                }
            },
            *block.terminator);
    }
}

const FrameObject* find_frame_object(const MachineFunction& fn, FrameId frame) {
    for (const auto& object : fn.frame_objects) {
        if (object.id == frame) {
            return &object;
        }
    }
    return nullptr;
}

bool should_hoist_object(const MachineFunction& fn, FrameId frame) {
    const auto* object = find_frame_object(fn, frame);
    return object && object->kind == FrameObjectKind::LocalSlot &&
           object->size > static_cast<std::uint32_t>(kLargeFrameObjectThreshold);
}

void record_frame_use(const Address& address, std::unordered_map<FrameId, int>& uses) {
    if (const auto* frame_addr = std::get_if<FrameAddress>(&address)) {
        ++uses[frame_addr->frame];
    }
}

void optimize_block(MachineFunction& fn, MachineBlock& block) {
    std::unordered_map<FrameId, int> uses;
    for (const auto& inst : block.instructions) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, Load>) {
                    record_frame_use(value.address, uses);
                } else if constexpr (std::is_same_v<T, Store>) {
                    record_frame_use(value.address, uses);
                } else if constexpr (std::is_same_v<T, FrameAddr>) {
                    if (value.offset == 0) {
                        ++uses[value.frame];
                    }
                }
            },
            inst);
    }

    std::unordered_map<FrameId, VirtualRegister> hoisted_bases;
    std::vector<Instruction> prefix;

    for (const auto& [frame, count] : uses) {
        if (count < kHoistUseThreshold || !should_hoist_object(fn, frame)) {
            continue;
        }
        VirtualRegister base{
            .id = fn.next_value++,
            .reg_class = RegisterClass::Gpr64,
        };
        hoisted_bases.emplace(frame, base);
        prefix.push_back(FrameAddr{
            .dest = base,
            .frame = frame,
            .offset = 0,
        });
    }

    if (prefix.empty()) {
        return;
    }

    std::vector<Instruction> rewritten;
    rewritten.reserve(prefix.size() + block.instructions.size());
    rewritten.insert(rewritten.end(), prefix.begin(), prefix.end());

    for (const auto& inst : block.instructions) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, Load>) {
                    if (const auto* frame_addr = std::get_if<FrameAddress>(&value.address)) {
                        if (const auto it = hoisted_bases.find(frame_addr->frame);
                            it != hoisted_bases.end()) {
                            rewritten.push_back(Load{
                                .dest = value.dest,
                                .width = value.width,
                                .address = RegisterAddress{
                                    .base = it->second,
                                    .offset = frame_addr->offset,
                                },
                            });
                            return;
                        }
                    }
                    rewritten.push_back(inst);
                } else if constexpr (std::is_same_v<T, Store>) {
                    if (const auto* frame_addr = std::get_if<FrameAddress>(&value.address)) {
                        if (const auto it = hoisted_bases.find(frame_addr->frame);
                            it != hoisted_bases.end()) {
                            rewritten.push_back(Store{
                                .address = RegisterAddress{
                                    .base = it->second,
                                    .offset = frame_addr->offset,
                                },
                                .width = value.width,
                                .src = value.src,
                            });
                            return;
                        }
                    }
                    rewritten.push_back(inst);
                } else if constexpr (std::is_same_v<T, FrameAddr>) {
                    if (value.offset == 0) {
                        if (const auto it = hoisted_bases.find(value.frame);
                            it != hoisted_bases.end()) {
                            rewritten.push_back(Copy{
                                .dest = value.dest,
                                .src = it->second,
                            });
                            return;
                        }
                    }
                    rewritten.push_back(inst);
                } else {
                    rewritten.push_back(inst);
                }
            },
            inst);
    }

    block.instructions = std::move(rewritten);
}

} // namespace

void optimize_large_frame_base_hoist(MachineFunction& fn) {
    refresh_next_value(fn);
    for (auto& block : fn.blocks) {
        optimize_block(fn, block);
    }
}

void optimize_large_frame_base_hoist(MachineModule& module) {
    for (auto& fn : module.functions) {
        optimize_large_frame_base_hoist(fn);
    }
}

} // namespace riscv
