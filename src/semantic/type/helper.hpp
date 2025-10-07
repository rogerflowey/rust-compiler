#pragma once

#include "common.hpp"
#include "type.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/utils.hpp"
#include <stdexcept>

namespace semantic{
namespace helper {

/* @brief Convert a TypeDef (which is a variant of StructDef*, EnumDef*, Trait*) to a Type
 * @param def The TypeDef to convert
 * @return The corresponding Type
*/
inline Type to_type(const TypeDef& def){
    return std::visit(Overloaded{
        [](hir::StructDef* sd) -> Type {
            return Type{StructType{.symbol = sd}};
        },
        [](hir::EnumDef* ed) -> Type {
            return Type{EnumType{.symbol = ed}};
        },
        [](hir::Trait*) -> Type {
            throw std::logic_error("Cannot convert Trait to Type");
        }
    }, def);
}

}
}