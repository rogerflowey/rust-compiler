#pragma once

#include "riscv/machine_ir.hpp"
#include "riscv/target.hpp"

#include <stdexcept>

namespace riscv {

class CallerSaveError : public std::runtime_error {
public:
    explicit CallerSaveError(const std::string& message) : std::runtime_error(message) {}
};

void preserve_caller_saved(MachineFunction& fn,
                           const TargetConfig& target = rv64_target());
void preserve_caller_saved(MachineModule& module,
                           const TargetConfig& target = rv64_target());

} // namespace riscv
