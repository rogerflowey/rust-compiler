#pragma once

#include "ir3/ir3.hpp"
#include "riscv/machine_ir.hpp"

#include <stdexcept>
#include <string>

namespace riscv {

class LoweringError : public std::runtime_error {
public:
    explicit LoweringError(const std::string& message)
        : std::runtime_error(message) {}
};

MachineModule lower_module(const ir3::Module& module);

} // namespace riscv
