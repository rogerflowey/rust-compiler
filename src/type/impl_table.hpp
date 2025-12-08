#pragma once

#include "semantic/hir/hir.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/utils.hpp"
#include "type/type.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace type {

struct TypeIdHasher {
    size_t operator()(const TypeId& type_id) const { return std::hash<TypeId>()(type_id); }
};

class ImplTable {
private:
    struct AssociatedItems {
        std::unordered_map<std::string, hir::Function*> functions;
        std::unordered_map<std::string, hir::ConstDef*> consts;
        std::unordered_map<std::string, hir::Method*> methods;
        std::vector<ast::Identifier> recorded_names;
    };

    std::unordered_map<TypeId, AssociatedItems, TypeIdHasher> items_by_type;

    static void record_name(AssociatedItems& items, const ast::Identifier& name) {
        auto it = std::find_if(items.recorded_names.begin(), items.recorded_names.end(),
                               [&](const ast::Identifier& existing) { return existing.name == name.name; });
        if (it == items.recorded_names.end()) {
            items.recorded_names.push_back(name);
        }
    }

    static hir::Method* get_array_len_method();

public:
    ImplTable() = default;

    void add_impl(TypeId type, hir::Impl& impl_symbol);
    hir::Function* lookup_function(TypeId type, const ast::Identifier& name) const;
    hir::ConstDef* lookup_const(TypeId type, const ast::Identifier& name) const;
    hir::Method* lookup_method(TypeId type, const ast::Identifier& name) const;
    bool has_impls(TypeId type) const;
    std::vector<ast::Identifier> get_associated_names(TypeId type) const;
    void add_predefined_method(TypeId type, std::string_view name, hir::Method* method);
};

inline void ImplTable::add_impl(TypeId type, hir::Impl& impl_symbol) {
    auto& bucket = items_by_type[type];

    for (auto& item : impl_symbol.items) {
        std::visit(Overloaded{[&](hir::Function& fn) {
                                 auto name = hir::helper::get_name(fn);
                                 bucket.functions[name.name] = &fn;
                                 record_name(bucket, name);
                             },
                             [&](hir::ConstDef& constant) {
                                 auto name = hir::helper::get_name(constant);
                                 bucket.consts[name.name] = &constant;
                                 record_name(bucket, name);
                             },
                             [&](hir::Method& method) {
                                 auto name = hir::helper::get_name(method);
                                 bucket.methods[name.name] = &method;
                                 record_name(bucket, name);
                             }},
                   item->value);
    }
}

inline hir::Method* ImplTable::get_array_len_method() {
    static hir::Method method = [] {
        hir::Method m{};
        m.sig.name = ast::Identifier("len");
        m.sig.self_param.is_reference = true;
        m.sig.self_param.is_mutable = false;
        m.sig.return_type = hir::TypeAnnotation{get_typeID(Type{PrimitiveKind::USIZE})};
        m.body = std::nullopt;
        return m;
    }();
    return &method;
}

inline hir::Function* ImplTable::lookup_function(TypeId type, const ast::Identifier& name) const {
    auto it = items_by_type.find(type);
    if (it == items_by_type.end()) {
        return nullptr;
    }
    auto fn_it = it->second.functions.find(name.name);
    return fn_it != it->second.functions.end() ? fn_it->second : nullptr;
}

inline hir::ConstDef* ImplTable::lookup_const(TypeId type, const ast::Identifier& name) const {
    auto it = items_by_type.find(type);
    if (it == items_by_type.end()) {
        return nullptr;
    }
    auto const_it = it->second.consts.find(name.name);
    return const_it != it->second.consts.end() ? const_it->second : nullptr;
}

inline hir::Method* ImplTable::lookup_method(TypeId type, const ast::Identifier& name) const {
    auto it = items_by_type.find(type);
    if (it != items_by_type.end()) {
        auto method_it = it->second.methods.find(name.name);
        if (method_it != it->second.methods.end()) {
            return method_it->second;
        }
    }

    // array type has a predefined method len
    const auto& t = get_type_from_id(type);
    if (std::holds_alternative<ArrayType>(t.value)) {
        return get_array_len_method();
    }
    return nullptr;
}

inline bool ImplTable::has_impls(TypeId type) const { return items_by_type.find(type) != items_by_type.end(); }

inline std::vector<ast::Identifier> ImplTable::get_associated_names(TypeId type) const {
    auto it = items_by_type.find(type);
    if (it == items_by_type.end()) {
        return {};
    }
    return it->second.recorded_names;
}

inline void ImplTable::add_predefined_method(TypeId type, std::string_view name, hir::Method* method) {
    auto& bucket = items_by_type[type];
    bucket.methods.emplace(name, method);
    record_name(bucket, ast::Identifier(std::string{name}));
}

} // namespace type

namespace semantic {
using type::ImplTable;
}
