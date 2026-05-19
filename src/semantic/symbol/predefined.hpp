#pragma once
#include "symbol/scope.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/type/impl_table.hpp"
#include "semantic/type/type.hpp"

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace semantic {

enum class PredefinedFunctionKind {
    Print,
    Println,
    PrintInt,
    PrintlnInt,
    GetString,
    GetInt,
    Exit,
};

enum class PredefinedMethodKind {
    I32ToString,
    U32ToString,
    UsizeToString,
    AnyIntToString,
    AnyUIntToString,
    StringAsStr,
    StringAsMutStr,
    StringLen,
    StringAppend,
    StrLen,
};

struct PredefinedMethodEntry {
    std::string name;
    hir::Method* method;
};

struct PredefinedTypeIdHasher {
    size_t operator()(TypeId type) const noexcept {
        return std::hash<const Type*>{}(type);
    }
};

inline std::unordered_map<TypeId, std::vector<PredefinedMethodEntry>, PredefinedTypeIdHasher>&
get_predefined_method_table() {
    static std::unordered_map<TypeId, std::vector<PredefinedMethodEntry>, PredefinedTypeIdHasher> table;
    return table;
}

inline void insert_predefined_method(TypeId receiver_type,
                                     std::string_view method_name,
                                     hir::Method* method) {
    auto& table = get_predefined_method_table();
    auto& entries = table[receiver_type];
    auto it = std::find_if(entries.begin(), entries.end(), [&](const PredefinedMethodEntry& entry) {
        return entry.method == method || entry.name == method_name;
    });
    if (it == entries.end()) {
        entries.push_back(PredefinedMethodEntry{std::string(method_name), method});
    }
}

inline const std::unordered_map<TypeId, std::vector<PredefinedMethodEntry>, PredefinedTypeIdHasher>&
get_predefined_methods() {
    return get_predefined_method_table();
}

// struct String {}
inline hir::StructDef struct_String = [] {
    hir::StructDef def{};
    def.name = ast::Identifier("String");
    return def;
}();

namespace {

inline TypeId unit_type() {
    static const TypeId id = get_typeID(Type{UnitType{}});
    return id;
}

inline TypeId i32_type() {
    static const TypeId id = get_typeID(Type{PrimitiveKind::I32});
    return id;
}

inline TypeId u32_type() {
    static const TypeId id = get_typeID(Type{PrimitiveKind::U32});
    return id;
}

inline TypeId usize_type() {
    static const TypeId id = get_typeID(Type{PrimitiveKind::USIZE});
    return id;
}

inline TypeId anyint_type() {
    static const TypeId id = get_typeID(Type{PrimitiveKind::__ANYINT__});
    return id;
}

inline TypeId anyuint_type() {
    static const TypeId id = get_typeID(Type{PrimitiveKind::__ANYUINT__});
    return id;
}

inline TypeId primitive_string_type() {
    static const TypeId id = get_typeID(Type{PrimitiveKind::STRING});
    return id;
}

inline TypeId string_struct_type() {
    auto struct_type = StructType();
    struct_type.symbol = &struct_String;
    static const TypeId id = get_typeID(Type{std::move(struct_type)});
    return id;
}

inline TypeId string_ref_type() {
    auto ref_type = ReferenceType();
    ref_type.referenced_type = primitive_string_type();
    ref_type.is_mutable = false;
    static const TypeId id = get_typeID(Type{std::move(ref_type)});
    return id;
}

inline TypeId string_mut_ref_type() {
    auto ref_type = ReferenceType();
    ref_type.referenced_type = primitive_string_type();
    ref_type.is_mutable = true;
    static const TypeId id = get_typeID(Type{std::move(ref_type)});
    return id;
}

inline std::unique_ptr<hir::Pattern> make_param_pattern(size_t index) {
    hir::BindingDef binding{
        hir::BindingDef::Unresolved{
            .is_mutable = false,
            .is_ref = false,
            .name = ast::Identifier(std::string("_arg") + std::to_string(index))
        }
    };
    return std::make_unique<hir::Pattern>(hir::PatternVariant{std::move(binding)});
}

inline hir::Function make_builtin_function(std::string_view name,
                                           std::initializer_list<TypeId> param_types,
                                           TypeId return_type) {
    hir::Function fn{};
    fn.name = ast::Identifier(std::string(name));
    fn.params.reserve(param_types.size());
    fn.param_type_annotations.reserve(param_types.size());

    size_t index = 0;
    for (TypeId type : param_types) {
        fn.params.push_back(make_param_pattern(index++));
        fn.param_type_annotations.emplace_back(hir::TypeAnnotation{type});
    }

    fn.return_type = hir::TypeAnnotation{return_type};
    fn.body = nullptr;
    fn.locals.clear();

    return fn;
}

inline hir::Method make_builtin_method(std::string_view name,
                                       bool self_is_reference,
                                       bool self_is_mutable,
                                       std::initializer_list<TypeId> param_types,
                                       TypeId return_type) {
    hir::Method method{};
    method.name = ast::Identifier(std::string(name));
    method.self_param.is_reference = self_is_reference;
    method.self_param.is_mutable = self_is_mutable;

    method.params.reserve(param_types.size());
    method.param_type_annotations.reserve(param_types.size());

    size_t index = 0;
    for (TypeId type : param_types) {
        method.params.push_back(make_param_pattern(index++));
        method.param_type_annotations.emplace_back(hir::TypeAnnotation{type});
    }

    method.return_type = hir::TypeAnnotation{return_type};
    method.body = nullptr;
    method.self_local.reset();
    method.locals.clear();

    return method;
}

inline hir::Function func_print = make_builtin_function("print", {string_ref_type()}, unit_type());
inline hir::Function func_println = make_builtin_function("println", {string_ref_type()}, unit_type());
inline hir::Function func_printInt = make_builtin_function("printInt", {i32_type()}, unit_type());
inline hir::Function func_printlnInt = make_builtin_function("printlnInt", {i32_type()}, unit_type());
inline hir::Function func_getString = make_builtin_function("getString", std::initializer_list<TypeId>{}, string_struct_type());
inline hir::Function func_getInt = make_builtin_function("getInt", std::initializer_list<TypeId>{}, i32_type());
inline hir::Function func_exit = make_builtin_function("exit", {i32_type()}, unit_type());

inline hir::Method method_i32_to_string = make_builtin_method("to_string", true, false, std::initializer_list<TypeId>{}, string_struct_type());
inline hir::Method method_u32_to_string = make_builtin_method("to_string", true, false, std::initializer_list<TypeId>{}, string_struct_type());
inline hir::Method method_usize_to_string = make_builtin_method("to_string", true, false, std::initializer_list<TypeId>{}, string_struct_type());
inline hir::Method method_anyint_to_string = make_builtin_method("to_string", true, false, std::initializer_list<TypeId>{}, string_struct_type());
inline hir::Method method_anyuint_to_string = make_builtin_method("to_string", true, false, std::initializer_list<TypeId>{}, string_struct_type());
inline hir::Method method_string_as_str = make_builtin_method("as_str", true, false, std::initializer_list<TypeId>{}, string_ref_type());
inline hir::Method method_string_as_mut_str = make_builtin_method("as_mut_str", true, true, std::initializer_list<TypeId>{}, string_mut_ref_type());
inline hir::Method method_string_len = make_builtin_method("len", true, false, std::initializer_list<TypeId>{}, usize_type());
inline hir::Method method_string_append = make_builtin_method("append", true, true, {string_ref_type()}, unit_type());
inline hir::Method method_str_len = make_builtin_method("len", true, false, std::initializer_list<TypeId>{}, usize_type());

struct PredefinedMethodRegistrar {
    PredefinedMethodRegistrar() {
        insert_predefined_method(i32_type(), "to_string", &method_i32_to_string);
        insert_predefined_method(u32_type(), "to_string", &method_u32_to_string);
        insert_predefined_method(usize_type(), "to_string", &method_usize_to_string);
        // Temporary support until integer inference is fully resolved.
        insert_predefined_method(anyint_type(), "to_string", &method_anyint_to_string);
        insert_predefined_method(anyuint_type(), "to_string", &method_anyuint_to_string);
        insert_predefined_method(string_struct_type(), "as_str", &method_string_as_str);
        insert_predefined_method(string_struct_type(), "as_mut_str", &method_string_as_mut_str);
        insert_predefined_method(string_struct_type(), "len", &method_string_len);
        insert_predefined_method(string_struct_type(), "append", &method_string_append);
        insert_predefined_method(primitive_string_type(), "len", &method_str_len);
    }
};

inline const PredefinedMethodRegistrar predefined_method_registrar{};

} // namespace

inline void inject_predefined_methods(ImplTable& impl_table) {
    const auto& table = get_predefined_methods();
    for (const auto& [type, entries] : table) {
        for (const auto& entry : entries) {
            impl_table.add_predefined_method(type, entry.name, entry.method);
        }
    }
}

inline Scope create_predefined_scope() {
  Scope scope;
  scope.define_type("String", &struct_String);
  scope.define_item("print", &func_print);
  scope.define_item("println", &func_println);
  scope.define_item("printInt", &func_printInt);
  scope.define_item("printlnInt", &func_printlnInt);
  scope.define_item("getString", &func_getString);
  scope.define_item("getInt", &func_getInt);
  scope.define_item("exit", &func_exit);

  return scope;
}

inline Scope& get_predefined_scope() {
    static Scope predefined_scope = create_predefined_scope();
    return predefined_scope;
}

inline std::optional<PredefinedFunctionKind>
predefined_function_kind(const hir::Function& function) {
    const auto has_signature = [&](std::string_view name,
                                   std::initializer_list<TypeId> param_types,
                                   TypeId return_type) {
        if (function.body != nullptr || function.name.name != name) {
            return false;
        }
        if (function.param_type_annotations.size() != param_types.size()) {
            return false;
        }

        std::size_t index = 0;
        for (TypeId expected : param_types) {
            const auto& annotation = function.param_type_annotations[index++];
            if (!annotation) {
                return false;
            }
            const auto* actual = std::get_if<TypeId>(&*annotation);
            if (!actual || *actual != expected) {
                return false;
            }
        }

        if (!function.return_type) {
            return false;
        }
        const auto* actual_return = std::get_if<TypeId>(&*function.return_type);
        return actual_return && *actual_return == return_type;
    };

    if (&function == &func_print) {
        return PredefinedFunctionKind::Print;
    }
    if (&function == &func_println) {
        return PredefinedFunctionKind::Println;
    }
    if (&function == &func_printInt) {
        return PredefinedFunctionKind::PrintInt;
    }
    if (&function == &func_printlnInt) {
        return PredefinedFunctionKind::PrintlnInt;
    }
    if (&function == &func_getString) {
        return PredefinedFunctionKind::GetString;
    }
    if (&function == &func_getInt) {
        return PredefinedFunctionKind::GetInt;
    }
    if (&function == &func_exit) {
        return PredefinedFunctionKind::Exit;
    }
    if (has_signature("print", {string_ref_type()}, unit_type())) {
        return PredefinedFunctionKind::Print;
    }
    if (has_signature("println", {string_ref_type()}, unit_type())) {
        return PredefinedFunctionKind::Println;
    }
    if (has_signature("printInt", {i32_type()}, unit_type())) {
        return PredefinedFunctionKind::PrintInt;
    }
    if (has_signature("printlnInt", {i32_type()}, unit_type())) {
        return PredefinedFunctionKind::PrintlnInt;
    }
    if (has_signature("getString", {}, string_struct_type())) {
        return PredefinedFunctionKind::GetString;
    }
    if (has_signature("getInt", {}, i32_type())) {
        return PredefinedFunctionKind::GetInt;
    }
    if (has_signature("exit", {i32_type()}, unit_type())) {
        return PredefinedFunctionKind::Exit;
    }
    return std::nullopt;
}

inline std::optional<PredefinedMethodKind>
predefined_method_kind(const hir::Method& method) {
    if (&method == &method_i32_to_string) {
        return PredefinedMethodKind::I32ToString;
    }
    if (&method == &method_u32_to_string) {
        return PredefinedMethodKind::U32ToString;
    }
    if (&method == &method_usize_to_string) {
        return PredefinedMethodKind::UsizeToString;
    }
    if (&method == &method_anyint_to_string) {
        return PredefinedMethodKind::AnyIntToString;
    }
    if (&method == &method_anyuint_to_string) {
        return PredefinedMethodKind::AnyUIntToString;
    }
    if (&method == &method_string_as_str) {
        return PredefinedMethodKind::StringAsStr;
    }
    if (&method == &method_string_as_mut_str) {
        return PredefinedMethodKind::StringAsMutStr;
    }
    if (&method == &method_string_len) {
        return PredefinedMethodKind::StringLen;
    }
    if (&method == &method_string_append) {
        return PredefinedMethodKind::StringAppend;
    }
    if (&method == &method_str_len) {
        return PredefinedMethodKind::StrLen;
    }
    return std::nullopt;
}

inline std::optional<std::string_view>
predefined_runtime_symbol(const hir::Function& function) {
    const auto kind = predefined_function_kind(function);
    if (!kind) {
        return std::nullopt;
    }

    switch (*kind) {
    case PredefinedFunctionKind::Print:
        return "__rcomp_builtin_print";
    case PredefinedFunctionKind::Println:
        return "__rcomp_builtin_println";
    case PredefinedFunctionKind::PrintInt:
        return "__rcomp_printInt";
    case PredefinedFunctionKind::PrintlnInt:
        return "__rcomp_printlnInt";
    case PredefinedFunctionKind::GetString:
        return "__rcomp_builtin_getString";
    case PredefinedFunctionKind::GetInt:
        return "__rcomp_getInt";
    case PredefinedFunctionKind::Exit:
        return "__rcomp_exit";
    }

    return std::nullopt;
}

inline std::optional<std::string_view>
predefined_runtime_symbol(const hir::Method& method) {
    const auto kind = predefined_method_kind(method);
    if (!kind) {
        return std::nullopt;
    }

    switch (*kind) {
    case PredefinedMethodKind::I32ToString:
        return "__rcomp_builtin_i32_to_string";
    case PredefinedMethodKind::U32ToString:
        return "__rcomp_builtin_u32_to_string";
    case PredefinedMethodKind::UsizeToString:
        return "__rcomp_builtin_usize_to_string";
    case PredefinedMethodKind::AnyIntToString:
        return "__rcomp_builtin_anyint_to_string";
    case PredefinedMethodKind::AnyUIntToString:
        return "__rcomp_builtin_anyuint_to_string";
    case PredefinedMethodKind::StringAsStr:
        return "__rcomp_builtin_string_as_str";
    case PredefinedMethodKind::StringAsMutStr:
        return "__rcomp_builtin_string_as_mut_str";
    case PredefinedMethodKind::StringLen:
        return "__rcomp_builtin_string_len";
    case PredefinedMethodKind::StringAppend:
        return "__rcomp_builtin_string_append";
    case PredefinedMethodKind::StrLen:
        return "__rcomp_builtin_str_len";
    }

    return std::nullopt;
}

} // namespace semantic
