#pragma once

#include "ir3/ir3.hpp"
#include "semantic/type/type.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace riscv {

using MachineValueId = std::size_t;
using BlockId = std::size_t;
using FrameId = std::size_t;

enum class RegisterClass { Gpr32 };

enum class PhysicalRegister {
    Zero,
    Ra,
    Sp,
    Gp,
    Tp,
    T0,
    T1,
    T2,
    T3,
    T4,
    T5,
    T6,
    S0,
    S1,
    S2,
    S3,
    S4,
    S5,
    S6,
    S7,
    S8,
    S9,
    S10,
    S11,
    A0,
    A1,
    A2,
    A3,
    A4,
    A5,
    A6,
    A7,
};

enum class FrameBase { None, S0 };

enum class FrameObjectKind {
    LocalSlot,
    IncomingArg,
    OutgoingArg,
    Spill,
    CalleeSave,
    CallerSave,
};

enum class BinaryOp {
    Add,
    Sub,
    Mul,
    Div,
    DivU,
    Rem,
    RemU,
    And,
    Or,
    Xor,
    Sll,
    Srl,
    Sra,
    Slt,
    SltU,
};

enum class CompareOp { Eq, Ne, LtS, LtU, LeS, LeU, GtS, GtU, GeS, GeU };

struct VirtualRegister {
    MachineValueId id = 0;
    RegisterClass reg_class = RegisterClass::Gpr32;

    bool operator==(const VirtualRegister&) const = default;
};

struct SpillRef {
    FrameId frame = 0;
    RegisterClass reg_class = RegisterClass::Gpr32;

    bool operator==(const SpillRef&) const = default;
};

using RegisterRef = std::variant<VirtualRegister, PhysicalRegister, SpillRef>;

struct FrameObject {
    FrameId id = 0;
    FrameObjectKind kind = FrameObjectKind::LocalSlot;
    std::uint32_t size = 0;
    std::uint32_t align = 1;
    semantic::TypeId host_type = semantic::invalid_type_id;
    std::optional<RegisterClass> spill_class;
    std::optional<ir3::SlotId> source_slot;
    std::string debug_name;
    std::optional<PhysicalRegister> saved_reg;
    std::optional<std::int32_t> materialized_offset;
};

struct FrameAddress {
    FrameId frame = 0;
    std::int32_t offset = 0;
};

struct RegisterAddress {
    RegisterRef base;
    std::int32_t offset = 0;
};

using Address = std::variant<FrameAddress, RegisterAddress>;

struct Copy {
    RegisterRef dest;
    RegisterRef src;
};

struct Li {
    RegisterRef dest;
    std::int32_t value = 0;
};

struct Binary {
    RegisterRef dest;
    BinaryOp op = BinaryOp::Add;
    RegisterRef lhs;
    RegisterRef rhs;
};

struct ShiftImm {
    RegisterRef dest;
    BinaryOp op = BinaryOp::Sll;
    RegisterRef lhs;
    std::uint8_t amount = 0;
};

struct Compare {
    RegisterRef dest;
    CompareOp op = CompareOp::Eq;
    RegisterRef lhs;
    RegisterRef rhs;
};

struct FrameAddr {
    RegisterRef dest;
    FrameId frame = 0;
    std::int32_t offset = 0;
};

struct Load {
    RegisterRef dest;
    Address address;
};

struct Store {
    Address address;
    RegisterRef src;
};

struct Call {
    std::string callee;
    std::vector<PhysicalRegister> uses;
    std::vector<PhysicalRegister> defs;
};

using Instruction =
    std::variant<Copy, Li, Binary, ShiftImm, Compare, FrameAddr, Load, Store, Call>;

struct Jump {
    BlockId target = 0;
};

struct BranchNonZero {
    RegisterRef condition;
    BlockId then_block = 0;
    BlockId else_block = 0;
};

struct BranchCond {
    CompareOp op = CompareOp::Eq;
    RegisterRef lhs;
    RegisterRef rhs;
    BlockId then_block = 0;
    BlockId else_block = 0;
};

struct Return {
    std::optional<RegisterRef> value;
};

struct Unreachable {};

using Terminator = std::variant<Jump, BranchNonZero, BranchCond, Return, Unreachable>;

struct MachinePhiIncoming {
    BlockId pred = 0;
    RegisterRef value;
};

struct MachinePhi {
    RegisterRef dest;
    std::vector<MachinePhiIncoming> incoming;
};

struct MachineBlock {
    BlockId id = 0;
    std::string name;
    std::vector<MachinePhi> phis;
    std::vector<Instruction> instructions;
    std::optional<Terminator> terminator;
};

struct MachineFunction {
    std::string symbol;
    std::vector<FrameObject> frame_objects;
    std::vector<MachineBlock> blocks;
    BlockId entry_block = 0;
    MachineValueId next_value = 0;
    std::optional<FrameBase> frame_base;
    std::optional<std::uint32_t> frame_size;
};

struct MachineModule {
    std::vector<MachineFunction> functions;
};

const char* register_class_name(RegisterClass reg_class);
const char* physical_register_name(PhysicalRegister reg);
bool is_fixed_register(PhysicalRegister reg);
bool is_allocatable_register(PhysicalRegister reg);
bool is_callee_saved_register(PhysicalRegister reg);
bool is_caller_saved_register(PhysicalRegister reg);
bool is_reserved_scratch_register(PhysicalRegister reg);
bool is_argument_register(PhysicalRegister reg);
bool is_abi_visible_register(PhysicalRegister reg);

} // namespace riscv
