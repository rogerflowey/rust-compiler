#pragma once

#include "semantic/type/type.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mir {

using TempId = std::uint32_t;
using LocalId = std::uint32_t;
using BasicBlockId = std::uint32_t;
using FunctionId = std::uint32_t;

struct LocalInfo {
    semantic::TypeId type = semantic::invalid_type_id;
    std::string debug_name;
};

struct BoolConstant {
    bool value = false;
};

struct IntConstant {
    std::uint64_t value = 0;
    bool is_negative = false;
    bool is_signed = false;
};

struct UnitConstant {};

using ConstantValue = std::variant<BoolConstant, IntConstant, UnitConstant>;

struct Constant {
    semantic::TypeId type = semantic::invalid_type_id;
    ConstantValue value;
};

struct Operand {
    std::variant<TempId, Constant> value;
};

struct LocalPlace {
    LocalId id = 0;
};

struct GlobalPlace {
    std::string symbol;
};

struct PointerPlace {
    TempId temp = 0;
};

using PlaceBase = std::variant<LocalPlace, GlobalPlace, PointerPlace>;

struct FieldProjection {
    std::size_t index = 0;
};

struct IndexProjection {
    TempId index = 0;
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

struct CastRValue {
    Operand value;
    semantic::TypeId target_type = semantic::invalid_type_id;
};

struct FieldAccessRValue {
    TempId base = 0;
    std::size_t index = 0;
};

struct IndexAccessRValue {
    TempId base = 0;
    TempId index = 0;
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
    CastRValue,
    FieldAccessRValue,
    IndexAccessRValue
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

struct CallStatement {
    std::optional<TempId> dest;
    FunctionId function = 0;
    std::vector<Operand> args;
};

using StatementVariant = std::variant<DefineStatement, LoadStatement, AssignStatement, CallStatement>;

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
    Terminator terminator{ReturnTerminator{std::nullopt}};
};

struct MirFunction {
    FunctionId id = 0;
    std::string name;
    std::vector<semantic::TypeId> temp_types;
    std::vector<LocalInfo> locals;
    std::vector<BasicBlock> basic_blocks;
    BasicBlockId start_block = 0;
    semantic::TypeId return_type = semantic::invalid_type_id;
};

struct MirModule {
    std::vector<MirFunction> functions;
};

} // namespace mir
