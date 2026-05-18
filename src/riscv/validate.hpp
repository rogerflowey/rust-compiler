#pragma once

#include "riscv/machine_ir.hpp"

#include <stdexcept>

namespace riscv {

enum class ValidationStage { PreRegAlloc, PostRegAlloc, PostPhiElim, PostFrameMaterialized };

class ValidationError : public std::runtime_error {
public:
    explicit ValidationError(const std::string& message)
        : std::runtime_error(message) {}
};

void validate_module(const MachineModule& module,
                     ValidationStage stage = ValidationStage::PreRegAlloc);

} // namespace riscv
