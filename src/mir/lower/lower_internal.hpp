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

struct GlobalContext {
	Place make_string_literal_place(const hir::Literal::String& literal);
	std::vector<MirGlobal> take_globals();

private:
	struct StringLiteralKey {
		std::string value;
		bool is_cstyle = false;

		bool operator==(const StringLiteralKey& other) const {
			return is_cstyle == other.is_cstyle && value == other.value;
		}
	};

	struct StringLiteralKeyHasher {
		std::size_t operator()(const StringLiteralKey& key) const noexcept {
			std::size_t hash = std::hash<std::string>{}(key.value);
			std::size_t flag = key.is_cstyle ? 0x9e3779b97f4a7c15ull : 0x7f4a7c159e3779b9ull;
			hash ^= flag + 0x9e3779b9 + (hash << 6) + (hash >> 2);
			return hash;
		}
	};

	std::vector<MirGlobal> globals;
	std::unordered_map<StringLiteralKey, std::size_t, StringLiteralKeyHasher> string_literal_lookup;

	std::size_t intern_string_literal(const hir::Literal::String& literal);
};

struct FunctionLowerer {
	enum class FunctionKind { Function, Method };

	FunctionLowerer(const hir::Function& function,
			   const std::unordered_map<const void*, FunctionId>& id_map,
		   FunctionId id,
		   std::string name,
		   GlobalContext* global_ctx);

	FunctionLowerer(const hir::Method& method,
			 const std::unordered_map<const void*, FunctionId>& id_map,
			 FunctionId id,
			 std::string name,
			 GlobalContext* global_ctx);

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
	const std::unordered_map<const void*, FunctionId>& function_ids;
	GlobalContext* global_context = nullptr;
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
	FunctionId lookup_function_id(const void* key) const;
	Operand emit_call(FunctionId target, TypeId result_type, std::vector<Operand>&& args);
	Operand emit_aggregate(AggregateRValue aggregate, TypeId result_type);
	Operand emit_array_repeat(Operand value, std::size_t count, TypeId result_type);
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
	void collect_function_parameters(const hir::Function& function);
	void collect_method_parameters(const hir::Method& method);
	void append_parameter(const hir::Local* local, TypeId type);
	const hir::Local* resolve_pattern_local(const hir::Pattern& pattern) const;

	LoopContext& push_loop_context(const void* key,
				   BasicBlockId continue_block,
				   BasicBlockId break_block,
				   std::optional<TypeId> break_type);
	LoopContext& lookup_loop_context(const void* key);
	LoopContext pop_loop_context(const void* key);
	void finalize_loop_context(const LoopContext& ctx);

	void lower_block(const hir::Block& hir_block);
	Operand lower_block_expr(const hir::Block& block, TypeId expected_type);
	void lower_statement(const hir::Stmt& stmt);
	void lower_statement_impl(const hir::LetStmt& let_stmt);
	void lower_statement_impl(const hir::ExprStmt& expr_stmt);
	void lower_pattern_store(const hir::Pattern& pattern, Operand value);
	void lower_pattern_store_impl(const hir::BindingDef& binding, const Operand& value);
	void lower_pattern_store_impl(const hir::ReferencePattern& ref_pattern, const Operand& value);

	LocalId require_local_id(const hir::Local* local) const;
	Place make_local_place(const hir::Local* local) const;
	Place make_local_place(LocalId local_id) const;
	LocalId create_synthetic_local(TypeId type, bool is_mutable_reference);
	Operand load_place_value(Place place, TypeId type);
	Operand lower_expr(const hir::Expr& expr);
	Place lower_expr_place(const hir::Expr& expr);
	Place ensure_reference_operand_place(const hir::Expr& operand,
					  const semantic::ExprInfo& operand_info,
					  bool mutable_reference);

	template <typename T>
	Place lower_place_impl(const T& node, const semantic::ExprInfo& info);

	Place lower_place_impl(const hir::Variable& variable, const semantic::ExprInfo& info);
	Place lower_place_impl(const hir::FieldAccess& field_access, const semantic::ExprInfo& info);
	Place lower_place_impl(const hir::Index& index_expr, const semantic::ExprInfo& info);
	Place lower_place_impl(const hir::UnaryOp& unary, const semantic::ExprInfo& info);

	Operand lower_expr_impl(const hir::Literal& literal, const semantic::ExprInfo& info);
	Operand lower_string_literal(const hir::Literal::String& literal, TypeId result_type);
	Operand lower_expr_impl(const hir::StructLiteral& struct_literal, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::ArrayLiteral& array_literal, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::ArrayRepeat& array_repeat, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::Variable& variable, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::ConstUse& const_use, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::StructConst& struct_const, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::EnumVariant& enum_variant, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::FieldAccess& field_access, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::Index& index_expr, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::Cast& cast_expr, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::BinaryOp& binary, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::Assignment& assignment, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::Block& block_expr, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::If& if_expr, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::Loop& loop_expr, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::While& while_expr, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::Break& break_expr, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::Continue& continue_expr, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::Return& return_expr, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::Call& call_expr, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::MethodCall& method_call, const semantic::ExprInfo& info);
	Operand lower_expr_impl(const hir::UnaryOp& unary, const semantic::ExprInfo& info);

	template <typename T>
	Operand lower_expr_impl(const T& unsupported, const semantic::ExprInfo& info);

    Operand emit_unary_value(const hir::UnaryOperator& op,
                             const hir::Expr& operand_expr,
                             TypeId result_type);

	Operand lower_if_expr(const hir::If& if_expr, const semantic::ExprInfo& info);
	Operand lower_short_circuit(const hir::BinaryOp& binary,
				 const semantic::ExprInfo& info,
				 bool is_and);
	Operand lower_loop_expr(const hir::Loop& loop_expr, const semantic::ExprInfo& info);
	Operand lower_while_expr(const hir::While& while_expr, const semantic::ExprInfo& info);
	Operand lower_break_expr(const hir::Break& break_expr);
	Operand lower_continue_expr(const hir::Continue& continue_expr);
	Operand lower_return_expr(const hir::Return& return_expr);
};

template <typename T>
Place FunctionLowerer::lower_place_impl(const T&, const semantic::ExprInfo&) {
	throw std::logic_error("Expression kind is not yet supported as a place in MIR lowering");
}

template <typename T>
Operand FunctionLowerer::lower_expr_impl(const T&, const semantic::ExprInfo&) {
	throw std::logic_error("Expression kind not supported yet in MIR lowering");
}

} // namespace mir::detail
