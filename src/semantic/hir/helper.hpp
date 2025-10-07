#pragma once

#include "ast/common.hpp"
#include "semantic/hir/hir.hpp"

#include <optional>
#include <variant>

namespace hir {

namespace helper {

using NamedItemPtr = std::variant<Function*, StructDef*, EnumDef*, ConstDef*, Trait*>;
// Utility to get the name of an HIR item
struct NameVisitor{
    ast::Identifier operator()(hir::StructDef* sd) const {
        return *sd->ast_node->name;
    };
    ast::Identifier operator()(hir::EnumDef* ed) const {
        return *ed->ast_node->name;
    };
    ast::Identifier operator()(hir::Function* fd) const {
        return *fd->ast_node->name;
    };
    ast::Identifier operator()(hir::ConstDef* cd) const {
        return *cd->ast_node->name;
    };
    ast::Identifier operator()(hir::Trait* td) const {
        return *td->ast_node->name;
    };
};

inline std::optional<NamedItemPtr> to_named_ptr(ItemVariant& item){
    return std::visit([](auto&& arg) -> std::optional<NamedItemPtr> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Impl>) {
            return std::nullopt;
        } else {
            return &arg;
        }
    }, item);
}
inline ast::Identifier get_name(const NamedItemPtr& named_item){
    return std::visit(NameVisitor{}, named_item);
}

} // namespace helper

} // namespace hir

