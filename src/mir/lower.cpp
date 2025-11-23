#include "mir/lower.hpp"

#include "mir/lower_common.hpp"
#include "mir/lower_const.hpp"

#include "semantic/hir/helper.hpp"
#include "semantic/type/type.hpp"

#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace mir {
namespace {

using namespace detail;

struct FunctionDescriptor {
	enum class Kind { Function, Method };
	Kind kind = Kind::Function;
	const void* key = nullptr;
	const hir::Function* function = nullptr;
	const hir::Method* method = nullptr;
	std::string name;
	FunctionId id = 0;
};

void add_function_descriptor(const hir::Function& function,
					 const std::string& scope,
					 std::vector<FunctionDescriptor>& out) {
	FunctionDescriptor descriptor;
	descriptor.kind = FunctionDescriptor::Kind::Function;
	descriptor.function = &function;
	descriptor.key = &function;
	descriptor.name = derive_function_name(function, scope);
	out.push_back(std::move(descriptor));
}

void add_method_descriptor(const hir::Method& method,
			       const std::string& scope,
			       std::vector<FunctionDescriptor>& out) {
	FunctionDescriptor descriptor;
	descriptor.kind = FunctionDescriptor::Kind::Method;
	descriptor.method = &method;
	descriptor.key = &method;
	descriptor.name = derive_method_name(method, scope);
	out.push_back(std::move(descriptor));
}

std::vector<FunctionDescriptor> collect_function_descriptors(const hir::Program& program) {
	std::vector<FunctionDescriptor> descriptors;
	for (const auto& item_ptr : program.items) {
		if (!item_ptr) {
			continue;
		}
		if (auto* function = std::get_if<hir::Function>(&item_ptr->value)) {
			add_function_descriptor(*function, std::string{}, descriptors);
			continue;
		}
		if (auto* impl = std::get_if<hir::Impl>(&item_ptr->value)) {
			semantic::TypeId impl_type = hir::helper::get_resolved_type(impl->for_type);
			std::string scope = type_name(impl_type);
			for (const auto& assoc_item : impl->items) {
				if (!assoc_item) {
					continue;
				}
				if (auto* method = std::get_if<hir::Method>(&assoc_item->value)) {
					add_method_descriptor(*method, scope, descriptors);
				} else if (auto* assoc_fn = std::get_if<hir::Function>(&assoc_item->value)) {
					add_function_descriptor(*assoc_fn, scope, descriptors);
				}
			}
		}
	}
	return descriptors;
}

struct FunctionLowerer {
	enum class FunctionKind { Function, Method };

	FunctionLowerer(const hir::Function& function,
				   const std::unordered_map<const void*, FunctionId>& id_map,
				   FunctionId id,
				   std::string name)
		: function_kind(FunctionKind::Function),
		  hir_function(&function),
		  function_ids(id_map) {
		initialize(id, std::move(name));
	}

	FunctionLowerer(const hir::Method& method,
				   const std::unordered_map<const void*, FunctionId>& id_map,
				   FunctionId id,
				   std::string name)
		: function_kind(FunctionKind::Method),
		  hir_method(&method),
		  function_ids(id_map) {
		initialize(id, std::move(name));
	}

	MirFunction lower() {
		const hir::Block* body = get_body();
		if (!body) {
			if (mir_function.return_type != get_unit_type()) {
				throw std::logic_error("Non-unit function missing body during MIR lowering");
			}
			emit_return(std::nullopt);
			return std::move(mir_function);
		}

		lower_block(*body);
		return std::move(mir_function);
	}

private:
	struct LoopContext {
		BasicBlockId continue_block = 0;
		BasicBlockId break_block = 0;
		std::optional<semantic::TypeId> break_type;
		std::optional<TempId> break_result;
		std::vector<PhiIncoming> break_incomings;
		std::vector<BasicBlockId> break_predecessors;
	};

	FunctionKind function_kind = FunctionKind::Function;
	const hir::Function* hir_function = nullptr;
	const hir::Method* hir_method = nullptr;
	const std::unordered_map<const void*, FunctionId>& function_ids;
	MirFunction mir_function;
	std::optional<BasicBlockId> current_block;
	std::vector<bool> block_terminated;
	std::unordered_map<const hir::Local*, LocalId> local_ids;
	std::vector<std::pair<const void*, LoopContext>> loop_stack;

	void initialize(FunctionId id, std::string name) {
		mir_function.id = id;
		mir_function.name = std::move(name);
		mir_function.return_type = resolve_return_type();
		init_locals();
		BasicBlockId entry = create_block();
		current_block = entry;
		mir_function.start_block = entry;
	}

	const hir::Block* get_body() const {
		if (function_kind == FunctionKind::Function) {
			return hir_function && hir_function->body ? hir_function->body.get() : nullptr;
		}
		return hir_method && hir_method->body ? hir_method->body.get() : nullptr;
	}

	const std::vector<std::unique_ptr<hir::Local>>& get_locals_vector() const {
		if (function_kind == FunctionKind::Function) {
			return hir_function->locals;
		}
		return hir_method->locals;
	}

	semantic::TypeId resolve_return_type() const {
		const auto& annotation = (function_kind == FunctionKind::Function)
			? hir_function->return_type
			: hir_method->return_type;
		if (annotation) {
			return hir::helper::get_resolved_type(*annotation);
		}
		return get_unit_type();
	}

	void init_locals() {
		auto register_local = [this](const hir::Local* local_ptr) {
			if (!local_ptr) {
				return;
			}
			if (!local_ptr->type_annotation) {
				throw std::logic_error("Local missing resolved type during MIR lowering");
			}
			LocalId id = static_cast<LocalId>(mir_function.locals.size());
			local_ids.emplace(local_ptr, id);

			LocalInfo info;
			info.type = hir::helper::get_resolved_type(*local_ptr->type_annotation);
			info.debug_name = local_ptr->name.name;
			mir_function.locals.push_back(std::move(info));
		};

		if (function_kind == FunctionKind::Method && hir_method && hir_method->self_local) {
			register_local(hir_method->self_local.get());
		}

		for (const auto& local_ptr : get_locals_vector()) {
			if (local_ptr) {
				register_local(local_ptr.get());
			}
		}
	}

	FunctionId lookup_function_id(const void* key) const {
		auto it = function_ids.find(key);
		if (it == function_ids.end()) {
			throw std::logic_error("Call target not registered during MIR lowering");
		}
		return it->second;
	}

	Operand emit_call(FunctionId target, semantic::TypeId result_type, std::vector<Operand>&& args) {
		bool result_needed = !is_unit_type(result_type) && !is_never_type(result_type);
		std::optional<TempId> dest;
		Operand result = make_unit_operand();
		if (result_needed) {
			TempId temp = allocate_temp(result_type);
			dest = temp;
			result = make_temp_operand(temp);
		}

		CallStatement call_stmt;
		call_stmt.dest = dest;
		call_stmt.function = target;
		call_stmt.args = std::move(args);
		Statement stmt;
		stmt.value = std::move(call_stmt);
		append_statement(std::move(stmt));
		return result;
	}

	Operand emit_aggregate(AggregateRValue aggregate, semantic::TypeId result_type) {
		TempId temp = allocate_temp(result_type);
		RValue rvalue;
		rvalue.value = std::move(aggregate);
		DefineStatement define{.dest = temp, .rvalue = std::move(rvalue)};
		Statement stmt;
		stmt.value = std::move(define);
		append_statement(std::move(stmt));
		return make_temp_operand(temp);
	}

	BasicBlockId create_block() {
		BasicBlockId id = static_cast<BasicBlockId>(mir_function.basic_blocks.size());
		mir_function.basic_blocks.emplace_back();
		block_terminated.push_back(false);
		return id;
	}

	bool block_is_terminated(BasicBlockId id) const {
		return block_terminated.at(id);
	}

	BasicBlockId current_block_id() const {
		if (!current_block) {
			throw std::logic_error("Current block not available");
		}
		return *current_block;
	}

	TempId allocate_temp(semantic::TypeId type) {
		TempId id = static_cast<TempId>(mir_function.temp_types.size());
		mir_function.temp_types.push_back(type);
		return id;
	}

	void append_statement(Statement statement) {
		if (!current_block) {
			return;
		}
		BasicBlockId block_id = *current_block;
		if (block_is_terminated(block_id)) {
			throw std::logic_error("Cannot append statement to terminated block");
		}
		mir_function.basic_blocks[block_id].statements.push_back(std::move(statement));
	}

	void set_terminator(BasicBlockId id, Terminator terminator) {
		if (block_is_terminated(id)) {
			throw std::logic_error("Terminator already set for block");
		}
		mir_function.basic_blocks[id].terminator = std::move(terminator);
		block_terminated[id] = true;
	}

	void terminate_current_block(Terminator terminator) {
		if (!current_block) {
			return;
		}
		set_terminator(*current_block, std::move(terminator));
		current_block.reset();
	}

	void add_goto_from_current(BasicBlockId target) {
		if (!current_block) {
			return;
		}
		if (block_is_terminated(*current_block)) {
			return;
		}
		GotoTerminator go{target};
		terminate_current_block(Terminator{std::move(go)});
	}

	void switch_to_block(BasicBlockId id) {
		current_block = id;
	}

	void branch_on_bool(const Operand& condition, BasicBlockId true_block, BasicBlockId false_block) {
		if (!current_block) {
			return;
		}
		SwitchIntTerminator term;
		term.discriminant = condition;
		term.targets.push_back(SwitchIntTarget{make_bool_constant(true), true_block});
		term.otherwise = false_block;
		terminate_current_block(Terminator{std::move(term)});
	}

	TempId materialize_operand(const Operand& operand, semantic::TypeId type) {
		if (const auto* temp = std::get_if<TempId>(&operand.value)) {
			return *temp;
		}
		if (!current_block) {
			throw std::logic_error("Cannot materialize operand without active block");
		}
		const auto& constant = std::get<Constant>(operand.value);
		if (constant.type != type) {
			throw std::logic_error("Operand type mismatch during materialization");
		}
		TempId dest = allocate_temp(type);
		ConstantRValue const_rvalue{constant};
		RValue rvalue;
		rvalue.value = const_rvalue;
		DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
		Statement stmt;
		stmt.value = std::move(define);
		append_statement(std::move(stmt));
		return dest;
	}

	Operand make_temp_operand(TempId temp) {
		Operand operand;
		operand.value = temp;
		return operand;
	}

	void emit_return(std::optional<Operand> value) {
		if (!current_block) {
			return;
		}
		ReturnTerminator ret{std::move(value)};
		terminate_current_block(Terminator{std::move(ret)});
	}

	LoopContext& push_loop_context(const void* key,
								   BasicBlockId continue_block,
								   BasicBlockId break_block,
								   std::optional<semantic::TypeId> break_type) {
		LoopContext ctx;
		ctx.continue_block = continue_block;
		ctx.break_block = break_block;
		ctx.break_type = break_type;
		if (break_type) {
			ctx.break_result = allocate_temp(*break_type);
		}
		loop_stack.emplace_back(key, std::move(ctx));
		return loop_stack.back().second;
	}

	LoopContext& lookup_loop_context(const void* key) {
		for (auto it = loop_stack.rbegin(); it != loop_stack.rend(); ++it) {
			if (it->first == key) {
				return it->second;
			}
		}
		throw std::logic_error("Loop context not found");
	}

	LoopContext pop_loop_context(const void* key) {
		if (loop_stack.empty() || loop_stack.back().first != key) {
			throw std::logic_error("Loop context stack corrupted");
		}
		LoopContext ctx = std::move(loop_stack.back().second);
		loop_stack.pop_back();
		return ctx;
	}

	void finalize_loop_context(const LoopContext& ctx) {
		if (ctx.break_result) {
			if (ctx.break_incomings.empty()) {
				throw std::logic_error("Loop expression expects value but no break produced one");
			}
			PhiNode phi;
			phi.dest = *ctx.break_result;
			phi.incoming = ctx.break_incomings;
			mir_function.basic_blocks[ctx.break_block].phis.push_back(std::move(phi));
		}
	}

	void lower_block(const hir::Block& hir_block) {
		for (const auto& stmt : hir_block.stmts) {
			if (!current_block) {
				break;
			}
			if (stmt) {
				lower_statement(*stmt);
			}
		}

		if (!current_block) {
			return;
		}

		if (hir_block.final_expr) {
			const auto& expr_ptr = *hir_block.final_expr;
			if (!expr_ptr) {
				emit_return(std::nullopt);
				return;
			}
			Operand value = lower_expr(*expr_ptr);
			emit_return(std::move(value));
			return;
		}

		if (mir_function.return_type == get_unit_type()) {
			emit_return(std::nullopt);
		} else {
			throw std::logic_error("Missing final expression for non-unit function");
		}
	}

	Operand lower_block_expr(const hir::Block& block, semantic::TypeId expected_type) {
		for (const auto& stmt : block.stmts) {
			if (!current_block) {
				break;
			}
			if (stmt) {
				lower_statement(*stmt);
			}
		}

		if (!current_block) {
			return make_unit_operand();
		}

		if (block.final_expr) {
			const auto& expr_ptr = *block.final_expr;
			if (expr_ptr) {
				return lower_expr(*expr_ptr);
			}
			return make_unit_operand();
		}

		if (is_unit_type(expected_type)) {
			return make_unit_operand();
		}

		throw std::logic_error("Block expression missing value");
	}

	void lower_statement(const hir::Stmt& stmt) {
		if (!current_block) {
			return;
		}
		std::visit([this](const auto& node) { lower_statement_impl(node); }, stmt.value);
	}

	void lower_statement_impl(const hir::LetStmt& let_stmt) {
		if (!current_block) {
			return;
		}
		if (!let_stmt.pattern) {
			throw std::logic_error("Let statement missing pattern during MIR lowering");
		}
		if (!let_stmt.initializer) {
			throw std::logic_error("Let statement without initializer not supported in MIR lowering");
		}
		Operand value = lower_expr(*let_stmt.initializer);
		lower_pattern_store(*let_stmt.pattern, value);
	}

	void lower_statement_impl(const hir::ExprStmt& expr_stmt) {
		if (!current_block) {
			return;
		}
		if (expr_stmt.expr) {
			(void)lower_expr(*expr_stmt.expr);
		}
	}

	void lower_pattern_store(const hir::Pattern& pattern, Operand value) {
		std::visit([this, &value](const auto& pat) { lower_pattern_store_impl(pat, value); }, pattern.value);
	}

	void lower_pattern_store_impl(const hir::BindingDef& binding, const Operand& value) {
		hir::Local* local = hir::helper::get_local(binding);
		Place dest = make_local_place(local);
		AssignStatement assign{.dest = std::move(dest), .src = value};
		Statement stmt;
		stmt.value = std::move(assign);
		append_statement(std::move(stmt));
	}

	void lower_pattern_store_impl(const hir::ReferencePattern&, const Operand&) {
		throw std::logic_error("Reference patterns not yet supported in MIR lowering");
	}

	LocalId require_local_id(const hir::Local* local) const {
		if (!local) {
			throw std::logic_error("Local pointer missing during MIR lowering");
		}
		auto it = local_ids.find(local);
		if (it == local_ids.end()) {
			throw std::logic_error("Local not registered during MIR lowering");
		}
		return it->second;
	}

	Place make_local_place(const hir::Local* local) const {
		Place place;
		place.base = LocalPlace{require_local_id(local)};
		return place;
	}

	Operand load_place_value(Place place, semantic::TypeId type) {
		TempId temp = allocate_temp(type);
		LoadStatement load{.dest = temp, .src = std::move(place)};
		Statement stmt;
		stmt.value = std::move(load);
		append_statement(std::move(stmt));
		return make_temp_operand(temp);
	}

	Operand lower_expr(const hir::Expr& expr) {
		semantic::ExprInfo info = hir::helper::get_expr_info(expr);
		return std::visit([this, &info](const auto& node) {
			return lower_expr_impl(node, info);
		}, expr.value);
	}

	Place lower_expr_place(const hir::Expr& expr) {
		semantic::ExprInfo info = hir::helper::get_expr_info(expr);
		if (!info.is_place) {
			throw std::logic_error("Expression is not a place in MIR lowering");
		}
		return std::visit([this, &info](const auto& node) {
			return lower_place_impl(node, info);
		}, expr.value);
	}

	template <typename T>
	Place lower_place_impl(const T&, const semantic::ExprInfo&) {
		throw std::logic_error("Expression kind is not yet supported as a place in MIR lowering");
	}

	Place lower_place_impl(const hir::Variable& variable, const semantic::ExprInfo& info) {
		if (!info.is_place) {
			throw std::logic_error("Variable without place capability encountered during MIR lowering");
		}
		return make_local_place(variable.local_id);
	}

	Place lower_place_impl(const hir::FieldAccess& field_access, const semantic::ExprInfo&) {
		if (!field_access.base) {
			throw std::logic_error("Field access missing base during MIR place lowering");
		}
		semantic::ExprInfo base_info = hir::helper::get_expr_info(*field_access.base);
		if (!base_info.is_place) {
			throw std::logic_error("Field access base is not a place during MIR place lowering");
		}
		Place place = lower_expr_place(*field_access.base);
		std::size_t index = hir::helper::get_field_index(field_access);
		place.projections.push_back(FieldProjection{index});
		return place;
	}

	Place lower_place_impl(const hir::Index& index_expr, const semantic::ExprInfo&) {
		if (!index_expr.base || !index_expr.index) {
			throw std::logic_error("Index expression missing base or index during MIR place lowering");
		}
		semantic::ExprInfo base_info = hir::helper::get_expr_info(*index_expr.base);
		if (!base_info.is_place) {
			throw std::logic_error("Index base is not a place during MIR place lowering");
		}
		Place place = lower_expr_place(*index_expr.base);
		semantic::ExprInfo idx_info = hir::helper::get_expr_info(*index_expr.index);
		Operand idx_operand = lower_expr(*index_expr.index);
		TempId index_temp = materialize_operand(idx_operand, idx_info.type);
		place.projections.push_back(IndexProjection{index_temp});
		return place;
	}

	Place lower_place_impl(const hir::UnaryOp& unary, const semantic::ExprInfo&) {
		if (unary.op != hir::UnaryOp::DEREFERENCE) {
			throw std::logic_error("Only dereference unary ops can be lowered as places");
		}
		if (!unary.rhs) {
			throw std::logic_error("Dereference expression missing operand during MIR place lowering");
		}
		semantic::ExprInfo operand_info = hir::helper::get_expr_info(*unary.rhs);
		Operand pointer_operand = lower_expr(*unary.rhs);
		TempId pointer_temp = materialize_operand(pointer_operand, operand_info.type);
		Place place;
		place.base = PointerPlace{pointer_temp};
		return place;
	}

	Operand lower_expr_impl(const hir::Literal& literal, const semantic::ExprInfo& info) {
		Constant constant = lower_literal(literal, info.type);
		return make_constant_operand(constant);
	}

	Operand lower_expr_impl(const hir::StructLiteral& struct_literal, const semantic::ExprInfo& info) {
		const auto& fields = hir::helper::get_canonical_fields(struct_literal);
		AggregateRValue aggregate;
		aggregate.kind = AggregateRValue::Kind::Struct;
		aggregate.elements.reserve(fields.initializers.size());
		for (const auto& initializer : fields.initializers) {
			if (!initializer) {
				throw std::logic_error("Struct literal field missing during MIR lowering");
			}
			aggregate.elements.push_back(lower_expr(*initializer));
		}
		return emit_aggregate(std::move(aggregate), info.type);
	}

	Operand lower_expr_impl(const hir::ArrayLiteral& array_literal, const semantic::ExprInfo& info) {
		AggregateRValue aggregate;
		aggregate.kind = AggregateRValue::Kind::Array;
		aggregate.elements.reserve(array_literal.elements.size());
		for (const auto& element : array_literal.elements) {
			if (!element) {
				throw std::logic_error("Array literal element missing during MIR lowering");
			}
			aggregate.elements.push_back(lower_expr(*element));
		}
		return emit_aggregate(std::move(aggregate), info.type);
	}

	Operand lower_expr_impl(const hir::ArrayRepeat& array_repeat, const semantic::ExprInfo& info) {
		if (!array_repeat.value) {
			throw std::logic_error("Array repeat missing value during MIR lowering");
		}
		AggregateRValue aggregate;
		aggregate.kind = AggregateRValue::Kind::Array;
		size_t count = hir::helper::get_array_count(array_repeat);
		aggregate.elements.reserve(count);
		Operand value = lower_expr(*array_repeat.value);
		for (size_t i = 0; i < count; ++i) {
			aggregate.elements.push_back(value);
		}
		return emit_aggregate(std::move(aggregate), info.type);
	}

	Operand lower_expr_impl(const hir::Variable& variable, const semantic::ExprInfo& info) {
		return load_place_value(lower_place_impl(variable, info), info.type);
	}

	Operand lower_expr_impl(const hir::ConstUse& const_use, const semantic::ExprInfo& info) {
		if (!const_use.def) {
			throw std::logic_error("Const use missing definition during MIR lowering");
		}
		semantic::TypeId type = info.type;
		if (!type && const_use.def->type) {
			type = hir::helper::get_resolved_type(*const_use.def->type);
		}
		if (!type) {
			throw std::logic_error("Const use missing resolved type during MIR lowering");
		}
		Constant constant = lower_const_definition(*const_use.def, type);
		return make_constant_operand(constant);
	}

	Operand lower_expr_impl(const hir::StructConst& struct_const, const semantic::ExprInfo& info) {
		if (!struct_const.assoc_const) {
			throw std::logic_error("Struct const missing associated const during MIR lowering");
		}
		semantic::TypeId type = info.type;
		if (!type && struct_const.assoc_const->type) {
			type = hir::helper::get_resolved_type(*struct_const.assoc_const->type);
		}
		if (!type) {
			throw std::logic_error("Struct const missing resolved type during MIR lowering");
		}
		Constant constant = lower_const_definition(*struct_const.assoc_const, type);
		return make_constant_operand(constant);
	}

	Operand lower_expr_impl(const hir::EnumVariant& enum_variant, const semantic::ExprInfo& info) {
		semantic::TypeId type = info.type;
		if (!type) {
			if (!enum_variant.enum_def) {
				throw std::logic_error("Enum variant missing enum definition during MIR lowering");
			}
			type = semantic::get_typeID(semantic::Type{semantic::EnumType{enum_variant.enum_def}});
		}
		Constant constant = lower_enum_variant(enum_variant, type);
		return make_constant_operand(constant);
	}

	Operand lower_expr_impl(const hir::FieldAccess& field_access, const semantic::ExprInfo& info) {
		if (info.is_place) {
			return load_place_value(lower_place_impl(field_access, info), info.type);
		}
		if (!field_access.base) {
			throw std::logic_error("Field access missing base during MIR lowering");
		}
		semantic::ExprInfo base_info = hir::helper::get_expr_info(*field_access.base);
		Operand base_operand = lower_expr(*field_access.base);
		TempId base_temp = materialize_operand(base_operand, base_info.type);
		TempId dest = allocate_temp(info.type);
		FieldAccessRValue field_rvalue{.base = base_temp, .index = hir::helper::get_field_index(field_access)};
		RValue rvalue;
		rvalue.value = std::move(field_rvalue);
		DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
		Statement stmt;
		stmt.value = std::move(define);
		append_statement(std::move(stmt));
		return make_temp_operand(dest);
	}

	Operand lower_expr_impl(const hir::Index& index_expr, const semantic::ExprInfo& info) {
		if (info.is_place) {
			return load_place_value(lower_place_impl(index_expr, info), info.type);
		}
		if (!index_expr.base || !index_expr.index) {
			throw std::logic_error("Index expression missing base or index during MIR lowering");
		}
		semantic::ExprInfo base_info = hir::helper::get_expr_info(*index_expr.base);
		Operand base_operand = lower_expr(*index_expr.base);
		TempId base_temp = materialize_operand(base_operand, base_info.type);
		semantic::ExprInfo idx_info = hir::helper::get_expr_info(*index_expr.index);
		Operand idx_operand = lower_expr(*index_expr.index);
		TempId index_temp = materialize_operand(idx_operand, idx_info.type);
		TempId dest = allocate_temp(info.type);
		IndexAccessRValue index_rvalue{.base = base_temp, .index = index_temp};
		RValue rvalue;
		rvalue.value = std::move(index_rvalue);
		DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
		Statement stmt;
		stmt.value = std::move(define);
		append_statement(std::move(stmt));
		return make_temp_operand(dest);
	}

	Operand lower_expr_impl(const hir::Cast& cast_expr, const semantic::ExprInfo& info) {
		if (!cast_expr.expr) {
			throw std::logic_error("Cast expression missing operand during MIR lowering");
		}
		if (!info.type) {
			throw std::logic_error("Cast expression missing resolved type during MIR lowering");
		}
		Operand operand = lower_expr(*cast_expr.expr);
		TempId dest = allocate_temp(info.type);
		CastRValue cast_rvalue{.value = operand, .target_type = info.type};
		RValue rvalue;
		rvalue.value = std::move(cast_rvalue);
		DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
		Statement stmt;
		stmt.value = std::move(define);
		append_statement(std::move(stmt));
		return make_temp_operand(dest);
	}

	Operand lower_expr_impl(const hir::BinaryOp& binary, const semantic::ExprInfo& info) {
		if (binary.op == hir::BinaryOp::AND || binary.op == hir::BinaryOp::OR) {
			return lower_short_circuit(binary, info, binary.op == hir::BinaryOp::AND);
		}

		if (!binary.lhs || !binary.rhs) {
			throw std::logic_error("Binary expression missing operand during MIR lowering");
		}

		semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*binary.lhs);
		semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*binary.rhs);

		Operand lhs = lower_expr(*binary.lhs);
		Operand rhs = lower_expr(*binary.rhs);

		BinaryOpRValue::Kind kind = classify_binary_kind(binary, lhs_info.type, rhs_info.type, info.type);

		TempId dest = allocate_temp(info.type);
		BinaryOpRValue binary_value{.kind = kind, .lhs = lhs, .rhs = rhs};
		RValue rvalue;
		rvalue.value = std::move(binary_value);
		DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};

		Statement stmt;
		stmt.value = std::move(define);
		append_statement(std::move(stmt));

		return make_temp_operand(dest);
	}

	Operand lower_expr_impl(const hir::Assignment& assignment, const semantic::ExprInfo&) {
		if (!assignment.lhs || !assignment.rhs) {
			throw std::logic_error("Assignment missing operands during MIR lowering");
		}
		Place dest = lower_expr_place(*assignment.lhs);
		Operand value = lower_expr(*assignment.rhs);
		AssignStatement assign{.dest = std::move(dest), .src = value};
		Statement stmt;
		stmt.value = std::move(assign);
		append_statement(std::move(stmt));
		return make_unit_operand();
	}

	Operand lower_expr_impl(const hir::Block& block_expr, const semantic::ExprInfo& info) {
		return lower_block_expr(block_expr, info.type);
	}

	Operand lower_expr_impl(const hir::If& if_expr, const semantic::ExprInfo& info) {
		return lower_if_expr(if_expr, info);
	}

	Operand lower_expr_impl(const hir::Loop& loop_expr, const semantic::ExprInfo& info) {
		return lower_loop_expr(loop_expr, info);
	}

	Operand lower_expr_impl(const hir::While& while_expr, const semantic::ExprInfo& info) {
		return lower_while_expr(while_expr, info);
	}

	Operand lower_expr_impl(const hir::Break& break_expr, const semantic::ExprInfo&) {
		return lower_break_expr(break_expr);
	}

	Operand lower_expr_impl(const hir::Continue& continue_expr, const semantic::ExprInfo&) {
		return lower_continue_expr(continue_expr);
	}

	Operand lower_expr_impl(const hir::Return& return_expr, const semantic::ExprInfo&) {
		return lower_return_expr(return_expr);
	}

	Operand lower_expr_impl(const hir::Call& call_expr, const semantic::ExprInfo& info) {
		if (!call_expr.callee) {
			throw std::logic_error("Call expression missing callee during MIR lowering");
		}
		const auto* func_use = std::get_if<hir::FuncUse>(&call_expr.callee->value);
		if (!func_use || !func_use->def) {
			throw std::logic_error("Call expression callee is not a resolved function use");
		}
		std::vector<Operand> args;
		args.reserve(call_expr.args.size());
		for (const auto& arg : call_expr.args) {
			if (!arg) {
				throw std::logic_error("Call argument missing during MIR lowering");
			}
			args.push_back(lower_expr(*arg));
		}
		FunctionId target = lookup_function_id(func_use->def);
		return emit_call(target, info.type, std::move(args));
	}

	Operand lower_expr_impl(const hir::MethodCall& method_call, const semantic::ExprInfo& info) {
		const hir::Method* method_def = hir::helper::get_method_def(method_call);
		if (!method_call.receiver) {
			throw std::logic_error("Method call missing receiver during MIR lowering");
		}
		FunctionId target = lookup_function_id(method_def);
		std::vector<Operand> args;
		args.reserve(method_call.args.size() + 1);
		args.push_back(lower_expr(*method_call.receiver));
		for (const auto& arg : method_call.args) {
			if (!arg) {
				throw std::logic_error("Method call argument missing during MIR lowering");
			}
			args.push_back(lower_expr(*arg));
		}
		return emit_call(target, info.type, std::move(args));
	}

	Operand emit_unary_value(hir::UnaryOp::Op op,
				 const hir::Expr& operand_expr,
				 semantic::TypeId result_type) {
		Operand operand = lower_expr(operand_expr);
		TempId dest = allocate_temp(result_type);
		UnaryOpRValue::Kind kind;
		switch (op) {
			case hir::UnaryOp::NOT: kind = UnaryOpRValue::Kind::Not; break;
			case hir::UnaryOp::NEGATE: kind = UnaryOpRValue::Kind::Neg; break;
			default:
				throw std::logic_error("Unsupported unary op kind for value lowering");
		}
		UnaryOpRValue unary_rvalue{.kind = kind, .operand = std::move(operand)};
		RValue rvalue;
		rvalue.value = std::move(unary_rvalue);
		DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
		Statement stmt;
		stmt.value = std::move(define);
		append_statement(std::move(stmt));
		return make_temp_operand(dest);
	}

	Operand lower_expr_impl(const hir::UnaryOp& unary, const semantic::ExprInfo& info) {
		if (!unary.rhs) {
			throw std::logic_error("Unary expression missing operand during MIR lowering");
		}
		switch (unary.op) {
			case hir::UnaryOp::NOT:
			case hir::UnaryOp::NEGATE:
				return emit_unary_value(unary.op, *unary.rhs, info.type);
			case hir::UnaryOp::REFERENCE:
			case hir::UnaryOp::MUTABLE_REFERENCE: {
				Place place = lower_expr_place(*unary.rhs);
				TempId dest = allocate_temp(info.type);
				RefRValue ref_rvalue{.place = std::move(place)};
				RValue rvalue;
				rvalue.value = std::move(ref_rvalue);
				DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
				Statement stmt;
				stmt.value = std::move(define);
				append_statement(std::move(stmt));
				return make_temp_operand(dest);
			}
			case hir::UnaryOp::DEREFERENCE:
				return load_place_value(lower_place_impl(unary, info), info.type);
		}
		throw std::logic_error("Unhandled unary operator during MIR lowering");
	}

	template <typename T>
	Operand lower_expr_impl(const T&, const semantic::ExprInfo&) {
		throw std::logic_error("Expression kind not supported yet in MIR lowering");
	}

	Operand lower_if_expr(const hir::If& if_expr, const semantic::ExprInfo& info) {
		Operand condition = lower_expr(*if_expr.condition);
		if (!current_block) {
			return make_unit_operand();
		}

		bool has_else = if_expr.else_expr && *if_expr.else_expr;
		if (!has_else && !is_unit_type(info.type)) {
			throw std::logic_error("If expression missing else branch for non-unit type");
		}

		BasicBlockId then_block = create_block();
		std::optional<BasicBlockId> else_block = has_else ? std::optional<BasicBlockId>(create_block()) : std::nullopt;
		BasicBlockId join_block = create_block();

		BasicBlockId false_target = else_block ? *else_block : join_block;
		branch_on_bool(condition, then_block, false_target);

		bool result_needed = !is_unit_type(info.type) && !is_never_type(info.type);
		std::vector<PhiIncoming> phi_incomings;

		switch_to_block(then_block);
		Operand then_value = lower_block_expr(*if_expr.then_block, info.type);
		std::optional<BasicBlockId> then_fallthrough = current_block ? std::optional<BasicBlockId>(*current_block) : std::nullopt;
		if (then_fallthrough && result_needed) {
			TempId value_temp = materialize_operand(then_value, info.type);
			phi_incomings.push_back(PhiIncoming{*then_fallthrough, value_temp});
		}
		if (then_fallthrough) {
			add_goto_from_current(join_block);
		}

		if (has_else) {
			switch_to_block(*else_block);
			Operand else_value = lower_expr(**if_expr.else_expr);
			std::optional<BasicBlockId> else_fallthrough = current_block ? std::optional<BasicBlockId>(*current_block) : std::nullopt;
			if (else_fallthrough && result_needed) {
				TempId value_temp = materialize_operand(else_value, info.type);
				phi_incomings.push_back(PhiIncoming{*else_fallthrough, value_temp});
			}
			if (else_fallthrough) {
				add_goto_from_current(join_block);
			}
		}

		bool join_reachable = (!phi_incomings.empty() || !result_needed) || !has_else;
		if (join_reachable) {
			current_block = join_block;
		} else {
			current_block.reset();
		}

		if (result_needed) {
			if (phi_incomings.empty()) {
				current_block.reset();
				return make_unit_operand();
			}
			TempId dest = allocate_temp(info.type);
			PhiNode phi;
			phi.dest = dest;
			phi.incoming = phi_incomings;
			mir_function.basic_blocks[join_block].phis.push_back(std::move(phi));
			return make_temp_operand(dest);
		}

		return make_unit_operand();
	}

	Operand lower_short_circuit(const hir::BinaryOp& binary,
								 const semantic::ExprInfo& info,
								 bool is_and) {
		Operand lhs = lower_expr(*binary.lhs);
		if (!current_block) {
			return make_unit_operand();
		}
		semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*binary.lhs);
		semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*binary.rhs);

		TempId lhs_temp = materialize_operand(lhs, lhs_info.type);
		Operand lhs_operand = make_temp_operand(lhs_temp);

		BasicBlockId lhs_block = current_block_id();
		BasicBlockId rhs_block = create_block();
		BasicBlockId join_block = create_block();

		TempId short_value_temp = materialize_operand(
			make_constant_operand(make_bool_constant(is_and ? false : true)),
			info.type);

		branch_on_bool(lhs_operand,
					   is_and ? rhs_block : join_block,
					   is_and ? join_block : rhs_block);

		std::vector<PhiIncoming> incomings;
		incomings.push_back(PhiIncoming{lhs_block, short_value_temp});

		switch_to_block(rhs_block);
		Operand rhs = lower_expr(*binary.rhs);
		std::optional<BasicBlockId> rhs_fallthrough = current_block ? std::optional<BasicBlockId>(*current_block) : std::nullopt;
		if (rhs_fallthrough) {
			TempId rhs_temp = materialize_operand(rhs, rhs_info.type);
			incomings.push_back(PhiIncoming{*rhs_fallthrough, rhs_temp});
			add_goto_from_current(join_block);
		}

		if (incomings.empty()) {
			current_block.reset();
			return make_unit_operand();
		}

		current_block = join_block;
		TempId dest = allocate_temp(info.type);
		PhiNode phi;
		phi.dest = dest;
		phi.incoming = incomings;
		mir_function.basic_blocks[join_block].phis.push_back(std::move(phi));
		return make_temp_operand(dest);
	}

	Operand lower_loop_expr(const hir::Loop& loop_expr, [[maybe_unused]] const semantic::ExprInfo& info) {
		BasicBlockId body_block = create_block();
		BasicBlockId break_block = create_block();

		if (current_block) {
			add_goto_from_current(body_block);
		}
		current_block = body_block;

		push_loop_context(&loop_expr, body_block, break_block, loop_expr.break_type);
		lower_block_expr(*loop_expr.body, get_unit_type());
		if (current_block) {
			add_goto_from_current(body_block);
		}

		LoopContext finalized = pop_loop_context(&loop_expr);
		finalize_loop_context(finalized);

		bool break_reachable = !finalized.break_predecessors.empty();
		if (finalized.break_result) {
			if (!break_reachable) {
				current_block.reset();
				return make_unit_operand();
			}
			current_block = finalized.break_block;
			return make_temp_operand(*finalized.break_result);
		}

		if (break_reachable) {
			current_block = finalized.break_block;
		} else {
			current_block.reset();
		}
		return make_unit_operand();
	}

	Operand lower_while_expr(const hir::While& while_expr, [[maybe_unused]] const semantic::ExprInfo& info) {
		BasicBlockId cond_block = create_block();
		BasicBlockId body_block = create_block();
		BasicBlockId break_block = create_block();

		if (current_block) {
			add_goto_from_current(cond_block);
		}
		current_block = cond_block;

		auto& ctx = push_loop_context(&while_expr, cond_block, break_block, while_expr.break_type);

		Operand condition = lower_expr(*while_expr.condition);
		if (current_block) {
			branch_on_bool(condition, body_block, break_block);
			ctx.break_predecessors.push_back(cond_block);
		}

		switch_to_block(body_block);
		lower_block_expr(*while_expr.body, get_unit_type());
		if (current_block) {
			add_goto_from_current(cond_block);
		}

		LoopContext finalized = pop_loop_context(&while_expr);
		finalize_loop_context(finalized);

		current_block = break_block;
		if (finalized.break_result) {
			return make_temp_operand(*finalized.break_result);
		}
		return make_unit_operand();
	}

	Operand lower_break_expr(const hir::Break& break_expr) {
		auto target = hir::helper::get_break_target(break_expr);
		const void* key = std::visit([](auto* loop_ptr) -> const void* { return loop_ptr; }, target);
		Operand break_value = break_expr.value ? lower_expr(**break_expr.value) : make_unit_operand();
		LoopContext& ctx = lookup_loop_context(key);
		BasicBlockId from_block = current_block ? current_block_id() : ctx.break_block;
		if (ctx.break_result) {
			semantic::TypeId ty = ctx.break_type.value();
			TempId temp = materialize_operand(break_value, ty);
			ctx.break_incomings.push_back(PhiIncoming{from_block, temp});
		}
		ctx.break_predecessors.push_back(from_block);

		add_goto_from_current(ctx.break_block);
		return make_unit_operand();
	}

	Operand lower_continue_expr(const hir::Continue& continue_expr) {
		auto target = hir::helper::get_continue_target(continue_expr);
		const void* key = std::visit([](auto* loop_ptr) -> const void* { return loop_ptr; }, target);
		LoopContext& ctx = lookup_loop_context(key);
		add_goto_from_current(ctx.continue_block);
		return make_unit_operand();
	}

	Operand lower_return_expr(const hir::Return& return_expr) {
		std::optional<Operand> value;
		if (return_expr.value && *return_expr.value) {
			value = lower_expr(**return_expr.value);
		}
		emit_return(value);
		return make_unit_operand();
	}

};

} // namespace

MirFunction lower_function(const hir::Function& function,
			   const std::unordered_map<const void*, FunctionId>& id_map,
			   FunctionId id) {
	FunctionLowerer lowerer(function, id_map, id, derive_function_name(function, std::string{}));
	return lowerer.lower();
}

MirFunction lower_function(const hir::Function& function) {
	std::unordered_map<const void*, FunctionId> ids;
	ids.emplace(&function, static_cast<FunctionId>(0));
	return lower_function(function, ids, 0);
}

MirModule lower_program(const hir::Program& program) {
	std::vector<FunctionDescriptor> descriptors = collect_function_descriptors(program);
	std::unordered_map<const void*, FunctionId> ids;
	ids.reserve(descriptors.size());
	for (auto& descriptor : descriptors) {
		descriptor.id = static_cast<FunctionId>(ids.size());
		ids.emplace(descriptor.key, descriptor.id);
	}

	MirModule module;
	module.functions.reserve(descriptors.size());
	for (const auto& descriptor : descriptors) {
		if (descriptor.kind == FunctionDescriptor::Kind::Function) {
			FunctionLowerer lowerer(*descriptor.function, ids, descriptor.id, descriptor.name);
			module.functions.push_back(lowerer.lower());
		} else {
			FunctionLowerer lowerer(*descriptor.method, ids, descriptor.id, descriptor.name);
			module.functions.push_back(lowerer.lower());
		}
	}
	return module;
}

} // namespace mir
