#pragma once

#include "riscv/asm_ir.hpp"
#include "riscv/machine_ir.hpp"

namespace riscv {

struct RuntimeHelperSelection {
    bool print_int = false;
    bool println_int = false;
    bool get_int = false;
    bool exit = false;
};

RuntimeHelperSelection collect_runtime_helpers(const MachineModule& module);
void append_runtime_helpers(AsmModule& module, const RuntimeHelperSelection& helpers);

} // namespace riscv
