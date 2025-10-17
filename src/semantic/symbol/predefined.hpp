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
static hir::StructDef struct_String = hir::StructDef();

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
        },
        nullptr
    };
    return std::make_unique<hir::Pattern>(hir::PatternVariant{std::move(binding)});
}

inline hir::Function make_builtin_function(std::initializer_list<TypeId> param_types,
                                           TypeId return_type) {
    hir::Function fn{};
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
    fn.ast_node = nullptr;

    return fn;
}

inline hir::Method make_builtin_method(bool self_is_reference,
                                       bool self_is_mutable,
                                       std::initializer_list<TypeId> param_types,
                                       TypeId return_type) {
    hir::Method method{};
    method.self_param.is_reference = self_is_reference;
    method.self_param.is_mutable = self_is_mutable;
    method.self_param.ast_node = nullptr;

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
    method.ast_node = nullptr;

    return method;
}

static hir::Function func_print = make_builtin_function({string_ref_type()}, unit_type());
static hir::Function func_println = make_builtin_function({string_ref_type()}, unit_type());
static hir::Function func_printInt = make_builtin_function({i32_type()}, unit_type());
static hir::Function func_printlnInt = make_builtin_function({i32_type()}, unit_type());
static hir::Function func_getString = make_builtin_function(std::initializer_list<TypeId>{}, string_struct_type());
static hir::Function func_getInt = make_builtin_function(std::initializer_list<TypeId>{}, i32_type());
static hir::Function func_exit = make_builtin_function({i32_type()}, unit_type());

static hir::Method method_u32_to_string = make_builtin_method(true, false, std::initializer_list<TypeId>{}, string_struct_type());
static hir::Method method_usize_to_string = make_builtin_method(true, false, std::initializer_list<TypeId>{}, string_struct_type());
static hir::Method method_string_as_str = make_builtin_method(true, false, std::initializer_list<TypeId>{}, string_ref_type());
static hir::Method method_string_as_mut_str = make_builtin_method(true, true, std::initializer_list<TypeId>{}, string_mut_ref_type());
static hir::Method method_string_len = make_builtin_method(true, false, std::initializer_list<TypeId>{}, usize_type());
static hir::Method method_string_append = make_builtin_method(true, true, {string_ref_type()}, unit_type());
static hir::Method method_str_len = make_builtin_method(true, false, std::initializer_list<TypeId>{}, usize_type());

struct PredefinedMethodRegistrar {
    PredefinedMethodRegistrar() {
        insert_predefined_method(u32_type(), "to_string", &method_u32_to_string);
        insert_predefined_method(usize_type(), "to_string", &method_usize_to_string);
        insert_predefined_method(string_struct_type(), "as_str", &method_string_as_str);
        insert_predefined_method(string_struct_type(), "as_mut_str", &method_string_as_mut_str);
        insert_predefined_method(string_struct_type(), "len", &method_string_len);
        insert_predefined_method(string_struct_type(), "append", &method_string_append);
        insert_predefined_method(primitive_string_type(), "len", &method_str_len);
    }
};

static const PredefinedMethodRegistrar predefined_method_registrar{};

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

} // namespace semantic