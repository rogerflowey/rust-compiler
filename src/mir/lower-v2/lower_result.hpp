#pragma once

#include "mir/mir.hpp"
#include "semantic/pass/semantic_check/expr_info.hpp"
#include <variant>
#include <optional>

namespace mir::lower_v2 {

// Forward declarations
class FunctionLowerer;

/// LowerResult: Universal adapter for expression lowering results
///
/// The core concept of V2: instead of separate return types (Operand vs Place vs void),
/// we unify all results into LowerResult which knows how to adapt itself to caller needs.
///
/// Kind meanings:
/// - Operand: Value is a scalar/temp in an Operand (register/constant).
///            Returned by: Literals, BinaryOps, Casts, Variables (loaded).
/// - Place: Value is sitting in memory (L-Value).
///          Returned by: Variable access (not loaded), Field access, Indexing.
/// - Written: Value has been written to the destination provided by caller.
///            Returned by: Struct Literals, Arrays, SRET Calls, If-Exprs with dest.
class LowerResult {
public:
    enum class Kind {
        Operand,  // The value is a scalar in an Operand
        Place,    // The value is in memory at a Place
        Written   // The value was written to the provided destination
    };

    // Constructors
    static LowerResult from_operand(Operand op);
    static LowerResult from_place(Place p);
    static LowerResult written();  // "I did what you asked, check your dest"

    // Accessors
    Kind kind() const { return kind_; }
    const Operand& as_operand_unchecked() const;
    const Place& as_place_unchecked() const;

    // === The "Universal Adapters" ===
    // These methods contain the logic previously scattered across lower_init.cpp
    // They enable LowerResult to adapt itself to caller requirements.

    /// "I am a BinaryOp. I need inputs as Values."
    /// - If Kind::Operand -> Returns it.
    /// - If Kind::Place   -> Emits Load(place) -> Temp -> Returns Temp.
    /// - If Kind::Written -> ERROR (Logic error in compiler).
    Operand as_operand(FunctionLowerer& ctx, const semantic::ExprInfo& info);

    /// "I am an Assignment (LHS) or AddressOf(&). I need a memory address."
    /// - If Kind::Place   -> Returns it.
    /// - If Kind::Operand -> Allocates Temp, emits Assign(Temp, Op) -> Returns Temp Place.
    /// - If Kind::Written -> ERROR (Logic error in compiler).
    Place as_place(FunctionLowerer& ctx, const semantic::ExprInfo& info);

    /// "I am a LetStmt. I have a variable `x`. Put the result there."
    /// - If Kind::Written -> No-op (Optimization Success: Copy Elision).
    /// - If Kind::Operand -> Emits Assign(dest, Op).
    /// - If Kind::Place   -> Emits Move or Copy assignment.
    void write_to_dest(FunctionLowerer& ctx, Place dest, const semantic::ExprInfo& info);

private:
    Kind kind_;
    std::variant<std::monostate, Operand, Place> data_;

    LowerResult(Kind k) : kind_(k) {}
    LowerResult(Kind k, Operand op) : kind_(k), data_(op) {}
    LowerResult(Kind k, Place p) : kind_(k), data_(p) {}
};

}  // namespace mir::lower_v2
