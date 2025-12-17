#include "mir/lower/lower_internal.hpp"

#include "mir/lower/lower_result.hpp"
#include "semantic/expr_info_helpers.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/utils.hpp"
#include "type/type.hpp"

namespace mir::detail {

namespace {

bool is_underscore_local(const hir::Local &local) {
  return local.name.name == "_";
}

} // namespace

template <typename T>
LowerResult FunctionLowerer::visit_node(const T &, const semantic::ExprInfo &,
                                        std::optional<Place>) {
  throw std::logic_error(
      "Expression kind not supported yet in MIR lowering (lower_node)");
}

LowerResult FunctionLowerer::lower_node(const hir::Expr &expr,
                                        std::optional<Place> dest_hint) {
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);
  bool was_reachable = is_reachable();

  LowerResult result = std::visit(
      Overloaded{
          [&](const hir::Literal &node) {
            return visit_literal(node, info, dest_hint);
          },
          [&](const hir::Variable &node) {
            return visit_variable(node, info, dest_hint);
          },
          [&](const hir::ConstUse &node) {
            return visit_const_use(node, info, dest_hint);
          },
          [&](const hir::StructConst &node) {
            return visit_struct_const(node, info, dest_hint);
          },
          [&](const hir::EnumVariant &node) {
            return visit_enum_variant(node, info, dest_hint);
          },
          [&](const hir::FieldAccess &node) {
            return visit_field_access(node, info, dest_hint);
          },
          [&](const hir::Index &node) {
            return visit_index(node, info, dest_hint);
          },
          [&](const hir::Cast &node) {
            return visit_cast(node, info, dest_hint);
          },
          [&](const hir::UnaryOp &node) {
            return visit_unary(node, info, dest_hint);
          },
          [&](const hir::StructLiteral &node) {
            return visit_struct_literal(node, info, dest_hint);
          },
          [&](const hir::ArrayLiteral &node) {
            return visit_array_literal(node, info, dest_hint);
          },
          [&](const hir::ArrayRepeat &node) {
            return visit_array_repeat(node, info, dest_hint);
          },
          [&](const hir::BinaryOp &node) {
            return visit_binary(node, info, dest_hint);
          },
          [&](const hir::Assignment &node) {
            return visit_assignment(node, info, dest_hint);
          },
          [&](const hir::Block &node) { return visit_block(node, info, dest_hint); },
          [&](const hir::If &node) { return visit_if(node, info, dest_hint); },
          [&](const hir::Loop &node) { return visit_loop(node, info, dest_hint); },
          [&](const hir::While &node) { return visit_while(node, info, dest_hint); },
          [&](const hir::Break &node) { return visit_break(node, info, dest_hint); },
          [&](const hir::Continue &node) {
            return visit_continue(node, info, dest_hint);
          },
          [&](const hir::Return &node) {
            return visit_return(node, info, dest_hint);
          },
          [&](const hir::Call &node) { return visit_call(node, info, dest_hint); },
          [&](const hir::MethodCall &node) {
            return visit_method_call(node, info, dest_hint);
          },
          [&](const auto &node) { return visit_node(node, info, dest_hint); }},
      expr.value);

  if (was_reachable && semantic::diverges(info) && is_reachable()) {
    throw std::logic_error("MIR lowering bug: semantically diverging expression "
                           "leaves MIR reachable");
  }

  return result;
}

Place FunctionLowerer::lower_node_place(const hir::Expr &expr) {
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);
  if (!info.is_place) {
    throw std::logic_error("Expression is not a place in MIR lowering");
  }
  LowerResult res = lower_node(expr);
  return res.as_place(*this, info);
}

Operand FunctionLowerer::lower_node_operand(const hir::Expr &expr) {
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);
  LowerResult res = lower_node(expr);
  auto materialized = materialize_result_operand(res, info);
  if (!materialized) {
    return Operand{};
  }
  return *materialized;
}

void FunctionLowerer::lower_stmt_node(const hir::Stmt &stmt) {
  if (!is_reachable()) {
    return;
  }
  std::visit(
      Overloaded{
          [&](const hir::LetStmt &let_stmt) {
            if (!let_stmt.pattern || !let_stmt.initializer) {
              throw std::logic_error(
                  "Let statement missing components during MIR lowering");
            }
            const hir::Expr &init_expr = *let_stmt.initializer;
            semantic::ExprInfo init_info = hir::helper::get_expr_info(init_expr);
            std::visit(
                Overloaded{
                    [&](const hir::BindingDef &binding) {
                      hir::Local *local = hir::helper::get_local(binding);
                      if (!local) {
                        throw std::logic_error("Let binding missing local during "
                                               "MIR lowering");
                      }
                      if (is_underscore_local(*local)) {
                        (void)lower_node(init_expr);
                        return;
                      }
                      if (!local->type_annotation) {
                        throw std::logic_error(
                            "Let binding missing resolved type during MIR lowering");
                      }
                      Place dest = make_local_place(local);
                      LowerResult res = lower_node(init_expr, dest);
                      res.write_to_dest(*this, std::move(dest), init_info);
                    },
                    [&](const hir::ReferencePattern &ref_pattern) {
                      lower_reference_let(ref_pattern, init_expr);
                    },
                    [&](const auto &) {
                      throw std::logic_error(
                          "Unsupported pattern variant in let statement");
                    }},
                let_stmt.pattern->value);
          },
          [&](const hir::ExprStmt &expr_stmt) {
            if (!expr_stmt.expr) {
              return;
            }
            semantic::ExprInfo info =
                hir::helper::get_expr_info(*expr_stmt.expr);
            bool expect_fallthrough = semantic::has_normal_endpoint(info);
            (void)lower_node(*expr_stmt.expr);
            if (!expect_fallthrough && is_reachable()) {
              throw std::logic_error("ExprStmt divergence mismatch: semantically "
                                     "diverging expression leaves block reachable");
            }
          }},
      stmt.value);
}

LowerResult FunctionLowerer::visit_literal(const hir::Literal &literal,
                                           const semantic::ExprInfo &info,
                                           std::optional<Place>) {
  if (!info.has_type || info.type == invalid_type_id) {
    throw std::logic_error("Literal missing resolved type during MIR lowering");
  }
  Constant constant = lower_literal(literal, info.type);
  return LowerResult::from_operand(make_constant_operand(std::move(constant)));
}

LowerResult FunctionLowerer::visit_variable(const hir::Variable &variable,
                                            const semantic::ExprInfo &info,
                                            std::optional<Place>) {
  return LowerResult::from_place(lower_place_impl(variable, info));
}

LowerResult FunctionLowerer::visit_const_use(const hir::ConstUse &const_use,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place>) {
  if (!const_use.def) {
    throw std::logic_error("Const use missing definition during MIR lowering");
  }
  TypeId type = info.type;
  if (type == invalid_type_id && const_use.def->type) {
    type = hir::helper::get_resolved_type(*const_use.def->type);
  }
  if (type == invalid_type_id) {
    throw std::logic_error("Const use missing resolved type during MIR lowering");
  }
  Constant constant = lower_const_definition(*const_use.def, type);
  return LowerResult::from_operand(make_constant_operand(std::move(constant)));
}

LowerResult
FunctionLowerer::visit_struct_const(const hir::StructConst &struct_const,
                                    const semantic::ExprInfo &info,
                                    std::optional<Place>) {
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
  return LowerResult::from_operand(make_constant_operand(std::move(constant)));
}

LowerResult
FunctionLowerer::visit_enum_variant(const hir::EnumVariant &enum_variant,
                                    const semantic::ExprInfo &info,
                                    std::optional<Place>) {
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
  return LowerResult::from_operand(make_constant_operand(std::move(constant)));
}

LowerResult
FunctionLowerer::visit_field_access(const hir::FieldAccess &field_access,
                                    const semantic::ExprInfo &info,
                                    std::optional<Place> dest_hint) {
  if (info.is_place) {
    return LowerResult::from_place(lower_place_impl(field_access, info));
  }
  if (!field_access.base) {
    throw std::logic_error("Field access missing base during MIR lowering");
  }
  semantic::ExprInfo base_info = hir::helper::get_expr_info(*field_access.base);
  TempId base_temp = materialize_place_base(*field_access.base, base_info);
  FieldAccessRValue field_rvalue{
      .base = base_temp, .index = hir::helper::get_field_index(field_access)};
  (void)dest_hint;
  return LowerResult::from_operand(
      emit_rvalue_to_temp(std::move(field_rvalue), info.type));
}

LowerResult FunctionLowerer::visit_index(const hir::Index &index_expr,
                                         const semantic::ExprInfo &info,
                                         std::optional<Place> dest_hint) {
  if (info.is_place) {
    return LowerResult::from_place(lower_place_impl(index_expr, info));
  }
  Place place = make_index_place(index_expr, true);
  (void)dest_hint;
  return LowerResult::from_operand(load_place_value(std::move(place), info.type));
}

LowerResult FunctionLowerer::visit_cast(const hir::Cast &cast_expr,
                                        const semantic::ExprInfo &info,
                                        std::optional<Place> dest_hint) {
  (void)dest_hint;
  if (!cast_expr.expr) {
    throw std::logic_error("Cast expression missing operand during MIR lowering");
  }
  Operand operand = lower_node_operand(*cast_expr.expr);
  CastRValue cast_rvalue{.value = operand, .target_type = info.type};
  return LowerResult::from_operand(
      emit_rvalue_to_temp(std::move(cast_rvalue), info.type));
}

LowerResult FunctionLowerer::visit_unary(const hir::UnaryOp &unary,
                                         const semantic::ExprInfo &info,
                                         std::optional<Place> dest_hint) {
  (void)dest_hint;
  if (!unary.rhs) {
    throw std::logic_error(
        "Unary expression missing operand during MIR lowering");
  }
  return std::visit(
      Overloaded{
          [&](const hir::UnaryNot &) {
            UnaryOpRValue unary_rvalue{.kind = UnaryOpRValue::Kind::Not,
                                       .operand = lower_node_operand(*unary.rhs)};
            return LowerResult::from_operand(
                emit_rvalue_to_temp(std::move(unary_rvalue), info.type));
          },
          [&](const hir::UnaryNegate &) {
            UnaryOpRValue unary_rvalue{.kind = UnaryOpRValue::Kind::Neg,
                                       .operand = lower_node_operand(*unary.rhs)};
            return LowerResult::from_operand(
                emit_rvalue_to_temp(std::move(unary_rvalue), info.type));
          },
          [&](const hir::Reference &reference) {
            semantic::ExprInfo operand_info =
                hir::helper::get_expr_info(*unary.rhs);
            Place place = ensure_reference_operand_place(
                *unary.rhs, operand_info, reference.is_mutable);
            RefRValue ref_rvalue{.place = std::move(place)};
            return LowerResult::from_operand(
                emit_rvalue_to_temp(std::move(ref_rvalue), info.type));
          },
          [&](const hir::Dereference &) {
            return LowerResult::from_operand(
                load_place_value(lower_place_impl(unary, info), info.type));
          }},
      unary.op);
}

LowerResult
FunctionLowerer::visit_struct_literal(const hir::StructLiteral &struct_literal,
                                      const semantic::ExprInfo &info,
                                      std::optional<Place> dest_hint) {
  if (!info.has_type || info.type == invalid_type_id) {
    throw std::logic_error("Struct literal missing type during MIR lowering");
  }
  
  TypeId normalized = canonicalize_type_for_mir(info.type);
  auto *struct_ty =
      std::get_if<type::StructType>(&type::get_type_from_id(normalized).value);
  if (!struct_ty) {
    throw std::logic_error(
        "Struct literal init without struct destination type");
  }

  const auto &struct_info =
      type::TypeContext::get_instance().get_struct(struct_ty->id);
  const auto &fields = hir::helper::get_canonical_fields(struct_literal);

  if (fields.initializers.size() != struct_info.fields.size()) {
    throw std::logic_error(
        "Struct literal field count mismatch during struct init");
  }

  // Determine target place (RVO: use hint if available, else allocate temp)
  Place target = dest_hint.value_or(
      make_local_place(create_synthetic_local(normalized, false)));

  // Build InitStruct pattern with fields
  InitStruct init_struct;
  init_struct.fields.resize(fields.initializers.size());

  for (std::size_t idx = 0; idx < fields.initializers.size(); ++idx) {
    if (!fields.initializers[idx]) {
      throw std::logic_error(
          "Struct literal field missing initializer during MIR lowering");
    }

    TypeId field_ty =
        canonicalize_type_for_mir(struct_info.fields[idx].type);
    if (field_ty == invalid_type_id) {
      throw std::logic_error(
          "Struct field missing resolved type during MIR lowering");
    }

    const hir::Expr &field_expr = *fields.initializers[idx];
    auto &leaf = init_struct.fields[idx];

    // Build sub-place target.field[idx]
    Place field_place = target;
    field_place.projections.push_back(FieldProjection{idx});

    // Try place-directed initialization for nested aggregates/calls (V2 path)
    // Fallback to operand if that doesn't work
    if (try_lower_init_outside(field_expr, std::move(field_place), field_ty)) {
      // Field was written directly to the place
      leaf = make_omitted_leaf();
    } else {
      // Fallback: compute an Operand and store in pattern
      Operand field_op = lower_operand(field_expr);
      leaf = make_value_leaf(std::move(field_op));
    }
  }

  // Emit init statement with the struct pattern
  InitPattern pattern;
  pattern.value = std::move(init_struct);
  emit_init_statement(std::move(target), std::move(pattern));

  // Return appropriate result based on whether we used the hint
  if (dest_hint) {
    return LowerResult::written();
  }
  return LowerResult::from_place(std::move(target));
}

LowerResult
FunctionLowerer::visit_array_literal(const hir::ArrayLiteral &array_literal,
                                     const semantic::ExprInfo &info,
                                     std::optional<Place> dest_hint) {
  if (!info.has_type || info.type == invalid_type_id) {
    throw std::logic_error("Array literal missing type during MIR lowering");
  }
  
  TypeId normalized = canonicalize_type_for_mir(info.type);
  
  // Determine target place (RVO: use hint if available, else allocate temp)
  Place target = dest_hint.value_or(
      make_local_place(create_synthetic_local(normalized, false)));

  // Build InitArrayLiteral pattern with elements
  InitArrayLiteral init_array;
  init_array.elements.resize(array_literal.elements.size());

  // Get the element type from the array type
  const type::Type &array_ty = type::get_type_from_id(normalized);
  const auto *array_type_info = std::get_if<type::ArrayType>(&array_ty.value);
  if (!array_type_info) {
    throw std::logic_error("Array literal init requires array destination type");
  }

  for (std::size_t idx = 0; idx < array_literal.elements.size(); ++idx) {
    const auto &elem_expr_ptr = array_literal.elements[idx];
    if (!elem_expr_ptr) {
      throw std::logic_error(
          "Array literal element missing during MIR lowering");
    }

    const hir::Expr &elem_expr = *elem_expr_ptr;
    auto &leaf = init_array.elements[idx];

    // Build sub-place target[idx]
    Place elem_place = target;
    TypeId usize_ty = type::get_typeID(type::Type{type::PrimitiveKind::USIZE});
    Operand idx_operand = make_const_operand(idx, usize_ty, false);
    elem_place.projections.push_back(IndexProjection{std::move(idx_operand)});

    // Try place-directed initialization for nested aggregates/calls (V2 path)
    // Fallback to operand if that doesn't work
    if (try_lower_init_outside(elem_expr, std::move(elem_place), array_type_info->element_type)) {
      leaf = make_omitted_leaf();
    } else {
      // Fallback: compute an Operand and store in pattern
      Operand elem_op = lower_operand(elem_expr);
      leaf = make_value_leaf(std::move(elem_op));
    }
  }

  // Emit init statement with the array pattern
  InitPattern pattern;
  pattern.value = std::move(init_array);
  emit_init_statement(std::move(target), std::move(pattern));

  // Return appropriate result based on whether we used the hint
  if (dest_hint) {
    return LowerResult::written();
  }
  return LowerResult::from_place(std::move(target));
}

LowerResult
FunctionLowerer::visit_array_repeat(const hir::ArrayRepeat &array_repeat,
                                    const semantic::ExprInfo &info,
                                    std::optional<Place> dest_hint) {
  if (!info.has_type || info.type == invalid_type_id) {
    throw std::logic_error("Array repeat missing type during MIR lowering");
  }
  
  TypeId normalized = canonicalize_type_for_mir(info.type);
  
  // Determine target place (RVO: use hint if available, else allocate temp)
  Place target = dest_hint.value_or(
      make_local_place(create_synthetic_local(normalized, false)));

  // Build InitArrayRepeat pattern
  InitArrayRepeat init_repeat;
  
  // Extract count from variant
  if (std::holds_alternative<size_t>(array_repeat.count)) {
    init_repeat.count = std::get<size_t>(array_repeat.count);
  } else {
    throw std::logic_error(
        "Array repeat count must be a compile-time constant during MIR lowering");
  }

  // Get the element type from the array type
  const type::Type &array_ty = type::get_type_from_id(normalized);
  const auto *array_type_info = std::get_if<type::ArrayType>(&array_ty.value);
  if (!array_type_info) {
    throw std::logic_error("Array repeat init requires array destination type");
  }

  // Try to initialize the element via place-directed init
  Place elem_place = target;
  IntConstant zero_const;
  zero_const.value = 0;
  zero_const.is_negative = false;
  zero_const.is_signed = false;
  Constant c;
  c.type = type::get_typeID(type::Type{type::PrimitiveKind::USIZE});
  c.value = std::move(zero_const);
  Operand zero_operand;
  zero_operand.value = std::move(c);
  elem_place.projections.push_back(IndexProjection{std::move(zero_operand)});

  // Try place-directed initialization for nested aggregates/calls (V2 path)
  // Fallback to operand if that doesn't work
  if (try_lower_init_outside(*array_repeat.value, std::move(elem_place), array_type_info->element_type)) {
    init_repeat.element = make_omitted_leaf();
  } else {
    // Fallback: compute an Operand and store in pattern
    Operand elem_op = lower_operand(*array_repeat.value);
    init_repeat.element = make_value_leaf(std::move(elem_op));
  }

  // Emit init statement with the array repeat pattern
  InitPattern pattern;
  pattern.value = std::move(init_repeat);
  emit_init_statement(std::move(target), std::move(pattern));

  // Return appropriate result based on whether we used the hint
  if (dest_hint) {
    return LowerResult::written();
  }
  return LowerResult::from_place(std::move(target));
}

LowerResult FunctionLowerer::visit_binary(const hir::BinaryOp &binary,
                                          const semantic::ExprInfo &info,
                                          std::optional<Place> dest_hint) {
  (void)dest_hint;
  if (std::get_if<hir::LogicalAnd>(&binary.op) ||
      std::get_if<hir::LogicalOr>(&binary.op)) {
    auto res = lower_short_circuit(binary, info,
                                   std::get_if<hir::LogicalAnd>(&binary.op) !=
                                       nullptr);
    if (res) {
      return LowerResult::from_operand(*res);
    }
    return LowerResult::written();
  }

  if (!binary.lhs || !binary.rhs) {
    throw std::logic_error(
        "Binary expression missing operand during MIR lowering");
  }

  semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*binary.lhs);
  semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*binary.rhs);

  Operand lhs = lower_node_operand(*binary.lhs);
  Operand rhs = lower_node_operand(*binary.rhs);

  BinaryOpRValue::Kind kind =
      classify_binary_kind(binary, lhs_info.type, rhs_info.type, info.type);

  BinaryOpRValue binary_value{.kind = kind, .lhs = lhs, .rhs = rhs};
  return LowerResult::from_operand(
      emit_rvalue_to_temp(std::move(binary_value), info.type));
}

LowerResult FunctionLowerer::visit_assignment(const hir::Assignment &assignment,
                                              const semantic::ExprInfo &info,
                                              std::optional<Place> dest_hint) {
  (void)dest_hint;
  (void)info;
  if (!assignment.lhs || !assignment.rhs) {
    throw std::logic_error("Assignment missing operands during MIR lowering");
  }
  if (std::get_if<hir::Underscore>(&assignment.lhs->value)) {
    const hir::Expr *rhs_expr = assignment.rhs.get();
    if (rhs_expr) {
      (void)lower_node(*rhs_expr);
    }
    return LowerResult::written();
  }

  semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*assignment.lhs);
  semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*assignment.rhs);

  if (lhs_info.is_place && rhs_info.is_place && lhs_info.has_type &&
      rhs_info.has_type && lhs_info.type == rhs_info.type &&
      is_aggregate_type(lhs_info.type)) {

    Place dest_place = lower_node_place(*assignment.lhs);
    Place src_place = lower_node_place(*assignment.rhs);

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
      return LowerResult::written();
    }

    Operand value =
        load_place_value(std::move(src_place), rhs_info.type);
    AssignStatement assign{.dest = std::move(dest_place),
                           .src = ValueSource{std::move(value)}};
    Statement stmt;
    stmt.value = std::move(assign);
    append_statement(std::move(stmt));
    return LowerResult::written();
  }

  Place dest = lower_node_place(*assignment.lhs);
  Operand value = lower_node_operand(*assignment.rhs);
  AssignStatement assign{.dest = std::move(dest),
                         .src = ValueSource{std::move(value)}};
  Statement stmt;
  stmt.value = std::move(assign);
  append_statement(std::move(stmt));
  return LowerResult::written();
}

LowerResult FunctionLowerer::visit_block(const hir::Block &block,
                                         const semantic::ExprInfo &info,
                                         std::optional<Place> dest_hint) {
  if (!lower_block_statements(block)) {
    return LowerResult::written();
  }

  if (block.final_expr) {
    const auto &expr_ptr = *block.final_expr;
    if (!expr_ptr) {
      throw std::logic_error("Ownership violated: Final expression");
    }
    LowerResult res = lower_node(*expr_ptr, dest_hint);
    if (dest_hint) {
      res.write_to_dest(*this, *dest_hint, info);
      return LowerResult::written();
    }
    return res;
  }

  if (is_unit_type(info.type) || is_never_type(info.type) || !is_reachable()) {
    return LowerResult::written();
  }

  throw std::logic_error("Block expression missing value");
}

LowerResult FunctionLowerer::visit_if(const hir::If &if_expr,
                                      const semantic::ExprInfo &info,
                                      std::optional<Place> dest_hint) {
  Operand condition = lower_node_operand(*if_expr.condition);
  if (!current_block) {
    return LowerResult::written();
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

  bool result_needed =
      !dest_hint && !is_unit_type(info.type) && !is_never_type(info.type);
  std::vector<PhiIncoming> phi_incomings;

  // THEN
  switch_to_block(then_block);
  LowerResult then_res = visit_block(*if_expr.then_block, info, dest_hint);
  std::optional<BasicBlockId> then_fallthrough =
      current_block ? std::optional<BasicBlockId>(*current_block)
                    : std::nullopt;
  if (then_fallthrough) {
    if (dest_hint) {
      then_res.write_to_dest(*this, *dest_hint, info);
    } else if (result_needed) {
      TempId value_temp =
          materialize_operand(then_res.as_operand(*this, info), info.type);
      phi_incomings.push_back(PhiIncoming{*then_fallthrough, value_temp});
    }
    add_goto_from_current(join_block);
  }

  std::optional<BasicBlockId> else_fallthrough;
  if (has_else) {
    switch_to_block(*else_block);
    LowerResult else_res = lower_node(**if_expr.else_expr, dest_hint);
    else_fallthrough =
        current_block ? std::optional<BasicBlockId>(*current_block)
                      : std::nullopt;
    if (else_fallthrough) {
      if (dest_hint) {
        else_res.write_to_dest(*this, *dest_hint, info);
      } else if (result_needed) {
        TempId value_temp =
            materialize_operand(else_res.as_operand(*this, info), info.type);
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

  if (dest_hint) {
    return LowerResult::written();
  }

  if (result_needed) {
    if (phi_incomings.empty()) {
      current_block.reset();
      return LowerResult::written();
    }
    TempId dest = allocate_temp(info.type);
    PhiNode phi;
    phi.dest = dest;
    phi.incoming = std::move(phi_incomings);
    mir_function.basic_blocks[join_block].phis.push_back(std::move(phi));
    return LowerResult::from_operand(make_temp_operand(dest));
  }

  return LowerResult::written();
}

LowerResult FunctionLowerer::visit_loop(const hir::Loop &loop_expr,
                                        const semantic::ExprInfo &info,
                                        std::optional<Place> dest_hint) {
  (void)dest_hint;
  auto res = lower_loop_expr(loop_expr, info);
  if (res) {
    return LowerResult::from_operand(*res);
  }
  return LowerResult::written();
}

LowerResult FunctionLowerer::visit_while(const hir::While &while_expr,
                                         const semantic::ExprInfo &info,
                                         std::optional<Place> dest_hint) {
  (void)dest_hint;
  auto res = lower_while_expr(while_expr, info);
  if (res) {
    return LowerResult::from_operand(*res);
  }
  return LowerResult::written();
}

LowerResult FunctionLowerer::visit_break(const hir::Break &break_expr,
                                         const semantic::ExprInfo &info,
                                         std::optional<Place> dest_hint) {
  (void)dest_hint;
  (void)info;
  auto res = lower_break_expr(break_expr);
  if (res) {
    return LowerResult::from_operand(*res);
  }
  return LowerResult::written();
}

LowerResult FunctionLowerer::visit_continue(const hir::Continue &continue_expr,
                                            const semantic::ExprInfo &info,
                                            std::optional<Place> dest_hint) {
  (void)info;
  (void)dest_hint;
  (void)lower_continue_expr(continue_expr);
  return LowerResult::written();
}

LowerResult FunctionLowerer::visit_return(const hir::Return &return_expr,
                                          const semantic::ExprInfo &info,
                                          std::optional<Place> dest_hint) {
  (void)info;
  (void)dest_hint;
  (void)lower_return_expr(return_expr);
  return LowerResult::written();
}

LowerResult FunctionLowerer::visit_call(const hir::Call &call_expr,
                                        const semantic::ExprInfo &info,
                                        std::optional<Place> dest_hint) {
  if (!call_expr.callee) {
    throw std::logic_error("Call expression missing callee during MIR lowering");
  }
  const auto *func_use =
      std::get_if<hir::FuncUse>(&call_expr.callee->value);
  if (!func_use || !func_use->def) {
    throw std::logic_error(
        "Call expression callee is not a resolved function use");
  }

  const hir::Function *hir_fn = func_use->def;
  mir::FunctionRef target = lookup_function(hir_fn);
  const MirFunctionSig &callee_sig = get_callee_sig(target);

  CallSite cs;
  cs.target = target;
  cs.callee_sig = &callee_sig;
  cs.result_type = info.type;
  cs.ctx = dest_hint ? CallSite::Context::Init : CallSite::Context::Expr;

  cs.args_exprs.reserve(call_expr.args.size());
  for (const auto &arg : call_expr.args) {
    if (!arg) {
      throw std::logic_error("Function call argument missing");
    }
    cs.args_exprs.push_back(arg.get());
  }

  bool is_sret = std::holds_alternative<mir::ReturnDesc::RetIndirectSRet>(
      callee_sig.return_desc.kind);

  if (is_sret) {
    Place target_place =
        dest_hint.value_or(make_local_place(create_synthetic_local(info.type,
                                                                   false)));
    cs.sret_dest = target_place;
    lower_callsite(cs);
    if (dest_hint) {
      return LowerResult::written();
    }
    return LowerResult::from_place(std::move(target_place));
  }

  cs.sret_dest = std::nullopt;
  auto res = lower_callsite(cs);
  if (dest_hint) {
    if (!res) {
      return LowerResult::written();
    }
    LowerResult lr = LowerResult::from_operand(*res);
    lr.write_to_dest(*this, *dest_hint, info);
    return LowerResult::written();
  }
  if (res) {
    return LowerResult::from_operand(*res);
  }
  return LowerResult::written();
}

LowerResult
FunctionLowerer::visit_method_call(const hir::MethodCall &method_call,
                                   const semantic::ExprInfo &info,
                                   std::optional<Place> dest_hint) {
  const hir::Method *method_def = hir::helper::get_method_def(method_call);
  if (!method_call.receiver) {
    throw std::logic_error("Method call missing receiver during MIR lowering");
  }

  mir::FunctionRef target = lookup_function(method_def);
  const MirFunctionSig &callee_sig = get_callee_sig(target);

  CallSite cs;
  cs.target = target;
  cs.callee_sig = &callee_sig;
  cs.result_type = info.type;
  cs.ctx = dest_hint ? CallSite::Context::Init : CallSite::Context::Expr;

  cs.args_exprs.push_back(method_call.receiver.get());

  for (const auto &arg : method_call.args) {
    if (!arg) {
      throw std::logic_error("Method call argument missing during MIR lowering");
    }
    cs.args_exprs.push_back(arg.get());
  }

  bool is_sret = std::holds_alternative<mir::ReturnDesc::RetIndirectSRet>(
      callee_sig.return_desc.kind);

  if (is_sret) {
    Place target_place =
        dest_hint.value_or(make_local_place(create_synthetic_local(info.type,
                                                                   false)));
    cs.sret_dest = target_place;
    lower_callsite(cs);
    if (dest_hint) {
      return LowerResult::written();
    }
    return LowerResult::from_place(std::move(target_place));
  }

  cs.sret_dest = std::nullopt;
  auto res = lower_callsite(cs);
  if (dest_hint) {
    if (!res) {
      return LowerResult::written();
    }
    LowerResult lr = LowerResult::from_operand(*res);
    lr.write_to_dest(*this, *dest_hint, info);
    return LowerResult::written();
  }
  if (res) {
    return LowerResult::from_operand(*res);
  }
  return LowerResult::written();
}

} // namespace mir::detail
