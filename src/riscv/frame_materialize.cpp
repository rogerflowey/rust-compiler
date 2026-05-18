#include "riscv/frame_materialize.hpp"

#include "riscv/layout.hpp"
#include "riscv/prologue_epilogue.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace riscv {
namespace {

constexpr std::uint32_t kCallFrameAlign = 16;

std::string frame_name(FrameId frame) {
    return "fi" + std::to_string(frame);
}

[[noreturn]] void fail(const MachineFunction& fn, const std::string& message) {
    throw FrameMaterializeError("Frame materialization failed for @" + fn.symbol +
                                ": " + message);
}

const FrameObject* find_frame_object(const MachineFunction& fn, FrameId id) {
    for (const auto& object : fn.frame_objects) {
        if (object.id == id) {
            return &object;
        }
    }
    return nullptr;
}

void ensure_unmaterialized(const MachineFunction& fn) {
    if (fn.frame_size) {
        fail(fn, "function already has a materialized frame size");
    }
    for (const auto& object : fn.frame_objects) {
        if (object.materialized_offset) {
            fail(fn,
                 "frame object " + frame_name(object.id) +
                     " already has a materialized offset");
        }
    }
}

void ensure_closed_frame(const MachineFunction& fn, const PrologueEpiloguePlan& plan) {
    if (fn.needs_frame_pointer != plan.needs_frame_pointer) {
        fail(fn, "needs_frame_pointer does not match prologue/epilogue plan");
    }

    std::unordered_set<PhysicalRegister> expected(plan.saved_registers.begin(),
                                                  plan.saved_registers.end());
    std::unordered_set<PhysicalRegister> actual;
    for (const auto& object : fn.frame_objects) {
        if (object.kind != FrameObjectKind::CalleeSave) {
            continue;
        }
        if (!object.callee_save_reg) {
            fail(fn, "callee-save frame object " + frame_name(object.id) +
                         " is missing its saved register");
        }
        if (!actual.insert(*object.callee_save_reg).second) {
            fail(fn, "duplicate callee-save frame object for register " +
                         std::string(physical_register_name(*object.callee_save_reg)));
        }
        if (!expected.contains(*object.callee_save_reg)) {
            fail(fn, "unexpected callee-save frame object for register " +
                         std::string(physical_register_name(*object.callee_save_reg)));
        }
    }

    for (const auto reg : plan.saved_registers) {
        if (!actual.contains(reg)) {
            fail(fn, "missing callee-save frame object for register " +
                         std::string(physical_register_name(reg)));
        }
    }
}

void assign_object(FrameObject& object, std::uint32_t& cursor) {
    cursor = align_to(cursor, object.align);
    object.materialized_offset = static_cast<std::int32_t>(cursor);
    cursor += object.size;
}

void assign_primary_frame_objects(MachineFunction& fn, std::uint32_t& cursor) {
    auto by_id = [](const FrameObject* lhs, const FrameObject* rhs) { return lhs->id < rhs->id; };
    std::vector<FrameObject*> outgoing;
    std::vector<FrameObject*> locals_and_spills;
    for (auto& object : fn.frame_objects) {
        if (object.kind == FrameObjectKind::IncomingArg ||
            object.kind == FrameObjectKind::CalleeSave) {
            continue;
        }
        if (object.kind == FrameObjectKind::OutgoingArg) {
            outgoing.push_back(&object);
        } else {
            locals_and_spills.push_back(&object);
        }
    }

    std::sort(outgoing.begin(), outgoing.end(), by_id);
    std::sort(locals_and_spills.begin(), locals_and_spills.end(), by_id);

    for (auto* object : outgoing) {
        assign_object(*object, cursor);
    }
    for (auto* object : locals_and_spills) {
        assign_object(*object, cursor);
    }
}

void assign_save_slots(MachineFunction& fn,
                       const std::vector<PhysicalRegister>& saved_regs,
                       std::uint32_t& cursor) {
    for (const auto reg : saved_regs) {
        FrameObject* object = nullptr;
        for (auto& candidate : fn.frame_objects) {
            if (candidate.kind == FrameObjectKind::CalleeSave &&
                candidate.callee_save_reg == reg) {
                object = &candidate;
                break;
            }
        }
        if (!object) {
            fail(fn,
                 "missing callee-save frame object for register " +
                     std::string(physical_register_name(reg)));
        }
        assign_object(*object, cursor);
    }
}

void assign_incoming_args(MachineFunction& fn, std::uint32_t frame_size) {
    std::vector<FrameObject*> incoming;
    for (auto& object : fn.frame_objects) {
        if (object.kind == FrameObjectKind::IncomingArg) {
            incoming.push_back(&object);
        }
    }
    std::sort(incoming.begin(), incoming.end(), [](const FrameObject* lhs, const FrameObject* rhs) {
        return lhs->id < rhs->id;
    });
    for (std::size_t i = 0; i < incoming.size(); ++i) {
        incoming[i]->materialized_offset =
            static_cast<std::int32_t>(frame_size + static_cast<std::uint32_t>(i * 4));
    }
}

} // namespace

void materialize_frame(MachineFunction& fn) {
    ensure_unmaterialized(fn);
    PrologueEpiloguePlan plan;
    try {
        plan = compute_prologue_epilogue_plan(fn);
    } catch (const PrologueEpilogueError& error) {
        throw FrameMaterializeError(error.what());
    }
    ensure_closed_frame(fn, plan);

    std::uint32_t cursor = 0;
    assign_primary_frame_objects(fn, cursor);
    assign_save_slots(fn, plan.saved_registers, cursor);

    const std::uint32_t frame_size = align_to(cursor, kCallFrameAlign);
    assign_incoming_args(fn, frame_size);

    fn.frame_size = frame_size;
}

void materialize_frame(MachineModule& module) {
    for (auto& fn : module.functions) {
        materialize_frame(fn);
    }
}

std::int32_t materialized_frame_offset(const MachineFunction& fn, FrameId frame) {
    const FrameObject* object = find_frame_object(fn, frame);
    if (!object) {
        throw FrameMaterializeError("Frame materialization query references unknown frame " +
                                    frame_name(frame));
    }
    if (!object->materialized_offset) {
        throw FrameMaterializeError("Frame object " + frame_name(frame) +
                                    " has no materialized offset");
    }
    return *object->materialized_offset;
}

} // namespace riscv
