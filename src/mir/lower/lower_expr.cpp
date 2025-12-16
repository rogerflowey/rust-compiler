#include "mir/lower/lower_internal.hpp"

#include "semantic/expr_info_helpers.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/utils.hpp"
#include "type/type.hpp"
#include <stdexcept>

namespace {

bool are_places_definitely_disjoint(const mir::Place &a, const mir::Place &b) {
  if (std::holds_alternative<mir::PointerPlace>(a.base) ||
      std::holds_alternative<mir::PointerPlace>(b.base)) {
    return false;
  }

  if ((std::holds_alternative<mir::LocalPlace>(a.base) &&
       std::holds_alternative<mir::GlobalPlace>(b.base)) ||
      (std::holds_alternative<mir::GlobalPlace>(a.base) &&
       std::holds_alternative<mir::LocalPlace>(b.base))) {
    return true;
  }

  if (const auto *lhs_local = std::get_if<mir::LocalPlace>(&a.base)) {
    if (const auto *rhs_local = std::get_if<mir::LocalPlace>(&b.base)) {
      return lhs_local->id != rhs_local->id;
    }
  }

  if (const auto *lhs_global = std::get_if<mir::GlobalPlace>(&a.base)) {
    if (const auto *rhs_global = std::get_if<mir::GlobalPlace>(&b.base)) {
      return lhs_global->global != rhs_global->global;
    }
  }

  return false;
}

} // namespace

namespace mir::detail {

// ============================================================================
// LowerResult Implementation
// ============================================================================

LowerResult LowerResult::operand(mir::Operand op) {
    return LowerResult(Kind::Operand, std::move(op));
}

LowerResult LowerResult::place(mir::Place p) {
    return LowerResult(Kind::Place, std::move(p));
}

LowerResult LowerResult::written() {
    return LowerResult(Kind::Written, std::monostate{});
}

mir::Operand LowerResult::as_operand(FunctionLowerer& fl, TypeId type) {
    switch (kind) {
        case Kind::Operand:
            return std::get<mir::Operand>(data);
        
        case Kind::Place: {
            mir::Place place = std::get<mir::Place>(data);
            return fl.load_place_value(std::move(place), type);
        }
        
        case Kind::Written:
            throw std::logic_error(
                "LowerResult::as_operand: value was already written to destination");
    }
    throw std::logic_error("LowerResult::as_operand: invalid kind");
}

mir::Place LowerResult::as_place(FunctionLowerer& fl, TypeId type) {
    switch (kind) {
        case Kind::Place:
            return std::get<mir::Place>(data);
        
        case Kind::Operand: {
            // Allocate a temporary and assign the operand to it
            mir::Operand op = std::get<mir::Operand>(data);
            LocalId temp_local = fl.create_synthetic_local(type, false);
            mir::Place temp_place = fl.make_local_place(temp_local);
            
            AssignStatement assign;
            assign.dest = temp_place;
            assign.src = ValueSource{std::move(op)};
            Statement stmt;
            stmt.value = std::move(assign);
            fl.append_statement(std::move(stmt));
            
            return temp_place;
        }
        
        case Kind::Written:
            throw std::logic_error(
                "LowerResult::as_place: value was already written to destination");
    }
    throw std::logic_error("LowerResult::as_place: invalid kind");
}

void LowerResult::write_to_dest(FunctionLowerer& fl, mir::Place dest, TypeId type) {
    switch (kind) {
        case Kind::Written:
            // No-op: value already written to destination (optimization worked!)
            return;
        
        case Kind::Operand: {
            // Emit Assign(dest, operand)
            mir::Operand op = std::get<mir::Operand>(data);
            AssignStatement assign;
            assign.dest = std::move(dest);
            assign.src = ValueSource{std::move(op)};
            Statement stmt;
            stmt.value = std::move(assign);
            fl.append_statement(std::move(stmt));
            return;
        }
        
        case Kind::Place: {
            // Emit Assign(dest, Copy(place))
            // For now, we load from the place and assign the value
            mir::Place src_place = std::get<mir::Place>(data);
            mir::Operand value = fl.load_place_value(std::move(src_place), type);
            
            AssignStatement assign;
            assign.dest = std::move(dest);
            assign.src = ValueSource{std::move(value)};
            Statement stmt;
            stmt.value = std::move(assign);
            fl.append_statement(std::move(stmt));
            return;
        }
    }
    throw std::logic_error("LowerResult::write_to_dest: invalid kind");
}

// ============================================================================
// FunctionLowerer Implementation
// ============================================================================

Operand FunctionLowerer::load_place_value(Place place, TypeId type) {
  TempId temp = allocate_temp(type);
  LoadStatement load{.dest = temp, .src = std::move(place)};
  Statement stmt;
  stmt.value = std::move(load);
  append_statement(std::move(stmt));
  return make_temp_operand(temp);
}

Operand FunctionLowerer::lower_operand(const hir::Expr &expr) {
  // Try to lower as a pure constant operand first (no temp needed).
  // Use unified API to get operand.
  if (auto const_operand = try_lower_to_const(expr)) {
    return std::move(*const_operand);
  }
  // Use unified API to get operand
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);
  LowerResult result = lower_expr(expr, std::nullopt);
  return result.as_operand(*this, info.type);
}

Operand FunctionLowerer::expect_operand(std::optional<Operand> value,
                                        const char *context) {
  if (!value) {
    throw std::logic_error(context);
  }
  return std::move(*value);
}

// === New Unified API Implementation ===

LowerResult FunctionLowerer::lower_expr(const hir::Expr &expr, std::optional<Place> maybe_dest) {
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);

  bool was_reachable = is_reachable();

  auto result = std::visit(
      [this, &info, &maybe_dest](const auto &node) { 
        return lower_expr_impl(node, info, maybe_dest); 
      },
      expr.value);

  if (was_reachable && semantic::diverges(info) && is_reachable()) {
    throw std::logic_error("MIR lowering bug: semantically diverging "
                           "expression leaves MIR reachable");
  }

  return result;
}

Place FunctionLowerer::lower_place(const hir::Expr &expr) {
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);
  if (!info.is_place) {
    throw std::logic_error("Expression is not a place in MIR lowering");
  }
  if (!info.has_type || info.type == invalid_type_id) {
    throw std::logic_error("Place expression missing resolved type");
  }
  return lower_expr(expr, std::nullopt).as_place(*this, info.type);
}

// === Place Implementation Helpers ===

Place FunctionLowerer::lower_place_impl(const hir::Variable &variable,
                                        const semantic::ExprInfo &info) {
  if (!info.is_place) {
    throw std::logic_error("Variable without place capability");
  }
  return make_local_place(variable.local_id);
}

Place FunctionLowerer::lower_place_impl(const hir::FieldAccess &field_access,
                                        const semantic::ExprInfo &) {
  if (!field_access.base) {
    throw std::logic_error("Field access missing base");
  }
  Place place = lower_place(*field_access.base);
  std::size_t index = hir::helper::get_field_index(field_access);
  place.projections.push_back(FieldProjection{index});
  return place;
}

Place FunctionLowerer::make_index_place(const hir::Index &index_expr,
                                        bool allow_temporary_base) {
  if (!index_expr.base || !index_expr.index) {
    throw std::logic_error("Index expression missing base or index");
  }
  semantic::ExprInfo base_info = hir::helper::get_expr_info(*index_expr.base);
  Place place;
  if (base_info.is_place) {
    place = lower_place(*index_expr.base);
  } else {
    if (!allow_temporary_base) {
      throw std::logic_error("Index base is not a place");
    }
    place = ensure_reference_operand_place(*index_expr.base, base_info, false);
  }
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
    throw std::logic_error("Only dereference unary ops can be lowered as places");
  }
  if (!unary.rhs) {
    throw std::logic_error("Dereference expression missing operand");
  }
  semantic::ExprInfo operand_info = hir::helper::get_expr_info(*unary.rhs);
  Operand pointer_operand = lower_operand(*unary.rhs);
  TempId pointer_temp = materialize_operand(pointer_operand, operand_info.type);
  Place deref_place;
  deref_place.base = PlaceBase{pointer_temp};
  deref_place.projections.push_back(DerefProjection{});
  return deref_place;
}

Place FunctionLowerer::ensure_reference_operand_place(
    const hir::Expr &operand, const semantic::ExprInfo &operand_info,
    bool mutable_reference) {
  if (!operand_info.has_type) {
    throw std::logic_error("Reference operand missing resolved type");
  }
  if (operand_info.is_place) {
    if (mutable_reference && !operand_info.is_mut) {
      throw std::logic_error("Mutable reference to immutable place");
    }
    return lower_place(operand);
  }

  Operand value = lower_operand(operand);
  LocalId temp_local = create_synthetic_local(operand_info.type, mutable_reference);
  AssignStatement assign;
  assign.dest = make_local_place(temp_local);
  assign.src = ValueSource{value};
  Statement stmt;
  stmt.value = std::move(assign);
  append_statement(std::move(stmt));
  return make_local_place(temp_local);
}

// === Dest-Ignorant Nodes (Scalars) ===
// These nodes ignore the destination hint and return Operand or Place

LowerResult FunctionLowerer::lower_expr_impl(const hir::Literal &literal,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> /* maybe_dest */) {
  // Literals are scalar values - ignore dest hint and return Operand
  Operand op = emit_rvalue_to_temp(build_literal_rvalue(literal, info), info.type);
  return LowerResult::operand(std::move(op));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::Variable &variable,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> /* maybe_dest */) {
  // Variables are places - return Place directly
  return LowerResult::place(lower_place_impl(variable, info));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::FieldAccess &field_access,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> /* maybe_dest */) {
  // Field accesses are places - return Place directly
  return LowerResult::place(lower_place_impl(field_access, info));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::Index &index_expr,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> /* maybe_dest */) {
  // Index expressions are places - return Place directly
  return LowerResult::place(lower_place_impl(index_expr, info));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::Cast &cast_expr,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> /* maybe_dest */) {
  // Casts are scalar operations - ignore dest hint
  if (!cast_expr.expr) {
    throw std::logic_error("Cast expression missing operand");
  }
  Operand operand = lower_operand(*cast_expr.expr);
  TypeId target_type = hir::helper::get_resolved_type(cast_expr.target_type);
  CastRValue cast_rvalue{.value = std::move(operand), .target_type = target_type};
  return LowerResult::operand(emit_rvalue_to_temp(std::move(cast_rvalue), info.type));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::BinaryOp &binary,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> /* maybe_dest */) {
  // Binary ops are scalar operations - ignore dest hint
  if (!binary.lhs || !binary.rhs) {
    throw std::logic_error("Binary operation missing operands");
  }
  semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*binary.lhs);
  semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*binary.rhs);
  Operand lhs = lower_operand(*binary.lhs);
  Operand rhs = lower_operand(*binary.rhs);
  BinaryOpRValue bin_op;
  bin_op.kind = classify_binary_kind(binary, lhs_info.type, rhs_info.type, info.type);
  bin_op.lhs = std::move(lhs);
  bin_op.rhs = std::move(rhs);
  return LowerResult::operand(emit_rvalue_to_temp(std::move(bin_op), info.type));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::UnaryOp &unary,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> /* maybe_dest */) {
  // Unary ops: deref is a place, others are scalar
  if (std::get_if<hir::Dereference>(&unary.op)) {
    // Dereference returns a place
    return LowerResult::place(lower_place_impl(unary, info));
  }
  // Other unary ops are scalar (Not, Neg, etc.)
  if (!unary.rhs) {
    throw std::logic_error("Unary operation missing operand");
  }
  UnaryOpRValue unary_rvalue;
  if (std::holds_alternative<hir::UnaryNot>(unary.op)) {
    unary_rvalue.kind = UnaryOpRValue::Kind::Not;
  } else if (std::holds_alternative<hir::UnaryNegate>(unary.op)) {
    unary_rvalue.kind = UnaryOpRValue::Kind::Neg;
  } else {
    throw std::logic_error("Unsupported unary operation");
  }
  unary_rvalue.operand = lower_operand(*unary.rhs);
  return LowerResult::operand(emit_rvalue_to_temp(std::move(unary_rvalue), info.type));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::ConstUse &const_use,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> /* maybe_dest */) {
  // Constants are scalar values - ignore dest hint
  if (!const_use.def) {
    throw std::logic_error("Const use missing definition");
  }
  TypeId type = info.type;
  ConstantRValue const_rvalue;
  const_rvalue.constant = lower_const_definition(*const_use.def, type);
  return LowerResult::operand(emit_rvalue_to_temp(std::move(const_rvalue), type));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::StructConst &struct_const,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> /* maybe_dest */) {
  // Struct constants are scalar values - ignore dest hint
  // StructConst is a zero-sized type constant
  ConstantRValue const_rvalue;
  const_rvalue.constant = Constant{};  // Empty constant for ZST
  return LowerResult::operand(emit_rvalue_to_temp(std::move(const_rvalue), info.type));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::EnumVariant &enum_variant,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> /* maybe_dest */) {
  // Enum variants are scalar values - ignore dest hint
  ConstantRValue const_rvalue;
  const_rvalue.constant = lower_enum_variant(enum_variant, info.type);
  return LowerResult::operand(emit_rvalue_to_temp(std::move(const_rvalue), info.type));
}

// === Dest-Aware Nodes (Aggregates) ===
// These nodes consume the destination hint and write directly to it

LowerResult FunctionLowerer::lower_expr_impl(const hir::StructLiteral &struct_literal,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> maybe_dest) {
  // StructLiteral is dest-aware: writes fields directly to destination
  TypeId normalized = canonicalize_type_for_mir(info.type);
  
  // Dest Selection: use provided dest or create temp
  Place target;
  if (maybe_dest) {
    target = std::move(*maybe_dest);
  } else {
    LocalId temp_local = create_synthetic_local(normalized, false);
    target = make_local_place(temp_local);
  }
  
  auto *struct_ty = std::get_if<type::StructType>(&type::get_type_from_id(normalized).value);
  if (!struct_ty) {
    throw std::logic_error("Struct literal without struct type");
  }

  const auto &struct_info = type::TypeContext::get_instance().get_struct(struct_ty->id);
  const auto &fields = hir::helper::get_canonical_fields(struct_literal);

  if (fields.initializers.size() != struct_info.fields.size()) {
    throw std::logic_error("Struct literal field count mismatch");
  }

  InitStruct init_struct;
  init_struct.fields.resize(fields.initializers.size());

  for (std::size_t idx = 0; idx < fields.initializers.size(); ++idx) {
    if (!fields.initializers[idx]) {
      throw std::logic_error("Struct literal field missing initializer");
    }

    TypeId field_ty = canonicalize_type_for_mir(struct_info.fields[idx].type);
    if (field_ty == invalid_type_id) {
      throw std::logic_error("Struct field missing resolved type");
    }

    const hir::Expr &field_expr = *fields.initializers[idx];
    auto &leaf = init_struct.fields[idx];

    // Build sub-place target.field[idx]
    Place field_place = target;
    field_place.projections.push_back(FieldProjection{idx});

    // Recursively lower field with destination hint
    LowerResult field_result = lower_expr(field_expr, field_place);
    
    // Check if field was written directly
    if (field_result.kind == LowerResult::Kind::Written) {
      // Field handled by direct write
      leaf = make_omitted_leaf();
    } else {
      // Field returned a value, store it via InitPattern
      Operand value = field_result.as_operand(*this, field_ty);
      leaf = make_value_leaf(std::move(value));
    }
  }

  InitPattern pattern;
  pattern.value = std::move(init_struct);
  emit_init_statement(target, std::move(pattern));
  
  // Return Written if dest was provided, else Place
  return maybe_dest.has_value() ? LowerResult::written() : LowerResult::place(std::move(target));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::ArrayLiteral &array_literal,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> maybe_dest) {
  // ArrayLiteral is dest-aware: writes elements directly to destination
  TypeId normalized = canonicalize_type_for_mir(info.type);
  
  // Dest Selection: use provided dest or create temp
  Place target;
  if (maybe_dest) {
    target = std::move(*maybe_dest);
  } else {
    LocalId temp_local = create_synthetic_local(normalized, false);
    target = make_local_place(temp_local);
  }
  
  InitArrayLiteral init_array;
  init_array.elements.resize(array_literal.elements.size());

  // Get the element type
  const type::Type &array_ty = type::get_type_from_id(normalized);
  const auto *array_type_info = std::get_if<type::ArrayType>(&array_ty.value);
  if (!array_type_info) {
    throw std::logic_error("Array literal requires array destination type");
  }
  TypeId element_type = array_type_info->element_type;

  for (std::size_t idx = 0; idx < array_literal.elements.size(); ++idx) {
    const auto &elem_expr_ptr = array_literal.elements[idx];
    if (!elem_expr_ptr) {
      throw std::logic_error("Array literal element missing");
    }

    const hir::Expr &elem_expr = *elem_expr_ptr;
    auto &leaf = init_array.elements[idx];

    // Build sub-place target[idx]
    Place elem_place = target;
    TypeId usize_ty = type::get_typeID(type::Type{type::PrimitiveKind::USIZE});
    Operand idx_operand = make_const_operand(idx, usize_ty, false);
    elem_place.projections.push_back(IndexProjection{std::move(idx_operand)});

    // Recursively lower element with destination hint
    LowerResult elem_result = lower_expr(elem_expr, elem_place);
    
    if (elem_result.kind == LowerResult::Kind::Written) {
      leaf = make_omitted_leaf();
    } else {
      Operand op = elem_result.as_operand(*this, element_type);
      leaf = make_value_leaf(std::move(op));
    }
  }

  InitPattern pattern;
  pattern.value = std::move(init_array);
  emit_init_statement(target, std::move(pattern));
  
  return maybe_dest.has_value() ? LowerResult::written() : LowerResult::place(std::move(target));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::ArrayRepeat &array_repeat,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> maybe_dest) {
  // ArrayRepeat is dest-aware: writes repeated value directly to destination
  TypeId normalized = canonicalize_type_for_mir(info.type);
  
  // Dest Selection: use provided dest or create temp
  Place target;
  if (maybe_dest) {
    target = std::move(*maybe_dest);
  } else {
    LocalId temp_local = create_synthetic_local(normalized, false);
    target = make_local_place(temp_local);
  }
  
  InitArrayRepeat init_repeat;
  
  // Extract count from variant
  if (std::holds_alternative<size_t>(array_repeat.count)) {
    init_repeat.count = std::get<size_t>(array_repeat.count);
  } else {
    throw std::logic_error("Array repeat count must be compile-time constant");
  }

  // Get the element type
  const type::Type &array_ty = type::get_type_from_id(normalized);
  const auto *array_type_info = std::get_if<type::ArrayType>(&array_ty.value);
  if (!array_type_info) {
    throw std::logic_error("Array repeat requires array destination type");
  }
  TypeId element_type = array_type_info->element_type;

  // Build sub-place for element at index 0
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

  // Recursively lower the repeated value with destination hint
  LowerResult elem_result = lower_expr(*array_repeat.value, elem_place);
  
  if (elem_result.kind == LowerResult::Kind::Written) {
    init_repeat.element = make_omitted_leaf();
  } else {
    Operand op = elem_result.as_operand(*this, element_type);
    init_repeat.element = make_value_leaf(std::move(op));
  }

  InitPattern pattern;
  pattern.value = std::move(init_repeat);
  emit_init_statement(target, std::move(pattern));
  
  return maybe_dest.has_value() ? LowerResult::written() : LowerResult::place(std::move(target));
}

// === Propagators (Control Flow) ===
// These nodes pass the destination hint through to their children

LowerResult FunctionLowerer::lower_expr_impl(const hir::Block &block_expr,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> maybe_dest) {
  // Block is a propagator: passes dest hint to final expression
  if (!lower_block_statements(block_expr)) {
    // Block diverges (unreachable)
    return LowerResult::written(); // Signal no value produced
  }

  if (block_expr.final_expr) {
    const auto &expr_ptr = *block_expr.final_expr;
    if (expr_ptr) {
      // Pass destination hint through to final expression
      return lower_expr(*expr_ptr, maybe_dest);
    }
    // No final expression - block produces unit
    if (is_unit_type(info.type) || is_never_type(info.type)) {
      return LowerResult::written();
    }
    throw std::logic_error("Block expression missing value");
  }

  // Block has no final expression - produces unit
  if (is_unit_type(info.type) || is_never_type(info.type)) {
    return LowerResult::written();
  }

  throw std::logic_error("Block expression missing value");
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::If &if_expr,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> maybe_dest) {
  // If is a propagator: passes dest hint to branches
  Operand condition = lower_operand(*if_expr.condition);
  if (!current_block) {
    return LowerResult::written();
  }

  bool has_else = if_expr.else_expr && *if_expr.else_expr;
  if (!has_else && !is_unit_type(info.type)) {
    throw std::logic_error("If expression missing else branch for non-unit type");
  }

  BasicBlockId then_block = create_block();
  std::optional<BasicBlockId> else_block =
      has_else ? std::optional<BasicBlockId>(create_block()) : std::nullopt;
  BasicBlockId join_block = create_block();

  BasicBlockId false_target = else_block ? *else_block : join_block;
  branch_on_bool(condition, then_block, false_target);

  bool result_needed = !is_unit_type(info.type) && !is_never_type(info.type);
  
  // If dest is provided and result is needed, branches write to dest
  // Otherwise, use Phi nodes
  if (maybe_dest.has_value() && result_needed) {
    // DEST-AWARE PATH: Branches write directly to destination
    
    // THEN branch
    switch_to_block(then_block);
    std::optional<Operand> then_value = lower_block_expr(*if_expr.then_block, info.type);
    bool then_reachable = current_block.has_value();
    
    if (then_reachable) {
      // Write then value to destination
      if (then_value) {
        AssignStatement assign;
        assign.dest = *maybe_dest;
        assign.src = ValueSource{std::move(*then_value)};
        Statement stmt;
        stmt.value = std::move(assign);
        append_statement(std::move(stmt));
      }
      add_goto_from_current(join_block);
    }

    // ELSE branch
    bool else_reachable = false;
    if (has_else) {
      switch_to_block(*else_block);
      LowerResult else_result = lower_expr(**if_expr.else_expr, std::nullopt);
      std::optional<Operand> else_value;
      if (else_result.kind != LowerResult::Kind::Written) {
        else_value = std::make_optional(else_result.as_operand(*this, info.type));
      }
      else_reachable = current_block.has_value();
      
      if (else_reachable) {
        // Write else value to destination
        if (else_value) {
          AssignStatement assign;
          assign.dest = *maybe_dest;
          assign.src = ValueSource{std::move(*else_value)};
          Statement stmt;
          stmt.value = std::move(assign);
          append_statement(std::move(stmt));
        }
        add_goto_from_current(join_block);
      }
    }

    // Set up join block
    bool join_reachable = then_reachable || else_reachable || !has_else;
    if (join_reachable) {
      current_block = join_block;
    } else {
      current_block.reset();
    }

    // Return Written since value was written to dest
    return LowerResult::written();
    
  } else {
    // DEST-IGNORANT PATH: Use Phi nodes
    std::vector<PhiIncoming> phi_incomings;

    // THEN branch
    switch_to_block(then_block);
    std::optional<Operand> then_value = lower_block_expr(*if_expr.then_block, info.type);
    std::optional<BasicBlockId> then_fallthrough =
        current_block ? std::optional<BasicBlockId>(*current_block) : std::nullopt;
    
    if (then_fallthrough) {
      if (result_needed) {
        TempId value_temp = materialize_operand(
            expect_operand(std::move(then_value), "Then branch must produce value"),
            info.type);
        phi_incomings.push_back(PhiIncoming{*then_fallthrough, value_temp});
      }
      add_goto_from_current(join_block);
    }

    // ELSE branch
    std::optional<BasicBlockId> else_fallthrough;
    if (has_else) {
      switch_to_block(*else_block);
      LowerResult else_result = lower_expr(**if_expr.else_expr, std::nullopt);
      std::optional<Operand> else_value;
      if (else_result.kind != LowerResult::Kind::Written) {
        else_value = std::make_optional(else_result.as_operand(*this, info.type));
      }
      else_fallthrough =
          current_block ? std::optional<BasicBlockId>(*current_block) : std::nullopt;
      
      if (else_fallthrough) {
        if (result_needed) {
          TempId value_temp = materialize_operand(
              expect_operand(std::move(else_value), "Else branch must produce value"),
              info.type);
          phi_incomings.push_back(PhiIncoming{*else_fallthrough, value_temp});
        }
        add_goto_from_current(join_block);
      }
    }

    // Set up join block
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
        return LowerResult::written();
      }
      
      TempId dest = allocate_temp(info.type);
      PhiNode phi;
      phi.dest = dest;
      phi.incoming = std::move(phi_incomings);
      mir_function.basic_blocks[join_block].phis.push_back(std::move(phi));
      
      return LowerResult::operand(make_temp_operand(dest));
    }

    return LowerResult::written();
  }
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::Assignment &assignment,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> /* maybe_dest */) {
  // NEW UNIFIED API: Assignment is a statement that produces unit value
  // The dest hint is ignored since assignment always produces unit (if anything)
  
  if (!assignment.lhs || !assignment.rhs) {
    throw std::logic_error("Assignment missing operands during MIR lowering");
  }
  
  // Handle underscore assignment (just evaluate RHS for side effects)
  if (std::get_if<hir::Underscore>(&assignment.lhs->value)) {
    const hir::Expr *rhs_expr = assignment.rhs.get();
    if (rhs_expr) {
      if (const auto *binary = std::get_if<hir::BinaryOp>(&rhs_expr->value)) {
        const hir::Expr *compound_rhs = nullptr;
        if (binary->lhs && std::get_if<hir::Underscore>(&binary->lhs->value)) {
          compound_rhs = binary->rhs.get();
        }
        if (compound_rhs) {
          (void)lower_expr(*compound_rhs, std::nullopt);
        } else {
          (void)lower_expr(*assignment.rhs, std::nullopt);
        }
      } else {
        (void)lower_expr(*assignment.rhs, std::nullopt);
      }
    }
    // Assignment produces no meaningful value
    return LowerResult::written();
  }

  semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*assignment.lhs);
  semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*assignment.rhs);

  // Special case: aggregate place-to-place copy optimization
  if (lhs_info.is_place && rhs_info.is_place && lhs_info.has_type &&
      rhs_info.has_type && lhs_info.type == rhs_info.type &&
      is_aggregate_type(lhs_info.type)) {

    Place dest_place = lower_place(*assignment.lhs);
    Place src_place = lower_place(*assignment.rhs);

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
    
    // Optimization doesn't apply; load and assign
    Operand value = load_place_value(std::move(src_place), rhs_info.type);
    AssignStatement assign{.dest = std::move(dest_place), .src = ValueSource{std::move(value)}};
    Statement stmt;
    stmt.value = std::move(assign);
    append_statement(std::move(stmt));
    
    return LowerResult::written();
  }

  // General case: NEW UNIFIED API
  // Lower LHS to place, then lower RHS with destination hint
  Place dest = lower_place(*assignment.lhs);
  TypeId dest_type = lhs_info.type;
  
  LowerResult result = lower_expr(*assignment.rhs, dest);
  result.write_to_dest(*this, std::move(dest), dest_type);
  
  // Assignment produces no meaningful value
  return LowerResult::written();
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::Loop &loop_expr,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> maybe_dest) {
  // Loop is a propagator for break values, but doesn't propagate dest to body
  // Body always produces unit; break values may write to dest if provided
  BasicBlockId body_block = create_block();
  BasicBlockId break_block = create_block();

  if (current_block) {
    add_goto_from_current(body_block);
  }
  current_block = body_block;

  push_loop_context(&loop_expr, body_block, break_block, loop_expr.break_type);
  (void)lower_block_expr(*loop_expr.body, get_unit_type());
  if (current_block) {
    add_goto_from_current(body_block);
  }

  LoopContext finalized = pop_loop_context(&loop_expr);
  
  // If dest is provided and loop has break value, write break values to dest
  if (maybe_dest.has_value() && finalized.break_result) {
    // Break values already stored in break_result temp via finalize_loop_context
    // No additional dest handling needed for loop body
    finalize_loop_context(finalized);
    
    bool break_reachable = !finalized.break_predecessors.empty();
    if (!break_reachable) {
      current_block.reset();
      return LowerResult::written();
    }
    current_block = finalized.break_block;
    return LowerResult::operand(make_temp_operand(*finalized.break_result));
  }
  
  finalize_loop_context(finalized);

  bool break_reachable = !finalized.break_predecessors.empty();
  if (finalized.break_result) {
    if (!break_reachable) {
      current_block.reset();
      return LowerResult::written();
    }
    current_block = finalized.break_block;
    return LowerResult::operand(make_temp_operand(*finalized.break_result));
  }

  if (break_reachable) {
    current_block = finalized.break_block;
  } else {
    current_block.reset();
  }
  return LowerResult::written();
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::While &while_expr,
                                             const semantic::ExprInfo &info,
                                             std::optional<Place> maybe_dest) {
  // While is similar to Loop
  BasicBlockId cond_block = create_block();
  BasicBlockId body_block = create_block();
  BasicBlockId break_block = create_block();

  if (current_block) {
    add_goto_from_current(cond_block);
  }
  current_block = cond_block;

  auto &ctx = push_loop_context(&while_expr, cond_block, break_block,
                                while_expr.break_type);

  Operand condition = lower_operand(*while_expr.condition);
  if (current_block) {
    branch_on_bool(condition, body_block, break_block);
    ctx.break_predecessors.push_back(cond_block);
  }

  switch_to_block(body_block);
  (void)lower_block_expr(*while_expr.body, get_unit_type());
  if (current_block) {
    add_goto_from_current(cond_block);
  }

  LoopContext finalized = pop_loop_context(&while_expr);
  finalize_loop_context(finalized);

  current_block = break_block;
  if (finalized.break_result) {
    return LowerResult::operand(make_temp_operand(*finalized.break_result));
  }
  return LowerResult::written();
}

} // namespace mir::detail
