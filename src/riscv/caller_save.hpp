#pragma once

#include "riscv/machine_ir.hpp"

#include <stdexcept>

namespace riscv {

class CallerSaveError : public std::runtime_error {
public:
    explicit CallerSaveError(const std::string& message) : std::runtime_error(message) {}
};

void preserve_caller_saved(MachineFunction& fn);
void preserve_caller_saved(MachineModule& module);

} // namespace riscv
