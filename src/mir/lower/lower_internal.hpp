#pragma once

#include "mir/mir.hpp"
#include "mir/lower/lower_common.hpp"
#include "mir/lower/lower_const.hpp"

#include "semantic/hir/hir.hpp"
#include "semantic/pass/semantic_check/expr_info.hpp"
#include "type/helper.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mir::detail {

// Forward declaration
struct FunctionLowerer;

/**
 * MIR Lowering Strategy: Unified Destination-Passing Style (DPS)
 *
 * Goal: Eliminate the legacy dichotomy between `lower_expr` and `lower_init`. 
 * Unify them into a single pipeline that naturally optimizes for direct memory 
 * writing (SRET/Aggregates) while simplifying scalar logic.
 *
 * The `LowerResult` Abstraction:
 * The result of lowering ANY expression is encapsulated here. It normalizes the 
 * outcome into three states:
 * 1.  **`Operand`**: A simple value (constant, temporary, or register). (e.g., `1 + 2`)
 * 2.  **`Place`**: An addressable L-value location. (e.g., variable `x`, `x.field`)
 * 3.  **`Written`**: A marker indicating the value has *already* been stored into 
 *     the provided `maybe_dest`.
 *
 * The `maybe_dest` Hint:
 * `lower_expr` accepts an `std::optional<Place> maybe_dest`.
 * -   **Interpretation:** This is a **Strong Suggestion**.
 * -   **Dest-Aware Nodes** (Structs, SRET Calls, If-Exprs) **MUST** attempt to 
 *     write directly to `maybe_dest` to avoid copies. If successful, they return `Written`.
 * -   **Dest-Ignorant Nodes** (BinaryOps, Literals) **SHOULD** ignore the hint and 
 *     return `Operand` or `Place`.
 *
 * Caller Responsibility:
 * The caller *must* ensure the result ends up where needed using 
 * `LowerResult::write_to_dest()`. This helper acts as the **Universal Adapter**: 
 * if the node ignored the hint (returning Operand/Place), this helper generates 
 * the necessary assignment code to bridge the gap.
 */
class LowerResult {
public:
    enum class Kind { Operand, Place, Written };

    // Factory methods
    static LowerResult operand(mir::Operand op);
    static LowerResult place(mir::Place p);
    static LowerResult written();

    // --- Centralized Conversion Helpers ---

    // 1. "I need a value to use in a computation (e.g., a + b)"
    //    - If Operand: returns it.
    //    - If Place: emits Load(place).
    //    - If Written: Throws logic_error (value was consumed by dest).
    mir::Operand as_operand(FunctionLowerer& fl, TypeId type);

    // 2. "I need a memory location (e.g., &x, or LHS of assign)"
    //    - If Place: returns it.
    //    - If Operand: allocates temp, emits Assign(temp, op), returns temp place.
    //    - If Written: Throws logic_error.
    mir::Place as_place(FunctionLowerer& fl, TypeId type);

    // 3. "I need the result to be in THIS specific variable" (The Adapter)
    //    - If Written: No-op (Optimization worked!).
    //    - If Operand: emits Assign(dest, op).
    //    - If Place: emits Assign(dest, Copy(place)).
    void write_to_dest(FunctionLowerer& fl, mir::Place dest, TypeId type);

private:
    Kind kind;
    std::variant<std::monostate, mir::Operand, mir::Place> data;
    
    LowerResult(Kind k, std::variant<std::monostate, mir::Operand, mir::Place> d)
        : kind(k), data(std::move(d)) {}
};

// Mid-layer call representation: unifies function calls and method calls
// Keeps expressions in "expr form" until ABI shaping (no early operand lowering)
struct CallSite {
  mir::FunctionRef target;
  const mir::MirFunctionSig* callee_sig;

  // Always in "expr form" at this layer
  // For function calls: args_exprs = call.args
  // For method calls: args_exprs = [receiver] + mcall.args (receiver at index 0)
  std::vector<const hir::Expr*> args_exprs;

  // Result handling
  std::optional<mir::Place> sret_dest;  // present iff callee return is sret
  mir::TypeId result_type;               // semantic result type
  
  enum class Context { Expr, Init } ctx; // lowering context (expr or init-dest)
};

struct FunctionLowerer {
        enum class FunctionKind { Function, Method };
        
	// Allow LowerResult to access private methods
	friend class LowerResult;

	FunctionLowerer(const hir::Function& function,
		   const std::unordered_map<const void*, mir::FunctionRef>& fn_map,
	   FunctionId id,
	   std::string name);

	FunctionLowerer(const hir::Method& method,
			 const std::unordered_map<const void*, mir::FunctionRef>& fn_map,
			 FunctionId id,
			 std::string name);

	MirFunction lower();

private:
	struct LoopContext {
		BasicBlockId continue_block = 0;
		BasicBlockId break_block = 0;
		std::optional<TypeId> break_type;
		std::optional<TempId> break_result;
		std::vector<PhiIncoming> break_incomings;
		std::vector<BasicBlockId> break_predecessors;
	};

	FunctionKind function_kind = FunctionKind::Function;
	const hir::Function* hir_function = nullptr;
	const hir::Method* hir_method = nullptr;
	const std::unordered_map<const void*, mir::FunctionRef>& function_map;
	MirFunction mir_function;
	std::optional<BasicBlockId> current_block;
	std::vector<bool> block_terminated;
	std::unordered_map<const hir::Local*, LocalId> local_ids;
	std::vector<std::pair<const void*, LoopContext>> loop_stack;
	size_t synthetic_local_counter = 0;

        // Return storage plan - unified representation of where returns go (SRET+NRVO)
        ReturnStoragePlan return_plan;

	void initialize(FunctionId id, std::string name);
	const hir::Block* get_body() const;
	const std::vector<std::unique_ptr<hir::Local>>& get_locals_vector() const;
	TypeId resolve_return_type() const;
        void init_locals();
        const hir::Local* pick_nrvo_local() const;
        ReturnStoragePlan build_return_plan();
        void apply_abi_aliasing(const ReturnStoragePlan& plan);
        mir::FunctionRef lookup_function(const void* key) const; // NEW: Returns FunctionRef
        const MirFunctionSig& get_callee_sig(mir::FunctionRef target) const;  // Extract signature from FunctionRef
	
	// Unified call lowering: single path for function calls, method calls, and init-context calls
	// Handles all ABI parameter kinds and return modes (direct/sret/void/never)
	// Returns: operand result if in expr context and callee returns directly; nullopt otherwise
	std::optional<Operand> lower_callsite(const CallSite& cs);
	
	bool try_lower_init_call(const hir::Call &call_expr, Place dest, TypeId dest_type);
	bool try_lower_init_method_call(const hir::MethodCall &mcall, Place dest, TypeId dest_type);
	Operand emit_aggregate(AggregateRValue aggregate, TypeId result_type);
	Operand emit_array_repeat(Operand value, std::size_t count, TypeId result_type);
	void emit_init_statement(Place dest, InitPattern pattern);
	template <typename RValueT>
	Operand emit_rvalue_to_temp(RValueT rvalue_kind, TypeId result_type);
	BasicBlockId create_block();
	bool block_is_terminated(BasicBlockId id) const;
	BasicBlockId current_block_id() const;
	TempId allocate_temp(TypeId type);
	void append_statement(Statement statement);
	void set_terminator(BasicBlockId id, Terminator terminator);
	void terminate_current_block(Terminator terminator);
	void add_goto_from_current(BasicBlockId target);
	void switch_to_block(BasicBlockId id);
	void branch_on_bool(const Operand& condition, BasicBlockId true_block, BasicBlockId false_block);
	TempId materialize_operand(const Operand& operand, TypeId type);
	Operand make_temp_operand(TempId temp);
	void emit_return(std::optional<Operand> value);
        void collect_parameters();
        void append_self_parameter();
        void append_explicit_parameters(const std::vector<std::unique_ptr<hir::Pattern>>& params,
                                   const std::vector<hir::TypeAnnotation>& annotations);
	void append_parameter(const hir::Local* local, TypeId type);
	const hir::Local* resolve_pattern_local(const hir::Pattern& pattern) const;
	bool is_reachable() const;
	void require_reachable(const char* context) const;

	LoopContext& push_loop_context(const void* key,
				   BasicBlockId continue_block,
				   BasicBlockId break_block,
				   std::optional<TypeId> break_type);
	LoopContext& lookup_loop_context(const void* key);
	LoopContext pop_loop_context(const void* key);
	void finalize_loop_context(const LoopContext& ctx);

	bool lower_block_statements(const hir::Block& block);
	void lower_block(const hir::Block& hir_block);
	std::optional<Operand> lower_block_expr(const hir::Block& block, TypeId expected_type);
        void lower_statement(const hir::Stmt& stmt);
        void lower_statement_impl(const hir::LetStmt& let_stmt);
        void lower_statement_impl(const hir::ExprStmt& expr_stmt);
        void lower_init(const hir::Expr& expr, Place dest, TypeId dest_type);
        bool try_lower_init_outside(const hir::Expr& expr, Place dest, TypeId dest_type);
        void lower_struct_init(const hir::StructLiteral& literal, Place dest, TypeId dest_type);
        void lower_array_literal_init(const hir::ArrayLiteral& array_literal, Place dest, TypeId dest_type);
        void lower_array_repeat_init(const hir::ArrayRepeat& array_repeat, Place dest, TypeId dest_type);
        void lower_let_pattern(const hir::Pattern& pattern, const hir::Expr& init_expr);
        void lower_binding_let(const hir::BindingDef& binding, const hir::Expr& init_expr);
        void lower_reference_let(const hir::ReferencePattern& ref_pattern,
                                 const hir::Expr& init_expr);
	void lower_pattern_from_expr(const hir::Pattern& pattern,
	                              const hir::Expr& expr,
	                              TypeId expr_type);

	std::optional<Operand> try_lower_to_const(const hir::Expr& expr);

	// RValue building helpers for lower_expr_impl
	AggregateRValue build_struct_aggregate(const hir::StructLiteral& struct_literal);
	AggregateRValue build_array_aggregate(const hir::ArrayLiteral& array_literal);
	ArrayRepeatRValue build_array_repeat_rvalue(const hir::ArrayRepeat& array_repeat);
	ConstantRValue build_literal_rvalue(const hir::Literal& lit,
		const semantic::ExprInfo& info);

	LocalId require_local_id(const hir::Local* local) const;
	Place make_local_place(const hir::Local* local) const;
	Place make_local_place(LocalId local_id) const;
	LocalId create_synthetic_local(TypeId type, bool is_mutable_reference);
	Operand load_place_value(Place place, TypeId type);
	
	// === Unified Lowering API ===
	
	// Main Entry Point - replaces old lower_expr, lower_init, lower_operand
	// Accepts an optional destination hint for direct memory writing optimization
	LowerResult lower_expr(const hir::Expr& expr, std::optional<Place> maybe_dest = std::nullopt);
	
	// Helper for L-Value contexts
	// Implementation: return lower_expr(expr, std::nullopt).as_place(*this, type);
	Place lower_place(const hir::Expr& expr);
	
	// === Legacy API (to be removed during refactoring) ===
	Operand lower_operand(const hir::Expr& expr);  // TODO: Remove - use lower_expr(...).as_operand()
	std::optional<Operand> lower_expr_legacy(const hir::Expr& expr);  // Renamed from lower_expr
	Place lower_expr_place(const hir::Expr& expr);  // TODO: Remove - use lower_place
	
	Operand make_const_operand(std::uint64_t value, TypeId type, bool is_signed = false);
	Place ensure_reference_operand_place(const hir::Expr& operand,
					  const semantic::ExprInfo& operand_info,
				  bool mutable_reference);
	Operand expect_operand(std::optional<Operand> value, const char* context);
	TempId materialize_place_base(const hir::Expr& base_expr,
				 const semantic::ExprInfo& base_info);
	Place make_index_place(const hir::Index& index_expr, bool allow_temporary_base);

	template <typename T>
	Place lower_place_impl(const T& node, const semantic::ExprInfo& info);

	Place lower_place_impl(const hir::Variable& variable, const semantic::ExprInfo& info);
	Place lower_place_impl(const hir::FieldAccess& field_access, const semantic::ExprInfo& info);
	Place lower_place_impl(const hir::Index& index_expr, const semantic::ExprInfo& info);
	Place lower_place_impl(const hir::UnaryOp& unary, const semantic::ExprInfo& info);

	// Internal Implementation Template - specialized for each HIR node type
	template <typename T>
	LowerResult lower_expr_impl(const T& node, const semantic::ExprInfo& info, std::optional<Place> maybe_dest);
	
	// === New Unified API Specializations ===
	// Dest-ignorant nodes (scalars and places)
	LowerResult lower_expr_impl(const hir::Literal& literal, const semantic::ExprInfo& info, std::optional<Place> maybe_dest);
	LowerResult lower_expr_impl(const hir::Variable& variable, const semantic::ExprInfo& info, std::optional<Place> maybe_dest);
	LowerResult lower_expr_impl(const hir::FieldAccess& field_access, const semantic::ExprInfo& info, std::optional<Place> maybe_dest);
	LowerResult lower_expr_impl(const hir::Index& index_expr, const semantic::ExprInfo& info, std::optional<Place> maybe_dest);
	LowerResult lower_expr_impl(const hir::Cast& cast_expr, const semantic::ExprInfo& info, std::optional<Place> maybe_dest);
	LowerResult lower_expr_impl(const hir::BinaryOp& binary, const semantic::ExprInfo& info, std::optional<Place> maybe_dest);
	LowerResult lower_expr_impl(const hir::UnaryOp& unary, const semantic::ExprInfo& info, std::optional<Place> maybe_dest);
	LowerResult lower_expr_impl(const hir::ConstUse& const_use, const semantic::ExprInfo& info, std::optional<Place> maybe_dest);
	LowerResult lower_expr_impl(const hir::StructConst& struct_const, const semantic::ExprInfo& info, std::optional<Place> maybe_dest);
	LowerResult lower_expr_impl(const hir::EnumVariant& enum_variant, const semantic::ExprInfo& info, std::optional<Place> maybe_dest);
	
	// === Legacy expr implementations (to be migrated) ===
	std::optional<Operand> lower_expr_impl_legacy(const hir::Literal& literal, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::StructLiteral& struct_literal, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::ArrayLiteral& array_literal, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::ArrayRepeat& array_repeat, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::Variable& variable, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::ConstUse& const_use, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::StructConst& struct_const, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::EnumVariant& enum_variant, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::FieldAccess& field_access, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::Index& index_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::Cast& cast_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::BinaryOp& binary, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::Assignment& assignment, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::Block& block_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::If& if_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::Loop& loop_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::While& while_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::Break& break_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::Continue& continue_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::Return& return_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::Call& call_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::MethodCall& method_call, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl_legacy(const hir::UnaryOp& unary, const semantic::ExprInfo& info);

	template <typename T>
	std::optional<Operand> lower_expr_impl_legacy(const T& unsupported, const semantic::ExprInfo& info);

	std::optional<Operand> lower_if_expr(const hir::If& if_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_short_circuit(const hir::BinaryOp& binary,
				 const semantic::ExprInfo& info,
				 bool is_and);
	std::optional<Operand> lower_loop_expr(const hir::Loop& loop_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_while_expr(const hir::While& while_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_break_expr(const hir::Break& break_expr);
	std::optional<Operand> lower_continue_expr(const hir::Continue& continue_expr);
	std::optional<Operand> lower_return_expr(const hir::Return& return_expr);
	
	// Central return handling: unifies logic for block returns and explicit return statements
	// Takes an optional unique_ptr<Expr> (from HIR) and handles all return types
	void handle_return_value(const std::optional<std::unique_ptr<hir::Expr>>& value_ptr, const char *context);
	
	// Process call arguments according to callee's ABI signature
};

template <typename T>
Place FunctionLowerer::lower_place_impl(const T&, const semantic::ExprInfo&) {
	throw std::logic_error("Expression kind is not yet supported as a place in MIR lowering");
}

// New unified API template
template <typename T>
LowerResult FunctionLowerer::lower_expr_impl(const T&, const semantic::ExprInfo&, std::optional<Place>) {
	throw std::logic_error("Expression kind not supported yet in MIR lowering");
}

// Legacy template (to be removed)
template <typename T>
std::optional<Operand> FunctionLowerer::lower_expr_impl_legacy(const T&, const semantic::ExprInfo&) {
	throw std::logic_error("Expression kind not supported yet in MIR lowering");
}

template <typename RValueT>
Operand FunctionLowerer::emit_rvalue_to_temp(RValueT rvalue_kind, TypeId result_type) {
	TempId dest = allocate_temp(result_type);
	RValue rvalue;
	rvalue.value = std::move(rvalue_kind);
	DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
	Statement stmt;
	stmt.value = std::move(define);
	append_statement(std::move(stmt));
	return make_temp_operand(dest);
}

// Utility helpers for InitLeaf construction
inline InitLeaf make_value_leaf(Operand op) {
	InitLeaf leaf;
	leaf.kind = InitLeaf::Kind::Value;
	leaf.value = ValueSource{op};
	return leaf;
}

inline InitLeaf make_omitted_leaf() {
	InitLeaf leaf;
	leaf.kind = InitLeaf::Kind::Omitted;
	return leaf;
}

} // namespace mir::detail
