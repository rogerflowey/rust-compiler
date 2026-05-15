#pragma once

#include "ir3/ir3.hpp"
#include "semantic/hir/hir.hpp"

#include <stdexcept>
#include <string>

namespace ir3 {

class LoweringError : public std::runtime_error {
public:
    explicit LoweringError(const std::string& message)
        : std::runtime_error(message) {}
};

Module lower_program(const hir::Program& program);

} // namespace ir3
