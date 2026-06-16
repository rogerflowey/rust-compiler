#include "riscv/layout.hpp"

#include "semantic/hir/hir.hpp"

#include <algorithm>
#include <variant>

namespace riscv {
namespace {

semantic::TypeId struct_field_type(const hir::StructDef& def, std::size_t index) {
    if (index >= def.fields.size()) {
        throw LayoutError("struct field index out of range during RV64 layout computation");
    }
    if (!def.fields[index].type) {
        throw LayoutError("struct field type is unresolved during RV64 layout computation");
    }
    return *def.fields[index].type;
}

} // namespace

std::uint32_t align_to(std::uint32_t value, std::uint32_t align) {
    if (align <= 1) {
        return value;
    }
    return ((value + align - 1) / align) * align;
}

Layout layout_of(semantic::TypeId type, const TargetConfig& target) {
    if (!type) {
        throw LayoutError("invalid host type during RV64 layout computation");
    }

    return std::visit(
        [&](const auto& value) -> Layout {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, semantic::PrimitiveKind>) {
                switch (value) {
                case semantic::PrimitiveKind::STRING:
                    return Layout{.size = target.xlen_bytes * 2, .align = target.xlen_bytes};
                case semantic::PrimitiveKind::I32:
                case semantic::PrimitiveKind::U32:
                case semantic::PrimitiveKind::ISIZE:
                case semantic::PrimitiveKind::USIZE:
                case semantic::PrimitiveKind::BOOL:
                case semantic::PrimitiveKind::CHAR:
                case semantic::PrimitiveKind::__ANYINT__:
                case semantic::PrimitiveKind::__ANYUINT__:
                    return Layout{.size = 4, .align = 4};
                }
                return Layout{.size = 4, .align = 4};
            } else if constexpr (std::is_same_v<T, semantic::StructType>) {
                if (!value.symbol) {
                    throw LayoutError("anonymous struct type has no layout source");
                }

                std::uint32_t offset = 0;
                std::uint32_t max_align = 1;
                for (std::size_t i = 0; i < value.symbol->fields.size(); ++i) {
                    const auto field = layout_of(struct_field_type(*value.symbol, i), target);
                    offset = align_to(offset, field.align);
                    offset += field.size;
                    max_align = std::max(max_align, field.align);
                }
                return Layout{.size = align_to(offset, max_align), .align = max_align};
            } else if constexpr (std::is_same_v<T, semantic::EnumType>) {
                return Layout{.size = 4, .align = 4};
            } else if constexpr (std::is_same_v<T, semantic::ReferenceType>) {
                return Layout{.size = target.xlen_bytes, .align = target.xlen_bytes};
            } else if constexpr (std::is_same_v<T, semantic::ArrayType>) {
                const auto element = layout_of(value.element_type, target);
                const auto stride = align_to(element.size, element.align);
                return Layout{
                    .size = stride * static_cast<std::uint32_t>(value.size),
                    .align = element.align,
                };
            } else if constexpr (std::is_same_v<T, semantic::UnitType> ||
                                 std::is_same_v<T, semantic::NeverType>) {
                return Layout{.size = 0, .align = 1};
            } else if constexpr (std::is_same_v<T, semantic::UnderscoreType>) {
                throw LayoutError("underscore type reached RV64 layout computation");
            } else {
                return Layout{};
            }
        },
        type->value);
}

std::uint32_t size_of(semantic::TypeId type, const TargetConfig& target) {
    return layout_of(type, target).size;
}

std::uint32_t align_of(semantic::TypeId type, const TargetConfig& target) {
    return layout_of(type, target).align;
}

std::uint32_t field_offset(semantic::TypeId type,
                           std::size_t index,
                           const TargetConfig& target) {
    auto* struct_type = type ? std::get_if<semantic::StructType>(&type->value) : nullptr;
    if (!struct_type || !struct_type->symbol) {
        throw LayoutError("field_offset requires a resolved struct type");
    }
    if (index >= struct_type->symbol->fields.size()) {
        throw LayoutError("struct field index out of range during offset query");
    }

    std::uint32_t offset = 0;
    for (std::size_t i = 0; i < index; ++i) {
        const auto field = layout_of(struct_field_type(*struct_type->symbol, i), target);
        offset = align_to(offset, field.align);
        offset += field.size;
    }

    const auto field = layout_of(struct_field_type(*struct_type->symbol, index), target);
    return align_to(offset, field.align);
}

std::uint32_t array_stride(semantic::TypeId type, const TargetConfig& target) {
    auto* array_type = type ? std::get_if<semantic::ArrayType>(&type->value) : nullptr;
    if (!array_type) {
        throw LayoutError("array_stride requires an array type");
    }
    const auto element = layout_of(array_type->element_type, target);
    return align_to(element.size, element.align);
}

} // namespace riscv
