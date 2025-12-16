#include "mir/lower-v2/lower_internal.hpp"
#include "mir/lower/lower_common.hpp"
#include "semantic/hir/helper.hpp"

namespace mir::lower_v2 {

using namespace detail;

// === Initialization & Setup ===

FunctionLowerer::FunctionLowerer(const hir::Function& function,
                                 const std::unordered_map<const void*, mir::FunctionRef>& fn_map,
                                 FunctionId id,
                                 std::string name)
    : function_kind(FunctionKind::Function),
      hir_function(&function),
      hir_method(nullptr),
      function_map(fn_map) {
    initialize(id, std::move(name));
}

FunctionLowerer::FunctionLowerer(const hir::Method& method,
                                 const std::unordered_map<const void*, mir::FunctionRef>& fn_map,
                                 FunctionId id,
                                 std::string name)
    : function_kind(FunctionKind::Method),
      hir_function(nullptr),
      hir_method(&method),
      function_map(fn_map) {
    initialize(id, std::move(name));
}

void FunctionLowerer::initialize(FunctionId id, std::string name) {
    mir_function.id = id;
    mir_function.name = std::move(name);
}

const hir::Block* FunctionLowerer::get_body() const {
    if (hir_function) {
        return hir_function->body.has_value() ? hir_function->body.value().block.get() : nullptr;
    }
    if (hir_method) {
        return hir_method->body.has_value() ? hir_method->body.value().block.get() : nullptr;
    }
    return nullptr;
}

const std::vector<std::unique_ptr<hir::Local>>& FunctionLowerer::get_locals_vector() const {
    static const std::vector<std::unique_ptr<hir::Local>> empty;
    return empty;
}

TypeId FunctionLowerer::resolve_return_type() const {
    return detail::get_unit_type();
}

MirFunction FunctionLowerer::lower() {
    const hir::Block* body = get_body();
    if (body) {
        lower_block(*body);
    } else {
        current_block = create_block();
        terminate_current_block(Terminator{GotoTerminator{.target = 0}});
    }
    return std::move(mir_function);
}

// === Public API ===

LowerResult FunctionLowerer::lower_node(const hir::Expr& expr, std::optional<Place> dest_hint) {
    auto info = hir::helper::get_expr_info(expr);
    return lower_node_impl(expr, info, dest_hint);
}

Place FunctionLowerer::lower_node_place(const hir::Expr& expr) {
    auto info = hir::helper::get_expr_info(expr);
    return lower_node_impl(expr, info, std::nullopt).as_place(*this, info);
}

Operand FunctionLowerer::lower_node_operand(const hir::Expr& expr) {
    auto info = hir::helper::get_expr_info(expr);
    return lower_node_impl(expr, info, std::nullopt).as_operand(*this, info);
}

// === Central Dispatcher ===

LowerResult FunctionLowerer::lower_node_impl(const hir::Expr& expr, const semantic::ExprInfo& info,
                                             std::optional<Place> dest_hint) {
    return std::visit(
        Overloaded{
            [this, &info, &dest_hint](const hir::Literal& node) {
                return visit_literal(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::UnresolvedIdentifier& node) {
                return visit_unresolved_identifier(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::TypeStatic& node) {
                return visit_type_static(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::Underscore& node) {
                return visit_underscore(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::FieldAccess& node) {
                return visit_field_access(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::StructLiteral& node) {
                return visit_struct_literal(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::ArrayLiteral& node) {
                return visit_array_literal(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::ArrayRepeat& node) {
                return visit_array_repeat(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::Index& node) {
                return visit_index(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::Assignment& node) {
                return visit_assignment(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::UnaryOp& node) {
                return visit_unary(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::BinaryOp& node) {
                return visit_binary(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::Cast& node) {
                return visit_cast(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::Call& node) {
                return visit_call(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::MethodCall& node) {
                return visit_method_call(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::Block& node) {
                return visit_block(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::If& node) {
                return visit_if(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::Loop& node) {
                return visit_loop(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::While& node) {
                return visit_while(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::Break& node) {
                return visit_break(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::Continue& node) {
                return visit_continue(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::Return& node) {
                return visit_return(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::Variable& node) {
                return visit_variable(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::ConstUse& node) {
                return visit_const_use(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::FuncUse& node) {
                return visit_func_use(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::StructConst& node) {
                return visit_struct_const(node, info, dest_hint);
            },
            [this, &info, &dest_hint](const hir::EnumVariant& node) {
                return visit_enum_variant(node, info, dest_hint);
            }
        },
        expr.value);
}

// === Visitor Stubs ===
LowerResult FunctionLowerer::visit_literal(const hir::Literal& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Literals are always scalars - ignore dest_hint and return Operand
    // Build the constant value from the literal
    ConstantRValue const_rval = build_literal_rvalue(node, info);
    
    // Emit to a temp and return as Operand
    Operand result = emit_rvalue_to_temp(std::move(const_rval), info.type);
    return LowerResult::from_operand(result);
}
LowerResult FunctionLowerer::visit_unresolved_identifier(const hir::UnresolvedIdentifier& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // UnresolvedIdentifier: Should be resolved by semantic pass
    throw std::logic_error("Unresolved identifier reached MIR lowering - invariant violation");
}
LowerResult FunctionLowerer::visit_type_static(const hir::TypeStatic& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // TypeStatic: Type as value (for things like type ascription)
    throw std::logic_error("Type static expressions not yet supported in MIR lowering");
}
LowerResult FunctionLowerer::visit_underscore(const hir::Underscore& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Underscore: Placeholder that shouldn't normally appear in lowering
    throw std::logic_error("Underscore expression reached MIR lowering - invariant violation");
}
LowerResult FunctionLowerer::visit_binary(const hir::BinaryOp& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Binary operations: special-case short-circuit operators
    if (std::get_if<hir::LogicalAnd>(&node.op)) {
        // Logical AND: short-circuit evaluation
        if (auto result = lower_short_circuit(node, info, true)) {
            return LowerResult::from_operand(*result);
        }
    }
    if (std::get_if<hir::LogicalOr>(&node.op)) {
        // Logical OR: short-circuit evaluation
        if (auto result = lower_short_circuit(node, info, false)) {
            return LowerResult::from_operand(*result);
        }
    }

    // Regular binary operations
    if (!node.lhs || !node.rhs) {
        throw std::logic_error("Binary expression missing operand during MIR lowering");
    }

    semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*node.lhs);
    semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*node.rhs);

    Operand lhs = lower_node(*node.lhs, std::nullopt).as_operand(*this, lhs_info);
    Operand rhs = lower_node(*node.rhs, std::nullopt).as_operand(*this, rhs_info);

    // Create binary operation RValue and emit to temp
    BinaryOpRValue binary_value;
    binary_value.lhs = lhs;
    binary_value.rhs = rhs;
    // Set kind based on operator - for now, use default IAdd placeholder
    binary_value.kind = BinaryOpRValue::Kind::IAdd;

    Operand result = emit_rvalue_to_temp(std::move(binary_value), info.type);
    return LowerResult::from_operand(result);
}
LowerResult FunctionLowerer::visit_unary(const hir::UnaryOp& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    if (!node.rhs) {
        throw std::logic_error("Unary expression missing operand during MIR lowering");
    }

    semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*node.rhs);
    Operand operand = lower_node(*node.rhs, std::nullopt).as_operand(*this, rhs_info);

    // Handle each unary operator
    std::optional<Operand> result = std::visit(
        Overloaded{
            [&](const hir::UnaryNot&) -> std::optional<Operand> {
                UnaryOpRValue unary_rvalue;
                unary_rvalue.kind = UnaryOpRValue::Kind::Not;
                unary_rvalue.operand = operand;
                return emit_rvalue_to_temp(std::move(unary_rvalue), info.type);
            },
            [&](const hir::UnaryNegate&) -> std::optional<Operand> {
                UnaryOpRValue unary_rvalue;
                unary_rvalue.kind = UnaryOpRValue::Kind::Neg;
                unary_rvalue.operand = operand;
                return emit_rvalue_to_temp(std::move(unary_rvalue), info.type);
            },
            [&](const hir::Reference&) -> std::optional<Operand> {
                throw std::logic_error("visit_unary: Reference operator not yet implemented");
            },
            [&](const auto&) -> std::optional<Operand> {
                throw std::logic_error("visit_unary: Unknown unary operator");
            }
        },
        node.op);

    if (result) {
        return LowerResult::from_operand(*result);
    }
    throw std::logic_error("visit_unary: Failed to lower operand");
}
LowerResult FunctionLowerer::visit_cast(const hir::Cast& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    if (!node.expr) {
        throw std::logic_error("Cast expression missing operand during MIR lowering");
    }
    if (!info.has_type || info.type == 0) {
        throw std::logic_error("Cast expression missing resolved type during MIR lowering");
    }

    semantic::ExprInfo expr_info = hir::helper::get_expr_info(*node.expr);
    Operand operand = lower_node(*node.expr, std::nullopt).as_operand(*this, expr_info);

    CastRValue cast_rvalue;
    cast_rvalue.value = operand;
    cast_rvalue.target_type = info.type;

    Operand result = emit_rvalue_to_temp(std::move(cast_rvalue), info.type);
    return LowerResult::from_operand(result);
}
LowerResult FunctionLowerer::visit_variable(const hir::Variable& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) { 
    Place place = make_local_place(node.local_id);
    return LowerResult::from_place(place);
}
LowerResult FunctionLowerer::visit_field_access(const hir::FieldAccess& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Field access is a place operation: returns a Place
    if (!node.base) {
        throw std::logic_error("Field access missing base expression during MIR lowering");
    }

    semantic::ExprInfo base_info = hir::helper::get_expr_info(*node.base);
    Place base_place = lower_node(*node.base, std::nullopt).as_place(*this, base_info);

    // Add field projection
    // For now, just use index 0 as placeholder - proper field resolution pending
    FieldProjection field_proj{
        .index = 0  // TODO: resolve from field identifier
    };
    base_place.projections.push_back(Projection{field_proj});

    return LowerResult::from_place(base_place);
}
LowerResult FunctionLowerer::visit_index(const hir::Index& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Index operation: array[index] returns a Place
    if (!node.base || !node.index) {
        throw std::logic_error("Index expression missing operand during MIR lowering");
    }

    semantic::ExprInfo base_info = hir::helper::get_expr_info(*node.base);
    Place base_place = lower_node(*node.base, std::nullopt).as_place(*this, base_info);

    semantic::ExprInfo index_info = hir::helper::get_expr_info(*node.index);
    Operand index_operand = lower_node(*node.index, std::nullopt).as_operand(*this, index_info);

    // Add index projection
    IndexProjection index_proj{
        .index = index_operand
    };
    base_place.projections.push_back(Projection{index_proj});

    return LowerResult::from_place(base_place);
}
LowerResult FunctionLowerer::visit_struct_literal(const hir::StructLiteral& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Struct literals: aggregates that should use dest_hint for RVO
    if (dest_hint) {
        // Great! We have a destination - initialize it directly
        AggregateRValue agg = build_struct_aggregate(node);
        // TODO: Implement place-directed initialization
        // For now, just emit to temp and return Written
        emit_aggregate(agg, info.type);
        return LowerResult::written();
    } else {
        // No destination provided - must allocate synthetic local
        AggregateRValue agg = build_struct_aggregate(node);
        Operand result = emit_aggregate(agg, info.type);
        return LowerResult::from_operand(result);
    }
}
LowerResult FunctionLowerer::visit_array_literal(const hir::ArrayLiteral& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Array literals: aggregates that should use dest_hint for RVO
    if (dest_hint) {
        // Great! We have a destination - initialize it directly
        AggregateRValue agg = build_array_aggregate(node);
        // TODO: Implement place-directed initialization
        // For now, just emit to temp and return Written
        emit_aggregate(agg, info.type);
        return LowerResult::written();
    } else {
        // No destination provided - must allocate synthetic local
        AggregateRValue agg = build_array_aggregate(node);
        Operand result = emit_aggregate(agg, info.type);
        return LowerResult::from_operand(result);
    }
}
LowerResult FunctionLowerer::visit_array_repeat(const hir::ArrayRepeat& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Array repeat: [value; count] - aggregate with destination passing
    if (!node.value) {
        throw std::logic_error("Array repeat missing value during MIR lowering");
    }

    semantic::ExprInfo value_info = hir::helper::get_expr_info(*node.value);
    Operand value = lower_node(*node.value, std::nullopt).as_operand(*this, value_info);

    // Handle count - for now assume it's a compile-time constant
    // TODO: Handle runtime-computed counts
    size_t count_val = 0;
    if (auto* count_expr = std::get_if<std::unique_ptr<hir::Expr>>(&node.count)) {
        // Runtime count - would need const evaluation
        throw std::logic_error("Dynamic array repeat count not yet implemented");
    } else if (auto* count_size = std::get_if<size_t>(&node.count)) {
        count_val = *count_size;
    }

    if (dest_hint) {
        // TODO: Implement place-directed initialization
        // For now, emit to temp
        emit_array_repeat(value, count_val, info.type);
        return LowerResult::written();
    } else {
        // No destination - emit to temp
        Operand result = emit_array_repeat(value, count_val, info.type);
        return LowerResult::from_operand(result);
    }
}
LowerResult FunctionLowerer::visit_block(const hir::Block& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Block: sequence statements then evaluate final expression
    // Propagate dest_hint to final expression

    // Lower all statements
    for (const auto& stmt : node.stmts) {
        if (stmt) {
            lower_statement(*stmt);
        }
    }

    // If there's a final expression, lower it with dest_hint
    if (node.final_expr && *node.final_expr) {
        return lower_node(**node.final_expr, dest_hint);
    }

    // Block with no final expression returns unit
    Operand unit_operand;
    unit_operand.value = TempId{0};  // TODO: create proper unit temp
    return LowerResult::from_operand(unit_operand);
}
LowerResult FunctionLowerer::visit_if(const hir::If& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // If: conditional branching
    // Creates two branches that converge with phi nodes
    if (!node.condition || !node.then_block) {
        throw std::logic_error("If expression missing condition or then-block during MIR lowering");
    }

    // Lower condition to operand
    semantic::ExprInfo cond_info = hir::helper::get_expr_info(*node.condition);
    Operand condition = lower_node(*node.condition, std::nullopt).as_operand(*this, cond_info);

    // Create then and else blocks
    BasicBlockId then_block_id = create_block();
    BasicBlockId else_block_id = create_block();
    BasicBlockId merge_block_id = create_block();

    // Branch from current block
    branch_on_bool(condition, then_block_id, else_block_id);

    // Lower then block
    switch_to_block(then_block_id);
    // Lower block statements and final expression manually
    for (const auto& stmt : node.then_block->stmts) {
        if (stmt) lower_statement(*stmt);
    }
    if (node.then_block->final_expr && *node.then_block->final_expr) {
        lower_node(**node.then_block->final_expr, dest_hint);
    }
    if (is_reachable()) {
        add_goto_from_current(merge_block_id);
    }

    // Lower else block
    switch_to_block(else_block_id);
    LowerResult else_result = LowerResult::written();
    if (node.else_expr) {
        else_result = lower_node(**node.else_expr, dest_hint);
    }
    if (is_reachable()) {
        add_goto_from_current(merge_block_id);
    }

    // Continue from merge block
    switch_to_block(merge_block_id);
    
    // For now, return written (TODO: phi node handling)
    return LowerResult::written();
}
LowerResult FunctionLowerer::visit_loop(const hir::Loop& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Loop: infinite loop until break
    if (!node.body) {
        throw std::logic_error("Loop missing body during MIR lowering");
    }

    BasicBlockId loop_block_id = create_block();
    BasicBlockId break_block_id = create_block();
    BasicBlockId continue_block_id = loop_block_id;

    // Jump to loop
    add_goto_from_current(loop_block_id);

    // Set up loop context
    push_loop_context(&node, continue_block_id, break_block_id, node.break_type);

    // Lower loop body
    switch_to_block(loop_block_id);
    // Lower block statements and final expression manually
    for (const auto& stmt : node.body->stmts) {
        if (stmt) lower_statement(*stmt);
    }
    if (node.body->final_expr && *node.body->final_expr) {
        lower_node(**node.body->final_expr, std::nullopt);
    }
    if (is_reachable()) {
        add_goto_from_current(loop_block_id);
    }

    // Pop loop context
    pop_loop_context(&node);

    // Continue from break block
    switch_to_block(break_block_id);
    
    return LowerResult::written();
}
LowerResult FunctionLowerer::visit_while(const hir::While& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // While: conditional loop
    if (!node.condition || !node.body) {
        throw std::logic_error("While loop missing condition or body during MIR lowering");
    }

    BasicBlockId loop_header_id = create_block();
    BasicBlockId loop_body_id = create_block();
    BasicBlockId break_block_id = create_block();

    // Jump to loop header
    add_goto_from_current(loop_header_id);

    // Set up loop context (continue goes to header)
    push_loop_context(&node, loop_header_id, break_block_id, std::nullopt);

    // Evaluate condition
    switch_to_block(loop_header_id);
    semantic::ExprInfo cond_info = hir::helper::get_expr_info(*node.condition);
    Operand condition = lower_node(*node.condition, std::nullopt).as_operand(*this, cond_info);
    branch_on_bool(condition, loop_body_id, break_block_id);

    // Lower loop body
    switch_to_block(loop_body_id);
    // Lower block statements and final expression manually
    for (const auto& stmt : node.body->stmts) {
        if (stmt) lower_statement(*stmt);
    }
    if (node.body->final_expr && *node.body->final_expr) {
        lower_node(**node.body->final_expr, std::nullopt);
    }
    if (is_reachable()) {
        add_goto_from_current(loop_header_id);
    }

    // Pop loop context
    pop_loop_context(&node);

    // Continue from break block
    switch_to_block(break_block_id);
    
    return LowerResult::written();
}
LowerResult FunctionLowerer::visit_break(const hir::Break& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Break: exit to break block
    auto& loop_ctx = lookup_loop_context(&node);
    add_goto_from_current(loop_ctx.break_block);
    return LowerResult::written();
}
LowerResult FunctionLowerer::visit_continue(const hir::Continue& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Continue: jump back to loop start
    auto& loop_ctx = lookup_loop_context(&node);
    add_goto_from_current(loop_ctx.continue_block);
    return LowerResult::written();
}
LowerResult FunctionLowerer::visit_return(const hir::Return& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Return: exit function with optional value
    if (node.value && *node.value) {
        semantic::ExprInfo value_info = hir::helper::get_expr_info(**node.value);
        Operand value = lower_node(**node.value, std::nullopt).as_operand(*this, value_info);
        emit_return(value);
    } else {
        emit_return(std::nullopt);
    }
    return LowerResult::written();
}
LowerResult FunctionLowerer::visit_call(const hir::Call& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Call: function call with arguments
    // TODO: Implement full call lowering with ABI handling
    throw std::logic_error("visit_call not yet fully implemented");
}
LowerResult FunctionLowerer::visit_method_call(const hir::MethodCall& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // MethodCall: method call on receiver with arguments
    // TODO: Implement full method call lowering with ABI handling
    throw std::logic_error("visit_method_call not yet fully implemented");
}
LowerResult FunctionLowerer::visit_const_use(const hir::ConstUse& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // ConstUse: reference to a const definition
    // TODO: Implement const lowering
    throw std::logic_error("visit_const_use not yet fully implemented");
}
LowerResult FunctionLowerer::visit_func_use(const hir::FuncUse& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // FuncUse: reference to a function for calling
    // TODO: Implement function reference lowering
    throw std::logic_error("visit_func_use not yet fully implemented");
}
LowerResult FunctionLowerer::visit_struct_const(const hir::StructConst& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // StructConst: reference to a const struct
    // TODO: Implement struct const lowering
    throw std::logic_error("visit_struct_const not yet fully implemented");
}
LowerResult FunctionLowerer::visit_enum_variant(const hir::EnumVariant& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // EnumVariant: enum variant value
    // TODO: Implement enum variant lowering
    throw std::logic_error("visit_enum_variant not yet fully implemented");
}
LowerResult FunctionLowerer::visit_assignment(const hir::Assignment& node, const semantic::ExprInfo& info, std::optional<Place> dest_hint) {
    // Assignment: lhs = rhs
    if (!node.lhs || !node.rhs) {
        throw std::logic_error("Assignment missing operands during MIR lowering");
    }

    // Get LHS place
    semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*node.lhs);
    Place lhs_place = lower_node(*node.lhs, std::nullopt).as_place(*this, lhs_info);

    // Get RHS value (provide lhs_place as dest_hint for RVO)
    semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*node.rhs);
    LowerResult rhs_result = lower_node(*node.rhs, lhs_place);

    // Write RHS to LHS
    rhs_result.write_to_dest(*this, lhs_place, rhs_info);

    // Return unit (assignments return () - the unit type)
    return LowerResult::written();
}

// === Helper Stubs ===
bool FunctionLowerer::lower_block_statements(const hir::Block& block) { return false; }
void FunctionLowerer::lower_block(const hir::Block& hir_block) {}
void FunctionLowerer::lower_statement(const hir::Stmt& stmt) {}
void FunctionLowerer::lower_statement_impl(const hir::LetStmt& let_stmt) {}
void FunctionLowerer::lower_statement_impl(const hir::ExprStmt& expr_stmt) {}
std::optional<Operand> FunctionLowerer::lower_block_expr(const hir::Block& block, TypeId expected_type) { return std::nullopt; }
void FunctionLowerer::handle_return_value(const std::optional<std::unique_ptr<hir::Expr>>& value_ptr, const char* context) {}
std::optional<Operand> FunctionLowerer::lower_short_circuit(const hir::BinaryOp& binary, const semantic::ExprInfo& info, bool is_and) { return std::nullopt; }
void FunctionLowerer::lower_let_pattern(const hir::Pattern& pattern, const hir::Expr& init_expr) {}
void FunctionLowerer::lower_binding_let(const hir::BindingDef& binding, const hir::Expr& init_expr) {}
void FunctionLowerer::lower_reference_let(const hir::ReferencePattern& ref_pattern, const hir::Expr& init_expr) {}
void FunctionLowerer::lower_pattern_from_expr(const hir::Pattern& pattern, const hir::Expr& expr, TypeId expr_type) {}
std::optional<Operand> FunctionLowerer::try_lower_to_const(const hir::Expr& expr) { return std::nullopt; }
AggregateRValue FunctionLowerer::build_struct_aggregate(const hir::StructLiteral& struct_literal) { return AggregateRValue{}; }
AggregateRValue FunctionLowerer::build_array_aggregate(const hir::ArrayLiteral& array_literal) { return AggregateRValue{}; }
ArrayRepeatRValue FunctionLowerer::build_array_repeat_rvalue(const hir::ArrayRepeat& array_repeat) { return ArrayRepeatRValue{}; }
ConstantRValue FunctionLowerer::build_literal_rvalue(const hir::Literal& lit, const semantic::ExprInfo& info) { return ConstantRValue{}; }
LocalId FunctionLowerer::require_local_id(const hir::Local* local) const { return 0; }
Place FunctionLowerer::make_local_place(const hir::Local* local) const { return make_local_place(require_local_id(local)); }
Place FunctionLowerer::make_local_place(LocalId local_id) const { Place p; p.base = LocalPlace{local_id}; return p; }
LocalId FunctionLowerer::create_synthetic_local(TypeId type, bool is_mutable_reference) { return 0; }
Operand FunctionLowerer::load_place_value(Place place, TypeId type) { return Operand{}; }
Operand FunctionLowerer::make_const_operand(std::uint64_t value, TypeId type, bool is_signed) { return Operand{}; }
Operand FunctionLowerer::make_temp_operand(TempId temp) { Operand op; op.value = temp; return op; }
TempId FunctionLowerer::materialize_operand(const Operand& operand, TypeId type) { return 0; }
TempId FunctionLowerer::materialize_place_base(const hir::Expr& base_expr, const semantic::ExprInfo& base_info) { return 0; }
Operand FunctionLowerer::emit_aggregate(AggregateRValue aggregate, TypeId result_type) { return Operand{}; }
Operand FunctionLowerer::emit_array_repeat(Operand value, std::size_t count, TypeId result_type) { return Operand{}; }
BasicBlockId FunctionLowerer::create_block() { BasicBlockId id = mir_function.basic_blocks.size(); mir_function.basic_blocks.emplace_back(); block_terminated.push_back(false); return id; }
bool FunctionLowerer::block_is_terminated(BasicBlockId id) const { if (id >= block_terminated.size()) return false; return block_terminated[id]; }
BasicBlockId FunctionLowerer::current_block_id() const { if (!current_block.has_value()) throw std::logic_error("No current block"); return current_block.value(); }
TempId FunctionLowerer::allocate_temp(TypeId type) { TempId id = mir_function.temp_types.size(); mir_function.temp_types.push_back(type); return id; }
void FunctionLowerer::append_statement(Statement statement) { if (!current_block.has_value()) throw std::logic_error("Cannot append statement without current block"); mir_function.basic_blocks[current_block.value()].statements.push_back(std::move(statement)); }
void FunctionLowerer::set_terminator(BasicBlockId id, Terminator terminator) { if (id >= mir_function.basic_blocks.size()) throw std::logic_error("Block ID out of range"); mir_function.basic_blocks[id].terminator = std::move(terminator); if (id < block_terminated.size()) block_terminated[id] = true; }
void FunctionLowerer::terminate_current_block(Terminator terminator) { set_terminator(current_block_id(), std::move(terminator)); }
void FunctionLowerer::add_goto_from_current(BasicBlockId target) { terminate_current_block(Terminator{GotoTerminator{.target = target}}); }
void FunctionLowerer::switch_to_block(BasicBlockId id) { current_block = id; }
void FunctionLowerer::branch_on_bool(const Operand& condition, BasicBlockId true_block, BasicBlockId false_block) {}
void FunctionLowerer::emit_return(std::optional<Operand> value) {}
void FunctionLowerer::collect_parameters() {}
void FunctionLowerer::append_self_parameter() {}
void FunctionLowerer::append_explicit_parameters(const std::vector<std::unique_ptr<hir::Pattern>>& params, const std::vector<hir::TypeAnnotation>& annotations) {}
void FunctionLowerer::append_parameter(const hir::Local* local, TypeId type) {}
const hir::Local* FunctionLowerer::resolve_pattern_local(const hir::Pattern& pattern) const { return nullptr; }
bool FunctionLowerer::is_reachable() const { return current_block.has_value() && !block_is_terminated(current_block.value()); }
void FunctionLowerer::require_reachable(const char* context) const { if (!is_reachable()) throw std::logic_error(std::string("Code after terminator: ") + context); }
FunctionLowerer::LoopContext& FunctionLowerer::push_loop_context(const void* key, BasicBlockId continue_block, BasicBlockId break_block, std::optional<TypeId> break_type) { loop_stack.emplace_back(key, LoopContext{.continue_block = continue_block, .break_block = break_block, .break_type = break_type}); return loop_stack.back().second; }
FunctionLowerer::LoopContext& FunctionLowerer::lookup_loop_context(const void* key) { for (auto it = loop_stack.rbegin(); it != loop_stack.rend(); ++it) { if (it->first == key) return it->second; } throw std::logic_error("Loop context not found"); }
FunctionLowerer::LoopContext FunctionLowerer::pop_loop_context(const void* key) { auto it = std::find_if(loop_stack.rbegin(), loop_stack.rend(), [key](const auto& pair) { return pair.first == key; }); if (it == loop_stack.rend()) throw std::logic_error("Loop context not found"); LoopContext ctx = it->second; loop_stack.erase(std::next(it).base()); return ctx; }
void FunctionLowerer::finalize_loop_context(const LoopContext& ctx) {}
void FunctionLowerer::init_locals() {}
const hir::Local* FunctionLowerer::pick_nrvo_local() const { return nullptr; }
ReturnStoragePlan FunctionLowerer::build_return_plan() { return ReturnStoragePlan{}; }
void FunctionLowerer::apply_abi_aliasing(const ReturnStoragePlan& plan) {}
mir::FunctionRef FunctionLowerer::lookup_function(const void* key) const { throw std::logic_error("lookup_function not yet implemented"); }
const MirFunctionSig& FunctionLowerer::get_callee_sig(mir::FunctionRef target) const { throw std::logic_error("get_callee_sig not yet implemented"); }

}  // namespace mir::lower_v2
