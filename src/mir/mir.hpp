#pragma once

#include "type/type.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace mir {

using TempId = std::uint32_t;
using LocalId = std::uint32_t;
using BasicBlockId = std::uint32_t;
using FunctionId = std::uint32_t;
using TypeId = type::TypeId;
using GlobalId = std::uint32_t;
inline constexpr TypeId invalid_type_id = type::invalid_type_id;

// Forward declarations
struct MirFunction;
struct ExternalFunction;

// Unified function reference for call targets
using FunctionRef = std::variant<MirFunction*, ExternalFunction*>;

struct LocalInfo {
    TypeId type = invalid_type_id;
    std::string debug_name;

    bool is_alias = false;
    std::optional<TempId> alias_temp;
};

struct FunctionParameter {
    LocalId local = 0;
    TypeId type = invalid_type_id;
    std::string name;
};

struct BoolConstant {
    bool value = false;
};

struct IntConstant {
    std::uint64_t value = 0;
    bool is_negative = false;
    bool is_signed = false;
};

struct CharConstant {
    char value = '\0';
};

struct StringConstant {
    std::string data;
    std::size_t length = 0;
    bool is_cstyle = false;
};

struct StringLiteralGlobal {
    StringConstant value;
};

using GlobalValue = std::variant<StringLiteralGlobal>;

struct MirGlobal {
    GlobalValue value;
};

using ConstantValue = std::variant<BoolConstant, IntConstant, CharConstant, StringConstant>;

struct Constant {
    TypeId type = invalid_type_id;
    ConstantValue value;
};

struct Operand {
    std::variant<TempId, Constant> value;
};

struct LocalPlace {
    LocalId id = 0;
};

struct GlobalPlace {
    GlobalId global = 0;
};

struct PointerPlace {
    TempId temp = 0;
};

using PlaceBase = std::variant<LocalPlace, GlobalPlace, PointerPlace>;

struct FieldProjection {
    std::size_t index = 0;
};

struct IndexProjection {
    Operand index;
};

using Projection = std::variant<FieldProjection, IndexProjection>;

struct Place {
    PlaceBase base;
    std::vector<Projection> projections;
};

struct BinaryOpRValue {
    enum class Kind {
        IAdd,
        UAdd,
        ISub,
        USub,
        IMul,
        UMul,
        IDiv,
        UDiv,
        IRem,
        URem,
        BoolAnd,
        BoolOr,
        BitAnd,
        BitXor,
        BitOr,
        Shl,
        ShrLogical,
        ShrArithmetic,
        ICmpEq,
        ICmpNe,
        ICmpLt,
        ICmpLe,
        ICmpGt,
        ICmpGe,
        UCmpEq,
        UCmpNe,
        UCmpLt,
        UCmpLe,
        UCmpGt,
        UCmpGe,
        BoolEq,
        BoolNe
    };

    Kind kind;
    Operand lhs;
    Operand rhs;
};

struct UnaryOpRValue {
    enum class Kind {
        Not,
        Neg,
        Deref
    };

    Kind kind;
    Operand operand;
};

struct RefRValue {
    Place place;
};

struct AggregateRValue {
    enum class Kind {
        Struct,
        Array
    };

    Kind kind;
    std::vector<Operand> elements;
};

struct ArrayRepeatRValue {
    Operand value;
    std::size_t count = 0;
};

struct CastRValue {
    Operand value;
    TypeId target_type = invalid_type_id;
};

struct FieldAccessRValue {
    TempId base = 0;
    std::size_t index = 0;
};

struct ConstantRValue {
    Constant constant;
};

using RValueVariant = std::variant<
    ConstantRValue,
    BinaryOpRValue,
    UnaryOpRValue,
    RefRValue,
    AggregateRValue,
    ArrayRepeatRValue,
    CastRValue,
    FieldAccessRValue
>;

struct RValue {
    RValueVariant value;
};

struct DefineStatement {
    TempId dest = 0;
    RValue rvalue;
};

struct LoadStatement {
    TempId dest = 0;
    Place src;
};

struct AssignStatement {
    Place dest;
    Operand src;
};

struct InitLeaf {
    enum class Kind {
        Omitted,   // this slot is initialized by other MIR statements
        Operand    // write this operand into the slot
    };

    Kind kind = Kind::Omitted;
    Operand operand;  // meaningful iff kind == Operand
};

struct InitStruct {
    // same length and order as canonical struct fields
    std::vector<InitLeaf> fields;
};

struct InitArrayLiteral {
    std::vector<InitLeaf> elements;
};

struct InitArrayRepeat {
    InitLeaf element;
    std::size_t count = 0;
};

struct InitGeneral { // Unused currently
    InitLeaf value;
};

struct InitCopy {
    Place src;
};

using InitPatternVariant =
    std::variant<InitStruct, InitArrayLiteral, InitArrayRepeat, InitGeneral, InitCopy>;

struct InitPattern {
    InitPatternVariant value;
};

struct InitStatement {
    Place dest;
    InitPattern pattern;
};

struct CallTarget {
    enum class Kind { Internal, External };
    Kind kind = Kind::Internal;
    std::uint32_t id = 0;  // FunctionId for Internal, ExternalFunction::Id for External
};

struct CallStatement {
    std::optional<TempId> dest;      // normal value return (non-sret)
    CallTarget target;
    std::vector<Operand> args;

    // If set, callee must write its result into this place (sret-style).
    std::optional<Place> sret_dest;
};

using StatementVariant = std::variant<DefineStatement, LoadStatement, AssignStatement, InitStatement, CallStatement>;

struct Statement {
    StatementVariant value;
};

struct PhiIncoming {
    BasicBlockId block = 0;
    TempId value = 0;
};

struct PhiNode {
    TempId dest = 0;
    std::vector<PhiIncoming> incoming;
};

struct GotoTerminator {
    BasicBlockId target = 0;
};

struct SwitchIntTarget {
    Constant match_value;
    BasicBlockId block = 0;
};

struct SwitchIntTerminator {
    Operand discriminant;
    std::vector<SwitchIntTarget> targets;
    BasicBlockId otherwise = 0;
};

struct ReturnTerminator {
    std::optional<Operand> value;
};

struct UnreachableTerminator {};

using TerminatorVariant = std::variant<GotoTerminator, SwitchIntTerminator, ReturnTerminator, UnreachableTerminator>;

struct Terminator {
    TerminatorVariant value;
};

struct BasicBlock {
    std::vector<PhiNode> phis;
    std::vector<Statement> statements;
    Terminator terminator{UnreachableTerminator{}};
};

struct ExternalFunction {
    using Id = std::uint32_t;
    static constexpr Id invalid_id = std::numeric_limits<Id>::max();

    Id id = invalid_id;
    std::string name;
    std::vector<TypeId> param_types;
    TypeId return_type = invalid_type_id;
};

struct MirFunction {
    FunctionId id = 0;
    std::string name;
    std::vector<FunctionParameter> params;
    std::vector<TypeId> temp_types;
    std::vector<LocalInfo> locals;
    std::vector<BasicBlock> basic_blocks;
    BasicBlockId start_block = 0;
    TypeId return_type = invalid_type_id;

    // SRET support: if uses_sret is true, the callee receives an implicit first
    // parameter (the return destination pointer), and returns void.
    bool uses_sret = false;
    std::optional<TempId> sret_temp;  // temp holding the sret pointer (&return_type)

    [[nodiscard]] TypeId get_temp_type(TempId temp) const {
        if (temp >= temp_types.size()) {
            throw std::out_of_range("Invalid TempId");
        }
        return temp_types[temp];
    }
    [[nodiscard]] const LocalInfo& get_local_info(LocalId local) const {
        if (local >= locals.size()) {
            throw std::out_of_range("Invalid LocalId");
        }
        return locals[local];
    }
    [[nodiscard]] const BasicBlock& get_basic_block(BasicBlockId bb) const {
        if (bb >= basic_blocks.size()) {
            throw std::out_of_range("Invalid BasicBlockId");
        }
        return basic_blocks[bb];
    }


};

struct MirModule {
    std::vector<MirGlobal> globals;
    std::vector<MirFunction> functions;
    std::vector<ExternalFunction> external_functions;
};

} // namespace mir
