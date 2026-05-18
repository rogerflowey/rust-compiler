#pragma once

#include "riscv/machine_ir.hpp"

#include <cstdint>
#include <stdexcept>

namespace riscv {

class FrameMaterializeError : public std::runtime_error {
public:
    explicit FrameMaterializeError(const std::string& message)
        : std::runtime_error(message) {}
};

void materialize_frame(MachineFunction& fn);
void materialize_frame(MachineModule& module);
std::int32_t materialized_frame_offset(const MachineFunction& fn, FrameId frame);

} // namespace riscv
