#pragma once
#include "semantic/type/type.hpp"
#include <optional>
#include <variant>

namespace semantic{

inline std::optional<TypeId> coerce(const TypeId from, const TypeId to){
    if(!std::holds_alternative<PrimitiveKind>(from->value)||!std::holds_alternative<PrimitiveKind>(to->value)){
        return std::nullopt;
    }
    auto p1 = std::get<PrimitiveKind>(from->value);
    auto p2 = std::get<PrimitiveKind>(to->value);
    if(p1==PrimitiveKind::__ANYINT__){
        return (p2==PrimitiveKind::I32 || p2==PrimitiveKind::ISIZE) ? std::optional<TypeId>(to) : std::nullopt;
    }
    if(p1==PrimitiveKind::__ANYUINT__){
        return (p2==PrimitiveKind::U32 || p2==PrimitiveKind::USIZE || p2==PrimitiveKind::__ANYINT__) ? std::optional<TypeId>(to) : std::nullopt;
    }
    return (p1==p2) ? std::optional<TypeId>(from) : std::nullopt;
};

} 