#include "riscv/frame_materialize.hpp"

#include "riscv/layout.hpp"

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

int callee_save_rank(PhysicalRegister reg) {
    switch (reg) {
    case PhysicalRegister::Ra:
        return 0;
    case PhysicalRegister::S0:
        return 1;
    case PhysicalRegister::S1:
        return 2;
    case PhysicalRegister::S2:
        return 3;
    case PhysicalRegister::S3:
        return 4;
    case PhysicalRegister::S4:
        return 5;
    case PhysicalRegister::S5:
        return 6;
    case PhysicalRegister::S6:
        return 7;
    case PhysicalRegister::S7:
        return 8;
    case PhysicalRegister::S8:
        return 9;
    case PhysicalRegister::S9:
        return 10;
    case PhysicalRegister::S10:
        return 11;
    case PhysicalRegister::S11:
        return 12;
    default:
        return -1;
    }
}

std::vector<FrameObject*> collect_save_slots(MachineFunction& fn) {
    std::unordered_set<PhysicalRegister> seen;
    std::vector<FrameObject*> save_slots;
    for (auto& object : fn.frame_objects) {
        if (object.kind != FrameObjectKind::CalleeSave) {
            continue;
        }
        if (!object.saved_reg) {
            fail(fn, "callee-save frame object " + frame_name(object.id) +
                         " is missing its saved register");
        }
        if (!is_callee_saved_register(*object.saved_reg)) {
            fail(fn, "callee-save frame object " + frame_name(object.id) +
                         " uses a non-callee-saved register");
        }
        if (callee_save_rank(*object.saved_reg) < 0) {
            fail(fn, "callee-save frame object " + frame_name(object.id) +
                         " uses an unsupported saved register");
        }
        if (!seen.insert(*object.saved_reg).second) {
            fail(fn, "duplicate callee-save frame object for register " +
                         std::string(physical_register_name(*object.saved_reg)));
        }
        save_slots.push_back(&object);
    }
    std::sort(save_slots.begin(), save_slots.end(), [](const FrameObject* lhs, const FrameObject* rhs) {
        const int lhs_rank = callee_save_rank(*lhs->saved_reg);
        const int rhs_rank = callee_save_rank(*rhs->saved_reg);
        if (lhs_rank != rhs_rank) {
            return lhs_rank < rhs_rank;
        }
        return lhs->id < rhs->id;
    });
    return save_slots;
}

void assign_object(FrameObject& object, std::uint32_t& cursor) {
    cursor = align_to(cursor, object.align);
    object.materialized_offset = static_cast<std::int32_t>(cursor);
    cursor += object.size;
}

void assign_primary_frame_objects(MachineFunction& fn, std::uint32_t& cursor) {
    auto by_id = [](const FrameObject* lhs, const FrameObject* rhs) { return lhs->id < rhs->id; };
    std::vector<FrameObject*> outgoing;
    std::vector<FrameObject*> small_hot;
    std::vector<FrameObject*> large_locals;

    auto is_large_local = [](const FrameObject& object) {
        return object.kind == FrameObjectKind::LocalSlot && object.size > 2048;
    };

    for (auto& object : fn.frame_objects) {
        if (object.kind == FrameObjectKind::IncomingArg ||
            object.kind == FrameObjectKind::CalleeSave) {
            continue;
        }
        if (object.kind == FrameObjectKind::OutgoingArg) {
            outgoing.push_back(&object);
        } else if (is_large_local(object)) {
            large_locals.push_back(&object);
        } else {
            small_hot.push_back(&object);
        }
    }

    std::sort(outgoing.begin(), outgoing.end(), by_id);
    std::sort(small_hot.begin(), small_hot.end(), by_id);
    std::sort(large_locals.begin(), large_locals.end(), by_id);

    for (auto* object : outgoing) {
        assign_object(*object, cursor);
    }
    for (auto* object : small_hot) {
        assign_object(*object, cursor);
    }
    for (auto* object : large_locals) {
        assign_object(*object, cursor);
    }
}

void assign_save_slots(const std::vector<FrameObject*>& save_slots, std::uint32_t& cursor) {
    for (auto* object : save_slots) {
        assign_object(*object, cursor);
    }
}

void assign_incoming_args(MachineFunction& fn,
                          std::uint32_t frame_size,
                          const TargetConfig& target) {
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
            static_cast<std::int32_t>(frame_size +
                                      static_cast<std::uint32_t>(i * target.xlen_bytes));
    }
}

} // namespace

void materialize_frame(MachineFunction& fn, const TargetConfig& target) {
    const auto save_slots = collect_save_slots(fn);

    std::uint32_t cursor = 0;
    assign_primary_frame_objects(fn, cursor);
    assign_save_slots(save_slots, cursor);

    const std::uint32_t frame_size = align_to(cursor, kCallFrameAlign);
    assign_incoming_args(fn, frame_size, target);

    fn.frame_size = frame_size;
}

void materialize_frame(MachineModule& module, const TargetConfig& target) {
    for (auto& fn : module.functions) {
        materialize_frame(fn, target);
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
