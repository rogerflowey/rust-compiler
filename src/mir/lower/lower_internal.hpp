#pragma once

#include "mir/mir.hpp"
#include "mir/lower/lower_common.hpp"
#include "mir/lower/lower_const.hpp"

#include "semantic/hir/hir.hpp"
#include "semantic/pass/semantic_check/expr_info.hpp"

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

enum class PatternStoreMode {
	Initialize,
	Assign,
};

// Unified value type for pattern stores: either an Operand (normal assign)
// or an RValue (for initializer lowering)
struct PatternValue {
	PatternStoreMode mode = PatternStoreMode::Assign;
	std::variant<Operand, RValue> value;
};

struct FunctionLowerer {
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

	void initialize(FunctionId id, std::string name);
	const hir::Block* get_body() const;
	const std::vector<std::unique_ptr<hir::Local>>& get_locals_vector() const;
	TypeId resolve_return_type() const;
	void init_locals();
	mir::FunctionRef lookup_function(const void* key) const; // NEW: Returns FunctionRef
	std::optional<Operand> emit_call(mir::FunctionRef target, TypeId result_type, std::vector<Operand>&& args);
	Operand emit_aggregate(AggregateRValue aggregate, TypeId result_type);
	Operand emit_array_repeat(Operand value, std::size_t count, TypeId result_type);
	template <typename RValueT>
	Operand emit_rvalue(RValueT rvalue_kind, TypeId result_type);
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
	std::optional<RValue> lower_expr_as_rvalue(const hir::Expr& expr);
	std::optional<RValue> try_lower_pure_rvalue(const hir::Expr& expr,
		const semantic::ExprInfo& info);
	void lower_pattern_store(const hir::Pattern& pattern,
	                         const PatternValue& pval);
	void lower_pattern_store_impl(const hir::BindingDef& binding,
	                              const PatternValue& pval);
	void lower_pattern_store_impl(const hir::ReferencePattern& ref_pattern,
	                              const PatternValue& pval);

	// RValue building helpers - shared between try_lower_pure_rvalue and lower_expr_impl
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
Operand FunctionLowerer::emit_rvalue(RValueT rvalue_kind, TypeId result_type) {
	TempId dest = allocate_temp(result_type);
	RValue rvalue;
	rvalue.value = std::move(rvalue_kind);
	DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
	Statement stmt;
	stmt.value = std::move(define);
	append_statement(std::move(stmt));
	return make_temp_operand(dest);
}

} // namespace mir::detail
