#include "riscv/validate.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace riscv {
namespace {

std::string vreg_name(const VirtualRegister& reg) {
    return "v" + std::to_string(reg.id);
}

std::string describe_frame(FrameId frame) {
    return "fi" + std::to_string(frame);
}

[[noreturn]] void fail(const MachineFunction& function, const std::string& message) {
    throw ValidationError("Machine IR validation failed for @" + function.symbol +
                          ": " + message);
}

std::unordered_map<BlockId, std::size_t> block_indices(const MachineFunction& function) {
    std::unordered_map<BlockId, std::size_t> indices;
    indices.reserve(function.blocks.size());
    for (std::size_t i = 0; i < function.blocks.size(); ++i) {
        indices.emplace(function.blocks[i].id, i);
    }
    return indices;
}

void validate_vreg(const MachineFunction& function,
                   const VirtualRegister& reg,
                   const char* role) {
    if (reg.reg_class != RegisterClass::Gpr32) {
        fail(function, std::string(role) + " " + vreg_name(reg) +
                           " has unsupported register class");
    }
    if (reg.id >= function.next_value) {
        fail(function, std::string(role) + " " + vreg_name(reg) +
                           " exceeds function.next_value");
    }
}

void validate_stage_physical_register(const MachineFunction& function,
                                      PhysicalRegister reg,
                                      const char* role,
                                      ValidationStage stage) {
    if (stage != ValidationStage::PreRegAlloc) {
        return;
    }
    if (!is_argument_register(reg)) {
        fail(function,
             std::string(role) + " uses physical register " + physical_register_name(reg) +
                 " outside the pre-RA ABI boundary");
    }
}

void validate_reg_use(const MachineFunction& function,
                      const RegisterRef& reg,
                      const char* role,
                      ValidationStage stage,
                      bool allow_physical,
                      bool allow_spill = false) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, VirtualRegister>) {
                validate_vreg(function, value, role);
                if (stage != ValidationStage::PreRegAlloc) {
                    fail(function, std::string(role) +
                                       " still holds a virtual register post-RA");
                }
            } else if constexpr (std::is_same_v<T, PhysicalRegister>) {
                if (!allow_physical) {
                    fail(function,
                         std::string(role) + " uses physical register " +
                             physical_register_name(value) +
                             " outside ABI/copy boundaries");
                }
            } else {
                if (stage == ValidationStage::PreRegAlloc) {
                    fail(function, std::string(role) + " uses a spill reference pre-RA");
                }
                if (!allow_spill) {
                    fail(function, std::string(role) +
                                       " uses a spill reference outside phi operands");
                }
            }
        },
        reg);
}

void validate_spill_ref(const MachineFunction& function,
                        const SpillRef& spill,
                        const char* role,
                        const std::unordered_map<FrameId, FrameObjectKind>& frame_kinds) {
    if (spill.reg_class != RegisterClass::Gpr32) {
        fail(function, std::string(role) + " uses unsupported spill register class");
    }
    const auto it = frame_kinds.find(spill.frame);
    if (it == frame_kinds.end()) {
        fail(function,
             std::string(role) + " references undefined spill frame " +
                 describe_frame(spill.frame));
    }
    if (it->second != FrameObjectKind::Spill) {
        fail(function,
             std::string(role) + " references non-spill frame " +
                 describe_frame(spill.frame));
    }
}

void validate_post_ra_phi(const MachineFunction& function,
                          const MachinePhi& phi,
                          const std::unordered_map<FrameId, FrameObjectKind>& frame_kinds) {
    auto check_phi_ref = [&](const RegisterRef& reg, const char* role) {
        validate_reg_use(function, reg, role, ValidationStage::PostRegAlloc, true, true);
        if (const auto* spill = std::get_if<SpillRef>(&reg)) {
            validate_spill_ref(function, *spill, role, frame_kinds);
        }
    };

    check_phi_ref(phi.dest, "phi destination");
    for (const auto& incoming : phi.incoming) {
        check_phi_ref(incoming.value, "phi incoming value");
    }
}

void reject_nonphysical_operand_post_ra(const MachineFunction& function,
                                        const RegisterRef& reg,
                                        const char* role) {
    if (std::holds_alternative<VirtualRegister>(reg)) {
        fail(function, std::string(role) + " still holds a virtual register post-RA");
    }
    if (const auto* spill = std::get_if<SpillRef>(&reg)) {
        fail(function,
             std::string(role) + " still holds spill reference " +
                 describe_frame(spill->frame) + " outside phi operands");
    }
}

template <class Fn>
void visit_instruction_uses(const Instruction& inst, Fn&& fn);

template <class Fn>
void visit_terminator_uses(const Terminator& terminator, Fn&& fn);

void validate_post_ra_operands(const MachineFunction& function,
                               const std::unordered_map<FrameId, FrameObjectKind>& frame_kinds) {
    for (const auto& block : function.blocks) {
        for (const auto& phi : block.phis) {
            validate_post_ra_phi(function, phi, frame_kinds);
        }
        for (const auto& inst : block.instructions) {
            visit_instruction_uses(inst, [&](const RegisterRef& reg, const char* role) {
                reject_nonphysical_operand_post_ra(function, reg, role);
            });
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, Copy> || std::is_same_v<T, Li> ||
                                  std::is_same_v<T, Binary> || std::is_same_v<T, Compare> ||
                                  std::is_same_v<T, FrameAddr> || std::is_same_v<T, Load>) {
                        reject_nonphysical_operand_post_ra(function,
                                                           value.dest,
                                                           "instruction destination");
                    }
                },
                inst);
        }
        if (block.terminator) {
            visit_terminator_uses(*block.terminator, [&](const RegisterRef& reg, const char* role) {
                reject_nonphysical_operand_post_ra(function, reg, role);
            });
        }
    }
}

void validate_post_phi_elim_operands(const MachineFunction& function) {
    for (const auto& block : function.blocks) {
        if (!block.phis.empty()) {
            fail(function,
                 "block bb" + std::to_string(block.id) +
                     " still contains phi nodes after phi elimination");
        }
        for (const auto& inst : block.instructions) {
            visit_instruction_uses(inst, [&](const RegisterRef& reg, const char* role) {
                reject_nonphysical_operand_post_ra(function, reg, role);
            });
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, Copy> || std::is_same_v<T, Li> ||
                                  std::is_same_v<T, Binary> || std::is_same_v<T, Compare> ||
                                  std::is_same_v<T, FrameAddr> || std::is_same_v<T, Load>) {
                        reject_nonphysical_operand_post_ra(function,
                                                           value.dest,
                                                           "instruction destination");
                    }
                },
                inst);
        }
        if (block.terminator) {
            visit_terminator_uses(*block.terminator, [&](const RegisterRef& reg, const char* role) {
                reject_nonphysical_operand_post_ra(function, reg, role);
            });
        }
    }
}

void validate_materialized_frame(const MachineFunction& function) {
    if (!function.frame_size) {
        fail(function, "frame size is missing after frame materialization");
    }
    if (*function.frame_size % 16 != 0) {
        fail(function, "materialized frame size must stay 16-byte aligned");
    }

    std::unordered_set<PhysicalRegister> saved_regs;
    for (const auto& object : function.frame_objects) {
        if (!object.materialized_offset) {
            fail(function,
                 "frame object " + describe_frame(object.id) +
                     " is missing a materialized offset");
        }
        if (*object.materialized_offset % static_cast<std::int32_t>(object.align) != 0) {
            fail(function,
                 "frame object " + describe_frame(object.id) +
                     " has a misaligned materialized offset");
        }
        if (object.kind == FrameObjectKind::IncomingArg) {
            if (*object.materialized_offset < static_cast<std::int32_t>(*function.frame_size)) {
                fail(function,
                     "incoming arg frame " + describe_frame(object.id) +
                         " must live above the closed frame");
            }
        } else if (*object.materialized_offset >=
                   static_cast<std::int32_t>(*function.frame_size)) {
            fail(function,
                 "frame object " + describe_frame(object.id) +
                     " must live inside the closed frame");
        }

        if (object.kind == FrameObjectKind::CalleeSave) {
            if (!object.callee_save_reg) {
                fail(function,
                     "callee-save frame object " + describe_frame(object.id) +
                         " is missing its saved register");
            }
            if (!is_callee_saved_register(*object.callee_save_reg)) {
                fail(function,
                     "callee-save frame object " + describe_frame(object.id) +
                         " uses a non-callee-saved register");
            }
            if (!saved_regs.insert(*object.callee_save_reg).second) {
                fail(function,
                     "duplicate callee-save frame object for register " +
                         std::string(physical_register_name(*object.callee_save_reg)));
            }
        } else if (object.callee_save_reg) {
            fail(function,
                 "non-callee-save frame object " + describe_frame(object.id) +
                     " unexpectedly records a saved register");
        }
    }
}

std::optional<VirtualRegister> defined_vreg(const Instruction& inst) {
    return std::visit(
        [](const auto& value) -> std::optional<VirtualRegister> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Copy>) {
                if (const auto* dest = std::get_if<VirtualRegister>(&value.dest)) {
                    return *dest;
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, Li> || std::is_same_v<T, Binary> ||
                                 std::is_same_v<T, Compare> ||
                                 std::is_same_v<T, FrameAddr> ||
                                 std::is_same_v<T, Load>) {
                return std::get_if<VirtualRegister>(&value.dest)
                           ? std::optional<VirtualRegister>(*std::get_if<VirtualRegister>(
                                 &value.dest))
                           : std::nullopt;
            } else {
                return std::nullopt;
            }
        },
        inst);
}

template <class Fn>
void visit_instruction_uses(const Instruction& inst, Fn&& fn) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Copy>) {
                fn(value.src, "copy source");
            } else if constexpr (std::is_same_v<T, Binary>) {
                fn(value.lhs, "binary lhs");
                fn(value.rhs, "binary rhs");
            } else if constexpr (std::is_same_v<T, Compare>) {
                fn(value.lhs, "compare lhs");
                fn(value.rhs, "compare rhs");
            } else if constexpr (std::is_same_v<T, Load>) {
                std::visit(
                    [&](const auto& addr) {
                        using AddressT = std::decay_t<decltype(addr)>;
                        if constexpr (std::is_same_v<AddressT, RegisterAddress>) {
                            fn(addr.base, "address base");
                        }
                    },
                    value.address);
            } else if constexpr (std::is_same_v<T, Store>) {
                fn(value.src, "store source");
                std::visit(
                    [&](const auto& addr) {
                        using AddressT = std::decay_t<decltype(addr)>;
                        if constexpr (std::is_same_v<AddressT, RegisterAddress>) {
                            fn(addr.base, "address base");
                        }
                    },
                    value.address);
            }
        },
        inst);
}

template <class Fn>
void visit_terminator_uses(const Terminator& terminator, Fn&& fn) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, BranchNonZero>) {
                fn(value.condition, "branch condition");
            } else if constexpr (std::is_same_v<T, Return>) {
                if (value.value) {
                    fn(*value.value, "return value");
                }
            }
        },
        terminator);
}

std::vector<std::vector<BlockId>> predecessor_lists(
    const MachineFunction& function,
    const std::unordered_map<BlockId, std::size_t>& indices) {
    std::vector<std::vector<BlockId>> predecessors(function.blocks.size());
    for (const auto& block : function.blocks) {
        std::visit(
            [&](const auto& term) {
                using T = std::decay_t<decltype(term)>;
                if constexpr (std::is_same_v<T, Jump>) {
                    predecessors[indices.at(term.target)].push_back(block.id);
                } else if constexpr (std::is_same_v<T, BranchNonZero>) {
                    predecessors[indices.at(term.then_block)].push_back(block.id);
                    predecessors[indices.at(term.else_block)].push_back(block.id);
                }
            },
            *block.terminator);
    }
    return predecessors;
}

std::vector<std::unordered_set<MachineValueId>> compute_available_in(
    const MachineFunction& function,
    const std::unordered_map<BlockId, std::size_t>& indices) {
    std::vector<std::unordered_set<MachineValueId>> available_in(function.blocks.size());
    std::vector<std::unordered_set<MachineValueId>> available_out(function.blocks.size());
    const auto predecessors = predecessor_lists(function, indices);

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < function.blocks.size(); ++i) {
            std::unordered_set<MachineValueId> next_in;
            if (function.blocks[i].id == function.entry_block || predecessors[i].empty()) {
                next_in.clear();
            } else {
                next_in = available_out[indices.at(predecessors[i].front())];
                for (std::size_t pred_index = 1; pred_index < predecessors[i].size();
                     ++pred_index) {
                    const auto& pred_out =
                        available_out[indices.at(predecessors[i][pred_index])];
                    for (auto it = next_in.begin(); it != next_in.end();) {
                        if (!pred_out.contains(*it)) {
                            it = next_in.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
            }

            auto next_out = next_in;
            // Phi destinations are defined at block entry (before any instruction).
            for (const auto& phi : function.blocks[i].phis) {
                if (const auto* vr = std::get_if<VirtualRegister>(&phi.dest)) {
                    next_out.insert(vr->id);
                }
            }
            for (const auto& inst : function.blocks[i].instructions) {
                if (const auto def = defined_vreg(inst)) {
                    next_out.insert(def->id);
                }
            }

            if (next_in != available_in[i] || next_out != available_out[i]) {
                available_in[i] = std::move(next_in);
                available_out[i] = std::move(next_out);
                changed = true;
            }
        }
    }

    return available_in;
}

void validate_ssa(const MachineFunction& function,
                  const std::unordered_map<BlockId, std::size_t>& indices) {
    const auto available_in = compute_available_in(function, indices);

    // Recompute available_out for phi-incoming validation.
    // available_out[i] = available_in[i] + phi defs + instruction defs.
    std::vector<std::unordered_set<MachineValueId>> available_out(function.blocks.size());
    for (std::size_t i = 0; i < function.blocks.size(); ++i) {
        available_out[i] = available_in[i];
        for (const auto& phi : function.blocks[i].phis) {
            if (const auto* vr = std::get_if<VirtualRegister>(&phi.dest)) {
                available_out[i].insert(vr->id);
            }
        }
        for (const auto& inst : function.blocks[i].instructions) {
            if (const auto def = defined_vreg(inst)) {
                available_out[i].insert(def->id);
            }
        }
    }

    auto check_use = [&](const RegisterRef& reg,
                         const char* role,
                         const std::unordered_set<MachineValueId>& available) {
        if (const auto* vreg = std::get_if<VirtualRegister>(&reg)) {
            validate_vreg(function, *vreg, role);
            if (!available.contains(vreg->id)) {
                fail(function,
                     std::string(role) + " uses undefined " + vreg_name(*vreg) +
                         " on at least one incoming path");
            }
        }
    };

    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        const auto& block = function.blocks[block_index];

        // Validate phi incoming values: each must be available at the end of its predecessor.
        for (const auto& phi : block.phis) {
            if (const auto* vr = std::get_if<VirtualRegister>(&phi.dest)) {
                validate_vreg(function, *vr, "phi destination");
            }
            for (const auto& incoming : phi.incoming) {
                if (!indices.contains(incoming.pred)) {
                    fail(function,
                         "phi in bb" + std::to_string(block.id) +
                             " references undefined predecessor bb" +
                             std::to_string(incoming.pred));
                }
                const auto pred_idx = indices.at(incoming.pred);
                if (const auto* vr = std::get_if<VirtualRegister>(&incoming.value)) {
                    if (!available_out[pred_idx].contains(vr->id)) {
                        fail(function,
                             "phi incoming " + vreg_name(*vr) +
                                 " from bb" + std::to_string(incoming.pred) +
                                 " is not defined on that path");
                    }
                    validate_vreg(function, *vr, "phi incoming value");
                }
            }
        }

        auto available = available_in[block_index];
        // Phi defs are available from block entry.
        for (const auto& phi : block.phis) {
            if (const auto* vr = std::get_if<VirtualRegister>(&phi.dest)) {
                available.insert(vr->id);
            }
        }
        for (std::size_t inst_index = 0; inst_index < block.instructions.size(); ++inst_index) {
            visit_instruction_uses(
                block.instructions[inst_index],
                [&](const RegisterRef& reg, const char* role) {
                    check_use(reg, role, available);
                });
            if (const auto def = defined_vreg(block.instructions[inst_index])) {
                available.insert(def->id);
            }
        }
        visit_terminator_uses(
            *block.terminator,
            [&](const RegisterRef& reg, const char* role) {
                check_use(reg, role, available);
            });
    }
}

void validate_address(const MachineFunction& function,
                      const Address& address,
                      const std::unordered_set<FrameId>& frame_ids,
                      ValidationStage stage) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, FrameAddress>) {
                if (!frame_ids.contains(value.frame)) {
                    fail(function, "address references undefined frame object " +
                                       describe_frame(value.frame));
                }
            } else {
                validate_reg_use(function,
                                 value.base,
                                 "address base",
                                 stage,
                                 stage != ValidationStage::PreRegAlloc);
            }
        },
        address);
}

void validate_instruction(const MachineFunction& function,
                          const Instruction& inst,
                          const std::unordered_set<FrameId>& frame_ids,
                          ValidationStage stage) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Copy>) {
                if (const auto* dest = std::get_if<PhysicalRegister>(&value.dest)) {
                    validate_stage_physical_register(function,
                                                     *dest,
                                                     "copy destination",
                                                     stage);
                }
                if (const auto* src = std::get_if<PhysicalRegister>(&value.src)) {
                    validate_stage_physical_register(function,
                                                     *src,
                                                     "copy source",
                                                     stage);
                }
                if (stage == ValidationStage::PreRegAlloc &&
                    std::holds_alternative<PhysicalRegister>(value.dest) &&
                    std::holds_alternative<PhysicalRegister>(value.src)) {
                    fail(function, "copy may not move between two physical registers");
                }
                validate_reg_use(function, value.dest, "copy destination", stage, true);
                validate_reg_use(function, value.src, "copy source", stage, true);
            } else if constexpr (std::is_same_v<T, Li>) {
                validate_reg_use(function,
                                 value.dest,
                                 "li destination",
                                 stage,
                                 stage != ValidationStage::PreRegAlloc);
                if (stage == ValidationStage::PreRegAlloc) {
                    if (const auto* vr = std::get_if<VirtualRegister>(&value.dest)) {
                        validate_vreg(function, *vr, "li destination");
                    } else {
                        fail(function, "li destination uses physical register pre-RA");
                    }
                }
            } else if constexpr (std::is_same_v<T, Binary>) {
                validate_reg_use(function,
                                 value.dest,
                                 "binary destination",
                                 stage,
                                 stage != ValidationStage::PreRegAlloc);
                if (stage == ValidationStage::PreRegAlloc) {
                    if (const auto* vr = std::get_if<VirtualRegister>(&value.dest)) {
                        validate_vreg(function, *vr, "binary destination");
                    } else {
                        fail(function, "binary destination uses physical register pre-RA");
                    }
                }
                validate_reg_use(function,
                                 value.lhs,
                                 "binary lhs",
                                 stage,
                                 stage != ValidationStage::PreRegAlloc);
                validate_reg_use(function,
                                 value.rhs,
                                 "binary rhs",
                                 stage,
                                 stage != ValidationStage::PreRegAlloc);
            } else if constexpr (std::is_same_v<T, Compare>) {
                validate_reg_use(function,
                                 value.dest,
                                 "compare destination",
                                 stage,
                                 stage != ValidationStage::PreRegAlloc);
                if (stage == ValidationStage::PreRegAlloc) {
                    if (const auto* vr = std::get_if<VirtualRegister>(&value.dest)) {
                        validate_vreg(function, *vr, "compare destination");
                    } else {
                        fail(function, "compare destination uses physical register pre-RA");
                    }
                }
                validate_reg_use(function,
                                 value.lhs,
                                 "compare lhs",
                                 stage,
                                 stage != ValidationStage::PreRegAlloc);
                validate_reg_use(function,
                                 value.rhs,
                                 "compare rhs",
                                 stage,
                                 stage != ValidationStage::PreRegAlloc);
            } else if constexpr (std::is_same_v<T, FrameAddr>) {
                validate_reg_use(function,
                                 value.dest,
                                 "frame_addr destination",
                                 stage,
                                 stage != ValidationStage::PreRegAlloc);
                if (stage == ValidationStage::PreRegAlloc) {
                    if (const auto* vr = std::get_if<VirtualRegister>(&value.dest)) {
                        validate_vreg(function, *vr, "frame_addr destination");
                    } else {
                        fail(function, "frame_addr destination uses physical register pre-RA");
                    }
                }
                if (!frame_ids.contains(value.frame)) {
                    fail(function,
                         "frame_addr references undefined frame object " +
                             describe_frame(value.frame));
                }
            } else if constexpr (std::is_same_v<T, Load>) {
                validate_reg_use(function,
                                 value.dest,
                                 "load destination",
                                 stage,
                                 stage != ValidationStage::PreRegAlloc);
                if (stage == ValidationStage::PreRegAlloc) {
                    if (const auto* vr = std::get_if<VirtualRegister>(&value.dest)) {
                        validate_vreg(function, *vr, "load destination");
                    } else {
                        fail(function, "load destination uses physical register pre-RA");
                    }
                }
                validate_address(function, value.address, frame_ids, stage);
            } else if constexpr (std::is_same_v<T, Store>) {
                validate_address(function, value.address, frame_ids, stage);
                validate_reg_use(function,
                                 value.src,
                                 "store source",
                                 stage,
                                 stage != ValidationStage::PreRegAlloc);
            } else if constexpr (std::is_same_v<T, Call>) {
                if (value.callee.empty()) {
                    fail(function, "call instruction has an empty callee");
                }
            }
        },
        inst);
}

void validate_terminator(const MachineFunction& function,
                         const Terminator& terminator,
                         const std::unordered_set<BlockId>& block_ids,
                         ValidationStage stage) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Jump>) {
                if (!block_ids.contains(value.target)) {
                    fail(function, "jump target bb" + std::to_string(value.target) +
                                       " is undefined");
                }
            } else if constexpr (std::is_same_v<T, BranchNonZero>) {
                validate_reg_use(function,
                                 value.condition,
                                 "branch condition",
                                 stage,
                                 stage != ValidationStage::PreRegAlloc);
                if (!block_ids.contains(value.then_block)) {
                    fail(function, "branch then target bb" +
                                       std::to_string(value.then_block) +
                                       " is undefined");
                }
                if (!block_ids.contains(value.else_block)) {
                    fail(function, "branch else target bb" +
                                       std::to_string(value.else_block) +
                                       " is undefined");
                }
            } else if constexpr (std::is_same_v<T, Return>) {
                if (value.value) {
                    validate_reg_use(function, *value.value, "return value", stage, true);
                    if (const auto* reg = std::get_if<PhysicalRegister>(&*value.value)) {
                        if (stage == ValidationStage::PreRegAlloc &&
                            *reg != PhysicalRegister::A0) {
                            fail(function,
                                 "return value uses physical register " +
                                     std::string(physical_register_name(*reg)) +
                                     " instead of a0");
                        }
                    }
                }
            }
        },
        terminator);
}

void validate_function(const MachineFunction& function, ValidationStage stage) {
    if (function.symbol.empty()) {
        throw ValidationError("Machine IR validation failed: function symbol is empty");
    }

    std::unordered_set<FrameId> frame_ids;
    std::unordered_map<FrameId, FrameObjectKind> frame_kinds;
    std::size_t outgoing_arg_areas = 0;
    for (const auto& object : function.frame_objects) {
        if (!frame_ids.insert(object.id).second) {
            fail(function, "duplicate frame object id " + describe_frame(object.id));
        }
        frame_kinds.emplace(object.id, object.kind);
        if (object.align == 0) {
            fail(function, "frame object " + describe_frame(object.id) +
                               " has zero alignment");
        }
        if (object.kind == FrameObjectKind::OutgoingArg) {
            ++outgoing_arg_areas;
            if (stage == ValidationStage::PreRegAlloc) {
                if (object.align < 16) {
                    fail(function,
                         "outgoing arg area " + describe_frame(object.id) +
                             " must have at least 16-byte alignment");
                }
                if (object.size % 16 != 0) {
                    fail(function,
                         "outgoing arg area " + describe_frame(object.id) +
                             " size must be 16-byte aligned");
                }
            }
        }
    }
    if (outgoing_arg_areas > 1) {
        fail(function, "multiple outgoing argument areas declared");
    }

    std::unordered_set<BlockId> block_ids;
    for (const auto& block : function.blocks) {
        if (!block_ids.insert(block.id).second) {
            fail(function, "duplicate block id bb" + std::to_string(block.id));
        }
    }

    if (!block_ids.contains(function.entry_block)) {
        fail(function, "entry block bb" + std::to_string(function.entry_block) +
                           " is undefined");
    }

    const auto indices = block_indices(function);
    for (const auto& block : function.blocks) {
        if (!block.terminator) {
            fail(function, "block bb" + std::to_string(block.id) +
                               " is missing a terminator");
        }
        for (const auto& inst : block.instructions) {
            validate_instruction(function, inst, frame_ids, stage);
        }
        validate_terminator(function, *block.terminator, block_ids, stage);
    }
    if (stage == ValidationStage::PreRegAlloc) {
        validate_ssa(function, indices);
    } else if (stage == ValidationStage::PostRegAlloc) {
        validate_post_ra_operands(function, frame_kinds);
    } else if (stage == ValidationStage::PostPhiElim) {
        validate_post_phi_elim_operands(function);
    } else {
        validate_post_phi_elim_operands(function);
        validate_materialized_frame(function);
    }
}

} // namespace

void validate_module(const MachineModule& module, ValidationStage stage) {
    for (const auto& function : module.functions) {
        validate_function(function, stage);
    }
}

} // namespace riscv
