#include "ir3/lower.hpp"

#include "riscv/layout.hpp"
#include "semantic/symbol/predefined.hpp"
#include "semantic/type/helper.hpp"

#include <cstdint>
#include <cctype>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace ir3 {
namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

constexpr std::uint32_t kBulkMemsetMinSize = 64;

semantic::TypeId resolved_type(const hir::TypeAnnotation& annotation) {
    if (auto* type = std::get_if<semantic::TypeId>(&annotation)) {
        return *type;
    }
    throw LoweringError("IR3 lowering requires resolved type annotations");
}

semantic::TypeId expr_type(const hir::Expr& expr) {
    if (!expr.expr_info || !expr.expr_info->type) {
        throw LoweringError("IR3 lowering requires ExprInfo on every expression");
    }
    return expr.expr_info->type;
}

semantic::TypeId local_type(const hir::Local& local) {
    if (!local.type_annotation) {
        throw LoweringError("IR3 lowering found a local without a resolved type");
    }
    return resolved_type(*local.type_annotation);
}

bool is_never(semantic::TypeId type) {
    return classify_host_type(type) == HostClass::Never;
}

std::optional<semantic::PrimitiveKind> primitive_kind(semantic::TypeId type) {
    if (!type) {
        return std::nullopt;
    }
    if (auto* primitive = std::get_if<semantic::PrimitiveKind>(&type->value)) {
        return *primitive;
    }
    return std::nullopt;
}

bool is_bool_type(semantic::TypeId type) {
    return primitive_kind(type) == semantic::PrimitiveKind::BOOL;
}

bool is_unsigned_integer_type(semantic::TypeId type) {
    auto primitive = primitive_kind(type);
    if (!primitive) {
        return false;
    }
    switch (*primitive) {
    case semantic::PrimitiveKind::U32:
    case semantic::PrimitiveKind::USIZE:
    case semantic::PrimitiveKind::CHAR:
    case semantic::PrimitiveKind::__ANYUINT__:
        return true;
    case semantic::PrimitiveKind::I32:
    case semantic::PrimitiveKind::ISIZE:
    case semantic::PrimitiveKind::BOOL:
    case semantic::PrimitiveKind::STRING:
    case semantic::PrimitiveKind::__ANYINT__:
        return false;
    }
    return false;
}

std::optional<std::uint8_t> byte_repeat_literal(const hir::Expr& expr,
                                                semantic::TypeId type) {
    const auto* literal = std::get_if<hir::Literal>(&expr.value);
    if (!literal) {
        return std::nullopt;
    }

    const auto primitive = primitive_kind(type);
    if (!primitive) {
        return std::nullopt;
    }

    return std::visit(
        Overloaded{
            [&](const hir::Literal::Integer& value) -> std::optional<std::uint8_t> {
                switch (*primitive) {
                case semantic::PrimitiveKind::I32:
                case semantic::PrimitiveKind::U32:
                case semantic::PrimitiveKind::ISIZE:
                case semantic::PrimitiveKind::USIZE:
                case semantic::PrimitiveKind::CHAR:
                case semantic::PrimitiveKind::__ANYINT__:
                case semantic::PrimitiveKind::__ANYUINT__: {
                    std::int64_t signed_value = static_cast<std::int64_t>(value.value);
                    if (value.is_negative) {
                        signed_value = -signed_value;
                    }
                    if (signed_value == 0) {
                        return std::uint8_t{0x00};
                    }
                    if (signed_value == -1) {
                        return std::uint8_t{0xff};
                    }
                    return std::nullopt;
                }
                case semantic::PrimitiveKind::BOOL:
                    return std::nullopt;
                case semantic::PrimitiveKind::STRING:
                    return std::nullopt;
                }
                return std::nullopt;
            },
            [&](bool value) -> std::optional<std::uint8_t> {
                if (*primitive != semantic::PrimitiveKind::BOOL) {
                    return std::nullopt;
                }
                return value ? std::uint8_t{0x01} : std::uint8_t{0x00};
            },
            [&](char value) -> std::optional<std::uint8_t> {
                if (*primitive != semantic::PrimitiveKind::CHAR) {
                    return std::nullopt;
                }
                return static_cast<std::uint8_t>(value);
            },
            [&](const hir::Literal::String&) -> std::optional<std::uint8_t> {
                return std::nullopt;
            },
        },
        literal->value);
}


const char* host_class_name(HostClass klass) {
    switch (klass) {
    case HostClass::I32:
        return "i32";
    case HostClass::Ptr:
        return "ptr";
    case HostClass::Unit:
        return "unit";
    case HostClass::Aggregate:
        return "aggregate";
    case HostClass::Never:
        return "never";
    }
    return "<host-class>";
}

const char* expr_kind_name(const hir::Expr& expr) {
    return std::visit(
        Overloaded{
            [](const hir::Literal&) { return "Literal"; },
            [](const hir::UnresolvedIdentifier&) { return "UnresolvedIdentifier"; },
            [](const hir::TypeStatic&) { return "TypeStatic"; },
            [](const hir::Underscore&) { return "Underscore"; },
            [](const hir::FieldAccess&) { return "FieldAccess"; },
            [](const hir::StructLiteral&) { return "StructLiteral"; },
            [](const hir::ArrayLiteral&) { return "ArrayLiteral"; },
            [](const hir::ArrayRepeat&) { return "ArrayRepeat"; },
            [](const hir::Index&) { return "Index"; },
            [](const hir::Assignment&) { return "Assignment"; },
            [](const hir::UnaryOp&) { return "UnaryOp"; },
            [](const hir::BinaryOp&) { return "BinaryOp"; },
            [](const hir::Cast&) { return "Cast"; },
            [](const hir::Call&) { return "Call"; },
            [](const hir::MethodCall&) { return "MethodCall"; },
            [](const hir::Block&) { return "Block"; },
            [](const hir::If&) { return "If"; },
            [](const hir::Loop&) { return "Loop"; },
            [](const hir::While&) { return "While"; },
            [](const hir::Break&) { return "Break"; },
            [](const hir::Continue&) { return "Continue"; },
            [](const hir::Return&) { return "Return"; },
            [](const hir::Variable&) { return "Variable"; },
            [](const hir::ConstUse&) { return "ConstUse"; },
            [](const hir::FuncUse&) { return "FuncUse"; },
            [](const hir::StructConst&) { return "StructConst"; },
            [](const hir::EnumVariant&) { return "EnumVariant"; },
        },
        expr.value);
}

std::size_t expr_span_start(const hir::Expr& expr) {
    return std::visit([](const auto& node) { return node.span.start; }, expr.value);
}

std::string function_symbol(const hir::Function& function) {
    return function.name.name;
}

std::string method_symbol(const hir::Method& method) {
    return method.name.name;
}

std::string sanitize_symbol_part(std::string text) {
    for (char& ch : text) {
        auto byte = static_cast<unsigned char>(ch);
        if (!std::isalnum(byte) && ch != '_') {
            ch = '$';
        }
    }
    return text;
}

std::string type_symbol(semantic::TypeId type) {
    if (!type) {
        return "invalid";
    }

    return std::visit(
        Overloaded{
            [](semantic::PrimitiveKind kind) -> std::string {
                switch (kind) {
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
                    return "str";
                case semantic::PrimitiveKind::__ANYINT__:
                    return "anyint";
                case semantic::PrimitiveKind::__ANYUINT__:
                    return "anyuint";
                }
                return "primitive";
            },
            [](const semantic::StructType& type) -> std::string {
                return "struct$" + sanitize_symbol_part(type.symbol ? type.symbol->name.name
                                                                    : "anonymous");
            },
            [](const semantic::EnumType& type) -> std::string {
                return "enum$" + sanitize_symbol_part(type.symbol ? type.symbol->name.name
                                                                  : "anonymous");
            },
            [](const semantic::ReferenceType& type) -> std::string {
                return std::string(type.is_mutable ? "refmut$" : "ref$") +
                       type_symbol(type.referenced_type);
            },
            [](const semantic::ArrayType& type) -> std::string {
                return "array$" + std::to_string(type.size) + "$" +
                       type_symbol(type.element_type);
            },
            [](const semantic::UnitType&) -> std::string {
                return "unit";
            },
            [](const semantic::NeverType&) -> std::string {
                return "never";
            },
            [](const semantic::UnderscoreType&) -> std::string {
                return "underscore";
            },
        },
        type->value);
}

class LoweringSymbols {
public:
    std::string define_function(const hir::Function& function,
                                std::optional<semantic::TypeId> impl_type = std::nullopt) {
        if (auto runtime_symbol = semantic::predefined_runtime_symbol(function)) {
            return define(function_symbols_, &function, std::string(*runtime_symbol));
        }
        auto base = impl_type ? type_symbol(*impl_type) + "$" + function_symbol(function)
                              : function_symbol(function);
        return define(function_symbols_, &function, std::move(base));
    }

    std::string define_method(const hir::Method& method) {
        if (auto runtime_symbol = semantic::predefined_runtime_symbol(method)) {
            return define(method_symbols_, &method, std::string(*runtime_symbol));
        }
        std::string base;
        if (method.self_local) {
            base = type_symbol(local_type(*method.self_local)) + "$" + method_symbol(method);
        } else {
            base = "method$" + method_symbol(method);
        }
        return define(method_symbols_, &method, std::move(base));
    }

    std::string symbol_for(const hir::Function& function) {
        if (auto it = function_symbols_.find(&function); it != function_symbols_.end()) {
            return it->second;
        }
        return define_function(function);
    }

    std::string symbol_for(const hir::Method& method) {
        if (auto it = method_symbols_.find(&method); it != method_symbols_.end()) {
            return it->second;
        }
        return define_method(method);
    }

private:
    std::unordered_map<const hir::Function*, std::string> function_symbols_;
    std::unordered_map<const hir::Method*, std::string> method_symbols_;
    std::unordered_map<std::string, std::size_t> used_bases_;

    template <typename T>
    std::string define(std::unordered_map<const T*, std::string>& symbols,
                       const T* key,
                       std::string base) {
        if (auto it = symbols.find(key); it != symbols.end()) {
            return it->second;
        }

        base = sanitize_symbol_part(std::move(base));
        auto& count = used_bases_[base];
        std::string symbol = count == 0 ? base : base + "$" + std::to_string(count);
        ++count;
        symbols.emplace(key, symbol);
        return symbol;
    }
};

BinaryOp lower_binary_op(hir::BinaryOp::Op op, semantic::TypeId operand_type) {
    const bool is_unsigned = is_unsigned_integer_type(operand_type);
    switch (op) {
    case hir::BinaryOp::ADD:
        return is_unsigned ? BinaryOp::UAdd : BinaryOp::SAdd;
    case hir::BinaryOp::SUB:
        return is_unsigned ? BinaryOp::USub : BinaryOp::SSub;
    case hir::BinaryOp::MUL:
        return is_unsigned ? BinaryOp::UMul : BinaryOp::SMul;
    case hir::BinaryOp::DIV:
        return is_unsigned ? BinaryOp::UDiv : BinaryOp::SDiv;
    case hir::BinaryOp::REM:
        return is_unsigned ? BinaryOp::URem : BinaryOp::SRem;
    case hir::BinaryOp::BIT_AND:
        return BinaryOp::BitAnd;
    case hir::BinaryOp::BIT_XOR:
        return BinaryOp::BitXor;
    case hir::BinaryOp::BIT_OR:
        return BinaryOp::BitOr;
    case hir::BinaryOp::SHL:
        return is_unsigned ? BinaryOp::UShl : BinaryOp::SShl;
    case hir::BinaryOp::SHR:
        return is_unsigned ? BinaryOp::LShr : BinaryOp::AShr;
    case hir::BinaryOp::EQ:
        return BinaryOp::Eq;
    case hir::BinaryOp::NE:
        return BinaryOp::Ne;
    case hir::BinaryOp::LT:
        return is_unsigned ? BinaryOp::ULt : BinaryOp::SLt;
    case hir::BinaryOp::GT:
        return is_unsigned ? BinaryOp::UGt : BinaryOp::SGt;
    case hir::BinaryOp::LE:
        return is_unsigned ? BinaryOp::ULe : BinaryOp::SLe;
    case hir::BinaryOp::GE:
        return is_unsigned ? BinaryOp::UGe : BinaryOp::SGe;
    case hir::BinaryOp::AND:
    case hir::BinaryOp::OR:
        break;
    }
    throw LoweringError("short-circuit operator must be lowered through CFG");
}

CastOp lower_cast_op(SsaClass source, SsaClass dest) {
    if (source == SsaClass::I32 && dest == SsaClass::I32) {
        return CastOp::I32ToI32;
    }
    if (source == SsaClass::Ptr && dest == SsaClass::Ptr) {
        return CastOp::PtrToPtr;
    }
    if (source == SsaClass::I32 && dest == SsaClass::Ptr) {
        return CastOp::I32ToPtr;
    }
    if (source == SsaClass::Ptr && dest == SsaClass::I32) {
        return CastOp::PtrToI32;
    }
    throw LoweringError("unsupported IR3 cast class pair");
}

class FunctionLowerer {
public:
    FunctionLowerer(Module& module,
                    LoweringSymbols& symbols,
                    std::string symbol,
                    riscv::TargetConfig target)
        : module_(module),
          symbols_(symbols),
          target_(target) {
        function_.symbol = std::move(symbol);
        current_ = add_block("bb0");
        function_.entry_block = current_;
    }

    Function finish() {
        return std::move(function_);
    }

    void lower_function(const hir::Function& source) {
        source_function_ = &source;
        function_.source_return_type = source.return_type
                                           ? resolved_type(*source.return_type)
                                           : semantic::get_typeID(
                                                 semantic::Type{semantic::UnitType{}});
        configure_return();
        declare_locals(source.locals);
        lower_params(source.params, source.param_type_annotations);
        lower_body_as_return(*source.body);
    }

    void lower_method(const hir::Method& source) {
        source_method_ = &source;
        function_.source_return_type = source.return_type
                                           ? resolved_type(*source.return_type)
                                           : semantic::get_typeID(
                                                 semantic::Type{semantic::UnitType{}});
        configure_return();
        if (source.self_local) {
            declare_local(*source.self_local);
        }
        declare_locals(source.locals);

        if (source.self_local) {
            auto self_type = local_type(*source.self_local);
            auto param = add_param("self", abi_param_class(self_type), self_type);
            bind_param_local(*source.self_local, param.id, self_type);
        }
        lower_params(source.params, source.param_type_annotations);
        lower_body_as_return(*source.body);
    }

private:
    struct Source {
        semantic::TypeId type = semantic::invalid_type_id;
        std::optional<ValueId> value;
        std::optional<Place> place;
    };

    struct LoopContext {
        BlockId continue_target;
        BlockId break_target;
    };

    Module& module_;
    LoweringSymbols& symbols_;
    riscv::TargetConfig target_;
    Function function_;
    BlockId current_ = 0;
    const hir::Function* source_function_ = nullptr;
    const hir::Method* source_method_ = nullptr;
    std::optional<Place> aggregate_return_dest_;
    std::unordered_map<const hir::Local*, SlotId> local_slots_;
    std::unordered_map<const hir::Loop*, LoopContext> loop_contexts_;
    std::unordered_map<const hir::While*, LoopContext> while_contexts_;

    BasicBlock& current_block() {
        return function_.blocks.at(current_);
    }

    bool current_is_open() const {
        return !function_.blocks.at(current_).terminator.has_value();
    }

    BlockId add_block(std::string name) {
        BlockId id = function_.blocks.size();
        if (!name.empty()) {
            name += "." + std::to_string(id);
        }
        function_.blocks.push_back(BasicBlock{
            .id = id,
            .name = std::move(name),
            .phis = {},
            .instructions = {},
            .terminator = std::nullopt,
        });
        return id;
    }

    void switch_to(BlockId block) {
        current_ = block;
    }

    void emit(Instruction inst) {
        if (!current_is_open()) {
            throw LoweringError("attempted to emit into a terminated IR3 block");
        }
        current_block().instructions.push_back(std::move(inst));
    }

    void terminate(Terminator terminator) {
        if (!current_is_open()) {
            throw LoweringError("attempted to terminate an already terminated IR3 block");
        }
        current_block().terminator = std::move(terminator);
    }

    Value new_value(SsaClass klass) {
        return Value{.id = function_.next_value++, .klass = klass};
    }

    Value add_param(std::string name, SsaClass klass, semantic::TypeId host_type) {
        Value value = new_value(klass);
        function_.params.push_back(Param{
            .value = value,
            .name = std::move(name),
            .host_type = host_type,
        });
        return value;
    }

    SsaClass abi_param_class(semantic::TypeId type) {
        if (auto klass = ssa_class_for(type)) {
            return *klass;
        }
        return SsaClass::Ptr;
    }

    void configure_return() {
        auto return_type = function_.source_return_type;
        if (auto klass = ssa_class_for(return_type)) {
            function_.return_class = *klass;
            return;
        }
        function_.return_class = std::nullopt;
        if (classify_host_type(return_type) == HostClass::Aggregate) {
            auto out = add_param("return_addr", SsaClass::Ptr, return_type);
            aggregate_return_dest_ = Place{
                .base = DerefBase{
                    .ptr = out.id,
                    .pointee_type = return_type,
                    .is_mutable = true,
                },
                .projections = {},
                .host_type = return_type,
                .is_mutable = true,
            };
        }
    }

    void declare_locals(const std::vector<std::unique_ptr<hir::Local>>& locals) {
        for (const auto& local : locals) {
            declare_local(*local);
        }
    }

    SlotId declare_local(const hir::Local& local) {
        if (local_slots_.contains(&local)) {
            return local_slots_.at(&local);
        }
        SlotId id = function_.slots.size();
        function_.slots.push_back(Slot{
            .id = id,
            .host_type = local_type(local),
            .is_mutable = true,
            .debug_name = local.name.name,
            .origin = SlotOrigin::User,
        });
        local_slots_.emplace(&local, id);
        return id;
    }

    SlotId temp_slot(semantic::TypeId type) {
        SlotId id = function_.slots.size();
        function_.slots.push_back(Slot{
            .id = id,
            .host_type = type,
            .is_mutable = true,
            .debug_name = "tmp" + std::to_string(id),
            .origin = SlotOrigin::Temp,
        });
        return id;
    }

    Place slot_place(SlotId slot) const {
        const auto& slot_def = function_.slots.at(slot);
        return Place{
            .base = SlotBase{.slot = slot},
            .projections = {},
            .host_type = slot_def.host_type,
            .is_mutable = slot_def.is_mutable,
        };
    }

    Place local_place(const hir::Local& local) {
        auto it = local_slots_.find(&local);
        if (it == local_slots_.end()) {
            throw LoweringError("IR3 lowering found an undeclared local");
        }
        return slot_place(it->second);
    }

    Source value_source(ValueId value, semantic::TypeId type) {
        return Source{.type = type, .value = value, .place = std::nullopt};
    }

    Source place_source(Place place) {
        auto type = place.host_type;
        return Source{.type = type, .value = std::nullopt, .place = std::move(place)};
    }

    void lower_params(const std::vector<std::unique_ptr<hir::Pattern>>& params,
                      const std::vector<std::optional<hir::TypeAnnotation>>& types) {
        if (params.size() != types.size()) {
            throw LoweringError("parameter pattern/type count mismatch");
        }
        for (std::size_t i = 0; i < params.size(); ++i) {
            if (!types[i]) {
                throw LoweringError("parameter without resolved type");
            }
            auto type = resolved_type(*types[i]);
            auto param = add_param("arg" + std::to_string(i), abi_param_class(type), type);
            if (ssa_class_for(type)) {
                bind_pattern(*params[i], value_source(param.id, type));
            } else {
                Place source_place{
                    .base = DerefBase{
                        .ptr = param.id,
                        .pointee_type = type,
                        .is_mutable = false,
                    },
                    .projections = {},
                    .host_type = type,
                    .is_mutable = false,
                };
                bind_pattern(*params[i], place_source(std::move(source_place)));
            }
        }
    }

    void bind_param_local(const hir::Local& local, ValueId param, semantic::TypeId type) {
        if (ssa_class_for(type)) {
            bind_pattern_to_place_or_value(local, value_source(param, type));
            return;
        }

        Place source_place{
            .base = DerefBase{
                .ptr = param,
                .pointee_type = type,
                .is_mutable = false,
            },
            .projections = {},
            .host_type = type,
            .is_mutable = false,
        };
        bind_pattern_to_place_or_value(local, place_source(std::move(source_place)));
    }

    void bind_pattern(const hir::Pattern& pattern, Source source) {
        std::visit(
            Overloaded{
                [&](const hir::BindingDef& binding) {
                    auto* local = std::get_if<hir::Local*>(&binding.local);
                    if (!local || !*local) {
                        throw LoweringError("unresolved binding reached IR3 lowering");
                    }
                    bind_pattern_to_place_or_value(**local, std::move(source));
                },
                [&](const hir::ReferencePattern& pattern) {
                    ValueId ptr = source.value ? *source.value : load_scalar(*source.place).id;
                    auto* ref = std::get_if<semantic::ReferenceType>(&source.type->value);
                    if (!ref) {
                        throw LoweringError("reference pattern source is not a reference type");
                    }
                    Place deref{
                        .base = DerefBase{
                            .ptr = ptr,
                            .pointee_type = ref->referenced_type,
                            .is_mutable = ref->is_mutable,
                        },
                        .projections = {},
                        .host_type = ref->referenced_type,
                        .is_mutable = ref->is_mutable,
                    };
                    bind_pattern(*pattern.subpattern, place_source(std::move(deref)));
                },
            },
            pattern.value);
    }

    void bind_pattern_to_place_or_value(const hir::Local& local, Source source) {
        Place dest = local_place(local);
        if (source.value) {
            store_value(dest, *source.value);
            return;
        }
        if (!source.place) {
            throw LoweringError("empty pattern binding source");
        }
        copy_or_load_store(dest, *source.place);
    }

    void lower_body_as_return(const hir::Block& body) {
        auto return_type = function_.source_return_type;
        if (classify_host_type(return_type) == HostClass::Aggregate) {
            lower_block_materialize(body, *aggregate_return_dest_);
            if (current_is_open()) {
                terminate(Return{.value = std::nullopt});
            }
            return;
        }

        if (auto klass = ssa_class_for(return_type)) {
            auto value = lower_block_value(body, return_type);
            if (current_is_open()) {
            if (!value) {
                throw LoweringError("value-returning function body did not produce a value");
            }
            terminate(Return{.value = *value});
            }
            (void)klass;
            return;
        }

        lower_block_effect(body);
        if (current_is_open()) {
            terminate(Return{.value = std::nullopt});
        }
    }

    void lower_block_effect(const hir::Block& block) {
        lower_nested_items(block);
        for (const auto& stmt : block.stmts) {
            if (!current_is_open()) {
                return;
            }
            lower_stmt(*stmt);
        }
        if (current_is_open() && block.final_expr) {
            lower_expr_effect(**block.final_expr);
        }
    }

    std::optional<ValueId> lower_block_value(const hir::Block& block,
                                             semantic::TypeId expected_type) {
        lower_nested_items(block);
        for (const auto& stmt : block.stmts) {
            if (!current_is_open()) {
                return std::nullopt;
            }
            lower_stmt(*stmt);
        }
        if (!current_is_open() || is_never(expected_type)) {
            return std::nullopt;
        }
        if (!block.final_expr) {
            if (classify_host_type(expected_type) == HostClass::Unit) {
                return std::nullopt;
            }
            throw LoweringError("non-unit block has no final expression");
        }
        return lower_expr_value_or_diverge(**block.final_expr, expected_type);
    }

    void lower_block_materialize(const hir::Block& block, const Place& dest) {
        lower_nested_items(block);
        for (const auto& stmt : block.stmts) {
            if (!current_is_open()) {
                return;
            }
            lower_stmt(*stmt);
        }
        if (current_is_open() && block.final_expr) {
            materialize(**block.final_expr, dest);
        }
    }

    void lower_nested_items(const hir::Block& block) {
        for (const auto& item : block.items) {
            lower_item(*item);
        }
    }

    void lower_item(const hir::Item& item) {
        std::visit(
            Overloaded{
                [&](const hir::Function& function) {
                    FunctionLowerer lowerer(module_, symbols_, symbols_.symbol_for(function),
                                           target_);
                    lowerer.lower_function(function);
                    module_.functions.push_back(lowerer.finish());
                },
                [&](const hir::Impl& impl) {
                    auto impl_type = resolved_type(impl.for_type);
                    for (const auto& associated : impl.items) {
                        std::visit(
                            Overloaded{
                                [&](const hir::Function& function) {
                                    symbols_.define_function(function, impl_type);
                                    FunctionLowerer lowerer(module_, symbols_,
                                                           symbols_.symbol_for(function),
                                                           target_);
                                    lowerer.lower_function(function);
                                    module_.functions.push_back(lowerer.finish());
                                },
                                [&](const hir::Method& method) {
                                    symbols_.define_method(method);
                                    FunctionLowerer lowerer(module_, symbols_,
                                                           symbols_.symbol_for(method),
                                                           target_);
                                    lowerer.lower_method(method);
                                    module_.functions.push_back(lowerer.finish());
                                },
                                [&](const hir::ConstDef&) {},
                            },
                            associated->value);
                    }
                },
                [&](const auto&) {},
            },
            item.value);
    }

    void lower_stmt(const hir::Stmt& stmt) {
        std::visit(
            Overloaded{
                [&](const hir::LetStmt& let) {
                    bind_pattern_to_expr(*let.pattern, *let.initializer);
                },
                [&](const hir::ExprStmt& expr) {
                    lower_expr_effect(*expr.expr);
                },
            },
            stmt.value);
    }

    void bind_pattern_to_expr(const hir::Pattern& pattern, const hir::Expr& expr) {
        if (auto* binding = std::get_if<hir::BindingDef>(&pattern.value)) {
            auto* local = std::get_if<hir::Local*>(&binding->local);
            if (!local || !*local) {
                throw LoweringError("unresolved let binding reached IR3 lowering");
            }
            materialize(expr, local_place(**local));
            return;
        }

        auto type = expr_type(expr);
        if (!ssa_class_for(type)) {
            throw LoweringError("reference pattern initializer must lower to a pointer value");
        }
        bind_pattern(pattern, value_source(lower_value(expr).id, type));
    }

    void lower_expr_effect(const hir::Expr& expr) {
        auto type = expr_type(expr);
        if (!current_is_open()) {
            return;
        }

        std::visit(
            Overloaded{
                [&](const hir::Assignment& assignment) {
                    Place dest = lower_place(*assignment.lhs);
                    materialize(*assignment.rhs, dest);
                },
                [&](const hir::Block& block) {
                    lower_block_effect(block);
                },
                [&](const hir::If& if_expr) {
                    lower_if_effect(if_expr);
                },
                [&](const hir::Loop& loop) {
                    lower_loop(loop);
                },
                [&](const hir::While& loop) {
                    lower_while(loop);
                },
                [&](const hir::Break& break_expr) {
                    lower_break(break_expr);
                },
                [&](const hir::Continue& continue_expr) {
                    lower_continue(continue_expr);
                },
                [&](const hir::Return& return_expr) {
                    lower_return(return_expr);
                },
                [&](const hir::Call& call) {
                    lower_call_effect(call, type);
                },
                [&](const hir::MethodCall& call) {
                    lower_method_call_effect(call, type);
                },
                [&](const auto&) {
                    if (classify_host_type(type) == HostClass::Aggregate) {
                        auto tmp = slot_place(temp_slot(type));
                        materialize(expr, tmp);
                    } else if (classify_host_type(type) != HostClass::Unit &&
                               classify_host_type(type) != HostClass::Never) {
                        (void)lower_value(expr);
                    }
                },
            },
            expr.value);
    }

    Value lower_value(const hir::Expr& expr) {
        auto type = expr_type(expr);
        auto klass = ssa_class_for(type);
        if (!klass) {
            throw LoweringError(std::string("attempted to lower ") +
                                expr_kind_name(expr) + " with host class " +
                                host_class_name(classify_host_type(type)) +
                                " as SSA value at source offset " +
                                std::to_string(expr_span_start(expr)));
        }

        return std::visit(
            Overloaded{
                [&](const hir::Literal& literal) {
                    return lower_literal(literal);
                },
                [&](const hir::Variable& variable) {
                    return load_scalar(local_place(*variable.local_id));
                },
                [&](const hir::ConstUse& constant) {
                    return lower_const_value(constant.def ? constant.def->const_value : std::nullopt);
                },
                [&](const hir::StructConst& constant) {
                    return lower_const_value(constant.assoc_const ? constant.assoc_const->const_value
                                                                  : std::nullopt);
                },
                [&](const hir::EnumVariant& variant) {
                    return iconst(static_cast<std::int64_t>(variant.variant_index));
                },
                [&](const hir::FieldAccess&) {
                    return load_scalar(lower_place(expr));
                },
                [&](const hir::Index&) {
                    return load_scalar(lower_place(expr));
                },
                [&](const hir::UnaryOp& unary) {
                    return lower_unary(unary, type);
                },
                [&](const hir::BinaryOp& binary) {
                    return lower_binary(binary);
                },
                [&](const hir::Cast& cast) {
                    auto input = lower_value(*cast.expr);
                    auto result = new_value(*klass);
                    emit(Cast{
                        .result = result,
                        .operand = input.id,
                        .op = lower_cast_op(input.klass, result.klass),
                    });
                    return result;
                },
                [&](const hir::Call& call) {
                    return lower_call(call, *klass);
                },
                [&](const hir::MethodCall& call) {
                    return lower_method_call(call, *klass);
                },
                [&](const hir::Block& block) {
                    auto value = lower_block_value(block, type);
                    if (!value) {
                        throw LoweringError("diverging block used where value is required");
                    }
                    return Value{.id = *value, .klass = *klass};
                },
                [&](const hir::If& if_expr) {
                    return lower_if_value(if_expr, type, *klass);
                },
                [&](const hir::Assignment&) -> Value {
                    throw LoweringError("unit assignment used as IR3 value");
                },
                [&](const hir::FuncUse&) -> Value {
                    throw LoweringError("function item used as first-class IR3 value");
                },
                [&](const hir::UnresolvedIdentifier&) -> Value {
                    throw LoweringError("unresolved identifier reached IR3 lowering");
                },
                [&](const hir::TypeStatic&) -> Value {
                    throw LoweringError("unresolved type static reached IR3 lowering");
                },
                [&](const hir::Underscore&) -> Value {
                    throw LoweringError("underscore expression reached IR3 lowering");
                },
                [&](const auto&) -> Value {
                    throw LoweringError("unsupported expression in IR3 value lowering");
                },
            },
            expr.value);
    }

    Value lower_literal(const hir::Literal& literal) {
        return std::visit(
            Overloaded{
                [&](const hir::Literal::Integer& integer) {
                    auto value = static_cast<std::int64_t>(integer.value);
                    return iconst(integer.is_negative ? -value : value);
                },
                [&](bool value) {
                    return iconst(value ? 1 : 0);
                },
                [&](char value) {
                    return iconst(static_cast<unsigned char>(value));
                },
                [&](const hir::Literal::String&) -> Value {
                    throw LoweringError("string literal IR3 lowering is not implemented yet");
                },
            },
            literal.value);
    }

    Value lower_const_value(const std::optional<semantic::ConstVariant>& constant) {
        if (!constant) {
            throw LoweringError("constant use without evaluated constant value");
        }
        return std::visit(
            Overloaded{
                [&](semantic::UintConst value) {
                    return iconst(value.value);
                },
                [&](semantic::IntConst value) {
                    return iconst(value.value);
                },
                [&](semantic::BoolConst value) {
                    return iconst(value.value ? 1 : 0);
                },
                [&](semantic::CharConst value) {
                    return iconst(static_cast<unsigned char>(value.value));
                },
                [&](const semantic::StringConst&) -> Value {
                    throw LoweringError("string constants are not scalar IR3 values");
                },
            },
            *constant);
    }

    Value lower_unary(const hir::UnaryOp& unary, semantic::TypeId result_type) {
        switch (unary.op) {
        case hir::UnaryOp::NOT: {
            auto input = lower_value(*unary.rhs);
            auto result = new_value(SsaClass::I32);
            emit(Unary{
                .result = result,
                .op = is_bool_type(result_type) ? UnaryOp::BoolNot
                                                : UnaryOp::BitNot,
                .operand = input.id,
            });
            return result;
        }
        case hir::UnaryOp::NEGATE: {
            auto input = lower_value(*unary.rhs);
            auto result = new_value(SsaClass::I32);
            emit(Unary{
                .result = result,
                .op = is_unsigned_integer_type(result_type) ? UnaryOp::UNeg
                                                            : UnaryOp::SNeg,
                .operand = input.id,
            });
            return result;
        }
        case hir::UnaryOp::DEREFERENCE:
            return load_scalar(lower_place_from_deref(unary));
        case hir::UnaryOp::REFERENCE:
        case hir::UnaryOp::MUTABLE_REFERENCE: {
            auto source = place_or_materialized_temp(*unary.rhs);
            auto result = new_value(SsaClass::Ptr);
            emit(Borrow{
                .result = result,
                .is_mutable = unary.op == hir::UnaryOp::MUTABLE_REFERENCE,
                .source = source,
            });
            (void)result_type;
            return result;
        }
        }
        throw LoweringError("unknown unary operator");
    }

    Value lower_binary(const hir::BinaryOp& binary) {
        if (binary.op == hir::BinaryOp::AND || binary.op == hir::BinaryOp::OR) {
            return lower_short_circuit(binary);
        }
        auto lhs = lower_value(*binary.lhs);
        auto rhs = lower_value(*binary.rhs);
        auto result = new_value(SsaClass::I32);
        emit(Binary{
            .result = result,
            .op = lower_binary_op(binary.op, expr_type(*binary.lhs)),
            .lhs = lhs.id,
            .rhs = rhs.id,
        });
        return result;
    }

    Value lower_short_circuit(const hir::BinaryOp& binary) {
        auto lhs = lower_value(*binary.lhs);
        BlockId rhs_block = add_block("sc.rhs");
        BlockId const_block = add_block("sc.const");
        BlockId join_block = add_block("sc.join");
        if (binary.op == hir::BinaryOp::AND) {
            terminate(Branch{.condition = lhs.id, .then_block = rhs_block, .else_block = const_block});
        } else {
            terminate(Branch{.condition = lhs.id, .then_block = const_block, .else_block = rhs_block});
        }

        switch_to(rhs_block);
        auto rhs = lower_value(*binary.rhs);
        BlockId rhs_pred = current_;
        if (current_is_open()) {
            terminate(Jump{.target = join_block});
        }

        switch_to(const_block);
        auto constant = iconst(binary.op == hir::BinaryOp::OR ? 1 : 0);
        BlockId const_pred = current_;
        terminate(Jump{.target = join_block});

        switch_to(join_block);
        auto result = new_value(SsaClass::I32);
        current_block().phis.push_back(Phi{
            .result = result,
            .incoming = {
                PhiIncoming{.pred = rhs_pred, .value = rhs.id},
                PhiIncoming{.pred = const_pred, .value = constant.id},
            },
        });
        return result;
    }

    Value lower_call(const hir::Call& call, SsaClass result_class) {
        auto* callee = std::get_if<hir::FuncUse>(&call.callee->value);
        if (!callee || !callee->def) {
            throw LoweringError("IR3 phase 1 only supports direct function calls");
        }
        return emit_direct_call_value(symbols_.symbol_for(*callee->def), call.args, result_class);
    }

    Value lower_method_call(const hir::MethodCall& call, SsaClass result_class) {
        auto* method = std::get_if<const hir::Method*>(&call.method);
        if (!method || !*method) {
            throw LoweringError("unresolved method call reached IR3 lowering");
        }
        std::vector<const hir::Expr*> args;
        args.push_back(call.receiver.get());
        for (const auto& arg : call.args) {
            args.push_back(arg.get());
        }
        return emit_direct_call_value(symbols_.symbol_for(**method), args, result_class);
    }

    void lower_call_effect(const hir::Call& call, semantic::TypeId result_type) {
        auto* callee = std::get_if<hir::FuncUse>(&call.callee->value);
        if (!callee || !callee->def) {
            throw LoweringError("IR3 phase 1 only supports direct function calls");
        }
        lower_direct_call_effect(symbols_.symbol_for(*callee->def), call.args, result_type);
    }

    void lower_method_call_effect(const hir::MethodCall& call, semantic::TypeId result_type) {
        auto* method = std::get_if<const hir::Method*>(&call.method);
        if (!method || !*method) {
            throw LoweringError("unresolved method call reached IR3 lowering");
        }
        std::vector<const hir::Expr*> args;
        args.push_back(call.receiver.get());
        for (const auto& arg : call.args) {
            args.push_back(arg.get());
        }
        lower_direct_call_effect(symbols_.symbol_for(**method), args, result_type);
    }

    void lower_direct_call_effect(const std::string& callee,
                                  const std::vector<std::unique_ptr<hir::Expr>>& args,
                                  semantic::TypeId result_type) {
        std::vector<const hir::Expr*> raw_args;
        raw_args.reserve(args.size());
        for (const auto& arg : args) {
            raw_args.push_back(arg.get());
        }
        lower_direct_call_effect(callee, raw_args, result_type);
    }

    void lower_direct_call_effect(const std::string& callee,
                                  const std::vector<const hir::Expr*>& args,
                                  semantic::TypeId result_type) {
        switch (classify_host_type(result_type)) {
        case HostClass::Aggregate: {
            auto tmp = slot_place(temp_slot(result_type));
            emit_direct_call_materialize(callee, args, tmp);
            return;
        }
        case HostClass::I32:
        case HostClass::Ptr:
            (void)emit_direct_call_value(callee, args, *ssa_class_for(result_type));
            return;
        case HostClass::Unit:
        case HostClass::Never:
            emit_direct_call_void(callee, args);
            return;
        }
    }

    void lower_call_materialize(const hir::Call& call, const Place& dest) {
        auto* callee = std::get_if<hir::FuncUse>(&call.callee->value);
        if (!callee || !callee->def) {
            throw LoweringError("IR3 phase 1 only supports direct function calls");
        }
        emit_direct_call_materialize(symbols_.symbol_for(*callee->def), call.args, dest);
    }

    void lower_method_call_materialize(const hir::MethodCall& call, const Place& dest) {
        auto* method = std::get_if<const hir::Method*>(&call.method);
        if (!method || !*method) {
            throw LoweringError("unresolved method call reached IR3 lowering");
        }
        std::vector<const hir::Expr*> args;
        args.push_back(call.receiver.get());
        for (const auto& arg : call.args) {
            args.push_back(arg.get());
        }
        emit_direct_call_materialize(symbols_.symbol_for(**method), args, dest);
    }

    Value emit_direct_call_value(const std::string& callee,
                                 const std::vector<std::unique_ptr<hir::Expr>>& args,
                                 SsaClass result_class) {
        std::vector<const hir::Expr*> raw_args;
        raw_args.reserve(args.size());
        for (const auto& arg : args) {
            raw_args.push_back(arg.get());
        }
        return emit_direct_call_value(callee, raw_args, result_class);
    }

    Value emit_direct_call_value(const std::string& callee,
                                 const std::vector<const hir::Expr*>& args,
                                 SsaClass result_class) {
        auto lowered_args = lower_call_args(args);
        auto result = new_value(result_class);
        emit(Call{
            .result = result,
            .callee = callee,
            .args = std::move(lowered_args),
        });
        return result;
    }

    void emit_direct_call_void(const std::string& callee,
                               const std::vector<const hir::Expr*>& args) {
        emit(Call{
            .result = std::nullopt,
            .callee = callee,
            .args = lower_call_args(args),
        });
    }

    void emit_direct_call_void_raw(const std::string& callee,
                                   std::vector<ValueId> args) {
        emit(Call{
            .result = std::nullopt,
            .callee = callee,
            .args = std::move(args),
        });
    }

    void emit_direct_call_materialize(const std::string& callee,
                                      const std::vector<std::unique_ptr<hir::Expr>>& args,
                                      const Place& dest) {
        std::vector<const hir::Expr*> raw_args;
        raw_args.reserve(args.size());
        for (const auto& arg : args) {
            raw_args.push_back(arg.get());
        }
        emit_direct_call_materialize(callee, raw_args, dest);
    }

    void emit_direct_call_materialize(const std::string& callee,
                                      const std::vector<const hir::Expr*>& args,
                                      const Place& dest) {
        auto lowered_args = lower_call_args(args);
        auto out = new_value(SsaClass::Ptr);
        emit(Borrow{.result = out, .is_mutable = true, .source = dest});
        lowered_args.insert(lowered_args.begin(), out.id);
        emit(Call{
            .result = std::nullopt,
            .callee = callee,
            .args = std::move(lowered_args),
        });
    }

    std::vector<ValueId> lower_call_args(const std::vector<const hir::Expr*>& args) {
        std::vector<ValueId> lowered_args;
        for (const auto* arg : args) {
            auto type = expr_type(*arg);
            if (ssa_class_for(type)) {
                lowered_args.push_back(lower_value(*arg).id);
            } else {
                auto place = place_or_materialized_temp(*arg);
                auto ptr = new_value(SsaClass::Ptr);
                emit(Borrow{.result = ptr, .is_mutable = false, .source = place});
                lowered_args.push_back(ptr.id);
            }
        }
        return lowered_args;
    }

    Value lower_if_value(const hir::If& if_expr,
                         semantic::TypeId result_type,
                         SsaClass result_class) {
        auto condition = lower_value(*if_expr.condition);
        BlockId then_block = add_block("if.then");
        BlockId else_block = add_block("if.else");
        BlockId join_block = add_block("if.join");
        terminate(Branch{.condition = condition.id, .then_block = then_block, .else_block = else_block});

        switch_to(then_block);
        auto then_value = lower_block_value(*if_expr.then_block, result_type);
        auto then_pred = current_;
        if (current_is_open()) {
            terminate(Jump{.target = join_block});
        }

        switch_to(else_block);
        std::optional<ValueId> else_value;
        if (if_expr.else_expr) {
            else_value = lower_expr_value_or_diverge(**if_expr.else_expr, result_type);
        } else {
            throw LoweringError("value-producing if expression has no else branch");
        }
        auto else_pred = current_;
        if (current_is_open()) {
            terminate(Jump{.target = join_block});
        }

        switch_to(join_block);
        auto result = new_value(result_class);
        std::vector<PhiIncoming> incoming;
        if (then_value) {
            incoming.push_back(PhiIncoming{.pred = then_pred, .value = *then_value});
        }
        if (else_value) {
            incoming.push_back(PhiIncoming{.pred = else_pred, .value = *else_value});
        }
        if (incoming.empty()) {
            throw LoweringError("diverging if expression used where value is required");
        }
        current_block().phis.push_back(Phi{.result = result, .incoming = std::move(incoming)});
        return result;
    }

    std::optional<ValueId> lower_expr_value_or_diverge(const hir::Expr& expr,
                                                       semantic::TypeId expected_type) {
        if (is_never(expr_type(expr))) {
            lower_expr_effect(expr);
            return std::nullopt;
        }
        if (auto* block = std::get_if<hir::Block>(&expr.value)) {
            return lower_block_value(*block, expected_type);
        }
        return lower_value(expr).id;
    }

    void lower_if_effect(const hir::If& if_expr) {
        auto condition = lower_value(*if_expr.condition);
        BlockId then_block = add_block("if.then");
        BlockId else_block = add_block("if.else");
        BlockId join_block = add_block("if.join");
        terminate(Branch{.condition = condition.id, .then_block = then_block, .else_block = else_block});

        switch_to(then_block);
        lower_block_effect(*if_expr.then_block);
        const bool then_continues = current_is_open();
        if (then_continues) {
            terminate(Jump{.target = join_block});
        }

        switch_to(else_block);
        if (if_expr.else_expr) {
            lower_expr_effect(**if_expr.else_expr);
        }
        const bool else_continues = current_is_open();
        if (else_continues) {
            terminate(Jump{.target = join_block});
        }

        switch_to(join_block);
        if (!then_continues && !else_continues) {
            terminate(Unreachable{});
        }
    }

    void lower_loop(const hir::Loop& loop) {
        BlockId header = add_block("loop.header");
        BlockId body = add_block("loop.body");
        BlockId exit = add_block("loop.exit");
        terminate(Jump{.target = header});
        switch_to(header);
        terminate(Jump{.target = body});

        loop_contexts_.emplace(&loop, LoopContext{.continue_target = header, .break_target = exit});
        switch_to(body);
        lower_block_effect(*loop.body);
        if (current_is_open()) {
            terminate(Jump{.target = header});
        }
        loop_contexts_.erase(&loop);
        switch_to(exit);
    }

    void lower_while(const hir::While& loop) {
        BlockId condition = add_block("while.cond");
        BlockId body = add_block("while.body");
        BlockId exit = add_block("while.exit");
        terminate(Jump{.target = condition});

        while_contexts_.emplace(&loop, LoopContext{.continue_target = condition, .break_target = exit});
        switch_to(condition);
        auto cond = lower_value(*loop.condition);
        terminate(Branch{.condition = cond.id, .then_block = body, .else_block = exit});

        switch_to(body);
        lower_block_effect(*loop.body);
        if (current_is_open()) {
            terminate(Jump{.target = condition});
        }
        while_contexts_.erase(&loop);
        switch_to(exit);
    }

    void lower_break(const hir::Break& break_expr) {
        if (break_expr.value) {
            throw LoweringError("break values need phi/destination lowering and are not implemented yet");
        }
        if (!break_expr.target) {
            throw LoweringError("unlinked break reached IR3 lowering");
        }
        BlockId target = std::visit(
            Overloaded{
                [&](hir::Loop* loop) {
                    return loop_contexts_.at(loop).break_target;
                },
                [&](hir::While* loop) {
                    return while_contexts_.at(loop).break_target;
                },
            },
            *break_expr.target);
        terminate(Jump{.target = target});
    }

    void lower_continue(const hir::Continue& continue_expr) {
        if (!continue_expr.target) {
            throw LoweringError("unlinked continue reached IR3 lowering");
        }
        BlockId target = std::visit(
            Overloaded{
                [&](hir::Loop* loop) {
                    return loop_contexts_.at(loop).continue_target;
                },
                [&](hir::While* loop) {
                    return while_contexts_.at(loop).continue_target;
                },
            },
            *continue_expr.target);
        terminate(Jump{.target = target});
    }

    void lower_return(const hir::Return& return_expr) {
        if (source_function_ && return_expr.target) {
            if (auto* function = std::get_if<hir::Function*>(&*return_expr.target);
                function && *function != source_function_) {
                throw LoweringError("return target does not match current function");
            }
        }
        if (source_method_ && return_expr.target) {
            if (auto* method = std::get_if<hir::Method*>(&*return_expr.target);
                method && *method != source_method_) {
                throw LoweringError("return target does not match current method");
            }
        }

        auto return_type = function_.source_return_type;
        if (classify_host_type(return_type) == HostClass::Aggregate) {
            if (!return_expr.value) {
                throw LoweringError("aggregate-returning function returned no value");
            }
            materialize(**return_expr.value, *aggregate_return_dest_);
            terminate(Return{.value = std::nullopt});
            return;
        }
        if (auto klass = ssa_class_for(return_type)) {
            if (!return_expr.value) {
                throw LoweringError("value-returning function returned no value");
            }
            auto value = lower_value(**return_expr.value);
            if (value.klass != *klass) {
                throw LoweringError("return value class does not match function return class");
            }
            terminate(Return{.value = value.id});
            return;
        }
        terminate(Return{.value = std::nullopt});
    }

    Place lower_place(const hir::Expr& expr) {
        return std::visit(
            Overloaded{
                [&](const hir::Variable& variable) {
                    return local_place(*variable.local_id);
                },
                [&](const hir::FieldAccess& access) {
                    auto base = place_or_materialized_temp(*access.base);
                    auto* field_index = std::get_if<std::size_t>(&access.field);
                    if (!field_index) {
                        throw LoweringError("unresolved field access reached IR3 lowering");
                    }
                    base.projections.push_back(FieldProjection{
                        .index = *field_index,
                        .result_type = expr_type(expr),
                    });
                    base.host_type = expr_type(expr);
                    return base;
                },
                [&](const hir::Index& index) {
                    auto base = place_or_materialized_temp(*index.base);
                    auto index_value = lower_value(*index.index);
                    base.projections.push_back(IndexProjection{
                        .index = index_value.id,
                        .result_type = expr_type(expr),
                    });
                    base.host_type = expr_type(expr);
                    return base;
                },
                [&](const hir::UnaryOp& unary) {
                    if (unary.op != hir::UnaryOp::DEREFERENCE) {
                        throw LoweringError("only dereference unary expressions are places");
                    }
                    return lower_place_from_deref(unary);
                },
                [&](const auto&) -> Place {
                    throw LoweringError("expression is not an IR3 place");
                },
            },
            expr.value);
    }

    Place lower_place_from_deref(const hir::UnaryOp& unary) {
        auto ptr = lower_value(*unary.rhs);
        auto rhs_type = expr_type(*unary.rhs);
        auto* ref = std::get_if<semantic::ReferenceType>(&rhs_type->value);
        if (!ref) {
            throw LoweringError("dereference operand is not reference-like");
        }
        return Place{
            .base = DerefBase{
                .ptr = ptr.id,
                .pointee_type = ref->referenced_type,
                .is_mutable = ref->is_mutable,
            },
            .projections = {},
            .host_type = ref->referenced_type,
            .is_mutable = ref->is_mutable,
        };
    }

    Place place_or_materialized_temp(const hir::Expr& expr) {
        if (expr.expr_info && expr.expr_info->is_place) {
            return lower_place(expr);
        }
        auto type = expr_type(expr);
        auto place = slot_place(temp_slot(type));
        materialize(expr, place);
        return place;
    }

    void materialize(const hir::Expr& expr, const Place& dest) {
        auto type = expr_type(expr);
        switch (classify_host_type(type)) {
        case HostClass::I32:
        case HostClass::Ptr:
            store_value(dest, lower_value(expr).id);
            return;
        case HostClass::Unit:
        case HostClass::Never:
            lower_expr_effect(expr);
            return;
        case HostClass::Aggregate:
            break;
        }

        if (expr.expr_info && expr.expr_info->is_place) {
            copy_or_load_store(dest, lower_place(expr));
            return;
        }

        std::visit(
            Overloaded{
                [&](const hir::Variable& variable) {
                    copy_or_load_store(dest, local_place(*variable.local_id));
                },
                [&](const hir::FieldAccess&) {
                    copy_or_load_store(dest, lower_place(expr));
                },
                [&](const hir::Index&) {
                    copy_or_load_store(dest, lower_place(expr));
                },
                [&](const hir::StructLiteral& literal) {
                    lower_struct_literal(literal, dest);
                },
                [&](const hir::ArrayLiteral& literal) {
                    lower_array_literal(literal, dest);
                },
                [&](const hir::ArrayRepeat& repeat) {
                    lower_array_repeat(repeat, dest);
                },
                [&](const hir::Call& call) {
                    lower_call_materialize(call, dest);
                },
                [&](const hir::MethodCall& call) {
                    lower_method_call_materialize(call, dest);
                },
                [&](const hir::Block& block) {
                    lower_block_materialize(block, dest);
                },
                [&](const hir::If& if_expr) {
                    lower_if_materialize(if_expr, dest);
                },
                [&](const auto&) {
                    throw LoweringError(std::string("unsupported aggregate materialization form: ") +
                                        expr_kind_name(expr) + " at source offset " +
                                        std::to_string(expr_span_start(expr)));
                },
            },
            expr.value);
    }

    void lower_struct_literal(const hir::StructLiteral& literal, const Place& dest) {
        auto* fields = std::get_if<hir::StructLiteral::CanonicalFields>(&literal.fields);
        if (!fields) {
            throw LoweringError("non-canonical struct literal reached IR3 lowering");
        }
        for (std::size_t i = 0; i < fields->initializers.size(); ++i) {
            Place field = dest;
            field.projections.push_back(FieldProjection{
                .index = i,
                .result_type = expr_type(*fields->initializers[i]),
            });
            field.host_type = expr_type(*fields->initializers[i]);
            materialize(*fields->initializers[i], field);
        }
    }

    void lower_array_literal(const hir::ArrayLiteral& literal, const Place& dest) {
        for (std::size_t i = 0; i < literal.elements.size(); ++i) {
            auto index = iconst(static_cast<std::int64_t>(i));
            Place element = dest;
            element.projections.push_back(IndexProjection{
                .index = index.id,
                .result_type = expr_type(*literal.elements[i]),
            });
            element.host_type = expr_type(*literal.elements[i]);
            materialize(*literal.elements[i], element);
        }
    }

    void lower_array_repeat(const hir::ArrayRepeat& repeat, const Place& dest) {
        auto* count = std::get_if<std::size_t>(&repeat.count);
        if (!count) {
            throw LoweringError("non-constant array repeat count reached IR3 lowering");
        }

        auto element_type = expr_type(*repeat.value);
        if (const auto fill_byte = byte_repeat_literal(*repeat.value, element_type)) {
            const auto dest_size = riscv::size_of(dest.host_type, target_);
            if (dest_size < kBulkMemsetMinSize) {
                goto fallback_repeat_lowering;
            }
            auto ptr = new_value(SsaClass::Ptr);
            emit(Borrow{.result = ptr, .is_mutable = true, .source = dest});
            auto fill = iconst(*fill_byte);
            auto size = iconst(static_cast<std::int64_t>(dest_size));
            emit_direct_call_void_raw("__rcomp_memset", {ptr.id, fill.id, size.id});
            return;
        }

fallback_repeat_lowering:
        auto element_temp = slot_place(temp_slot(element_type));
        materialize(*repeat.value, element_temp);
        if (!current_is_open()) {
            return;
        }

        auto index_type = semantic::get_typeID(
            semantic::Type{semantic::PrimitiveKind::USIZE});
        auto index_slot = slot_place(temp_slot(index_type));
        store_value(index_slot, iconst(0).id);

        BlockId condition = add_block("array.repeat.cond");
        BlockId body = add_block("array.repeat.body");
        BlockId exit = add_block("array.repeat.exit");
        terminate(Jump{.target = condition});

        switch_to(condition);
        auto index = load_scalar(index_slot);
        auto bound = iconst(static_cast<std::int64_t>(*count));
        auto should_continue = new_value(SsaClass::I32);
        emit(Binary{
            .result = should_continue,
            .op = BinaryOp::ULt,
            .lhs = index.id,
            .rhs = bound.id,
        });
        terminate(Branch{
            .condition = should_continue.id,
            .then_block = body,
            .else_block = exit,
        });

        switch_to(body);
        auto body_index = load_scalar(index_slot);
        Place element = dest;
        element.projections.push_back(IndexProjection{
            .index = body_index.id,
            .result_type = element_type,
        });
        element.host_type = element_type;
        copy_or_load_store(element, element_temp);

        auto one = iconst(1);
        auto next = new_value(SsaClass::I32);
        emit(Binary{
            .result = next,
            .op = BinaryOp::UAdd,
            .lhs = body_index.id,
            .rhs = one.id,
        });
        store_value(index_slot, next.id);
        terminate(Jump{.target = condition});

        switch_to(exit);
    }

    void lower_if_materialize(const hir::If& if_expr, const Place& dest) {
        auto condition = lower_value(*if_expr.condition);
        BlockId then_block = add_block("if.then");
        BlockId else_block = add_block("if.else");
        BlockId join_block = add_block("if.join");
        terminate(Branch{.condition = condition.id, .then_block = then_block, .else_block = else_block});

        switch_to(then_block);
        lower_block_materialize(*if_expr.then_block, dest);
        if (current_is_open()) {
            terminate(Jump{.target = join_block});
        }

        switch_to(else_block);
        if (!if_expr.else_expr) {
            throw LoweringError("aggregate if expression has no else branch");
        }
        materialize(**if_expr.else_expr, dest);
        if (current_is_open()) {
            terminate(Jump{.target = join_block});
        }

        switch_to(join_block);
    }

    Value load_scalar(const Place& source) {
        auto klass = ssa_class_for(source.host_type);
        if (!klass) {
            throw LoweringError("attempted to load aggregate place as scalar");
        }
        auto result = new_value(*klass);
        emit(Load{.result = result, .source = source});
        return result;
    }

    void store_value(const Place& dest, ValueId value) {
        auto klass = ssa_class_for(dest.host_type);
        if (!klass) {
            throw LoweringError("attempted to store scalar into aggregate place");
        }
        emit(Store{.klass = *klass, .dest = dest, .value = value});
    }

    void copy_or_load_store(const Place& dest, const Place& source) {
        if (auto klass = ssa_class_for(dest.host_type)) {
            auto value = load_scalar(source);
            emit(Store{.klass = *klass, .dest = dest, .value = value.id});
        } else {
            emit(Copy{.dest = dest, .source = source});
        }
    }

    Value iconst(std::int64_t value) {
        auto result = new_value(SsaClass::I32);
        emit(IConst{.result = result, .value = value});
        return result;
    }
};

void collect_item_symbols(LoweringSymbols& symbols, const hir::Item& item);

void collect_block_symbols(LoweringSymbols& symbols, const hir::Block* block) {
    if (!block) {
        return;
    }
    for (const auto& item : block->items) {
        collect_item_symbols(symbols, *item);
    }
}

void collect_item_symbols(LoweringSymbols& symbols, const hir::Item& item) {
    std::visit(
        Overloaded{
            [&](const hir::Function& function) {
                symbols.define_function(function);
                collect_block_symbols(symbols, function.body.get());
            },
            [&](const hir::Impl& impl) {
                auto impl_type = resolved_type(impl.for_type);
                for (const auto& associated : impl.items) {
                    std::visit(
                        Overloaded{
                            [&](const hir::Function& function) {
                                symbols.define_function(function, impl_type);
                                collect_block_symbols(symbols, function.body.get());
                            },
                            [&](const hir::Method& method) {
                                symbols.define_method(method);
                                collect_block_symbols(symbols, method.body.get());
                            },
                            [&](const hir::ConstDef&) {},
                        },
                        associated->value);
                }
            },
            [&](const auto&) {},
        },
        item.value);
}

void lower_top_level_item(Module& module,
                          LoweringSymbols& symbols,
                          const hir::Item& item,
                          const riscv::TargetConfig& target) {
    std::visit(
        Overloaded{
            [&](const hir::Function& function) {
                FunctionLowerer lowerer(module, symbols, symbols.symbol_for(function), target);
                lowerer.lower_function(function);
                module.functions.push_back(lowerer.finish());
            },
            [&](const hir::Impl& impl) {
                auto impl_type = resolved_type(impl.for_type);
                for (const auto& associated : impl.items) {
                    std::visit(
                        Overloaded{
                            [&](const hir::Function& function) {
                                symbols.define_function(function, impl_type);
                                FunctionLowerer lowerer(module, symbols,
                                                       symbols.symbol_for(function), target);
                                lowerer.lower_function(function);
                                module.functions.push_back(lowerer.finish());
                            },
                            [&](const hir::Method& method) {
                                symbols.define_method(method);
                                FunctionLowerer lowerer(module, symbols,
                                                       symbols.symbol_for(method), target);
                                lowerer.lower_method(method);
                                module.functions.push_back(lowerer.finish());
                            },
                            [&](const hir::ConstDef&) {},
                        },
                        associated->value);
                }
            },
            [&](const auto&) {},
        },
        item.value);
}

} // namespace

Module lower_program(const hir::Program& program, const riscv::TargetConfig& target) {
    Module module;
    LoweringSymbols symbols;
    for (const auto& item : program.items) {
        collect_item_symbols(symbols, *item);
    }
    for (const auto& item : program.items) {
        lower_top_level_item(module, symbols, *item, target);
    }
    return module;
}

} // namespace ir3
