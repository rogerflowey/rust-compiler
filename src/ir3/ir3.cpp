#include "ir3/ir3.hpp"

namespace ir3 {

HostClass classify_host_type(semantic::TypeId type) {
    if (type == semantic::invalid_type_id) {
        return HostClass::Aggregate;
    }

    if (auto* primitive = std::get_if<semantic::PrimitiveKind>(&type->value)) {
        switch (*primitive) {
        case semantic::PrimitiveKind::I32:
        case semantic::PrimitiveKind::U32:
        case semantic::PrimitiveKind::ISIZE:
        case semantic::PrimitiveKind::USIZE:
        case semantic::PrimitiveKind::BOOL:
        case semantic::PrimitiveKind::CHAR:
        case semantic::PrimitiveKind::__ANYINT__:
        case semantic::PrimitiveKind::__ANYUINT__:
            return HostClass::I32;
        case semantic::PrimitiveKind::STRING:
            return HostClass::Aggregate;
        }
    }

    if (std::holds_alternative<semantic::ReferenceType>(type->value)) {
        return HostClass::Ptr;
    }
    if (std::holds_alternative<semantic::UnitType>(type->value)) {
        return HostClass::Unit;
    }
    if (std::holds_alternative<semantic::NeverType>(type->value)) {
        return HostClass::Never;
    }
    if (std::holds_alternative<semantic::EnumType>(type->value)) {
        return HostClass::I32;
    }
    return HostClass::Aggregate;
}

std::optional<SsaClass> ssa_class_for(semantic::TypeId type) {
    switch (classify_host_type(type)) {
    case HostClass::I32:
        return SsaClass::I32;
    case HostClass::Ptr:
        return SsaClass::Ptr;
    case HostClass::Unit:
    case HostClass::Aggregate:
    case HostClass::Never:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace ir3
