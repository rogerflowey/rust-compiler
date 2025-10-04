#pragma once

#include "common.hpp"
#include "type.hpp"
#include "semantic/hir/hir.hpp"

namespace semantic{
namespace helper {

/* @brief Convert a TypeDef (which is a variant of StructDef*, EnumDef*, Trait*) to a Type
 * @param def The TypeDef to convert
 * @return The corresponding Type
*/
inline Type to_type(const TypeDef& def){
    struct Visitor{
        Type operator()(hir::StructDef* sd) const {
            return Type{StructType{.symbol = sd}};
        }
        Type operator()(hir::EnumDef* ed) const {
            return Type{EnumType{.symbol = ed}};
        }
        Type operator()(hir::Trait* td) const {
            throw std::logic_error("Cannot convert Trait to Type");
        }
    };
}

}
}