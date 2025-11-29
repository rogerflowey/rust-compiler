#include "mir/lower/lower_internal.hpp"

#include "semantic/hir/helper.hpp"
#include "type/type.hpp"
#include "semantic/utils.hpp"

namespace mir::detail {

Operand FunctionLowerer::load_place_value(Place place, TypeId type) {
	TempId temp = allocate_temp(type);
	LoadStatement load{.dest = temp, .src = std::move(place)};
	Statement stmt;
	stmt.value = std::move(load);
	append_statement(std::move(stmt));
	return make_temp_operand(temp);
}

Operand FunctionLowerer::lower_expr(const hir::Expr& expr) {
	semantic::ExprInfo info = hir::helper::get_expr_info(expr);
	return std::visit([this, &info](const auto& node) {
		return lower_expr_impl(node, info);
	}, expr.value);
}

Place FunctionLowerer::lower_expr_place(const hir::Expr& expr) {
	semantic::ExprInfo info = hir::helper::get_expr_info(expr);
	if (!info.is_place) {
		throw std::logic_error("Expression is not a place in MIR lowering");
	}
	return std::visit([this, &info](const auto& node) {
		return lower_place_impl(node, info);
	}, expr.value);
}

Place FunctionLowerer::lower_place_impl(const hir::Variable& variable, const semantic::ExprInfo& info) {
	if (!info.is_place) {
		throw std::logic_error("Variable without place capability encountered during MIR lowering");
	}
	return make_local_place(variable.local_id);
}

Place FunctionLowerer::lower_place_impl(const hir::FieldAccess& field_access, const semantic::ExprInfo&) {
	if (!field_access.base) {
		throw std::logic_error("Should Not Happen: Field access missing base during MIR place lowering");
	}
	semantic::ExprInfo base_info = hir::helper::get_expr_info(*field_access.base);
	if (!base_info.is_place) {
		throw std::logic_error("Should Not Happen: Field access base is not a place during MIR place lowering");
	}
	Place place = lower_expr_place(*field_access.base);
	std::size_t index = hir::helper::get_field_index(field_access);
	place.projections.push_back(FieldProjection{index});
	return place;
}

Place FunctionLowerer::lower_place_impl(const hir::Index& index_expr, const semantic::ExprInfo&) {
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

Place FunctionLowerer::lower_place_impl(const hir::UnaryOp& unary, const semantic::ExprInfo&) {
    if (!std::holds_alternative<hir::Dereference>(unary.op)) {
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

Place FunctionLowerer::ensure_reference_operand_place(const hir::Expr& operand,
					      const semantic::ExprInfo& operand_info,
					      bool mutable_reference) {
	if (!operand_info.has_type) {
		throw std::logic_error("Reference operand missing resolved type during MIR lowering");
	}
	if (operand_info.is_place) {
		if (mutable_reference && !operand_info.is_mut) {
			throw std::logic_error("Mutable reference to immutable place encountered during MIR lowering");
		}
		return lower_expr_place(operand);
	}

	Operand value = lower_expr(operand);
	LocalId temp_local = create_synthetic_local(operand_info.type, mutable_reference);
	AssignStatement assign;
	assign.dest = make_local_place(temp_local);
	assign.src = value;
	Statement stmt;
	stmt.value = std::move(assign);
	append_statement(std::move(stmt));
	return make_local_place(temp_local);
}

Operand FunctionLowerer::lower_expr_impl(const hir::Literal& literal, const semantic::ExprInfo& info) {
	Constant constant = lower_literal(literal, info.type);
	return make_constant_operand(constant);
}

Operand FunctionLowerer::lower_expr_impl(const hir::StructLiteral& struct_literal, const semantic::ExprInfo& info) {
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

Operand FunctionLowerer::lower_expr_impl(const hir::ArrayLiteral& array_literal, const semantic::ExprInfo& info) {
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

Operand FunctionLowerer::lower_expr_impl(const hir::ArrayRepeat& array_repeat, const semantic::ExprInfo& info) {
	if (!array_repeat.value) {
		throw std::logic_error("Array repeat missing value during MIR lowering");
	}
	size_t count = hir::helper::get_array_count(array_repeat);
	Operand value = lower_expr(*array_repeat.value);
	return emit_array_repeat(std::move(value), count, info.type);
}

Operand FunctionLowerer::lower_expr_impl(const hir::Variable& variable, const semantic::ExprInfo& info) {
	return load_place_value(lower_place_impl(variable, info), info.type);
}

Operand FunctionLowerer::lower_expr_impl(const hir::ConstUse& const_use, const semantic::ExprInfo& info) {
	if (!const_use.def) {
		throw std::logic_error("Const use missing definition during MIR lowering");
	}
	TypeId type = info.type;
	if (!type && const_use.def->type) {
		type = hir::helper::get_resolved_type(*const_use.def->type);
	}
	if (!type) {
		throw std::logic_error("Const use missing resolved type during MIR lowering");
	}
	Constant constant = lower_const_definition(*const_use.def, type);
	return make_constant_operand(constant);
}

Operand FunctionLowerer::lower_expr_impl(const hir::StructConst& struct_const, const semantic::ExprInfo& info) {
	if (!struct_const.assoc_const) {
		throw std::logic_error("Struct const missing associated const during MIR lowering");
	}
	TypeId type = info.type;
	if (!type && struct_const.assoc_const->type) {
		type = hir::helper::get_resolved_type(*struct_const.assoc_const->type);
	}
	if (!type) {
		throw std::logic_error("Struct const missing resolved type during MIR lowering");
	}
	Constant constant = lower_const_definition(*struct_const.assoc_const, type);
	return make_constant_operand(constant);
}

Operand FunctionLowerer::lower_expr_impl(const hir::EnumVariant& enum_variant, const semantic::ExprInfo& info) {
        TypeId type = info.type;
        if (!type) {
                if (!enum_variant.enum_def) {
                        throw std::logic_error("Enum variant missing enum definition during MIR lowering");
                }
                auto enum_id = type::TypeContext::get_instance().get_or_register_enum(enum_variant.enum_def);
                type = type::get_typeID(type::Type{type::EnumType{enum_id}});
        }
        Constant constant = lower_enum_variant(enum_variant, type);
        return make_constant_operand(constant);
}

Operand FunctionLowerer::lower_expr_impl(const hir::FieldAccess& field_access, const semantic::ExprInfo& info) {
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

Operand FunctionLowerer::lower_expr_impl(const hir::Index& index_expr, const semantic::ExprInfo& info) {
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

Operand FunctionLowerer::lower_expr_impl(const hir::Cast& cast_expr, const semantic::ExprInfo& info) {
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

Operand FunctionLowerer::lower_expr_impl(const hir::BinaryOp& binary, const semantic::ExprInfo& info) {
        if (std::holds_alternative<hir::LogicalAnd>(binary.op)) {
                return lower_short_circuit(binary, info, true);
        }
        if (std::holds_alternative<hir::LogicalOr>(binary.op)) {
                return lower_short_circuit(binary, info, false);
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

Operand FunctionLowerer::lower_expr_impl(const hir::Assignment& assignment, const semantic::ExprInfo&) {
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

Operand FunctionLowerer::lower_expr_impl(const hir::Block& block_expr, const semantic::ExprInfo& info) {
	return lower_block_expr(block_expr, info.type);
}

Operand FunctionLowerer::lower_expr_impl(const hir::If& if_expr, const semantic::ExprInfo& info) {
	return lower_if_expr(if_expr, info);
}

Operand FunctionLowerer::lower_expr_impl(const hir::Loop& loop_expr, const semantic::ExprInfo& info) {
	return lower_loop_expr(loop_expr, info);
}

Operand FunctionLowerer::lower_expr_impl(const hir::While& while_expr, const semantic::ExprInfo& info) {
	return lower_while_expr(while_expr, info);
}

Operand FunctionLowerer::lower_expr_impl(const hir::Break& break_expr, const semantic::ExprInfo&) {
	return lower_break_expr(break_expr);
}

Operand FunctionLowerer::lower_expr_impl(const hir::Continue& continue_expr, const semantic::ExprInfo&) {
	return lower_continue_expr(continue_expr);
}

Operand FunctionLowerer::lower_expr_impl(const hir::Return& return_expr, const semantic::ExprInfo&) {
	return lower_return_expr(return_expr);
}

Operand FunctionLowerer::lower_expr_impl(const hir::Call& call_expr, const semantic::ExprInfo& info) {
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

Operand FunctionLowerer::lower_expr_impl(const hir::MethodCall& method_call, const semantic::ExprInfo& info) {
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

Operand FunctionLowerer::emit_unary_value(const hir::UnaryOperator& op,
                                          const hir::Expr& operand_expr,
                                          TypeId result_type) {
        Operand operand = lower_expr(operand_expr);
        TempId dest = allocate_temp(result_type);
        UnaryOpRValue::Kind kind;
        kind = std::visit(Overloaded{
                [](const hir::UnaryNot&) { return UnaryOpRValue::Kind::Not; },
                [](const hir::UnaryNegate&) { return UnaryOpRValue::Kind::Neg; },
                [](const auto&) -> UnaryOpRValue::Kind {
                        throw std::logic_error("Unsupported unary op kind for value lowering");
                }
        }, op);
        UnaryOpRValue unary_rvalue{.kind = kind, .operand = std::move(operand)};
        RValue rvalue;
        rvalue.value = std::move(unary_rvalue);
	DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
	Statement stmt;
	stmt.value = std::move(define);
	append_statement(std::move(stmt));
	return make_temp_operand(dest);
}

Operand FunctionLowerer::lower_expr_impl(const hir::UnaryOp& unary, const semantic::ExprInfo& info) {
        if (!unary.rhs) {
                throw std::logic_error("Unary expression missing operand during MIR lowering");
        }
        return std::visit(Overloaded{
                [&](const hir::UnaryNot&) {
                        return emit_unary_value(unary.op, *unary.rhs, info.type);
                },
                [&](const hir::UnaryNegate&) {
                        return emit_unary_value(unary.op, *unary.rhs, info.type);
                },
                [&](const hir::Reference&) {
                        semantic::ExprInfo operand_info = hir::helper::get_expr_info(*unary.rhs);
                        const auto &reference = std::get<hir::Reference>(unary.op);
                        Place place = ensure_reference_operand_place(*unary.rhs, operand_info, reference.is_mutable);
                        TempId dest = allocate_temp(info.type);
                        RefRValue ref_rvalue{.place = std::move(place)};
                        RValue rvalue;
                        rvalue.value = std::move(ref_rvalue);
                        DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
                        Statement stmt;
                        stmt.value = std::move(define);
                        append_statement(std::move(stmt));
                        return make_temp_operand(dest);
                },
                [&](const hir::Dereference&) {
                        return load_place_value(lower_place_impl(unary, info), info.type);
                }
        }, unary.op);
}

Operand FunctionLowerer::lower_if_expr(const hir::If& if_expr, const semantic::ExprInfo& info) {
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

Operand FunctionLowerer::lower_short_circuit(const hir::BinaryOp& binary,
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

Operand FunctionLowerer::lower_loop_expr(const hir::Loop& loop_expr, [[maybe_unused]] const semantic::ExprInfo& info) {
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

Operand FunctionLowerer::lower_while_expr(const hir::While& while_expr, [[maybe_unused]] const semantic::ExprInfo& info) {
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

Operand FunctionLowerer::lower_break_expr(const hir::Break& break_expr) {
	auto target = hir::helper::get_break_target(break_expr);
	const void* key = std::visit([](auto* loop_ptr) -> const void* { return loop_ptr; }, target);
	Operand break_value = break_expr.value ? lower_expr(**break_expr.value) : make_unit_operand();
	LoopContext& ctx = lookup_loop_context(key);
	BasicBlockId from_block = current_block ? current_block_id() : ctx.break_block;
	if (ctx.break_result) {
		TypeId ty = ctx.break_type.value();
		TempId temp = materialize_operand(break_value, ty);
		ctx.break_incomings.push_back(PhiIncoming{from_block, temp});
	}
	ctx.break_predecessors.push_back(from_block);

	add_goto_from_current(ctx.break_block);
	return make_unit_operand();
}

Operand FunctionLowerer::lower_continue_expr(const hir::Continue& continue_expr) {
	auto target = hir::helper::get_continue_target(continue_expr);
	const void* key = std::visit([](auto* loop_ptr) -> const void* { return loop_ptr; }, target);
	LoopContext& ctx = lookup_loop_context(key);
	add_goto_from_current(ctx.continue_block);
	return make_unit_operand();
}

Operand FunctionLowerer::lower_return_expr(const hir::Return& return_expr) {
	std::optional<Operand> value;
	if (return_expr.value && *return_expr.value) {
		value = lower_expr(**return_expr.value);
	}
	emit_return(value);
	return make_unit_operand();
}

} // namespace mir::detail
