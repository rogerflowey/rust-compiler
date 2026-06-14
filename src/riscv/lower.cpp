#include "riscv/lower.hpp"

#include "riscv/layout.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace riscv {
namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

constexpr std::array kArgumentRegisters = {
    PhysicalRegister::A0,
    PhysicalRegister::A1,
    PhysicalRegister::A2,
    PhysicalRegister::A3,
    PhysicalRegister::A4,
    PhysicalRegister::A5,
    PhysicalRegister::A6,
    PhysicalRegister::A7,
};

constexpr std::uint32_t kXLenBytes = 8;

std::vector<PhysicalRegister> call_arg_uses(std::size_t arg_count) {
    arg_count = std::min(arg_count, kArgumentRegisters.size());
    return std::vector<PhysicalRegister>(kArgumentRegisters.begin(),
                                         kArgumentRegisters.begin() +
                                             static_cast<std::ptrdiff_t>(arg_count));
}

std::vector<PhysicalRegister> allocatable_call_defs() {
    return std::vector<PhysicalRegister>(kArgumentRegisters.begin(), kArgumentRegisters.end());
}

bool fits_i32(std::int64_t value) {
    return value >= std::numeric_limits<std::int32_t>::min() &&
           value <= std::numeric_limits<std::int32_t>::max();
}

bool fits_u32(std::int64_t value) {
    return value >= 0 &&
           value <= static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
}

std::int32_t checked_i32(std::int64_t value, std::string_view what) {
    if (!fits_i32(value)) {
        throw LoweringError(std::string(what) + " does not fit in 32-bit immediate");
    }
    return static_cast<std::int32_t>(value);
}

std::int32_t checked_i32(std::uint32_t value, std::string_view what) {
    return checked_i32(static_cast<std::int64_t>(value), what);
}

std::int32_t checked_rv32_word(std::int64_t value, std::string_view what) {
    if (fits_i32(value)) {
        return static_cast<std::int32_t>(value);
    }
    if (fits_u32(value)) {
        return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
    }
    throw LoweringError(std::string(what) + " does not fit in 32-bit immediate");
}

MachineWidth width_for_ssa_class(ir3::SsaClass klass) {
    return klass == ir3::SsaClass::Ptr ? MachineWidth::XLen : MachineWidth::Word;
}

MachineWidth width_for_host_type(semantic::TypeId type) {
    if (const auto klass = ir3::ssa_class_for(type)) {
        return width_for_ssa_class(*klass);
    }
    return MachineWidth::Word;
}

semantic::TypeId array_element_type(semantic::TypeId type) {
    auto* array = type ? std::get_if<semantic::ArrayType>(&type->value) : nullptr;
    if (!array) {
        throw LoweringError("index projection reached Machine IR lowering on a non-array type");
    }
    return array->element_type;
}

bool is_supported_runtime_builtin_symbol(const std::string& callee) {
    static const std::unordered_map<std::string, bool> supported = {
        {"__rcomp_printInt", true},
        {"__rcomp_printlnInt", true},
        {"__rcomp_getInt", true},
        {"__rcomp_exit", true},
    };
    return supported.contains(callee);
}

bool is_unsupported_runtime_builtin_symbol(const std::string& callee) {
    static const std::unordered_map<std::string, bool> unsupported = {
        {"__rcomp_builtin_print", true},
        {"__rcomp_builtin_println", true},
        {"__rcomp_builtin_getString", true},
        {"__rcomp_builtin_i32_to_string", true},
        {"__rcomp_builtin_u32_to_string", true},
        {"__rcomp_builtin_usize_to_string", true},
        {"__rcomp_builtin_anyint_to_string", true},
        {"__rcomp_builtin_anyuint_to_string", true},
        {"__rcomp_builtin_string_as_str", true},
        {"__rcomp_builtin_string_as_mut_str", true},
        {"__rcomp_builtin_string_len", true},
        {"__rcomp_builtin_string_append", true},
        {"__rcomp_builtin_str_len", true},
    };
    return unsupported.contains(callee);
}

std::string lower_callee_symbol(const std::string& callee) {
    if (callee == "printInt") {
        return "__rcomp_printInt";
    }
    if (callee == "printlnInt") {
        return "__rcomp_printlnInt";
    }
    if (callee == "getInt") {
        return "__rcomp_getInt";
    }
    if (callee == "exit") {
        return "__rcomp_exit";
    }
    if (is_supported_runtime_builtin_symbol(callee)) {
        return callee;
    }
    if (is_unsupported_runtime_builtin_symbol(callee)) {
        throw LoweringError("builtin @" + callee +
                            " requires runtime support that the RV64 backend does not "
                            "implement yet");
    }
    return callee;
}

class FunctionLowerer {
public:
    explicit FunctionLowerer(const ir3::Function& source)
        : source_(source) {
        function_.symbol = source.symbol;
        function_.entry_block = source.entry_block;
        function_.next_value = source.next_value;
    }

    MachineFunction lower() {
        create_block_shells();
        declare_slot_frames();
        declare_incoming_arg_frames();
        declare_outgoing_arg_frames();
        lower_blocks();
        lower_phis();
        return std::move(function_);
    }

private:
    struct LoweredPlace {
        Address address;
        semantic::TypeId host_type = semantic::invalid_type_id;
    };

    const ir3::Function& source_;
    MachineFunction function_;
    std::unordered_map<ir3::BlockId, std::size_t> source_block_indices_;
    std::unordered_map<BlockId, std::size_t> machine_block_indices_;
    std::unordered_map<ir3::SlotId, FrameId> slot_frames_;
    std::unordered_map<std::size_t, FrameId> incoming_arg_frames_;
    std::optional<FrameId> outgoing_arg_area_;
    BlockId next_block_id_ = 0;

    MachineBlock& block(BlockId id) {
        return function_.blocks.at(machine_block_indices_.at(id));
    }

    const MachineBlock& block(BlockId id) const {
        return function_.blocks.at(machine_block_indices_.at(id));
    }

    const ir3::BasicBlock& source_block(ir3::BlockId id) const {
        return source_.blocks.at(source_block_indices_.at(id));
    }

    VirtualRegister vreg(ir3::ValueId id) const {
        return VirtualRegister{.id = id};
    }

    VirtualRegister fresh_temp() {
        return VirtualRegister{.id = function_.next_value++};
    }

    void emit(MachineBlock& block_ref, Instruction inst) {
        block_ref.instructions.push_back(std::move(inst));
    }

    FrameId add_frame_object(FrameObject object) {
        object.id = function_.frame_objects.size();
        function_.frame_objects.push_back(std::move(object));
        return function_.frame_objects.back().id;
    }

    BlockId add_machine_block(MachineBlock block_ref) {
        const auto id = block_ref.id;
        machine_block_indices_.emplace(id, function_.blocks.size());
        function_.blocks.push_back(std::move(block_ref));
        return id;
    }

    void create_block_shells() {
        function_.blocks.reserve(source_.blocks.size());
        for (const auto& source_block_ref : source_.blocks) {
            source_block_indices_.emplace(source_block_ref.id, source_block_indices_.size());
            next_block_id_ = std::max(next_block_id_, source_block_ref.id + 1);
            add_machine_block(MachineBlock{
                .id = source_block_ref.id,
                .name = source_block_ref.name,
                .phis = {},
                .instructions = {},
                .terminator = std::nullopt,
            });
        }
    }

    void declare_slot_frames() {
        for (const auto& slot : source_.slots) {
            const auto size = size_of(slot.host_type);
            const auto align = align_of(slot.host_type);
            const auto frame = add_frame_object(FrameObject{
                .kind = FrameObjectKind::LocalSlot,
                .size = size,
                .align = align,
                .host_type = slot.host_type,
                .spill_class = std::nullopt,
                .source_slot = slot.id,
                .debug_name = slot.debug_name,
                .saved_reg = std::nullopt,
                .materialized_offset = std::nullopt,
            });
            slot_frames_.emplace(slot.id, frame);
        }
    }

    void declare_incoming_arg_frames() {
        for (std::size_t i = kArgumentRegisters.size(); i < source_.params.size(); ++i) {
            const auto frame = add_frame_object(FrameObject{
                .kind = FrameObjectKind::IncomingArg,
                .size = kXLenBytes,
                .align = kXLenBytes,
                .host_type = semantic::invalid_type_id,
                .spill_class = std::nullopt,
                .source_slot = std::nullopt,
                .debug_name = "arg" + std::to_string(i) + ".stack",
                .saved_reg = std::nullopt,
                .materialized_offset = std::nullopt,
            });
            incoming_arg_frames_.emplace(i, frame);
        }
    }

    void declare_outgoing_arg_frames() {
        std::size_t max_overflow_args = 0;
        for (const auto& source_block_ref : source_.blocks) {
            for (const auto& inst : source_block_ref.instructions) {
                if (const auto* call = std::get_if<ir3::Call>(&inst)) {
                    if (call->args.size() > kArgumentRegisters.size()) {
                        max_overflow_args =
                            std::max(max_overflow_args,
                                     call->args.size() - kArgumentRegisters.size());
                    }
                }
            }
        }

        if (max_overflow_args == 0) {
            return;
        }

        const auto raw_size = static_cast<std::uint32_t>(max_overflow_args * kXLenBytes);
        const auto rounded_size = align_to(raw_size, 16);
        outgoing_arg_area_ = add_frame_object(FrameObject{
                .kind = FrameObjectKind::OutgoingArg,
                .size = rounded_size,
                .align = 16,
                .host_type = semantic::invalid_type_id,
                .spill_class = std::nullopt,
                .source_slot = std::nullopt,
                .debug_name = "outgoing",
                .saved_reg = std::nullopt,
                .materialized_offset = std::nullopt,
            });
    }

    void lower_blocks() {
        for (const auto& source_block_ref : source_.blocks) {
            auto& dest = block(source_block_ref.id);
            if (source_block_ref.id == function_.entry_block) {
                lower_entry_moves(dest);
            }
            for (const auto& inst : source_block_ref.instructions) {
                lower_instruction(dest, inst);
            }
            dest.terminator = lower_terminator(dest, source_block_ref);
        }
    }

    void lower_entry_moves(MachineBlock& dest) {
        for (std::size_t i = 0; i < source_.params.size(); ++i) {
            const auto param_vreg = vreg(source_.params[i].value.id);
            if (i < kArgumentRegisters.size()) {
                emit(dest,
                     Copy{
                         .dest = param_vreg,
                         .src = kArgumentRegisters[i],
                     });
                continue;
            }

            emit(dest,
                 Load{
                     .dest = param_vreg,
                     .width = MachineWidth::XLen,
                     .address = FrameAddress{
                         .frame = incoming_arg_frames_.at(i),
                         .offset = 0,
                     },
                 });
        }
    }

    void lower_instruction(MachineBlock& dest, const ir3::Instruction& inst) {
        std::visit(
            Overloaded{
                [&](const ir3::IConst& iconst) {
                    emit(dest,
                         Li{
                             .dest = vreg(iconst.result.id),
                             .value = checked_rv32_word(iconst.value, "integer constant"),
                         });
                },
                [&](const ir3::Load& load) {
                    emit(dest,
                         Load{
                             .dest = vreg(load.result.id),
                             .width = width_for_ssa_class(load.result.klass),
                             .address = lower_place_address(dest, load.source).address,
                         });
                },
                [&](const ir3::Store& store) {
                    emit(dest,
                         Store{
                             .address = lower_place_address(dest, store.dest).address,
                             .width = width_for_ssa_class(store.klass),
                             .src = vreg(store.value),
                         });
                },
                [&](const ir3::Copy& copy) {
                    lower_copy(dest, copy);
                },
                [&](const ir3::Borrow& borrow) {
                    lower_borrow(dest, borrow);
                },
                [&](const ir3::Unary& unary) {
                    lower_unary(dest, unary);
                },
                [&](const ir3::Binary& binary) {
                    lower_binary(dest, binary);
                },
                [&](const ir3::Cast& cast) {
                    emit(dest,
                         Copy{
                             .dest = vreg(cast.result.id),
                             .src = vreg(cast.operand),
                         });
                },
                [&](const ir3::Call& call) {
                    lower_call(dest, call);
                },
            },
            inst);
    }

    void lower_copy(MachineBlock& dest, const ir3::Copy& copy) {
        const auto lowered_dest = lower_place_address(dest, copy.dest);
        const auto lowered_source = lower_place_address(dest, copy.source);
        if (ir3::ssa_class_for(copy.dest.host_type)) {
            const auto tmp = fresh_temp();
            emit(dest,
                 Load{
                     .dest = tmp,
                     .width = width_for_host_type(copy.source.host_type),
                     .address = lowered_source.address,
                 });
            emit(dest,
                 Store{
                     .address = lowered_dest.address,
                     .width = width_for_host_type(copy.dest.host_type),
                     .src = tmp,
                 });
            return;
        }

        const auto size = checked_i32(size_of(copy.dest.host_type), "aggregate copy size");
        auto dest_ptr = fresh_temp();
        auto src_ptr = fresh_temp();
        materialize_pointer(dest, lowered_dest.address, dest_ptr);
        materialize_pointer(dest, lowered_source.address, src_ptr);
        auto size_reg = fresh_temp();
        emit(dest, Li{.dest = size_reg, .value = size});
        emit(dest, Copy{.dest = PhysicalRegister::A0, .src = dest_ptr});
        emit(dest, Copy{.dest = PhysicalRegister::A1, .src = src_ptr});
        emit(dest, Copy{.dest = PhysicalRegister::A2, .src = size_reg});
        emit(dest,
             Call{
                 .callee = "__rcomp_memmove",
                 .uses = call_arg_uses(3),
                 .defs = allocatable_call_defs(),
             });
    }

    void lower_borrow(MachineBlock& dest, const ir3::Borrow& borrow) {
        materialize_pointer(dest,
                            lower_place_address(dest, borrow.source).address,
                            vreg(borrow.result.id));
    }

    void lower_unary(MachineBlock& dest, const ir3::Unary& unary) {
        const auto input = vreg(unary.operand);
        const auto result = vreg(unary.result.id);
        switch (unary.op) {
        case ir3::UnaryOp::SNeg:
        case ir3::UnaryOp::UNeg: {
            auto zero = fresh_temp();
            emit(dest, Li{.dest = zero, .value = 0});
            emit(dest,
                 Binary{
                     .dest = result,
                     .op = BinaryOp::Sub,
                     .width = MachineWidth::Word,
                     .lhs = zero,
                     .rhs = input,
                 });
            return;
        }
        case ir3::UnaryOp::BoolNot: {
            auto zero = fresh_temp();
            emit(dest, Li{.dest = zero, .value = 0});
            emit(dest,
                 Compare{
                     .dest = result,
                     .op = CompareOp::Eq,
                     .lhs = input,
                     .rhs = zero,
                 });
            return;
        }
        case ir3::UnaryOp::BitNot: {
            auto all_ones = fresh_temp();
            emit(dest, Li{.dest = all_ones, .value = -1});
            emit(dest,
                 Binary{
                     .dest = result,
                     .op = BinaryOp::Xor,
                     .lhs = input,
                     .rhs = all_ones,
                 });
            return;
        }
        }
        throw LoweringError("unknown IR3 unary operator during Machine IR lowering");
    }

    void lower_binary(MachineBlock& dest, const ir3::Binary& binary) {
        const auto lhs = vreg(binary.lhs);
        const auto rhs = vreg(binary.rhs);
        const auto out = vreg(binary.result.id);
        const auto width = width_for_ssa_class(binary.result.klass);
        switch (binary.op) {
        case ir3::BinaryOp::SAdd:
        case ir3::BinaryOp::UAdd:
            emit(dest, Binary{.dest = out, .op = BinaryOp::Add, .width = width, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::SSub:
        case ir3::BinaryOp::USub:
            emit(dest, Binary{.dest = out, .op = BinaryOp::Sub, .width = width, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::SMul:
        case ir3::BinaryOp::UMul:
            emit(dest, Binary{.dest = out, .op = BinaryOp::Mul, .width = width, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::SDiv:
            emit(dest, Binary{.dest = out, .op = BinaryOp::Div, .width = width, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::UDiv:
            emit(dest, Binary{.dest = out, .op = BinaryOp::DivU, .width = width, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::SRem:
            emit(dest, Binary{.dest = out, .op = BinaryOp::Rem, .width = width, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::URem:
            emit(dest, Binary{.dest = out, .op = BinaryOp::RemU, .width = width, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::BitAnd:
            emit(dest, Binary{.dest = out, .op = BinaryOp::And, .width = width, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::BitXor:
            emit(dest, Binary{.dest = out, .op = BinaryOp::Xor, .width = width, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::BitOr:
            emit(dest, Binary{.dest = out, .op = BinaryOp::Or, .width = width, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::SShl:
        case ir3::BinaryOp::UShl:
            emit(dest, Binary{.dest = out, .op = BinaryOp::Sll, .width = width, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::AShr:
            emit(dest, Binary{.dest = out, .op = BinaryOp::Sra, .width = width, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::LShr:
            emit(dest, Binary{.dest = out, .op = BinaryOp::Srl, .width = width, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::Eq:
            emit(dest, Compare{.dest = out, .op = CompareOp::Eq, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::Ne:
            emit(dest, Compare{.dest = out, .op = CompareOp::Ne, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::SLt:
            emit(dest, Compare{.dest = out, .op = CompareOp::LtS, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::ULt:
            emit(dest, Compare{.dest = out, .op = CompareOp::LtU, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::SGt:
            emit(dest, Compare{.dest = out, .op = CompareOp::GtS, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::UGt:
            emit(dest, Compare{.dest = out, .op = CompareOp::GtU, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::SLe:
            emit(dest, Compare{.dest = out, .op = CompareOp::LeS, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::ULe:
            emit(dest, Compare{.dest = out, .op = CompareOp::LeU, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::SGe:
            emit(dest, Compare{.dest = out, .op = CompareOp::GeS, .lhs = lhs, .rhs = rhs});
            return;
        case ir3::BinaryOp::UGe:
            emit(dest, Compare{.dest = out, .op = CompareOp::GeU, .lhs = lhs, .rhs = rhs});
            return;
        }
        throw LoweringError("unknown IR3 binary operator during Machine IR lowering");
    }

    void lower_call(MachineBlock& dest, const ir3::Call& call) {
        for (std::size_t i = 0; i < call.args.size(); ++i) {
            const auto arg = vreg(call.args[i]);
            if (i < kArgumentRegisters.size()) {
                emit(dest,
                     Copy{
                         .dest = kArgumentRegisters[i],
                         .src = arg,
                     });
                continue;
            }

            emit(dest,
                 Store{
                         .address = FrameAddress{
                         .frame = *outgoing_arg_area_,
                         .offset = checked_i32(static_cast<std::uint32_t>(
                                                   (i - kArgumentRegisters.size()) * kXLenBytes),
                                               "outgoing argument offset"),
                     },
                     .width = MachineWidth::XLen,
                     .src = arg,
                 });
        }

        emit(dest,
             Call{
                 .callee = lower_callee_symbol(call.callee),
                 .uses = call_arg_uses(call.args.size()),
                 .defs = allocatable_call_defs(),
             });

        if (call.result) {
            emit(dest,
                 Copy{
                     .dest = vreg(call.result->id),
                     .src = PhysicalRegister::A0,
                 });
        }
    }

    std::optional<Terminator> lower_terminator(MachineBlock& dest,
                                               const ir3::BasicBlock& source_block_ref) {
        if (!source_block_ref.terminator) {
            throw LoweringError("IR3 block is missing a terminator during Machine IR lowering");
        }

        return std::visit(
            Overloaded{
                [](const ir3::Jump& jump) -> Terminator {
                    return Jump{.target = jump.target};
                },
                [&](const ir3::Branch& branch) -> Terminator {
                    return BranchNonZero{
                        .condition = vreg(branch.condition),
                        .then_block = branch.then_block,
                        .else_block = branch.else_block,
                    };
                },
                [&](const ir3::Return& ret) -> Terminator {
                    if (ret.value) {
                        emit(dest,
                             Copy{
                                 .dest = PhysicalRegister::A0,
                                 .src = vreg(*ret.value),
                             });
                        return Return{.value = PhysicalRegister::A0};
                    }
                    return Return{};
                },
                [](const ir3::Unreachable&) -> Terminator {
                    return Unreachable{};
                },
            },
            *source_block_ref.terminator);
    }

    LoweredPlace lower_place_address(MachineBlock& dest, const ir3::Place& place) {
        semantic::TypeId current_type = std::visit(
            Overloaded{
                [&](const ir3::SlotBase& base) { return source_.slots.at(base.slot).host_type; },
                [&](const ir3::DerefBase& base) { return base.pointee_type; },
            },
            place.base);

        std::optional<FrameId> frame;
        std::optional<VirtualRegister> reg_base;
        std::int32_t offset = 0;
        std::optional<VirtualRegister> materialized_base;

        std::visit(
            Overloaded{
                [&](const ir3::SlotBase& base) {
                    frame = slot_frames_.at(base.slot);
                },
                [&](const ir3::DerefBase& base) {
                    reg_base = vreg(base.ptr);
                },
            },
            place.base);

        auto ensure_base_pointer = [&]() -> VirtualRegister {
            if (materialized_base) {
                return *materialized_base;
            }

            if (frame) {
                const auto temp = fresh_temp();
                emit(dest,
                     FrameAddr{
                         .dest = temp,
                         .frame = *frame,
                         .offset = offset,
                     });
                materialized_base = temp;
                offset = 0;
                return temp;
            }

            if (!reg_base) {
                throw LoweringError("place lowering lost both frame and register bases");
            }

            if (offset == 0) {
                materialized_base = *reg_base;
                return *materialized_base;
            }

            const auto temp = add_constant(dest, *reg_base, offset);
            materialized_base = temp;
            offset = 0;
            return temp;
        };

        for (const auto& projection : place.projections) {
            std::visit(
                Overloaded{
                    [&](const ir3::FieldProjection& field) {
                        offset += checked_i32(field_offset(current_type, field.index),
                                              "field offset");
                        current_type = field.result_type;
                    },
                    [&](const ir3::IndexProjection& index) {
                        auto base_ptr = ensure_base_pointer();
                        auto stride = checked_i32(array_stride(current_type), "array stride");
                        auto index_reg = vreg(index.index);
                        VirtualRegister scaled = index_reg;
                        if (stride != 1) {
                            auto stride_reg = fresh_temp();
                            emit(dest, Li{.dest = stride_reg, .value = stride});
                            scaled = fresh_temp();
                            emit(dest,
                                 Binary{
                                     .dest = scaled,
                                     .op = BinaryOp::Mul,
                                     .width = MachineWidth::Word,
                                     .lhs = index_reg,
                                     .rhs = stride_reg,
                                 });
                        }

                        auto added = fresh_temp();
                        emit(dest,
                             Binary{
                                 .dest = added,
                                 .op = BinaryOp::Add,
                                 .width = MachineWidth::XLen,
                                 .lhs = base_ptr,
                                 .rhs = scaled,
                             });
                        materialized_base = added;
                        reg_base = added;
                        frame.reset();
                        current_type = array_element_type(current_type);
                    },
                },
                projection);
        }

        if (materialized_base) {
            return LoweredPlace{
                .address = RegisterAddress{
                    .base = *materialized_base,
                    .offset = offset,
                },
                .host_type = place.host_type,
            };
        }
        if (frame) {
            return LoweredPlace{
                .address = FrameAddress{
                    .frame = *frame,
                    .offset = offset,
                },
                .host_type = place.host_type,
            };
        }
        if (reg_base) {
            return LoweredPlace{
                .address = RegisterAddress{
                    .base = *reg_base,
                    .offset = offset,
                },
                .host_type = place.host_type,
            };
        }
        throw LoweringError("place lowering did not produce a Machine IR address");
    }

    VirtualRegister add_constant(MachineBlock& dest, RegisterRef base, std::int32_t offset) {
        if (offset == 0) {
            if (const auto* reg = std::get_if<VirtualRegister>(&base)) {
                return *reg;
            }
        }

        auto imm = fresh_temp();
        emit(dest, Li{.dest = imm, .value = offset});
        auto out = fresh_temp();
        emit(dest,
             Binary{
                 .dest = out,
                 .op = BinaryOp::Add,
                 .width = MachineWidth::XLen,
                 .lhs = base,
                 .rhs = imm,
             });
        return out;
    }

    void materialize_pointer(MachineBlock& dest,
                             const Address& address,
                             VirtualRegister out) {
        std::visit(
            Overloaded{
                [&](const FrameAddress& frame_addr) {
                    emit(dest,
                         FrameAddr{
                             .dest = out,
                             .frame = frame_addr.frame,
                             .offset = frame_addr.offset,
                         });
                },
                [&](const RegisterAddress& reg_addr) {
                    if (reg_addr.offset == 0) {
                        emit(dest, Copy{.dest = out, .src = reg_addr.base});
                        return;
                    }
                    auto adjusted = add_constant(dest, reg_addr.base, reg_addr.offset);
                    emit(dest, Copy{.dest = out, .src = adjusted});
                },
            },
            address);
    }

    void lower_phis() {
        for (const auto& source_block_ref : source_.blocks) {
            auto& dest = block(source_block_ref.id);
            for (const auto& phi : source_block_ref.phis) {
                MachinePhi machine_phi;
                machine_phi.dest = vreg(phi.result.id);
                for (const auto& incoming : phi.incoming) {
                    machine_phi.incoming.push_back(MachinePhiIncoming{
                        .pred = incoming.pred,
                        .value = vreg(incoming.value),
                    });
                }
                dest.phis.push_back(std::move(machine_phi));
            }
        }
    }

}; // FunctionLowerer

} // namespace (anonymous)

MachineModule lower_module(const ir3::Module& module) {
    MachineModule machine_module;
    machine_module.functions.reserve(module.functions.size());
    for (const auto& function : module.functions) {
        machine_module.functions.push_back(FunctionLowerer(function).lower());
    }
    return machine_module;
}

} // namespace riscv
