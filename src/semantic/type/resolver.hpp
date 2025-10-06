#pragma once
#include "semantic/hir/hir.hpp"
#include "type/type.hpp"
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
    struct Visitor{
        semantic::TypeId operator()(const std::unique_ptr<hir::DefType>& def_type) {
            auto type_def = def_type
        }
    };
    semantic::TypeId resolve_type_node(const hir::TypeNode& type_node){
        
    }
    
};
}