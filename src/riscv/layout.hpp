#pragma once

#include "semantic/type/type.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace riscv {

struct Layout {
    std::uint32_t size = 0;
    std::uint32_t align = 1;
};

class LayoutError : public std::runtime_error {
public:
    explicit LayoutError(const std::string& message)
        : std::runtime_error(message) {}
};

std::uint32_t align_to(std::uint32_t value, std::uint32_t align);
Layout layout_of(semantic::TypeId type);
std::uint32_t size_of(semantic::TypeId type);
std::uint32_t align_of(semantic::TypeId type);
std::uint32_t field_offset(semantic::TypeId type, std::size_t index);
std::uint32_t array_stride(semantic::TypeId type);

} // namespace riscv
