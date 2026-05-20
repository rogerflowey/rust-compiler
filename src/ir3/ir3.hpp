#pragma once

#include "semantic/type/type.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ir3 {

using ValueId = std::size_t;
using BlockId = std::size_t;
using SlotId = std::size_t;

enum class SsaClass { I32, Ptr };
enum class SlotOrigin { User, Temp };
enum class UnaryOp { SNeg, UNeg, BoolNot, BitNot };
enum class BinaryOp {
    SAdd,
    UAdd,
    SSub,
    USub,
    SMul,
    UMul,
    SDiv,
    UDiv,
    SRem,
    URem,
    BitAnd,
    BitXor,
    BitOr,
    SShl,
    UShl,
    AShr,
    LShr,
    Eq,
    Ne,
    SLt,
    ULt,
    SGt,
    UGt,
    SLe,
    ULe,
    SGe,
    UGe
};
enum class CastOp { I32ToI32, PtrToPtr, I32ToPtr, PtrToI32 };

struct Value {
    ValueId id;
    SsaClass klass;
};

struct Param {
    Value value;
    std::string name;
    semantic::TypeId host_type = semantic::invalid_type_id;
};

struct Slot {
    SlotId id;
    semantic::TypeId host_type = semantic::invalid_type_id;
    bool is_mutable = false;
    std::string debug_name;
    SlotOrigin origin = SlotOrigin::User;
};

struct SlotBase {
    SlotId slot;
};

struct DerefBase {
    ValueId ptr;
    semantic::TypeId pointee_type = semantic::invalid_type_id;
    bool is_mutable = false;
};

struct FieldProjection {
    std::size_t index;
    semantic::TypeId result_type = semantic::invalid_type_id;
};

struct IndexProjection {
    ValueId index;
    semantic::TypeId result_type = semantic::invalid_type_id;
};

using Projection = std::variant<FieldProjection, IndexProjection>;

struct Place {
    std::variant<SlotBase, DerefBase> base;
    std::vector<Projection> projections;
    semantic::TypeId host_type = semantic::invalid_type_id;
    bool is_mutable = false;
};

struct PhiIncoming {
    BlockId pred;
    ValueId value;
};

struct Phi {
    Value result;
    std::vector<PhiIncoming> incoming;
};

struct IConst {
    Value result;
    std::int64_t value;
};

struct Load {
    Value result;
    Place source;
};

struct Store {
    SsaClass klass;
    Place dest;
    ValueId value;
};

struct Copy {
    Place dest;
    Place source;
};

struct Borrow {
    Value result;
    bool is_mutable = false;
    Place source;
};

struct Unary {
    Value result;
    UnaryOp op;
    ValueId operand;
};

struct Binary {
    Value result;
    BinaryOp op;
    ValueId lhs;
    ValueId rhs;
};

struct Cast {
    Value result;
    ValueId operand;
    CastOp op;
};

struct Call {
    std::optional<Value> result;
    std::string callee;
    std::vector<ValueId> args;
};

using Instruction =
    std::variant<IConst, Load, Store, Copy, Borrow, Unary, Binary, Cast, Call>;

struct Jump {
    BlockId target;
};

struct Branch {
    ValueId condition;
    BlockId then_block;
    BlockId else_block;
};

struct Return {
    std::optional<ValueId> value;
};

struct Unreachable {};

using Terminator = std::variant<Jump, Branch, Return, Unreachable>;

struct BasicBlock {
    BlockId id;
    std::string name;
    std::vector<Phi> phis;
    std::vector<Instruction> instructions;
    std::optional<Terminator> terminator;
};

struct Function {
    std::string symbol;
    std::vector<Param> params;
    std::optional<SsaClass> return_class;
    semantic::TypeId source_return_type = semantic::invalid_type_id;
    std::vector<Slot> slots;
    std::vector<BasicBlock> blocks;
    BlockId entry_block = 0;
    ValueId next_value = 0;
};

struct Module {
    std::vector<Function> functions;
};

enum class HostClass { I32, Ptr, Unit, Aggregate, Never };

HostClass classify_host_type(semantic::TypeId type);
std::optional<SsaClass> ssa_class_for(semantic::TypeId type);
std::size_t struct_field_count(semantic::TypeId type);
semantic::TypeId struct_field_type(semantic::TypeId type, std::size_t index);

} // namespace ir3
