#include "mir/lower/lower_internal.hpp"

#include "semantic/expr_info_helpers.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/utils.hpp"
#include "type/type.hpp"
#include <stdexcept>

namespace mir::detail {

Operand FunctionLowerer::load_place_value(Place place, TypeId type) {
  TempId temp = allocate_temp(type);
  LoadStatement load{.dest = temp, .src = std::move(place)};
  Statement stmt;
  stmt.value = std::move(load);
  append_statement(std::move(stmt));
  return make_temp_operand(temp);
}

std::optional<Operand>
FunctionLowerer::materialize_result_operand(LowerResult &result,
                                            const semantic::ExprInfo &info) {
  if (!is_reachable()) {
    return std::nullopt;
  }
  if (is_unit_type(info.type) || is_never_type(info.type)) {
    return std::nullopt;
  }
  return result.as_operand(*this, info);
}

Operand FunctionLowerer::lower_operand(const hir::Expr &expr) {
  // Try to lower as a pure constant operand first (no temp needed).
  // Fall back to lower_expr if that doesn't work.
  if (auto const_operand = try_lower_to_const(expr)) {
    return std::move(*const_operand);
  }
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);
  LowerResult res = lower_node(expr);
  return res.as_operand(*this, info);
}

std::optional<Operand> FunctionLowerer::lower_expr(const hir::Expr &expr) {
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);

  bool was_reachable = is_reachable();

  LowerResult result = lower_node(expr);

  if (was_reachable && semantic::diverges(info) && is_reachable()) {
    throw std::logic_error("MIR lowering bug: semantically diverging "
                           "expression leaves MIR reachable");
  }

  return materialize_result_operand(result, info);
}

Place FunctionLowerer::lower_expr_place(const hir::Expr &expr) {
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);
  if (!info.is_place) {
    throw std::logic_error("Expression is not a place in MIR lowering");
  }
  LowerResult res = lower_node(expr);
  return res.as_place(*this, info);
}

Operand FunctionLowerer::expect_operand(std::optional<Operand> value,
                                        const char *context) {
  if (!value) {
    throw std::logic_error(context);
  }
  return std::move(*value);
}

Place FunctionLowerer::lower_place_impl(const hir::Variable &variable,
                                        const semantic::ExprInfo &info) {
  if (!info.is_place) {
    throw std::logic_error(
        "Variable without place capability encountered during MIR lowering");
  }
  return make_local_place(variable.local_id);
}

Place FunctionLowerer::lower_place_impl(const hir::FieldAccess &field_access,
                                        const semantic::ExprInfo &) {
  if (!field_access.base) {
    throw std::logic_error("Should Not Happen: Field access missing base "
                           "during MIR place lowering");
  }
  semantic::ExprInfo base_info = hir::helper::get_expr_info(*field_access.base);
  if (!base_info.is_place) {
    throw std::logic_error("Should Not Happen: Field access base is not a "
                           "place during MIR place lowering");
  }
  Place place = lower_expr_place(*field_access.base);
  std::size_t index = hir::helper::get_field_index(field_access);
  place.projections.push_back(FieldProjection{index});
  return place;
}

Place FunctionLowerer::make_index_place(const hir::Index &index_expr,
                                        bool allow_temporary_base) {
  if (!index_expr.base || !index_expr.index) {
    throw std::logic_error(
        "Index expression missing base or index during MIR place lowering");
  }
  semantic::ExprInfo base_info = hir::helper::get_expr_info(*index_expr.base);
  Place place;
  if (base_info.is_place) {
    place = lower_expr_place(*index_expr.base);
  } else {
    if (!allow_temporary_base) {
      throw std::logic_error(
          "Index base is not a place during MIR place lowering");
    }
    place = ensure_reference_operand_place(*index_expr.base, base_info, false);
  }
  semantic::ExprInfo idx_info = hir::helper::get_expr_info(*index_expr.index);
  Operand idx_operand = lower_operand(*index_expr.index);
  place.projections.push_back(IndexProjection{std::move(idx_operand)});
  return place;
}

Place FunctionLowerer::lower_place_impl(const hir::Index &index_expr,
                                        const semantic::ExprInfo &) {
  return make_index_place(index_expr, false);
}

Place FunctionLowerer::lower_place_impl(const hir::UnaryOp &unary,
                                        const semantic::ExprInfo &) {
  if (!std::get_if<hir::Dereference>(&unary.op)) {
    throw std::logic_error(
        "Only dereference unary ops can be lowered as places");
  }
  if (!unary.rhs) {
    throw std::logic_error(
        "Dereference expression missing operand during MIR place lowering");
  }
  semantic::ExprInfo operand_info = hir::helper::get_expr_info(*unary.rhs);
  Operand pointer_operand = expect_operand(
      lower_expr(*unary.rhs), "Dereference operand must produce value");
  TempId pointer_temp = materialize_operand(pointer_operand, operand_info.type);
  Place place;
  place.base = PointerPlace{pointer_temp};
  return place;
}

Place FunctionLowerer::ensure_reference_operand_place(
    const hir::Expr &operand, const semantic::ExprInfo &operand_info,
    bool mutable_reference) {
  if (!operand_info.has_type) {
    throw std::logic_error(
        "Reference operand missing resolved type during MIR lowering");
  }
  auto is_place_shape = [](const hir::Expr &expr) {
    return std::holds_alternative<hir::Variable>(expr.value) ||
           std::holds_alternative<hir::FieldAccess>(expr.value) ||
           std::holds_alternative<hir::Index>(expr.value) ||
           (std::holds_alternative<hir::UnaryOp>(expr.value) &&
            std::holds_alternative<hir::Dereference>(
                std::get<hir::UnaryOp>(expr.value).op));
  };
  if (operand_info.is_place || is_place_shape(operand)) {
    if (mutable_reference && !operand_info.is_mut) {
      throw std::logic_error("Mutable reference to immutable place encountered "
                             "during MIR lowering");
    }
    return lower_expr_place(operand);
  }

  Operand value = lower_operand(operand);
  LocalId temp_local =
      create_synthetic_local(operand_info.type, mutable_reference);
  AssignStatement assign;
  assign.dest = make_local_place(temp_local);
  assign.src = ValueSource{value};
  Statement stmt;
  stmt.value = std::move(assign);
  append_statement(std::move(stmt));
  return make_local_place(temp_local);
}

TempId
FunctionLowerer::materialize_place_base(const hir::Expr &base_expr,
                                        const semantic::ExprInfo &base_info) {
  if (!base_info.has_type || base_info.type == invalid_type_id) {
    throw std::logic_error(
        "Expression base missing resolved type during MIR lowering");
  }
  Operand base_operand;
  if (base_info.is_place) {
    base_operand =
        load_place_value(lower_expr_place(base_expr), base_info.type);
  } else {
    base_operand = lower_operand(base_expr);
  }
  return materialize_operand(base_operand, base_info.type);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::Literal &literal,
                                 const semantic::ExprInfo &info) {
  return emit_rvalue_to_temp(build_literal_rvalue(literal, info), info.type);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::StructLiteral &struct_literal,
                                 const semantic::ExprInfo &info) {
  return emit_rvalue_to_temp(build_struct_aggregate(struct_literal), info.type);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::ArrayLiteral &array_literal,
                                 const semantic::ExprInfo &info) {
  return emit_rvalue_to_temp(build_array_aggregate(array_literal), info.type);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::ArrayRepeat &array_repeat,
                                 const semantic::ExprInfo &info) {
  // Array repeats are aggregated values: construct using InitArrayRepeat pattern
  // Allocate a temporary local to hold the result
  TypeId normalized = canonicalize_type_for_mir(info.type);
  LocalId temp_local = create_synthetic_local(normalized, false);
  Place temp_place = make_local_place(temp_local);
  
  // Use place-directed init (same as array literals)
  lower_array_repeat_init(array_repeat, std::move(temp_place), normalized);
  
  // Load the constructed value from the temporary
  return load_place_value(make_local_place(temp_local), normalized);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::Variable &variable,
                                 const semantic::ExprInfo &info) {
  return load_place_value(lower_place_impl(variable, info), info.type);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::ConstUse &const_use,
                                 const semantic::ExprInfo &info) {
  if (!const_use.def) {
    throw std::logic_error("Const use missing definition during MIR lowering");
  }
  TypeId type = info.type;
  if (type == invalid_type_id && const_use.def->type) {
    type = hir::helper::get_resolved_type(*const_use.def->type);
  }
  if (type == invalid_type_id) {
    throw std::logic_error(
        "Const use missing resolved type during MIR lowering");
  }
  Constant constant = lower_const_definition(*const_use.def, type);
  return make_constant_operand(constant);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::StructConst &struct_const,
                                 const semantic::ExprInfo &info) {
  if (!struct_const.assoc_const) {
    throw std::logic_error(
        "Struct const missing associated const during MIR lowering");
  }
  TypeId type = info.type;
  if (type == invalid_type_id && struct_const.assoc_const->type) {
    type = hir::helper::get_resolved_type(*struct_const.assoc_const->type);
  }
  if (type == invalid_type_id) {
    throw std::logic_error(
        "Struct const missing resolved type during MIR lowering");
  }
  Constant constant = lower_const_definition(*struct_const.assoc_const, type);
  return make_constant_operand(constant);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::EnumVariant &enum_variant,
                                 const semantic::ExprInfo &info) {
  TypeId type = info.type;
  if (type == invalid_type_id) {
    if (!enum_variant.enum_def) {
      throw std::logic_error(
          "Enum variant missing enum definition during MIR lowering");
    }
    auto enum_id_opt = type::TypeContext::get_instance().try_get_enum_id(
        enum_variant.enum_def);
    if (!enum_id_opt) {
      throw std::logic_error(
          "Enum not registered during MIR lowering. Enum registration passes "
          "must complete before lowering.");
    }
    type = type::get_typeID(type::Type{type::EnumType{*enum_id_opt}});
  }
  Constant constant = lower_enum_variant(enum_variant, type);
  return make_constant_operand(constant);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::FieldAccess &field_access,
                                 const semantic::ExprInfo &info) {
  if (info.is_place) {
    return load_place_value(lower_place_impl(field_access, info), info.type);
  }
  if (!field_access.base) {
    throw std::logic_error("Field access missing base during MIR lowering");
  }
  semantic::ExprInfo base_info = hir::helper::get_expr_info(*field_access.base);
  TempId base_temp = materialize_place_base(*field_access.base, base_info);
  FieldAccessRValue field_rvalue{
      .base = base_temp, .index = hir::helper::get_field_index(field_access)};
  return emit_rvalue_to_temp(std::move(field_rvalue), info.type);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::Index &index_expr,
                                 const semantic::ExprInfo &info) {
  if (info.is_place) {
    return load_place_value(lower_place_impl(index_expr, info), info.type);
  }
  Place place = make_index_place(index_expr, true);
  return load_place_value(std::move(place), info.type);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::Cast &cast_expr,
                                 const semantic::ExprInfo &info) {
  if (!cast_expr.expr) {
    throw std::logic_error(
        "Cast expression missing operand during MIR lowering");
  }
  if (!info.type) {
    throw std::logic_error(
        "Cast expression missing resolved type during MIR lowering");
  }
  Operand operand = lower_operand(*cast_expr.expr);
  CastRValue cast_rvalue{.value = operand, .target_type = info.type};
  return emit_rvalue_to_temp(std::move(cast_rvalue), info.type);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::BinaryOp &binary,
                                 const semantic::ExprInfo &info) {
  if (std::get_if<hir::LogicalAnd>(&binary.op)) {
    return lower_short_circuit(binary, info, true);
  }
  if (std::get_if<hir::LogicalOr>(&binary.op)) {
    return lower_short_circuit(binary, info, false);
  }

  if (!binary.lhs || !binary.rhs) {
    throw std::logic_error(
        "Binary expression missing operand during MIR lowering");
  }

  semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*binary.lhs);
  semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*binary.rhs);

  Operand lhs = lower_operand(*binary.lhs);
  Operand rhs = lower_operand(*binary.rhs);

  BinaryOpRValue::Kind kind =
      classify_binary_kind(binary, lhs_info.type, rhs_info.type, info.type);

  BinaryOpRValue binary_value{.kind = kind, .lhs = lhs, .rhs = rhs};
  return emit_rvalue_to_temp(std::move(binary_value), info.type);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::Assignment &assignment,
                                 const semantic::ExprInfo &) {
  if (!assignment.lhs || !assignment.rhs) {
    throw std::logic_error("Assignment missing operands during MIR lowering");
  }
  if (std::get_if<hir::Underscore>(&assignment.lhs->value)) {
    const hir::Expr *rhs_expr = assignment.rhs.get();
    if (rhs_expr) {
      if (const auto *binary = std::get_if<hir::BinaryOp>(&rhs_expr->value)) {
        const hir::Expr *compound_rhs = nullptr;
        if (binary->lhs && std::get_if<hir::Underscore>(&binary->lhs->value)) {
          compound_rhs = binary->rhs.get();
        }
        if (compound_rhs) {
          // For underscore assignment, just lower for side effects
          (void)lower_expr(*compound_rhs);
        } else {
          // For underscore assignment, just lower for side effects
          (void)lower_expr(*assignment.rhs);
        }
      } else {
        // For underscore assignment, just lower for side effects
        (void)lower_expr(*assignment.rhs);
      }
    }
    return std::nullopt;
  }

  semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*assignment.lhs);
  semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*assignment.rhs);

  if (lhs_info.is_place && rhs_info.is_place && lhs_info.has_type &&
      rhs_info.has_type && lhs_info.type == rhs_info.type &&
      is_aggregate_type(lhs_info.type)) {

    Place dest_place = lower_expr_place(*assignment.lhs);
    Place src_place = lower_expr_place(*assignment.rhs);

    if (are_places_definitely_disjoint(dest_place, src_place)) {
      InitCopy copy{.src = std::move(src_place)};
      InitPattern pattern;
      pattern.value = std::move(copy);

      InitStatement init_stmt;
      init_stmt.dest = std::move(dest_place);
      init_stmt.pattern = std::move(pattern);

      Statement stmt;
      stmt.value = std::move(init_stmt);
      append_statement(std::move(stmt));
      return std::nullopt;
    }
    
    // Optimization doesn't apply; reuse already-lowered places
    // to avoid double-evaluation of RHS place expression
    Operand value = load_place_value(std::move(src_place), rhs_info.type);
    AssignStatement assign{.dest = std::move(dest_place), .src = ValueSource{std::move(value)}};
    Statement stmt;
    stmt.value = std::move(assign);
    append_statement(std::move(stmt));
    return std::nullopt;
  }

  Place dest = lower_expr_place(*assignment.lhs);
  Operand value = expect_operand(lower_expr(*assignment.rhs),
                                 "Assignment rhs must produce value");
  AssignStatement assign{.dest = std::move(dest), .src = ValueSource{std::move(value)}};
  Statement stmt;
  stmt.value = std::move(assign);
  append_statement(std::move(stmt));
  return std::nullopt;
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::Block &block_expr,
                                 const semantic::ExprInfo &info) {
  return lower_block_expr(block_expr, info.type);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::If &if_expr,
                                 const semantic::ExprInfo &info) {
  return lower_if_expr(if_expr, info);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::Loop &loop_expr,
                                 const semantic::ExprInfo &info) {
  return lower_loop_expr(loop_expr, info);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::While &while_expr,
                                 const semantic::ExprInfo &info) {
  return lower_while_expr(while_expr, info);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::Break &break_expr,
                                 const semantic::ExprInfo &) {
  return lower_break_expr(break_expr);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::Continue &continue_expr,
                                 const semantic::ExprInfo &) {
  return lower_continue_expr(continue_expr);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::Return &return_expr,
                                 const semantic::ExprInfo &) {
  return lower_return_expr(return_expr);
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::Call &call_expr,
                                 const semantic::ExprInfo &info) {
  if (!call_expr.callee) {
    throw std::logic_error(
        "Call expression missing callee during MIR lowering");
  }
  const auto *func_use = std::get_if<hir::FuncUse>(&call_expr.callee->value);
  if (!func_use || !func_use->def) {
    throw std::logic_error(
        "Call expression callee is not a resolved function use");
  }

  const hir::Function *hir_fn = func_use->def;
  mir::FunctionRef target = lookup_function(hir_fn);
  const MirFunctionSig& callee_sig = get_callee_sig(target);
  
  // Build CallSite for unified lowering
  CallSite cs;
  cs.target = target;
  cs.callee_sig = &callee_sig;
  cs.result_type = info.type;
  cs.ctx = CallSite::Context::Expr;
  
  // For function calls: args are the direct arguments
  cs.args_exprs.reserve(call_expr.args.size());
  for (const auto& arg : call_expr.args) {
    if (!arg) {
      throw std::logic_error("Function call argument missing");
    }
    cs.args_exprs.push_back(arg.get());
  }
  
  // Handle SRET result placement
  if (std::holds_alternative<mir::ReturnDesc::RetIndirectSRet>(callee_sig.return_desc.kind)) {
    // Create synthetic local to receive result
    LocalId tmp_local = create_synthetic_local(info.type, /*is_mut_ref*/ false);
    cs.sret_dest = make_local_place(tmp_local);
    
    // Lower the call with sret destination
    lower_callsite(cs);
    
    // Load result from sret destination
    return load_place_value(*cs.sret_dest, info.type);
  } else {
    // Direct return: lower without sret destination
    cs.sret_dest = std::nullopt;
    return lower_callsite(cs);
  }
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::MethodCall &method_call,
                                 const semantic::ExprInfo &info) {
  const hir::Method *method_def = hir::helper::get_method_def(method_call);
  if (!method_call.receiver) {
    throw std::logic_error("Method call missing receiver during MIR lowering");
  }

  mir::FunctionRef target = lookup_function(method_def);
  const MirFunctionSig& callee_sig = get_callee_sig(target);
  
  // Build CallSite for unified lowering
  // For method calls: args_exprs = [receiver] + method_call.args
  CallSite cs;
  cs.target = target;
  cs.callee_sig = &callee_sig;
  cs.result_type = info.type;
  cs.ctx = CallSite::Context::Expr;
  
  // Receiver is the first argument
  cs.args_exprs.push_back(method_call.receiver.get());
  
  // Add explicit arguments
  for (const auto& arg : method_call.args) {
    if (!arg) {
      throw std::logic_error("Method call argument missing during MIR lowering");
    }
    cs.args_exprs.push_back(arg.get());
  }
  
  // Handle SRET result placement
  if (std::holds_alternative<mir::ReturnDesc::RetIndirectSRet>(callee_sig.return_desc.kind)) {
    // Create synthetic local to receive result
    LocalId tmp_local = create_synthetic_local(info.type, /*is_mut_ref*/ false);
    cs.sret_dest = make_local_place(tmp_local);
    
    // Lower the call with sret destination
    lower_callsite(cs);
    
    // Load result from sret destination
    return load_place_value(*cs.sret_dest, info.type);
  } else {
    // Direct return: lower without sret destination
    cs.sret_dest = std::nullopt;
    return lower_callsite(cs);
  }
}

std::optional<Operand>
FunctionLowerer::lower_expr_impl(const hir::UnaryOp &unary,
                                 const semantic::ExprInfo &info) {
  if (!unary.rhs) {
    throw std::logic_error(
        "Unary expression missing operand during MIR lowering");
  }
  return std::visit(
      Overloaded{
          [&](const hir::UnaryNot &) {
            UnaryOpRValue unary_rvalue{
                .kind = UnaryOpRValue::Kind::Not,
                .operand = lower_operand(*unary.rhs)};
            return emit_rvalue_to_temp(std::move(unary_rvalue), info.type);
          },
          [&](const hir::UnaryNegate &) {
            UnaryOpRValue unary_rvalue{
                .kind = UnaryOpRValue::Kind::Neg,
                .operand = lower_operand(*unary.rhs)};
            return emit_rvalue_to_temp(std::move(unary_rvalue), info.type);
          },
          [&](const hir::Reference &reference) {
            semantic::ExprInfo operand_info =
                hir::helper::get_expr_info(*unary.rhs);
            Place place = ensure_reference_operand_place(
                *unary.rhs, operand_info, reference.is_mutable);
            RefRValue ref_rvalue{.place = std::move(place)};
            return emit_rvalue_to_temp(std::move(ref_rvalue), info.type);
          },
          [&](const hir::Dereference &) {
            return load_place_value(lower_place_impl(unary, info), info.type);
          }},
      unary.op);
}

std::optional<Operand>
FunctionLowerer::lower_if_expr(const hir::If &if_expr,
                               const semantic::ExprInfo &info) {
  Operand condition = lower_operand(*if_expr.condition);
  if (!current_block) {
    return std::nullopt;
  }

  bool has_else = if_expr.else_expr && *if_expr.else_expr;
  if (!has_else && !is_unit_type(info.type)) {
    throw std::logic_error(
        "If expression missing else branch for non-unit type");
  }

  BasicBlockId then_block = create_block();
  std::optional<BasicBlockId> else_block =
      has_else ? std::optional<BasicBlockId>(create_block()) : std::nullopt;
  BasicBlockId join_block = create_block();

  BasicBlockId false_target = else_block ? *else_block : join_block;
  branch_on_bool(condition, then_block, false_target);

  bool result_needed = !is_unit_type(info.type) && !is_never_type(info.type);
  std::vector<PhiIncoming> phi_incomings;

  // THEN
  switch_to_block(then_block);
  std::optional<Operand> then_value =
      lower_block_expr(*if_expr.then_block, info.type);
  std::optional<BasicBlockId> then_fallthrough =
      current_block ? std::optional<BasicBlockId>(*current_block)
                    : std::nullopt;
  if (then_fallthrough) {
    if (result_needed) {
      TempId value_temp = materialize_operand(
          expect_operand(std::move(then_value),
                         "Then branch must produce value"),
          info.type);
      phi_incomings.push_back(PhiIncoming{*then_fallthrough, value_temp});
    }
    add_goto_from_current(join_block);
  }

  std::optional<BasicBlockId> else_fallthrough;
  if (has_else) {
    switch_to_block(*else_block);
    std::optional<Operand> else_value = lower_expr(**if_expr.else_expr);
    else_fallthrough =
        current_block ? std::optional<BasicBlockId>(*current_block)
                      : std::nullopt;
    if (else_fallthrough) {
      if (result_needed) {
        TempId value_temp = materialize_operand(
            expect_operand(std::move(else_value),
                           "Else branch must produce value"),
            info.type);
        phi_incomings.push_back(PhiIncoming{*else_fallthrough, value_temp});
      }
      add_goto_from_current(join_block);
    }
  }

  bool then_reachable = then_fallthrough.has_value();
  bool else_reachable = has_else && else_fallthrough.has_value();
  bool join_reachable = then_reachable || else_reachable || !has_else;

  if (join_reachable) {
    current_block = join_block;
  } else {
    current_block.reset();
  }

  if (result_needed) {
    if (phi_incomings.empty()) {
      current_block.reset();
      return std::nullopt;
    }
    TempId dest = allocate_temp(info.type);
    PhiNode phi;
    phi.dest = dest;
    phi.incoming = std::move(phi_incomings);
    mir_function.basic_blocks[join_block].phis.push_back(std::move(phi));
    return make_temp_operand(dest);
  }

  return std::nullopt;
}


std::optional<Operand> FunctionLowerer::lower_short_circuit(
    const hir::BinaryOp &binary, const semantic::ExprInfo &info, bool is_and) {
  Operand lhs = lower_operand(*binary.lhs);
  if (!current_block) {
    return std::nullopt;
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

  branch_on_bool(lhs_operand, is_and ? rhs_block : join_block,
                 is_and ? join_block : rhs_block);

  std::vector<PhiIncoming> incomings;
  incomings.push_back(PhiIncoming{lhs_block, short_value_temp});

  switch_to_block(rhs_block);
  Operand rhs = lower_operand(*binary.rhs);
  std::optional<BasicBlockId> rhs_fallthrough =
      current_block ? std::optional<BasicBlockId>(*current_block)
                    : std::nullopt;
  if (rhs_fallthrough) {
    TempId rhs_temp = materialize_operand(rhs, rhs_info.type);
    incomings.push_back(PhiIncoming{*rhs_fallthrough, rhs_temp});
    add_goto_from_current(join_block);
  }

  if (incomings.empty()) {
    current_block.reset();
    return std::nullopt;
  }

  current_block = join_block;
  TempId dest = allocate_temp(info.type);
  PhiNode phi;
  phi.dest = dest;
  phi.incoming = incomings;
  mir_function.basic_blocks[join_block].phis.push_back(std::move(phi));
  return make_temp_operand(dest);
}

std::optional<Operand> FunctionLowerer::lower_loop_expr(
    const hir::Loop &loop_expr,
    [[maybe_unused]] const semantic::ExprInfo &info) {
  LowerResult res = lower_loop_node(loop_expr, info, std::nullopt);
  return materialize_result_operand(res, info);
}

std::optional<Operand> FunctionLowerer::lower_while_expr(
    const hir::While &while_expr,
    [[maybe_unused]] const semantic::ExprInfo &info) {
  LowerResult res = lower_while_node(while_expr, info, std::nullopt);
  return materialize_result_operand(res, info);
}

std::optional<Operand>
FunctionLowerer::lower_break_expr(const hir::Break &break_expr) {
  semantic::ExprInfo info = {};
  if (break_expr.value) {
    info = hir::helper::get_expr_info(**break_expr.value);
  }
  LowerResult res = lower_break_node(break_expr, info, std::nullopt);
  if (res.is_written()) {
    return std::nullopt;
  }
  return materialize_result_operand(res, info);
}

std::optional<Operand>
FunctionLowerer::lower_continue_expr(const hir::Continue &continue_expr) {
  LowerResult res =
      lower_continue_node(continue_expr, semantic::ExprInfo{}, std::nullopt);
  if (res.is_written()) {
    return std::nullopt;
  }
  return materialize_result_operand(res, semantic::ExprInfo{});
}

void FunctionLowerer::handle_return_value(const std::optional<std::unique_ptr<hir::Expr>>& value_ptr, 
                                            const char *context) {
  // Central place to handle all return types using the return storage plan
  
  const auto& return_desc = mir_function.sig.return_desc;

  // Case 1: function returns `never` - diverges unconditionally
  if (is_never(return_desc)) {
    if (value_ptr && *value_ptr) {
      (void)lower_node(**value_ptr);
    }
    if (is_reachable()) {
      throw std::logic_error(std::string(context) +
                             ": diverging function must not reach here");
    }
    UnreachableTerminator term{};
    terminate_current_block(Terminator{std::move(term)});
    return;
  }

  // Case 2: sret return - indirect return via caller-allocated result location
  if (is_indirect_sret(return_desc)) {
    if (!value_ptr || !*value_ptr) {
      throw std::logic_error(std::string(context) +
                             ": sret function requires explicit return value");
    }

    // Use the return plan to determine the result destination
    if (!return_plan.is_sret) {
      throw std::logic_error(std::string(context) +
                             ": return descriptor is sret but plan is not");
    }

    Place result_place = return_plan.return_place();

    semantic::ExprInfo info = hir::helper::get_expr_info(**value_ptr);
    LowerResult res = lower_node(**value_ptr, result_place);
    res.write_to_dest(*this, result_place, info);
    
    // For sret, we emit a void return since the result is stored in the result_local
    emit_return(std::nullopt);
    return;
  }

  // Case 3: void semantic return
  if (is_void_semantic(return_desc)) {
    // Void semantic: compute the value (for side effects) but don't return it
    if (value_ptr && *value_ptr) {
      (void)lower_node(**value_ptr);
    }
    emit_return(std::nullopt);
    return;
  }

  // Case 4: direct return - value is returned directly via return value
  if (std::holds_alternative<ReturnDesc::RetDirect>(return_desc.kind)) {
    std::optional<Operand> value;
    if (value_ptr && *value_ptr) {
      value = lower_node_operand(**value_ptr);
    }
    if(!is_reachable()){
      // Function is not reachable here - no return needed
      return;
    }

    if (!value) {
      throw std::logic_error(std::string(context) +
                             ": missing return value for direct return function");
    }

    TypeId ret_type = return_plan.ret_type;

    const bool is_const_like =
        value_ptr &&
        std::visit(
            Overloaded{
                [](const hir::ConstUse &) { return true; },
                [](const hir::StructConst &) { return true; },
                [](const hir::EnumVariant &) { return true; },
                [](const auto &) { return false; }},
            (**value_ptr).value);

    if (is_const_like) {
      emit_return(std::move(value));
      return;
    }

    TempId ret_temp = materialize_operand(*value, ret_type);
    emit_return(make_temp_operand(ret_temp));
    return;
  }

  // Should not reach here - all cases handled above
  throw std::logic_error(std::string(context) + 
                        ": unhandled return descriptor type");
}

std::optional<Operand>
FunctionLowerer::lower_return_expr(const hir::Return &return_expr) {
  handle_return_value(return_expr.value, "Return statement");
  return std::nullopt;
}

} // namespace mir::detail
