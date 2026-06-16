#pragma once

#include "riscv/asm_ir.hpp"
#include "riscv/machine_ir.hpp"
#include "riscv/target.hpp"

#include <stdexcept>

namespace riscv {

class AsmLoweringError : public std::runtime_error {
public:
    explicit AsmLoweringError(const std::string& message)
        : std::runtime_error(message) {}
};

AsmFunction lower_to_asm(const MachineFunction& fn,
                         const TargetConfig& target = rv64_target());
AsmModule lower_functions_to_asm(const MachineModule& module,
                                 const TargetConfig& target = rv64_target());
AsmModule lower_to_asm(const MachineModule& module,
                       const TargetConfig& target = rv64_target());

} // namespace riscv
