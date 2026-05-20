#include "riscv/pretty_print.hpp"

#include "semantic/hir/hir.hpp"

#include <ostream>
#include <sstream>
#include <type_traits>
#include <unordered_map>

namespace riscv {
namespace {

std::unordered_map<BlockId, const MachineBlock*> block_map(const MachineFunction& function) {
    std::unordered_map<BlockId, const MachineBlock*> blocks;
    blocks.reserve(function.blocks.size());
    for (const auto& block : function.blocks) {
        blocks.emplace(block.id, &block);
    }
    return blocks;
}

std::string block_name(const MachineFunction& function, BlockId block) {
    auto blocks = block_map(function);
    if (const auto it = blocks.find(block); it != blocks.end() && !it->second->name.empty()) {
        return it->second->name;
    }
    return "bb" + std::to_string(block);
}

std::string vreg_name(const VirtualRegister& reg) {
    return "v" + std::to_string(reg.id);
}

std::string frame_name(FrameId frame) {
    return "fi" + std::to_string(frame);
}

std::string register_name(const RegisterRef& reg) {
    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, VirtualRegister>) {
                return vreg_name(value);
            } else if constexpr (std::is_same_v<T, SpillRef>) {
                return "spill(" + frame_name(value.frame) + ")";
            } else {
                return physical_register_name(value);
            }
        },
        reg);
}

std::string type_name(semantic::TypeId type) {
    if (!type) {
        return "<invalid>";
    }

    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, semantic::PrimitiveKind>) {
                switch (value) {
                case semantic::PrimitiveKind::I32:
                    return "i32";
                case semantic::PrimitiveKind::U32:
                    return "u32";
                case semantic::PrimitiveKind::ISIZE:
                    return "isize";
                case semantic::PrimitiveKind::USIZE:
                    return "usize";
                case semantic::PrimitiveKind::BOOL:
                    return "bool";
                case semantic::PrimitiveKind::CHAR:
                    return "char";
                case semantic::PrimitiveKind::STRING:
                    return "string";
                case semantic::PrimitiveKind::__ANYINT__:
                    return "__anyint__";
                case semantic::PrimitiveKind::__ANYUINT__:
                    return "__anyuint__";
                }
            } else if constexpr (std::is_same_v<T, semantic::StructType>) {
                return value.symbol ? value.symbol->name.name : "<struct>";
            } else if constexpr (std::is_same_v<T, semantic::EnumType>) {
                return value.symbol ? value.symbol->name.name : "<enum>";
            } else if constexpr (std::is_same_v<T, semantic::ReferenceType>) {
                return std::string(value.is_mutable ? "&mut " : "&") +
                       type_name(value.referenced_type);
            } else if constexpr (std::is_same_v<T, semantic::ArrayType>) {
                return "[" + type_name(value.element_type) + "; " +
                       std::to_string(value.size) + "]";
            } else if constexpr (std::is_same_v<T, semantic::UnitType>) {
                return "()";
            } else if constexpr (std::is_same_v<T, semantic::NeverType>) {
                return "!";
            } else if constexpr (std::is_same_v<T, semantic::UnderscoreType>) {
                return "_";
            }
            return "<type>";
        },
        type->value);
}

const char* frame_kind_name(FrameObjectKind kind) {
    switch (kind) {
    case FrameObjectKind::LocalSlot:
        return "slot";
    case FrameObjectKind::IncomingArg:
        return "incoming_arg";
    case FrameObjectKind::OutgoingArg:
        return "outgoing_arg";
    case FrameObjectKind::Spill:
        return "spill";
    case FrameObjectKind::CalleeSave:
        return "callee_save";
    case FrameObjectKind::CallerSave:
        return "caller_save";
    }
    return "<frame-kind>";
}

const char* binary_name(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add:
        return "add";
    case BinaryOp::Sub:
        return "sub";
    case BinaryOp::Mul:
        return "mul";
    case BinaryOp::Div:
        return "div";
    case BinaryOp::DivU:
        return "divu";
    case BinaryOp::Rem:
        return "rem";
    case BinaryOp::RemU:
        return "remu";
    case BinaryOp::And:
        return "and";
    case BinaryOp::Or:
        return "or";
    case BinaryOp::Xor:
        return "xor";
    case BinaryOp::Sll:
        return "sll";
    case BinaryOp::Srl:
        return "srl";
    case BinaryOp::Sra:
        return "sra";
    case BinaryOp::Slt:
        return "slt";
    case BinaryOp::SltU:
        return "sltu";
    }
    return "<binary>";
}

const char* compare_name(CompareOp op) {
    switch (op) {
    case CompareOp::Eq:
        return "cmp.eq";
    case CompareOp::Ne:
        return "cmp.ne";
    case CompareOp::LtS:
        return "cmp.lt.s";
    case CompareOp::LtU:
        return "cmp.lt.u";
    case CompareOp::LeS:
        return "cmp.le.s";
    case CompareOp::LeU:
        return "cmp.le.u";
    case CompareOp::GtS:
        return "cmp.gt.s";
    case CompareOp::GtU:
        return "cmp.gt.u";
    case CompareOp::GeS:
        return "cmp.ge.s";
    case CompareOp::GeU:
        return "cmp.ge.u";
    }
    return "<compare>";
}

const char* frame_base_name(FrameBase base) {
    switch (base) {
    case FrameBase::None:
        return "none";
    case FrameBase::S0:
        return "s0";
    }
    return "<frame-base>";
}

std::string address_name(const Address& address) {
    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, FrameAddress>) {
                return "[" + frame_name(value.frame) +
                       (value.offset >= 0 ? " + " : " - ") +
                       std::to_string(value.offset >= 0 ? value.offset : -value.offset) + "]";
            } else {
                return "[" + register_name(value.base) +
                       (value.offset >= 0 ? " + " : " - ") +
                       std::to_string(value.offset >= 0 ? value.offset : -value.offset) + "]";
            }
        },
        address);
}

void print_instruction(std::ostream& out, const Instruction& inst) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Copy>) {
                if (std::holds_alternative<VirtualRegister>(value.dest)) {
                    out << "  " << register_name(value.dest) << " = copy "
                        << register_name(value.src) << "\n";
                } else {
                    out << "  copy " << register_name(value.dest) << ", "
                        << register_name(value.src) << "\n";
                }
            } else if constexpr (std::is_same_v<T, Li>) {
                out << "  " << register_name(value.dest) << " = li " << value.value << "\n";
            } else if constexpr (std::is_same_v<T, Binary>) {
                out << "  " << register_name(value.dest) << " = " << binary_name(value.op)
                    << " " << register_name(value.lhs) << ", "
                    << register_name(value.rhs) << "\n";
            } else if constexpr (std::is_same_v<T, Compare>) {
                out << "  " << register_name(value.dest) << " = " << compare_name(value.op)
                    << " " << register_name(value.lhs) << ", "
                    << register_name(value.rhs) << "\n";
            } else if constexpr (std::is_same_v<T, FrameAddr>) {
                out << "  " << register_name(value.dest) << " = frame_addr "
                    << frame_name(value.frame);
                if (value.offset != 0) {
                    out << (value.offset > 0 ? " + " : " - ")
                        << (value.offset > 0 ? value.offset : -value.offset);
                }
                out << "\n";
            } else if constexpr (std::is_same_v<T, Load>) {
                out << "  " << register_name(value.dest) << " = load "
                    << address_name(value.address) << "\n";
            } else if constexpr (std::is_same_v<T, Store>) {
                out << "  store " << address_name(value.address) << ", "
                    << register_name(value.src) << "\n";
            } else if constexpr (std::is_same_v<T, Call>) {
                out << "  call @" << value.callee << "\n";
            }
        },
        inst);
}

void print_terminator(std::ostream& out,
                      const MachineFunction& function,
                      const Terminator& terminator) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Jump>) {
                out << "  j " << block_name(function, value.target) << "\n";
            } else if constexpr (std::is_same_v<T, BranchNonZero>) {
                out << "  brnz " << register_name(value.condition) << ", "
                    << block_name(function, value.then_block) << ", "
                    << block_name(function, value.else_block) << "\n";
            } else if constexpr (std::is_same_v<T, Return>) {
                out << "  ret";
                if (value.value) {
                    out << " " << register_name(*value.value);
                }
                out << "\n";
            } else if constexpr (std::is_same_v<T, Unreachable>) {
                out << "  unreachable\n";
            }
        },
        terminator);
}

void print_function(std::ostream& out, const MachineFunction& function) {
    out << "mfn @" << function.symbol << "\n";
    out << "frame:";
    if (function.frame_size) {
        out << " size " << *function.frame_size;
        if (function.frame_base) {
            out << " base " << frame_base_name(*function.frame_base);
        }
    }
    out << "\n";
    for (const auto& object : function.frame_objects) {
        out << "  " << frame_name(object.id) << ": " << frame_kind_name(object.kind);
        if (object.host_type) {
            out << " " << type_name(object.host_type);
        } else if (object.spill_class) {
            out << " " << register_class_name(*object.spill_class);
        }
        if (object.saved_reg) {
            out << " " << physical_register_name(*object.saved_reg);
        }
        out << " size " << object.size << " align " << object.align;
        if (object.materialized_offset) {
            out << " offset " << *object.materialized_offset;
        }
        if (object.debug_name.empty()) {
            out << "\n";
        } else {
            out << " " << object.debug_name << "\n";
        }
    }
    if (!function.frame_objects.empty()) {
        out << "\n";
    }

    for (const auto& block : function.blocks) {
        out << block_name(function, block.id) << ":\n";
        for (const auto& phi : block.phis) {
            out << "  " << register_name(phi.dest) << " = phi [";
            for (std::size_t i = 0; i < phi.incoming.size(); ++i) {
                if (i != 0) {
                    out << ", ";
                }
                out << block_name(function, phi.incoming[i].pred) << ": "
                    << register_name(phi.incoming[i].value);
            }
            out << "]\n";
        }
        for (const auto& inst : block.instructions) {
            print_instruction(out, inst);
        }
        if (block.terminator) {
            print_terminator(out, function, *block.terminator);
        }
        out << "\n";
    }
}

} // namespace

void print_module(std::ostream& out, const MachineModule& module) {
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        if (i != 0) {
            out << "\n";
        }
        print_function(out, module.functions[i]);
    }
}

std::string to_string(const MachineModule& module) {
    std::ostringstream out;
    print_module(out, module);
    return out.str();
}

} // namespace riscv
