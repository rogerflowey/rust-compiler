#include "ir3/pretty_print.hpp"

#include "semantic/hir/hir.hpp"

#include <sstream>
#include <ostream>
#include <type_traits>

namespace ir3 {
namespace {

std::string ssa_name(ValueId value) {
    return "%" + std::to_string(value);
}

std::string block_name(const Function& function, BlockId block) {
    if (block < function.blocks.size() && !function.blocks[block].name.empty()) {
        return function.blocks[block].name;
    }
    return "bb" + std::to_string(block);
}

const char* class_name(SsaClass klass) {
    switch (klass) {
    case SsaClass::I32:
        return "i32";
    case SsaClass::Ptr:
        return "ptr";
    }
    return "<class>";
}

const char* slot_origin_name(SlotOrigin origin) {
    switch (origin) {
    case SlotOrigin::User:
        return "user";
    case SlotOrigin::Temp:
        return "temp";
    }
    return "slot";
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

std::string place_name(const Place& place) {
    std::string text;
    std::visit(
        [&](const auto& base) {
            using T = std::decay_t<decltype(base)>;
            if constexpr (std::is_same_v<T, SlotBase>) {
                text = "slot(%" + std::to_string(base.slot) + ")";
            } else {
                text = "deref(" + ssa_name(base.ptr) + ")";
            }
        },
        place.base);

    for (const auto& projection : place.projections) {
        std::visit(
            [&](const auto& proj) {
                using T = std::decay_t<decltype(proj)>;
                if constexpr (std::is_same_v<T, FieldProjection>) {
                    text += ".field(" + std::to_string(proj.index) + ")";
                } else {
                    text += "[" + ssa_name(proj.index) + "]";
                }
            },
            projection);
    }
    return text;
}

const char* unary_name(UnaryOp op) {
    switch (op) {
    case UnaryOp::Neg:
        return "neg";
    case UnaryOp::Not:
        return "not";
    }
    return "<unary>";
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
    case BinaryOp::Rem:
        return "rem";
    case BinaryOp::BitAnd:
        return "bit_and";
    case BinaryOp::BitXor:
        return "bit_xor";
    case BinaryOp::BitOr:
        return "bit_or";
    case BinaryOp::Shl:
        return "shl";
    case BinaryOp::Shr:
        return "shr";
    case BinaryOp::Eq:
        return "eq";
    case BinaryOp::Ne:
        return "ne";
    case BinaryOp::Lt:
        return "lt";
    case BinaryOp::Gt:
        return "gt";
    case BinaryOp::Le:
        return "le";
    case BinaryOp::Ge:
        return "ge";
    }
    return "<binary>";
}

void print_instruction(std::ostream& out, const Instruction& inst) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, IConst>) {
                out << "  " << ssa_name(value.result.id) << " = iconst "
                    << value.value << "\n";
            } else if constexpr (std::is_same_v<T, Load>) {
                out << "  " << ssa_name(value.result.id) << " = load."
                    << class_name(value.result.klass) << " "
                    << place_name(value.source) << "\n";
            } else if constexpr (std::is_same_v<T, Store>) {
                out << "  store." << class_name(value.klass) << " "
                    << place_name(value.dest) << ", " << ssa_name(value.value)
                    << "\n";
            } else if constexpr (std::is_same_v<T, Copy>) {
                out << "  copy " << place_name(value.dest) << ", "
                    << place_name(value.source) << "\n";
            } else if constexpr (std::is_same_v<T, Borrow>) {
                out << "  " << ssa_name(value.result.id) << " = borrow "
                    << (value.is_mutable ? "mut " : "imm ")
                    << place_name(value.source) << "\n";
            } else if constexpr (std::is_same_v<T, Unary>) {
                out << "  " << ssa_name(value.result.id) << " = "
                    << unary_name(value.op) << " " << ssa_name(value.operand)
                    << "\n";
            } else if constexpr (std::is_same_v<T, Binary>) {
                out << "  " << ssa_name(value.result.id) << " = "
                    << binary_name(value.op) << " " << ssa_name(value.lhs)
                    << ", " << ssa_name(value.rhs) << "\n";
            } else if constexpr (std::is_same_v<T, Cast>) {
                out << "  " << ssa_name(value.result.id) << " = cast."
                    << class_name(value.result.klass) << " "
                    << ssa_name(value.operand) << "\n";
            } else if constexpr (std::is_same_v<T, Call>) {
                if (value.result) {
                    out << "  " << ssa_name(value.result->id) << " = ";
                } else {
                    out << "  ";
                }
                out << "call @" << value.callee << "(";
                for (std::size_t i = 0; i < value.args.size(); ++i) {
                    if (i != 0) {
                        out << ", ";
                    }
                    out << ssa_name(value.args[i]);
                }
                out << ")\n";
            }
        },
        inst);
}

void print_terminator(std::ostream& out,
                      const Function& function,
                      const Terminator& terminator) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Jump>) {
                out << "  jump " << block_name(function, value.target) << "\n";
            } else if constexpr (std::is_same_v<T, Branch>) {
                out << "  branch " << ssa_name(value.condition) << ", "
                    << block_name(function, value.then_block) << ", "
                    << block_name(function, value.else_block) << "\n";
            } else if constexpr (std::is_same_v<T, Return>) {
                out << "  return";
                if (value.value) {
                    out << " " << ssa_name(*value.value);
                }
                out << "\n";
            } else if constexpr (std::is_same_v<T, Unreachable>) {
                out << "  unreachable\n";
            }
        },
        terminator);
}

void print_function(std::ostream& out, const Function& function) {
    out << "fn @" << function.symbol << "(";
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        const auto& param = function.params[i];
        out << ssa_name(param.value.id) << ": " << class_name(param.value.klass);
        if (!param.name.empty()) {
            out << " /* " << param.name << " */";
        }
    }
    out << ")";
    if (function.return_class) {
        out << " -> " << class_name(*function.return_class);
    }
    out << " {\n";

    for (const auto& slot : function.slots) {
        out << "  slot %" << slot.id << " : " << type_name(slot.host_type) << " "
            << (slot.is_mutable ? "mut " : "imm ")
            << slot_origin_name(slot.origin);
        if (!slot.debug_name.empty()) {
            out << " /* " << slot.debug_name << " */";
        }
        out << "\n";
    }
    if (!function.slots.empty() && !function.blocks.empty()) {
        out << "\n";
    }

    for (const auto& block : function.blocks) {
        out << block_name(function, block.id) << ":\n";
        for (const auto& phi : block.phis) {
            out << "  " << ssa_name(phi.result.id) << " = phi "
                << class_name(phi.result.klass) << " [";
            for (std::size_t i = 0; i < phi.incoming.size(); ++i) {
                if (i != 0) {
                    out << ", ";
                }
                out << block_name(function, phi.incoming[i].pred) << ": "
                    << ssa_name(phi.incoming[i].value);
            }
            out << "]\n";
        }
        for (const auto& inst : block.instructions) {
            print_instruction(out, inst);
        }
        if (block.terminator) {
            print_terminator(out, function, *block.terminator);
        } else {
            out << "  <missing terminator>\n";
        }
        out << "\n";
    }
    out << "}\n";
}

} // namespace

void print_module(std::ostream& out, const Module& module) {
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        if (i != 0) {
            out << "\n";
        }
        print_function(out, module.functions[i]);
    }
}

std::string to_string(const Module& module) {
    std::ostringstream out;
    print_module(out, module);
    return out.str();
}

} // namespace ir3
