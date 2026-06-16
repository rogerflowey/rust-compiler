#pragma once

#include <cstdint>
#include <string_view>

namespace riscv {

enum class RiscvXLen { X32, X64 };

struct TargetConfig {
    RiscvXLen xlen = RiscvXLen::X64;
    std::uint32_t xlen_bytes = 8;
    std::string_view march = "rv64gc";
    std::string_view mabi = "lp64d";
};

constexpr TargetConfig rv64_target() {
    return TargetConfig{
        .xlen = RiscvXLen::X64,
        .xlen_bytes = 8,
        .march = "rv64gc",
        .mabi = "lp64d",
    };
}

constexpr TargetConfig rv32_target() {
    return TargetConfig{
        .xlen = RiscvXLen::X32,
        .xlen_bytes = 4,
        .march = "rv32im",
        .mabi = "ilp32",
    };
}

constexpr bool is_rv32(const TargetConfig& target) {
    return target.xlen == RiscvXLen::X32;
}

constexpr bool is_rv64(const TargetConfig& target) {
    return target.xlen == RiscvXLen::X64;
}

} // namespace riscv
