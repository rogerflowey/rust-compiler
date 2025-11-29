#pragma once

#include "semantic/common.hpp"
#include "semantic/const/evaluator.hpp"
#include "semantic/hir/hir.hpp"
#include "src/utils/error.hpp"
#include "type/type.hpp"
#include <optional>
#include <stdexcept>
#include <variant>

namespace type {
// the type resolver to change hir::TypeNode to TypeId
class TypeResolver {
public:
    TypeId resolve(hir::TypeAnnotation& type_annotation) {
        auto* type_id_ptr = std::get_if<TypeId>(&type_annotation);
        if (type_id_ptr) {
            return *type_id_ptr;
        }
        auto* type_node_ptr = std::get_if<std::unique_ptr<hir::TypeNode>>(&type_annotation);
        if (!type_node_ptr || !*type_node_ptr) {
            throw SemanticError("Type annotation is null", span::Span::invalid());
        }
        TypeId type_id = resolve_type_node(**type_node_ptr);
        type_annotation = type_id; // update the variant to TypeId
        return type_id;
    }

private:
    struct DefVisitor {
        TypeId operator()(const hir::StructDef* def) {
            auto id = TypeContext::get_instance().get_or_register_struct(def);
            return get_typeID(Type{StructType{.id = id}});
        }
        TypeId operator()(const hir::EnumDef* def) {
            auto id = TypeContext::get_instance().get_or_register_enum(def);
            return get_typeID(Type{EnumType{.id = id}});
        }
        TypeId operator()(const hir::Trait*) {
            throw std::logic_error("Trait cannot be used as a concrete type");
        }
    };

    struct Visitor {
        TypeResolver& resolver;
        std::optional<TypeId> operator()(const std::unique_ptr<hir::DefType>& def_type) {
            auto type_def = std::get_if<TypeDef>(&def_type->def);
            if (!type_def) {
                return std::nullopt;
            }
            return std::visit(DefVisitor{}, *type_def);
        }
        std::optional<TypeId> operator()(const std::unique_ptr<hir::PrimitiveType>& prim_type) {
            return get_typeID(Type{static_cast<PrimitiveKind>(prim_type->kind)});
        }
        std::optional<TypeId> operator()(const std::unique_ptr<hir::ArrayType>& array_type) {
            auto element_type_id = resolver.resolve(array_type->element_type);
            auto size = const_eval::evaluate_const_expression(
                *array_type->size, get_typeID(Type{PrimitiveKind::USIZE}));
            if (!size) {
                throw SemanticError("Const value type mismatch for array type", array_type->span);
            }
            if (auto size_uint = std::get_if<UintConst>(&*size)) {
                return get_typeID(Type{ArrayType{
                    .element_type = element_type_id,
                    .size = size_uint->value,
                }});
            }
            throw SemanticError("Const value type mismatch for array type", array_type->span);
        }
        std::optional<TypeId> operator()(const std::unique_ptr<hir::ReferenceType>& ref_type) {
            auto referenced_type_id = resolver.resolve(ref_type->referenced_type);
            return get_typeID(Type{ReferenceType{
                .referenced_type = referenced_type_id,
                .is_mutable = ref_type->is_mutable
            }});
        }
        std::optional<TypeId> operator()(const std::unique_ptr<hir::UnitType>&) {
            return get_typeID(Type{UnitType{}});
        }
    };
    TypeId resolve_type_node(const hir::TypeNode& type_node) {
        auto type_id_opt = std::visit(Visitor{*this}, type_node.value);
        if (!type_id_opt) {
            throw SemanticError("Failed to resolve type node", type_node.span);
        }
        return *type_id_opt;
    }
};
} // namespace type
