#include "riscv/machine_ir.hpp"

namespace riscv {

const char* register_class_name(RegisterClass reg_class) {
    switch (reg_class) {
    case RegisterClass::Gpr64:
        return "gpr64";
    }
    return "<reg-class>";
}

const char* physical_register_name(PhysicalRegister reg) {
    switch (reg) {
    case PhysicalRegister::Zero:
        return "zero";
    case PhysicalRegister::Ra:
        return "ra";
    case PhysicalRegister::Sp:
        return "sp";
    case PhysicalRegister::Gp:
        return "gp";
    case PhysicalRegister::Tp:
        return "tp";
    case PhysicalRegister::T0:
        return "t0";
    case PhysicalRegister::T1:
        return "t1";
    case PhysicalRegister::T2:
        return "t2";
    case PhysicalRegister::T3:
        return "t3";
    case PhysicalRegister::T4:
        return "t4";
    case PhysicalRegister::T5:
        return "t5";
    case PhysicalRegister::T6:
        return "t6";
    case PhysicalRegister::S0:
        return "s0";
    case PhysicalRegister::S1:
        return "s1";
    case PhysicalRegister::S2:
        return "s2";
    case PhysicalRegister::S3:
        return "s3";
    case PhysicalRegister::S4:
        return "s4";
    case PhysicalRegister::S5:
        return "s5";
    case PhysicalRegister::S6:
        return "s6";
    case PhysicalRegister::S7:
        return "s7";
    case PhysicalRegister::S8:
        return "s8";
    case PhysicalRegister::S9:
        return "s9";
    case PhysicalRegister::S10:
        return "s10";
    case PhysicalRegister::S11:
        return "s11";
    case PhysicalRegister::A0:
        return "a0";
    case PhysicalRegister::A1:
        return "a1";
    case PhysicalRegister::A2:
        return "a2";
    case PhysicalRegister::A3:
        return "a3";
    case PhysicalRegister::A4:
        return "a4";
    case PhysicalRegister::A5:
        return "a5";
    case PhysicalRegister::A6:
        return "a6";
    case PhysicalRegister::A7:
        return "a7";
    }
    return "<preg>";
}

bool is_fixed_register(PhysicalRegister reg) {
    switch (reg) {
    case PhysicalRegister::Zero:
    case PhysicalRegister::Ra:
    case PhysicalRegister::Sp:
    case PhysicalRegister::Gp:
    case PhysicalRegister::Tp:
    case PhysicalRegister::S0:
        return true;
    default:
        return false;
    }
}

bool is_callee_saved_register(PhysicalRegister reg) {
    switch (reg) {
    case PhysicalRegister::Ra:
    case PhysicalRegister::S0:
    case PhysicalRegister::S1:
    case PhysicalRegister::S2:
    case PhysicalRegister::S3:
    case PhysicalRegister::S4:
    case PhysicalRegister::S5:
    case PhysicalRegister::S6:
    case PhysicalRegister::S7:
    case PhysicalRegister::S8:
    case PhysicalRegister::S9:
    case PhysicalRegister::S10:
    case PhysicalRegister::S11:
        return true;
    default:
        return false;
    }
}

bool is_allocatable_register(PhysicalRegister reg) {
    switch (reg) {
    case PhysicalRegister::S1:
    case PhysicalRegister::S2:
    case PhysicalRegister::S3:
    case PhysicalRegister::S4:
    case PhysicalRegister::S5:
    case PhysicalRegister::S6:
    case PhysicalRegister::S7:
    case PhysicalRegister::S8:
    case PhysicalRegister::S9:
    case PhysicalRegister::S10:
    case PhysicalRegister::S11:
    case PhysicalRegister::A0:
    case PhysicalRegister::A1:
    case PhysicalRegister::A2:
    case PhysicalRegister::A3:
    case PhysicalRegister::A4:
    case PhysicalRegister::A5:
    case PhysicalRegister::A6:
    case PhysicalRegister::A7:
        return true;
    default:
        return false;
    }
}

bool is_caller_saved_register(PhysicalRegister reg) {
    switch (reg) {
    case PhysicalRegister::A0:
    case PhysicalRegister::A1:
    case PhysicalRegister::A2:
    case PhysicalRegister::A3:
    case PhysicalRegister::A4:
    case PhysicalRegister::A5:
    case PhysicalRegister::A6:
    case PhysicalRegister::A7:
        return true;
    default:
        return false;
    }
}

bool is_reserved_scratch_register(PhysicalRegister reg) {
    switch (reg) {
    case PhysicalRegister::T0:
    case PhysicalRegister::T1:
    case PhysicalRegister::T2:
        return true;
    default:
        return false;
    }
}

bool is_argument_register(PhysicalRegister reg) {
    switch (reg) {
    case PhysicalRegister::A0:
    case PhysicalRegister::A1:
    case PhysicalRegister::A2:
    case PhysicalRegister::A3:
    case PhysicalRegister::A4:
    case PhysicalRegister::A5:
    case PhysicalRegister::A6:
    case PhysicalRegister::A7:
        return true;
    default:
        return false;
    }
}

bool is_abi_visible_register(PhysicalRegister reg) {
    return is_argument_register(reg) || reg == PhysicalRegister::Ra ||
           reg == PhysicalRegister::Sp || reg == PhysicalRegister::S0 ||
           reg == PhysicalRegister::Zero;
}

} // namespace riscv
