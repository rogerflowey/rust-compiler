#pragma once

#include "riscv/machine_ir.hpp"

#include <stdexcept>
#include <vector>

namespace riscv {

struct PrologueEpiloguePlan {
    bool needs_frame_pointer = false;
    std::vector<PhysicalRegister> saved_registers;
};

class PrologueEpilogueError : public std::runtime_error {
public:
    explicit PrologueEpilogueError(const std::string& message)
        : std::runtime_error(message) {}
};

PrologueEpiloguePlan compute_prologue_epilogue_plan(const MachineFunction& fn);

void insert_prologue_epilogue(MachineFunction& fn);
void insert_prologue_epilogue(MachineModule& module);

} // namespace riscv
