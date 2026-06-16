#include "riscv/prologue_epilogue.hpp"

#include "semantic/type/type.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace riscv {
namespace {

constexpr std::array<PhysicalRegister, 11> kAllocatableRegs = {
    PhysicalRegister::S1,  PhysicalRegister::S2,  PhysicalRegister::S3,
    PhysicalRegister::S4,  PhysicalRegister::S5,  PhysicalRegister::S6,
    PhysicalRegister::S7,  PhysicalRegister::S8,  PhysicalRegister::S9,
    PhysicalRegister::S10, PhysicalRegister::S11,
};

[[noreturn]] void fail(const MachineFunction& fn, const std::string& message) {
    throw PrologueEpilogueError("Prologue/epilogue insertion failed for @" + fn.symbol +
                                ": " + message);
}

void record_used_register(const RegisterRef& reg,
                          std::unordered_set<PhysicalRegister>& used_allocatable) {
    if (const auto* phys = std::get_if<PhysicalRegister>(&reg);
        phys && is_allocatable_register(*phys)) {
        used_allocatable.insert(*phys);
    }
}

void record_used_registers_in_address(const Address& address,
                                      std::unordered_set<PhysicalRegister>& used_allocatable) {
    if (const auto* reg_addr = std::get_if<RegisterAddress>(&address)) {
        record_used_register(reg_addr->base, used_allocatable);
    }
}

MachineBlock& entry_block(MachineFunction& fn) {
    for (auto& block : fn.blocks) {
        if (block.id == fn.entry_block) {
            return block;
        }
    }
    fail(fn, "entry block bb" + std::to_string(fn.entry_block) + " is undefined");
}

std::unordered_map<PhysicalRegister, FrameId> append_missing_save_slots(
    MachineFunction& fn,
    const std::vector<PhysicalRegister>& saved_regs,
    const TargetConfig& target) {
    std::unordered_map<PhysicalRegister, FrameId> save_slots;

    for (const auto reg : saved_regs) {
        const FrameId id = fn.frame_objects.size();
        fn.frame_objects.push_back(FrameObject{
            .id = id,
            .kind = FrameObjectKind::CalleeSave,
            .size = target.xlen_bytes,
            .align = target.xlen_bytes,
            .host_type = semantic::invalid_type_id,
            .spill_class = std::nullopt,
            .source_slot = std::nullopt,
            .debug_name = std::string("save.") + physical_register_name(reg),
            .saved_reg = reg,
            .materialized_offset = std::nullopt,
        });
        save_slots.emplace(reg, id);
    }

    return save_slots;
}

std::vector<Instruction> save_instructions(
    const std::vector<PhysicalRegister>& saved_regs,
    const std::unordered_map<PhysicalRegister, FrameId>& save_slots) {
    std::vector<Instruction> instructions;
    instructions.reserve(saved_regs.size());
    for (const auto reg : saved_regs) {
        instructions.push_back(Store{
            .address = FrameAddress{.frame = save_slots.at(reg), .offset = 0},
            .width = MachineWidth::XLen,
            .src = reg,
        });
    }
    return instructions;
}

std::vector<Instruction> restore_instructions(
    const std::vector<PhysicalRegister>& saved_regs,
    const std::unordered_map<PhysicalRegister, FrameId>& save_slots) {
    std::vector<Instruction> instructions;
    instructions.reserve(saved_regs.size());
    for (auto it = saved_regs.rbegin(); it != saved_regs.rend(); ++it) {
        instructions.push_back(Load{
            .dest = *it,
            .width = MachineWidth::XLen,
            .address = FrameAddress{.frame = save_slots.at(*it), .offset = 0},
        });
    }
    return instructions;
}

} // namespace

PrologueEpiloguePlan compute_prologue_epilogue_plan(const MachineFunction& fn) {
    bool has_call = false;
    std::unordered_set<PhysicalRegister> used_allocatable;

    for (const auto& block : fn.blocks) {
        if (!block.phis.empty()) {
            fail(fn,
                 "post-phi-elim input required; block bb" + std::to_string(block.id) +
                     " still has phi nodes");
        }
        if (!block.terminator) {
            fail(fn,
                 "block bb" + std::to_string(block.id) +
                     " is missing a terminator before prologue/epilogue insertion");
        }

        for (const auto& inst : block.instructions) {
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, Copy>) {
                        record_used_register(value.dest, used_allocatable);
                        record_used_register(value.src, used_allocatable);
                    } else if constexpr (std::is_same_v<T, Li>) {
                        record_used_register(value.dest, used_allocatable);
                    } else if constexpr (std::is_same_v<T, Binary> ||
                                         std::is_same_v<T, Compare>) {
                        record_used_register(value.dest, used_allocatable);
                        record_used_register(value.lhs, used_allocatable);
                        record_used_register(value.rhs, used_allocatable);
                    } else if constexpr (std::is_same_v<T, ShiftImm>) {
                        record_used_register(value.dest, used_allocatable);
                        record_used_register(value.lhs, used_allocatable);
                    } else if constexpr (std::is_same_v<T, FrameAddr>) {
                        record_used_register(value.dest, used_allocatable);
                    } else if constexpr (std::is_same_v<T, Load>) {
                        record_used_register(value.dest, used_allocatable);
                        record_used_registers_in_address(value.address, used_allocatable);
                    } else if constexpr (std::is_same_v<T, Store>) {
                        record_used_register(value.src, used_allocatable);
                        record_used_registers_in_address(value.address, used_allocatable);
                    } else if constexpr (std::is_same_v<T, Call>) {
                        has_call = true;
                    }
                },
                inst);
        }

        std::visit(
            [&](const auto& term) {
                using T = std::decay_t<decltype(term)>;
                if constexpr (std::is_same_v<T, BranchNonZero>) {
                    record_used_register(term.condition, used_allocatable);
                } else if constexpr (std::is_same_v<T, BranchCond>) {
                    record_used_register(term.lhs, used_allocatable);
                    record_used_register(term.rhs, used_allocatable);
                } else if constexpr (std::is_same_v<T, Return>) {
                    if (term.value) {
                        record_used_register(*term.value, used_allocatable);
                    }
                }
            },
            *block.terminator);
    }

    const bool has_non_save_frame_object =
        std::any_of(fn.frame_objects.begin(), fn.frame_objects.end(), [](const FrameObject& object) {
            return object.kind != FrameObjectKind::CalleeSave &&
                   object.kind != FrameObjectKind::CallerSave;
        });

    const bool has_material_frame =
        has_non_save_frame_object || has_call || !used_allocatable.empty();

    PrologueEpiloguePlan plan{
        .frame_base = has_material_frame ? FrameBase::S0 : FrameBase::None,
        .saved_registers = {},
    };

    if (has_call) {
        plan.saved_registers.push_back(PhysicalRegister::Ra);
    }
    if (plan.frame_base == FrameBase::S0) {
        plan.saved_registers.push_back(PhysicalRegister::S0);
    }
    for (const auto reg : kAllocatableRegs) {
        if (used_allocatable.contains(reg)) {
            plan.saved_registers.push_back(reg);
        }
    }
    return plan;
}

void insert_prologue_epilogue(MachineFunction& fn, const TargetConfig& target) {
    if (fn.frame_base.has_value()) {
        fail(fn, "frame base already selected");
    }
    if (fn.frame_size) {
        fail(fn, "function already has a materialized frame size");
    }
    for (const auto& object : fn.frame_objects) {
        if (object.materialized_offset) {
            fail(fn, "frame object fi" + std::to_string(object.id) +
                         " already has a materialized offset");
        }
    }

    const auto plan = compute_prologue_epilogue_plan(fn);
    auto save_slots = append_missing_save_slots(fn, plan.saved_registers, target);
    fn.frame_base = plan.frame_base;

    if (plan.saved_registers.empty()) {
        return;
    }

    auto saves = save_instructions(plan.saved_registers, save_slots);
    auto restores = restore_instructions(plan.saved_registers, save_slots);

    auto& entry = entry_block(fn);
    entry.instructions.insert(entry.instructions.begin(), saves.begin(), saves.end());

    for (auto& block : fn.blocks) {
        if (!block.terminator || !std::holds_alternative<Return>(*block.terminator)) {
            continue;
        }
        block.instructions.insert(block.instructions.end(), restores.begin(), restores.end());
    }
}

void insert_prologue_epilogue(MachineModule& module, const TargetConfig& target) {
    for (auto& fn : module.functions) {
        insert_prologue_epilogue(fn, target);
    }
}

} // namespace riscv
