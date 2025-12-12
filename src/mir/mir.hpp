#pragma once

#include "type/type.hpp"
#include "mir/function_sig.hpp"

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


    // Alias means: not allocating the local
    // instead, treat the temp as the storage of the local
    bool is_alias = false;
    std::variant<std::monostate, TempId, AbiParamIndex> alias_target;
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

/// ValueSource: Reference to a value at MIR semantic level.
/// Represents "use this value" without specifying HOW to obtain it.
/// Can be either a direct value (Operand) or a location in memory (Place).
struct ValueSource {
    std::variant<Operand, Place> source;
};

// Return storage plan: determines where return values are stored (SRET+NRVO handling)
// Computed once during function initialization, encapsulates all return-storage decisions
struct ReturnStoragePlan {
    bool is_sret = false;                          // true if using SRET (indirect return)
    TypeId ret_type = invalid_type_id;             // semantic return type

    // Only valid if is_sret:
    AbiParamIndex sret_abi_index = 0;             // index of SRET param in abi_params
    LocalId return_slot_local = std::numeric_limits<LocalId>::max(); // the local aliased to sret (NRVO or synthetic)
    bool uses_nrvo_local = false;                  // true if return_slot_local is the NRVO local

    // Helper to create the return destination place
    Place return_place() const {
        if (is_sret) {
            Place p;
            p.base = LocalPlace{return_slot_local};
            return p;
        }
        throw std::logic_error("return_place() called on non-SRET plan");
    }
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
    // DEPRECATED: Aggregates should be constructed using InitStatement.
    // This is kept for now but should not be used for new code.
    enum class Kind {
        Struct,
        Array
    };

    Kind kind;
    std::vector<Operand> elements;
};

struct ArrayRepeatRValue {
    // DEPRECATED: Array repeats should be constructed using InitStatement with InitArrayRepeat.
    // This is kept for now but should not be used for new code.
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
    ValueSource src;
};

struct InitLeaf {
    enum class Kind {
        Omitted,   // this slot is initialized by other MIR statements
        Value      // write this value into the slot
    };

    Kind kind = Kind::Omitted;
    ValueSource value;  // meaningful iff kind == Value
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
    std::vector<ValueSource> args;    // args[i] corresponds to sig.params[i]

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
    MirFunctionSig sig;  // ABI and type information
};

struct MirFunction {
    FunctionId id = 0;
    std::string name;
    MirFunctionSig sig;                     // function signature (params, ABI, return info)
    
    std::vector<TypeId> temp_types;
    std::vector<LocalInfo> locals;
    std::vector<BasicBlock> basic_blocks;
    BasicBlockId start_block = 0;

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

    // Convenience methods for common queries
    [[nodiscard]] TypeId semantic_return_type() const {
        return return_type(sig.return_desc);
    }
    
    [[nodiscard]] bool uses_sret() const {
        return is_indirect_sret(sig.return_desc);
    }
    
    [[nodiscard]] bool returns_never() const {
        return is_never(sig.return_desc);
    }
    
    [[nodiscard]] bool returns_void_semantic() const {
        return is_void_semantic(sig.return_desc);
    }
};

struct MirModule {
    std::vector<MirGlobal> globals;
    std::vector<MirFunction> functions;
    std::vector<ExternalFunction> external_functions;
};

} // namespace mir
