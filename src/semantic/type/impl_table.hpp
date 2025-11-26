#pragma once

#include "hir/hir.hpp"
#include "hir/helper.hpp"
#include "semantic/type/type.hpp"
#include "semantic/utils.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace semantic {

struct TypeIdHasher {
  size_t operator()(const TypeId &type_id) const {
    return std::hash<const Type*>()(type_id);
  }
};

class ImplTable {
private:
    struct AssociatedItems {
        std::unordered_map<std::string, hir::Function *> functions;
        std::unordered_map<std::string, hir::ConstDef *> consts;
        std::unordered_map<std::string, hir::Method *> methods;
        std::vector<ast::Identifier> recorded_names;
    };

    std::unordered_map<TypeId, AssociatedItems, TypeIdHasher> items_by_type;

    static void record_name(AssociatedItems &items, const ast::Identifier &name) {
        auto it = std::find_if(items.recorded_names.begin(),
                                                     items.recorded_names.end(),
                                                     [&](const ast::Identifier &existing) {
                                                         return existing.name == name.name;
                                                     });
        if (it == items.recorded_names.end()) {
            items.recorded_names.push_back(name);
        }
    }

    static hir::Method *get_array_len_method();

public:
    ImplTable() = default;

    void add_impl(TypeId type, hir::Impl &impl_symbol);
    hir::Function *lookup_function(TypeId type, const ast::Identifier &name) const;
    hir::ConstDef *lookup_const(TypeId type, const ast::Identifier &name) const;
    hir::Method *lookup_method(TypeId type, const ast::Identifier &name) const;
    bool has_impls(TypeId type) const;
    std::vector<ast::Identifier> get_associated_names(TypeId type) const;
    void add_predefined_method(TypeId type, std::string_view name,
                                                         hir::Method *method);
};

inline void ImplTable::add_impl(TypeId type, hir::Impl &impl_symbol) {
    auto &bucket = items_by_type[type];

    for (auto &item : impl_symbol.items) {
        std::visit(Overloaded{[&](hir::Function &fn) {
                                                         auto name = hir::helper::get_name(fn);
                                                         bucket.functions[name.name] = &fn;
                                                         record_name(bucket, name);
                                                     },
                                                     [&](hir::ConstDef &constant) {
                                                         auto name = hir::helper::get_name(constant);
                                                         bucket.consts[name.name] = &constant;
                                                         record_name(bucket, name);
                                                     },
                                                     [&](hir::Method &method) {
                                                         auto name = hir::helper::get_name(method);
                                                         bucket.methods[name.name] = &method;
                                                         record_name(bucket, name);
                                                     }},
                             item->value);
    }
}

inline hir::Method *ImplTable::get_array_len_method() {
    static hir::Method method = [] {
        hir::Method m{};
        m.name = ast::Identifier("len");
        m.self_param.is_reference = true;
        m.self_param.is_mutable = false;
        m.return_type = hir::TypeAnnotation{get_typeID(Type{PrimitiveKind::USIZE})};
        return m;
    }();
    return &method;
}

inline hir::Function *
ImplTable::lookup_function(TypeId type, const ast::Identifier &name) const {
    auto it = items_by_type.find(type);
    if (it == items_by_type.end()) {
        return nullptr;
    }
    auto fn_it = it->second.functions.find(name.name);
    return fn_it != it->second.functions.end() ? fn_it->second : nullptr;
}

inline hir::ConstDef *
ImplTable::lookup_const(TypeId type, const ast::Identifier &name) const {
    auto it = items_by_type.find(type);
    if (it == items_by_type.end()) {
        return nullptr;
    }
    auto const_it = it->second.consts.find(name.name);
    return const_it != it->second.consts.end() ? const_it->second : nullptr;
}

inline hir::Method *
ImplTable::lookup_method(TypeId type, const ast::Identifier &name) const {
    auto it = items_by_type.find(type);
    if (it != items_by_type.end()) {
        auto method_it = it->second.methods.find(name.name);
        if (method_it != it->second.methods.end()) {
            return method_it->second;
        }
    }

    if (name.name == "len" && std::holds_alternative<ArrayType>(type->value)) {
        return get_array_len_method();
    }

    return nullptr;
}

inline bool ImplTable::has_impls(TypeId type) const {
    return items_by_type.contains(type);
}

inline std::vector<ast::Identifier>
ImplTable::get_associated_names(TypeId type) const {
    auto it = items_by_type.find(type);
    if (it == items_by_type.end()) {
        return {};
    }
    return it->second.recorded_names;
}

inline void ImplTable::add_predefined_method(TypeId type, std::string_view name,
                                                                                         hir::Method *method) {
    auto &bucket = items_by_type[type];
    std::string method_name{name};
    bucket.methods[method_name] = method;
    record_name(bucket, ast::Identifier{method_name});
}

} // namespace semantic