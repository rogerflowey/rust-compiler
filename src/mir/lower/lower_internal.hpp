#pragma once

#include "mir/mir.hpp"
#include "mir/lower/lower_common.hpp"
#include "mir/lower/lower_const.hpp"
#include "mir/lower/lower_result.hpp"

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
        friend class LowerResult;
        enum class FunctionKind { Function, Method };

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
	Operand lower_operand(const hir::Expr& expr);
	Operand make_const_operand(std::uint64_t value, TypeId type, bool is_signed = false);
	std::optional<Operand> lower_expr(const hir::Expr& expr);
	Place lower_expr_place(const hir::Expr& expr);
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

	std::optional<Operand> lower_expr_impl(const hir::Literal& literal, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::StructLiteral& struct_literal, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::ArrayLiteral& array_literal, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::ArrayRepeat& array_repeat, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::Variable& variable, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::ConstUse& const_use, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::StructConst& struct_const, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::EnumVariant& enum_variant, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::FieldAccess& field_access, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::Index& index_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::Cast& cast_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::BinaryOp& binary, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::Assignment& assignment, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::Block& block_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::If& if_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::Loop& loop_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::While& while_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::Break& break_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::Continue& continue_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::Return& return_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::Call& call_expr, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::MethodCall& method_call, const semantic::ExprInfo& info);
	std::optional<Operand> lower_expr_impl(const hir::UnaryOp& unary, const semantic::ExprInfo& info);

	template <typename T>
	std::optional<Operand> lower_expr_impl(const T& unsupported, const semantic::ExprInfo& info);

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

        // === V2 lowering API ===
        LowerResult lower_node(const hir::Expr& expr, std::optional<Place> dest_hint = std::nullopt);
        void lower_stmt_node(const hir::Stmt& stmt);
        Place lower_node_place(const hir::Expr& expr);
        Operand lower_node_operand(const hir::Expr& expr);

        template <typename T>
        LowerResult visit_node(const T& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);

        LowerResult visit_struct_literal(const hir::StructLiteral& node, const semantic::ExprInfo& info, std::optional<Place> dest);
        LowerResult visit_array_literal(const hir::ArrayLiteral& node, const semantic::ExprInfo& info, std::optional<Place> dest);
        LowerResult visit_array_repeat(const hir::ArrayRepeat& node, const semantic::ExprInfo& info, std::optional<Place> dest);
        LowerResult visit_variable(const hir::Variable& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_const_use(const hir::ConstUse& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_struct_const(const hir::StructConst& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_enum_variant(const hir::EnumVariant& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_field_access(const hir::FieldAccess& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_index(const hir::Index& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_cast(const hir::Cast& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_block(const hir::Block& node, const semantic::ExprInfo& info, std::optional<Place> dest);
        LowerResult visit_if(const hir::If& node, const semantic::ExprInfo& info, std::optional<Place> dest);
        LowerResult visit_binary(const hir::BinaryOp& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_literal(const hir::Literal& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_call(const hir::Call& node, const semantic::ExprInfo& info, std::optional<Place> dest);
        LowerResult visit_method_call(const hir::MethodCall& node, const semantic::ExprInfo& info, std::optional<Place> dest);
        LowerResult visit_assignment(const hir::Assignment& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_loop(const hir::Loop& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_while(const hir::While& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_break(const hir::Break& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_continue(const hir::Continue& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_return(const hir::Return& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
        LowerResult visit_unary(const hir::UnaryOp& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint);
	
	// Process call arguments according to callee's ABI signature
};

template <typename T>
Place FunctionLowerer::lower_place_impl(const T&, const semantic::ExprInfo&) {
	throw std::logic_error("Expression kind is not yet supported as a place in MIR lowering");
}

template <typename T>
std::optional<Operand> FunctionLowerer::lower_expr_impl(const T&, const semantic::ExprInfo&) {
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
