#pragma once
#include "common.hpp"
#include "const/const.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/const/evaluator.hpp"
#include "type/type.hpp"
#include <optional>
#include <stdexcept>
#include <variant>
namespace semantic{
// the type resolver to change hir::TypeNode to TypeId
class TypeResolver{
public:
    semantic::TypeId resolve(hir::TypeAnnotation& type_annotation){
        auto* type_id_ptr = std::get_if<semantic::TypeId>(&type_annotation);
        if(type_id_ptr){
            return *type_id_ptr;
        }
        auto* type_node_ptr = std::get_if<std::unique_ptr<hir::TypeNode>>(&type_annotation);
        if(!type_node_ptr || !*type_node_ptr){
            throw std::runtime_error("Type annotation is null");
        }
        auto type_id = resolve_type_node(**type_node_ptr);
        type_annotation = type_id; // update the variant to TypeId
        return type_id;
    }

private:
    struct DefVisitor{
        TypeId operator()(const hir::StructDef* def){
            return get_typeID(Type{StructType{
                .symbol = def
            }});
        }
        TypeId operator()(const hir::EnumDef* def){
            return get_typeID(Type{EnumType{
                .symbol = def
            }});
        }
        TypeId operator()(const hir::Trait* ){
            throw std::logic_error("Trait cannot be used as a concrete type");
        }
    };

    struct Visitor{
        TypeResolver& resolver;
        std::optional<TypeId> operator()(const std::unique_ptr<hir::DefType>& def_type) {
            auto type_def = std::get_if<TypeDef>(&def_type->def);
            if(!type_def){
                return std::nullopt;
            }
            return std::visit(DefVisitor{},*type_def);
        }
        std::optional<TypeId> operator()(const std::unique_ptr<hir::PrimitiveType>& prim_type){
            return get_typeID(Type{static_cast<PrimitiveKind>(prim_type->kind)});
        }
        std::optional<TypeId> operator()(const std::unique_ptr<hir::ArrayType>& array_type){
            auto element_type_id = resolver.resolve(array_type->element_type);
            auto size = evaluate_const(*array_type->size);
            if(auto size_uint = std::get_if<UintConst>(&size)){
                return get_typeID(Type{ArrayType{
                    .element_type = element_type_id,
                    .size = size_uint->value,
                }});
            } else{
                throw std::logic_error("Const value type mismatch for array type");
            }
        }
        std::optional<TypeId> operator()(const std::unique_ptr<hir::ReferenceType>& ref_type){
            auto referenced_type_id = resolver.resolve(ref_type->referenced_type);
            return get_typeID(Type{ReferenceType{
                .referenced_type = referenced_type_id,
                .is_mutable = ref_type->is_mutable
            }});
        }
        std::optional<TypeId> operator()(const std::unique_ptr<hir::UnitType>&){
            return get_typeID(Type{UnitType{}});
        }
    };
    TypeId resolve_type_node(const hir::TypeNode& type_node){
        auto type_id_opt = std::visit(Visitor{*this}, type_node.value);
        if(!type_id_opt){
            throw std::runtime_error("Failed to resolve type node");
        }
        return *type_id_opt;
    }
    
};
}