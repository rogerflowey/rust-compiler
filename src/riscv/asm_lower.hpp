#pragma once

#include "riscv/asm_ir.hpp"
#include "riscv/machine_ir.hpp"

#include <stdexcept>

namespace riscv {

class AsmLoweringError : public std::runtime_error {
public:
    explicit AsmLoweringError(const std::string& message)
        : std::runtime_error(message) {}
};

AsmFunction lower_to_asm(const MachineFunction& fn);
AsmModule lower_functions_to_asm(const MachineModule& module);
AsmModule lower_to_asm(const MachineModule& module);

} // namespace riscv
