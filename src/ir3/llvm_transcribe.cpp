#include "ir3/llvm_transcribe.hpp"

#include "semantic/hir/hir.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>
#include <unordered_set>

namespace ir3 {
namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

struct Layout {
    std::uint64_t size = 0;
    std::uint64_t align = 1;
};

struct CallSignature {
    std::optional<SsaClass> result;
    std::vector<SsaClass> args;
};

bool operator==(const CallSignature& lhs, const CallSignature& rhs) {
    return lhs.result == rhs.result && lhs.args == rhs.args;
}

std::uint64_t align_to(std::uint64_t value, std::uint64_t align) {
    if (align <= 1) {
        return value;
    }
    return ((value + align - 1) / align) * align;
}

bool is_bool_type(semantic::TypeId type) {
    auto* primitive = type ? std::get_if<semantic::PrimitiveKind>(&type->value) : nullptr;
    return primitive && *primitive == semantic::PrimitiveKind::BOOL;
}

bool is_unsigned_type(semantic::TypeId type) {
    auto* primitive = type ? std::get_if<semantic::PrimitiveKind>(&type->value) : nullptr;
    if (!primitive) {
        return false;
    }
    return *primitive == semantic::PrimitiveKind::U32 ||
           *primitive == semantic::PrimitiveKind::USIZE ||
           *primitive == semantic::PrimitiveKind::CHAR ||
           *primitive == semantic::PrimitiveKind::__ANYUINT__;
}

std::string llvm_string_literal(const std::string& text) {
    std::string out = "\"";
    for (unsigned char ch : text) {
        if (std::isalnum(ch) || ch == '_' || ch == '$' || ch == '.' || ch == '-') {
            out += static_cast<char>(ch);
            continue;
        }
        const char digits[] = "0123456789ABCDEF";
        out += '\\';
        out += digits[(ch >> 4) & 0xf];
        out += digits[ch & 0xf];
    }
    out += '"';
    return out;
}

std::string global_name(const std::string& symbol) {
    return "@" + llvm_string_literal(symbol);
}

std::string value_name(ValueId id) {
    return "%v" + std::to_string(id);
}

std::string block_label(BlockId id) {
    return "bb" + std::to_string(id);
}

std::string block_ref(BlockId id) {
    return "%" + block_label(id);
}

const char* llvm_ssa_type(SsaClass klass) {
    switch (klass) {
    case SsaClass::I32:
        return "i32";
    case SsaClass::Ptr:
        return "ptr";
    }
    return "void";
}

void emit_signature_tail(std::ostream& out, const CallSignature& signature) {
    out << "(";
    for (std::size_t i = 0; i < signature.args.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << llvm_ssa_type(signature.args[i]);
    }
    out << ")";
}

std::string llvm_type(semantic::TypeId type);
Layout layout_of(semantic::TypeId type);

semantic::TypeId struct_field_type(const hir::StructDef& def, std::size_t index) {
    if (index >= def.fields.size()) {
        throw TranscriptionError("struct field index out of range during LLVM transcription");
    }
    if (!def.fields[index].type) {
        throw TranscriptionError("struct field type is unresolved during LLVM transcription");
    }
    return *def.fields[index].type;
}

std::string llvm_type(semantic::TypeId type) {
    if (!type) {
        throw TranscriptionError("invalid host type during LLVM transcription");
    }

    return std::visit(
        Overloaded{
            [](semantic::PrimitiveKind kind) -> std::string {
                switch (kind) {
                case semantic::PrimitiveKind::I32:
                case semantic::PrimitiveKind::U32:
                case semantic::PrimitiveKind::ISIZE:
                case semantic::PrimitiveKind::USIZE:
                case semantic::PrimitiveKind::BOOL:
                case semantic::PrimitiveKind::CHAR:
                case semantic::PrimitiveKind::__ANYINT__:
                case semantic::PrimitiveKind::__ANYUINT__:
                    return "i32";
                case semantic::PrimitiveKind::STRING:
                    return "{ ptr, i32 }";
                }
                return "i32";
            },
            [](const semantic::StructType& type) -> std::string {
                if (!type.symbol) {
                    throw TranscriptionError("anonymous struct type has no layout source");
                }
                std::string text = "{ ";
                for (std::size_t i = 0; i < type.symbol->fields.size(); ++i) {
                    if (i != 0) {
                        text += ", ";
                    }
                    text += llvm_type(struct_field_type(*type.symbol, i));
                }
                text += " }";
                return text;
            },
            [](const semantic::EnumType&) -> std::string {
                return "i32";
            },
            [](const semantic::ReferenceType&) -> std::string {
                return "ptr";
            },
            [](const semantic::ArrayType& type) -> std::string {
                return "[" + std::to_string(type.size) + " x " +
                       llvm_type(type.element_type) + "]";
            },
            [](const semantic::UnitType&) -> std::string {
                return "{}";
            },
            [](const semantic::NeverType&) -> std::string {
                return "{}";
            },
            [](const semantic::UnderscoreType&) -> std::string {
                throw TranscriptionError("underscore type reached LLVM transcription");
            },
        },
        type->value);
}

Layout layout_of(semantic::TypeId type) {
    if (!type) {
        throw TranscriptionError("invalid host type during layout computation");
    }

    return std::visit(
        Overloaded{
            [](semantic::PrimitiveKind kind) -> Layout {
                switch (kind) {
                case semantic::PrimitiveKind::STRING:
                    return Layout{.size = 16, .align = 8};
                case semantic::PrimitiveKind::I32:
                case semantic::PrimitiveKind::U32:
                case semantic::PrimitiveKind::ISIZE:
                case semantic::PrimitiveKind::USIZE:
                case semantic::PrimitiveKind::BOOL:
                case semantic::PrimitiveKind::CHAR:
                case semantic::PrimitiveKind::__ANYINT__:
                case semantic::PrimitiveKind::__ANYUINT__:
                    return Layout{.size = 4, .align = 4};
                }
                return Layout{.size = 4, .align = 4};
            },
            [](const semantic::StructType& type) -> Layout {
                if (!type.symbol) {
                    throw TranscriptionError("anonymous struct type has no layout source");
                }
                std::uint64_t offset = 0;
                std::uint64_t max_align = 1;
                for (std::size_t i = 0; i < type.symbol->fields.size(); ++i) {
                    auto field = layout_of(struct_field_type(*type.symbol, i));
                    offset = align_to(offset, field.align);
                    offset += field.size;
                    max_align = std::max(max_align, field.align);
                }
                return Layout{.size = align_to(offset, max_align), .align = max_align};
            },
            [](const semantic::EnumType&) -> Layout {
                return Layout{.size = 4, .align = 4};
            },
            [](const semantic::ReferenceType&) -> Layout {
                return Layout{.size = 8, .align = 8};
            },
            [](const semantic::ArrayType& type) -> Layout {
                auto element = layout_of(type.element_type);
                return Layout{
                    .size = align_to(element.size, element.align) * type.size,
                    .align = element.align,
                };
            },
            [](const semantic::UnitType&) -> Layout {
                return Layout{.size = 0, .align = 1};
            },
            [](const semantic::NeverType&) -> Layout {
                return Layout{.size = 0, .align = 1};
            },
            [](const semantic::UnderscoreType&) -> Layout {
                throw TranscriptionError("underscore type reached layout computation");
            },
        },
        type->value);
}

semantic::TypeId array_element_type(semantic::TypeId type) {
    auto* array = type ? std::get_if<semantic::ArrayType>(&type->value) : nullptr;
    if (!array) {
        throw TranscriptionError("index projection applied to a non-array place");
    }
    return array->element_type;
}

class FunctionEmitter {
public:
    FunctionEmitter(std::ostream& out,
                    const Function& function,
                    const std::unordered_set<std::string>& defined_functions,
                    std::vector<std::pair<std::string, CallSignature>>& external_declarations,
                    bool& needs_memcpy)
        : out_(out), function_(function), needs_memcpy_(needs_memcpy),
          defined_functions_(defined_functions),
          external_declarations_(external_declarations),
          value_classes_(function.next_value) {}

    void emit() {
        seed_value_classes();
        emit_signature();
        out_ << " {\n";
        out_ << "entry:\n";
        emit_slot_allocas();
        out_ << "  br label " << block_ref(function_.entry_block) << "\n";
        for (const auto& block : function_.blocks) {
            emit_block(block);
        }
        out_ << "}\n";
    }

private:
    std::ostream& out_;
    const Function& function_;
    bool& needs_memcpy_;
    const std::unordered_set<std::string>& defined_functions_;
    std::vector<std::pair<std::string, CallSignature>>& external_declarations_;
    std::vector<std::optional<SsaClass>> value_classes_;
    std::size_t temp_counter_ = 0;

    void seed_value_classes() {
        for (const auto& param : function_.params) {
            define_value(param.value);
        }
        for (const auto& block : function_.blocks) {
            for (const auto& phi : block.phis) {
                define_value(phi.result);
            }
            for (const auto& inst : block.instructions) {
                std::visit(
                    Overloaded{
                        [&](const IConst& value) { define_value(value.result); },
                        [&](const Load& value) { define_value(value.result); },
                        [&](const Borrow& value) { define_value(value.result); },
                        [&](const Unary& value) { define_value(value.result); },
                        [&](const Binary& value) { define_value(value.result); },
                        [&](const Cast& value) { define_value(value.result); },
                        [&](const Call& value) {
                            if (value.result) {
                                define_value(*value.result);
                            }
                        },
                        [&](const auto&) {},
                    },
                    inst);
            }
        }
    }

    void define_value(Value value) {
        if (value.id >= value_classes_.size()) {
            value_classes_.resize(value.id + 1);
        }
        value_classes_[value.id] = value.klass;
    }

    SsaClass value_class(ValueId value) const {
        if (value >= value_classes_.size() || !value_classes_[value]) {
            throw TranscriptionError("use of undefined IR3 value %" +
                                     std::to_string(value));
        }
        return *value_classes_[value];
    }

    std::string value_type(ValueId value) const {
        return llvm_ssa_type(value_class(value));
    }

    std::string temp(const char* prefix) {
        return "%" + std::string(prefix) + "." + std::to_string(temp_counter_++);
    }

    void emit_signature() {
        out_ << "define ";
        if (function_.return_class) {
            out_ << llvm_ssa_type(*function_.return_class);
        } else {
            out_ << "void";
        }
        out_ << " " << global_name(function_.symbol) << "(";
        for (std::size_t i = 0; i < function_.params.size(); ++i) {
            if (i != 0) {
                out_ << ", ";
            }
            const auto& param = function_.params[i];
            out_ << llvm_ssa_type(param.value.klass) << " " << value_name(param.value.id);
        }
        out_ << ")";
    }

    void emit_slot_allocas() {
        for (const auto& slot : function_.slots) {
            auto layout = layout_of(slot.host_type);
            out_ << "  " << slot_name(slot.id) << " = alloca "
                 << llvm_type(slot.host_type) << ", align " << layout.align << "\n";
        }
    }

    void emit_block(const BasicBlock& block) {
        out_ << block_label(block.id) << ":\n";
        for (const auto& phi : block.phis) {
            out_ << "  " << value_name(phi.result.id) << " = phi "
                 << llvm_ssa_type(phi.result.klass);
            for (std::size_t i = 0; i < phi.incoming.size(); ++i) {
                if (i != 0) {
                    out_ << ",";
                }
                out_ << " [ " << value_name(phi.incoming[i].value) << ", "
                     << block_ref(phi.incoming[i].pred) << " ]";
            }
            out_ << "\n";
        }
        for (const auto& inst : block.instructions) {
            emit_instruction(inst);
        }
        if (!block.terminator) {
            throw TranscriptionError("IR3 block is missing a terminator");
        }
        emit_terminator(*block.terminator);
    }

    std::string slot_name(SlotId slot) const {
        return "%slot" + std::to_string(slot);
    }

    struct Address {
        std::string ptr;
        semantic::TypeId pointee_type = semantic::invalid_type_id;
    };

    Address place_address(const Place& place,
                          std::optional<std::string> target_name = std::nullopt) {
        Address address = std::visit(
            Overloaded{
                [&](const SlotBase& base) -> Address {
                    if (base.slot >= function_.slots.size()) {
                        throw TranscriptionError("slot place references an unknown slot");
                    }
                    return Address{
                        .ptr = slot_name(base.slot),
                        .pointee_type = function_.slots[base.slot].host_type,
                    };
                },
                [&](const DerefBase& base) -> Address {
                    return Address{
                        .ptr = value_name(base.ptr),
                        .pointee_type = base.pointee_type,
                    };
                },
            },
            place.base);

        for (std::size_t i = 0; i < place.projections.size(); ++i) {
            const bool is_last = i + 1 == place.projections.size();
            std::string name = is_last && target_name ? *target_name : temp("gep");
            std::visit(
                Overloaded{
                    [&](const FieldProjection& projection) {
                        out_ << "  " << name << " = getelementptr inbounds "
                             << llvm_type(address.pointee_type) << ", ptr "
                             << address.ptr << ", i32 0, i32 " << projection.index
                             << "\n";
                        address = Address{
                            .ptr = name,
                            .pointee_type = projection.result_type,
                        };
                    },
                    [&](const IndexProjection& projection) {
                        auto element_type = array_element_type(address.pointee_type);
                        out_ << "  " << name << " = getelementptr inbounds "
                             << llvm_type(address.pointee_type) << ", ptr "
                             << address.ptr << ", i32 0, i32 "
                             << value_name(projection.index) << "\n";
                        address = Address{
                            .ptr = name,
                            .pointee_type = element_type,
                        };
                    },
                },
                place.projections[i]);
        }

        if (target_name && place.projections.empty()) {
            out_ << "  " << *target_name << " = getelementptr inbounds "
                 << llvm_type(address.pointee_type) << ", ptr " << address.ptr
                 << ", i32 0\n";
            address.ptr = *target_name;
        }
        return address;
    }

    void emit_instruction(const Instruction& inst) {
        std::visit(
            Overloaded{
                [&](const IConst& value) {
                    out_ << "  " << value_name(value.result.id) << " = add i32 0, "
                         << value.value << "\n";
                },
                [&](const Load& value) {
                    auto address = place_address(value.source);
                    out_ << "  " << value_name(value.result.id) << " = load "
                         << llvm_ssa_type(value.result.klass) << ", ptr "
                         << address.ptr << ", align "
                         << layout_of(value.source.host_type).align << "\n";
                },
                [&](const Store& value) {
                    auto address = place_address(value.dest);
                    out_ << "  store " << llvm_ssa_type(value.klass) << " "
                         << value_name(value.value) << ", ptr " << address.ptr
                         << ", align " << layout_of(value.dest.host_type).align
                         << "\n";
                },
                [&](const Copy& value) {
                    emit_copy(value);
                },
                [&](const Borrow& value) {
                    place_address(value.source, value_name(value.result.id));
                },
                [&](const Unary& value) {
                    emit_unary(value);
                },
                [&](const Binary& value) {
                    emit_binary(value);
                },
                [&](const Cast& value) {
                    emit_cast(value);
                },
                [&](const Call& value) {
                    emit_call(value);
                },
            },
            inst);
    }

    void emit_copy(const Copy& value) {
        auto size = layout_of(value.dest.host_type).size;
        if (size == 0) {
            return;
        }
        auto align = std::min(layout_of(value.dest.host_type).align,
                              layout_of(value.source.host_type).align);
        auto dest = place_address(value.dest);
        auto source = place_address(value.source);
        needs_memcpy_ = true;
        out_ << "  call void @llvm.memcpy.p0.p0.i64(ptr " << dest.ptr
             << ", ptr " << source.ptr << ", i64 " << size << ", i1 false)\n";
        (void)align;
    }

    void emit_unary(const Unary& value) {
        switch (value.op) {
        case UnaryOp::Neg:
            out_ << "  " << value_name(value.result.id) << " = sub i32 0, "
                 << value_name(value.operand) << "\n";
            return;
        case UnaryOp::Not:
            if (is_bool_type(value.host_type)) {
                auto cmp = temp("not");
                out_ << "  " << cmp << " = icmp eq i32 "
                     << value_name(value.operand) << ", 0\n";
                out_ << "  " << value_name(value.result.id)
                     << " = zext i1 " << cmp << " to i32\n";
            } else {
                out_ << "  " << value_name(value.result.id) << " = xor i32 "
                     << value_name(value.operand) << ", -1\n";
            }
            return;
        }
        throw TranscriptionError("unknown unary operation");
    }

    void emit_binary(const Binary& value) {
        auto lhs = value_name(value.lhs);
        auto rhs = value_name(value.rhs);
        switch (value.op) {
        case BinaryOp::Add:
            out_ << "  " << value_name(value.result.id) << " = add i32 " << lhs
                 << ", " << rhs << "\n";
            return;
        case BinaryOp::Sub:
            out_ << "  " << value_name(value.result.id) << " = sub i32 " << lhs
                 << ", " << rhs << "\n";
            return;
        case BinaryOp::Mul:
            out_ << "  " << value_name(value.result.id) << " = mul i32 " << lhs
                 << ", " << rhs << "\n";
            return;
        case BinaryOp::Div:
            out_ << "  " << value_name(value.result.id) << " = "
                 << (is_unsigned_type(value.operand_type) ? "udiv" : "sdiv")
                 << " i32 " << lhs << ", " << rhs << "\n";
            return;
        case BinaryOp::Rem:
            out_ << "  " << value_name(value.result.id) << " = "
                 << (is_unsigned_type(value.operand_type) ? "urem" : "srem")
                 << " i32 " << lhs << ", " << rhs << "\n";
            return;
        case BinaryOp::BitAnd:
            out_ << "  " << value_name(value.result.id) << " = and i32 " << lhs
                 << ", " << rhs << "\n";
            return;
        case BinaryOp::BitXor:
            out_ << "  " << value_name(value.result.id) << " = xor i32 " << lhs
                 << ", " << rhs << "\n";
            return;
        case BinaryOp::BitOr:
            out_ << "  " << value_name(value.result.id) << " = or i32 " << lhs
                 << ", " << rhs << "\n";
            return;
        case BinaryOp::Shl:
            out_ << "  " << value_name(value.result.id) << " = shl i32 " << lhs
                 << ", " << rhs << "\n";
            return;
        case BinaryOp::Shr:
            out_ << "  " << value_name(value.result.id) << " = "
                 << (is_unsigned_type(value.operand_type) ? "lshr" : "ashr")
                 << " i32 " << lhs << ", " << rhs << "\n";
            return;
        case BinaryOp::Eq:
        case BinaryOp::Ne:
        case BinaryOp::Lt:
        case BinaryOp::Gt:
        case BinaryOp::Le:
        case BinaryOp::Ge:
            emit_compare(value, lhs, rhs);
            return;
        }
        throw TranscriptionError("unknown binary operation");
    }

    void emit_compare(const Binary& value,
                      const std::string& lhs,
                      const std::string& rhs) {
        std::string pred;
        switch (value.op) {
        case BinaryOp::Eq:
            pred = "eq";
            break;
        case BinaryOp::Ne:
            pred = "ne";
            break;
        case BinaryOp::Lt:
            pred = is_unsigned_type(value.operand_type) ? "ult" : "slt";
            break;
        case BinaryOp::Gt:
            pred = is_unsigned_type(value.operand_type) ? "ugt" : "sgt";
            break;
        case BinaryOp::Le:
            pred = is_unsigned_type(value.operand_type) ? "ule" : "sle";
            break;
        case BinaryOp::Ge:
            pred = is_unsigned_type(value.operand_type) ? "uge" : "sge";
            break;
        default:
            throw TranscriptionError("non-comparison operation reached comparison emitter");
        }
        auto cmp = temp("cmp");
        out_ << "  " << cmp << " = icmp " << pred << " i32 " << lhs << ", "
             << rhs << "\n";
        out_ << "  " << value_name(value.result.id) << " = zext i1 " << cmp
             << " to i32\n";
    }

    void emit_cast(const Cast& value) {
        auto source_class = value_class(value.operand);
        auto dest = value.result.klass;
        if (source_class == SsaClass::I32 && dest == SsaClass::I32) {
            out_ << "  " << value_name(value.result.id) << " = add i32 0, "
                 << value_name(value.operand) << "\n";
            return;
        }
        if (source_class == SsaClass::Ptr && dest == SsaClass::Ptr) {
            out_ << "  " << value_name(value.result.id)
                 << " = getelementptr i8, ptr " << value_name(value.operand)
                 << ", i64 0\n";
            return;
        }
        if (source_class == SsaClass::I32 && dest == SsaClass::Ptr) {
            out_ << "  " << value_name(value.result.id) << " = inttoptr i32 "
                 << value_name(value.operand) << " to ptr\n";
            return;
        }
        if (source_class == SsaClass::Ptr && dest == SsaClass::I32) {
            out_ << "  " << value_name(value.result.id) << " = ptrtoint ptr "
                 << value_name(value.operand) << " to i32\n";
            return;
        }
        throw TranscriptionError("unsupported IR3 cast during LLVM transcription");
    }

    void emit_call(const Call& value) {
        record_external_call(value);
        out_ << "  ";
        if (value.result) {
            out_ << value_name(value.result->id) << " = ";
        }
        out_ << "call ";
        if (value.result) {
            out_ << llvm_ssa_type(value.result->klass);
        } else {
            out_ << "void";
        }
        out_ << " " << global_name(value.callee) << "(";
        for (std::size_t i = 0; i < value.args.size(); ++i) {
            if (i != 0) {
                out_ << ", ";
            }
            out_ << value_type(value.args[i]) << " " << value_name(value.args[i]);
        }
        out_ << ")\n";
    }

    void record_external_call(const Call& value) {
        if (defined_functions_.contains(value.callee)) {
            return;
        }

        CallSignature signature{
            .result = value.result ? std::optional<SsaClass>(value.result->klass)
                                   : std::nullopt,
            .args = {},
        };
        signature.args.reserve(value.args.size());
        for (auto arg : value.args) {
            signature.args.push_back(value_class(arg));
        }

        auto it = std::find_if(
            external_declarations_.begin(),
            external_declarations_.end(),
            [&](const auto& declaration) { return declaration.first == value.callee; });
        if (it == external_declarations_.end()) {
            external_declarations_.emplace_back(value.callee, std::move(signature));
            return;
        }
        if (!(it->second == signature)) {
            throw TranscriptionError("external function called with inconsistent signature: " +
                                     value.callee);
        }
    }

    void emit_terminator(const Terminator& terminator) {
        std::visit(
            Overloaded{
                [&](const Jump& value) {
                    out_ << "  br label " << block_ref(value.target) << "\n";
                },
                [&](const Branch& value) {
                    auto cond = temp("brcond");
                    out_ << "  " << cond << " = icmp ne i32 "
                         << value_name(value.condition) << ", 0\n";
                    out_ << "  br i1 " << cond << ", label "
                         << block_ref(value.then_block) << ", label "
                         << block_ref(value.else_block) << "\n";
                },
                [&](const Return& value) {
                    if (value.value) {
                        out_ << "  ret " << value_type(*value.value) << " "
                             << value_name(*value.value) << "\n";
                    } else {
                        out_ << "  ret void\n";
                    }
                },
                [&](const Unreachable&) {
                    out_ << "  unreachable\n";
                },
            },
            terminator);
    }
};

} // namespace

void transcribe_llvm(std::ostream& out, const Module& module) {
    bool needs_memcpy = false;
    std::unordered_set<std::string> defined_functions;
    for (const auto& function : module.functions) {
        defined_functions.insert(function.symbol);
    }
    std::vector<std::pair<std::string, CallSignature>> external_declarations;

    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        if (i != 0) {
            out << "\n";
        }
        FunctionEmitter emitter(out,
                                module.functions[i],
                                defined_functions,
                                external_declarations,
                                needs_memcpy);
        emitter.emit();
    }
    for (const auto& [symbol, signature] : external_declarations) {
        if (!module.functions.empty()) {
            out << "\n";
        }
        out << "declare ";
        if (signature.result) {
            out << llvm_ssa_type(*signature.result);
        } else {
            out << "void";
        }
        out << " " << global_name(symbol);
        emit_signature_tail(out, signature);
        out << "\n";
    }
    if (needs_memcpy) {
        if (!module.functions.empty() || !external_declarations.empty()) {
            out << "\n";
        }
        out << "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n";
    }
}

std::string to_llvm_string(const Module& module) {
    std::ostringstream out;
    transcribe_llvm(out, module);
    return out.str();
}

} // namespace ir3
