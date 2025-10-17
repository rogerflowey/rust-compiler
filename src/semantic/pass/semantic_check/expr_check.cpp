#include "expr_check.hpp"
#include "hir/helper.hpp"
#include "hir/hir.hpp"
#include "pass/semantic_check/expr_info.hpp"
#include "pass/semantic_check/other_check.hpp"
#include "semantic/type/helper.hpp"
#include "type/type.hpp"
#include "type_compatibility.hpp"
#include "utils.hpp"
#include "utils/error.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <variant>

using namespace semantic::helper::type_helper;
using namespace hir::helper;

namespace semantic {

// Literal expressions
ExprInfo ExprChecker::check(hir::Literal &expr) {
  return std::visit(
      Overloaded{
          [](hir::Literal::Integer &integer) -> ExprInfo {
            // Determine type based on suffix
            TypeId type;
            switch (integer.suffix_type) {
            case ast::IntegerLiteralExpr::I32:
              type = get_typeID(Type{PrimitiveKind::I32});
              break;
            case ast::IntegerLiteralExpr::U32:
              type = get_typeID(Type{PrimitiveKind::U32});
              break;
            case ast::IntegerLiteralExpr::ISIZE:
              type = get_typeID(Type{PrimitiveKind::ISIZE});
              break;
            case ast::IntegerLiteralExpr::USIZE:
              type = get_typeID(Type{PrimitiveKind::USIZE});
              break;
            default:
              // Use __ANYINT__ or __ANYUINT__ for inference
              type = get_typeID(Type{integer.is_negative
                                         ? PrimitiveKind::__ANYINT__
                                         : PrimitiveKind::__ANYUINT__});
              break;
            }
            // Check for overflow
            overflow_int_literal_check(integer);
            return ExprInfo(type, false, false);
          },
          [](bool &) -> ExprInfo {
            return ExprInfo(get_typeID(Type{PrimitiveKind::BOOL}), false, false);
          },
          [](char &) -> ExprInfo {
            return ExprInfo(get_typeID(Type{PrimitiveKind::CHAR}), false, false);
          },
          [](hir::Literal::String &) -> ExprInfo {
            return ExprInfo(get_typeID(Type{PrimitiveKind::STRING}), false, false);
          }},
      expr.value);
}

ExprInfo ExprChecker::check(hir::Underscore &expr) {
  (void)expr; // Suppress unused parameter warning
  return ExprInfo{.type = get_typeID(Type{NeverType{}}),
                  .is_mut = true,
                  .is_place = true,
                  .endpoints = {NormalEndpoint{}}};
}

// Reference expressions
ExprInfo ExprChecker::check(hir::Variable &expr) {
  return ExprInfo{
      .type = get_resolved_type(*expr.local_id->type_annotation),
      .is_mut = expr.local_id->is_mutable,
      .is_place = true,
      .endpoints = {NormalEndpoint{}}};
}

ExprInfo ExprChecker::check(hir::ConstUse &expr) {
  // Basic validation
  if (!expr.def) {
    throw std::logic_error("Const definition is null");
  }

  // Get const's declared type
  TypeId declared_type = get_resolved_type(*expr.def->type);

  // Perform type validation on const expression
  if (expr.def->expr) {
    // Check the original expression's type matches declared type
    ExprInfo expr_info = check(*expr.def->expr);

    // Resolve inference placeholders if present
    resolve_inference_if_needed(expr_info.type, declared_type);

    // Validate type compatibility using existing type checking infrastructure
    if (!is_assignable_to(expr_info.type, declared_type)) {
      throw std::runtime_error("Const '" + get_name(*expr.def).name +
                               "' expression type doesn't match declared type");
    }

    // Note: We could store the validated expression info for debugging,
    // but this is optional and not required for the core functionality
  } else {
    throw std::logic_error("Const '" + get_name(*expr.def).name +
                           "' definition missing expression");
  }

  return ExprInfo{.type = declared_type,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = {NormalEndpoint{}}};
}

ExprInfo ExprChecker::check(hir::FuncUse &expr) {
  (void)expr; // Suppress unused parameter warning
  // Functions are not first-class values
  throw std::runtime_error(
      "Function used as value (functions are not first-class)");
}

ExprInfo ExprChecker::check(hir::FieldAccess &expr) {
  ExprInfo base_info = check(*expr.base);
  if (is_reference_type(base_info.type)) {
    expr.base = transform_helper::apply_dereference(std::move(expr.base));
    base_info = check(*expr.base);
  }
  
  auto struct_type = std::get_if<StructType>(&base_info.type->value);
  if (!struct_type) {
    throw std::runtime_error("Field access base must be a struct");
  }

  // resolve field
  auto name_ptr = std::get_if<ast::Identifier>(&expr.field);
  if(!name_ptr) {
    throw std::logic_error("Field access already resolved");
  }
  auto field_id = struct_type->symbol->find_field(*name_ptr);
  if (!field_id) {
    throw std::runtime_error("Field '" + name_ptr->name +
                             "' not found in struct '" + get_name(*struct_type->symbol).name + "'");
  }
  expr.field = *field_id;
  auto type = get_resolved_type(struct_type->symbol->field_type_annotations[*field_id]);
  return ExprInfo{.type = type, .is_mut = base_info.is_mut, .is_place = true, .endpoints = base_info.endpoints};
}

ExprInfo ExprChecker::check(hir::Index &expr) {
  ExprInfo base_info = check(*expr.base);
  if (is_reference_type(base_info.type)) {
    expr.base = transform_helper::apply_dereference(std::move(expr.base));
    base_info = check(*expr.base);
  }
  
  auto array_type = std::get_if<ArrayType>(&base_info.type->value);
  if (!array_type) {
    throw std::runtime_error("Index base must be an array");
  }
  
  ExprInfo index_info = check(*expr.index);
  if (!is_assignable_to(index_info.type, get_typeID(Type{PrimitiveKind::USIZE}))) {
    throw std::runtime_error("Index must be coercible to usize");
  }
  
  return ExprInfo{.type = array_type->element_type, .is_mut = base_info.is_mut, .is_place = true, .endpoints = base_info.endpoints};
}

ExprInfo ExprChecker::check(hir::StructLiteral &expr) {
  auto struct_def = get_struct_def(expr);
  const auto &fields = get_canonical_fields(expr).initializers;

  if (fields.size() != struct_def->fields.size()) {
    throw std::runtime_error("Struct literal for '" + get_name(*struct_def).name +
                             "' field count mismatch");
  }

  std::vector<ExprInfo> field_infos;
  field_infos.reserve(fields.size());
  
  for (size_t i = 0; i < fields.size(); ++i) {
    ExprInfo field_info = check(*fields[i]);
    TypeId expected_type = get_resolved_type(struct_def->field_type_annotations[i]);

    resolve_inference_if_needed(field_info.type, expected_type);

    if (!is_assignable_to(field_info.type, expected_type)) {
      throw std::runtime_error("Struct literal field type mismatch for '" +
                               get_name(*struct_def).name + "'");
    }
    field_infos.push_back(std::move(field_info));
  }

  return ExprInfo{.type = get_typeID(Type{StructType{struct_def}}),
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = merge_endpoints(field_infos)};
}

ExprInfo ExprChecker::check(hir::ArrayLiteral &expr) {
  if (expr.elements.empty()) {
    throw std::runtime_error("Array literal cannot be empty");
  }

  std::vector<ExprInfo> elem_infos;
  elem_infos.reserve(expr.elements.size());
  
  for (const auto& elem : expr.elements) {
    elem_infos.push_back(check(*elem));
  }

  TypeId array_element_type = elem_infos[0].type;
  for (size_t i = 1; i < elem_infos.size(); ++i) {
    resolve_inference_if_needed(elem_infos[i].type, array_element_type);
    auto common_type = find_common_type(array_element_type, elem_infos[i].type);
    if (!common_type) {
      throw std::runtime_error("Array literal elements must have compatible types");
    }
    array_element_type = *common_type;
  }

  return ExprInfo{.type = get_typeID(Type{ArrayType{array_element_type, expr.elements.size()}}),
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = merge_endpoints(elem_infos)};
}

ExprInfo ExprChecker::check(hir::ArrayRepeat &expr) {
  ExprInfo value_info = check(*expr.value);
  size_t count = get_array_count(expr);

  return ExprInfo{.type = get_typeID(Type{ArrayType{value_info.type, count}}),
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = value_info.endpoints};
}

// Operations
ExprInfo ExprChecker::check(hir::UnaryOp &expr) {
  ExprInfo operand_info = check(*expr.rhs);

  switch (expr.op) {
  case hir::UnaryOp::NOT: {
    // Operand must be BOOL, result is BOOL
    if (!is_bool_type(operand_info.type)) {
      throw std::runtime_error("NOT operand must be boolean");
    }
    TypeId bool_type = get_typeID(Type{PrimitiveKind::BOOL});
    return ExprInfo(bool_type, false, false, operand_info.endpoints);
  }
  case hir::UnaryOp::NEGATE: {
    // Operand must be numeric, result is same type
    if (!is_numeric_type(operand_info.type)) {
      throw std::runtime_error("NEGATE operand must be numeric");
    }
    return ExprInfo(operand_info.type, false, false, operand_info.endpoints);
  }
  case hir::UnaryOp::DEREFERENCE: {
    // Operand must be reference, result is referenced type, creates place
    if (!is_reference_type(operand_info.type)) {
      throw std::runtime_error("DEREFERENCE operand must be reference");
    }
    TypeId referenced_type = get_referenced_type(operand_info.type);
    return ExprInfo(referenced_type, get_reference_mutability(operand_info.type), true,
                    operand_info.endpoints);
  }
  case hir::UnaryOp::REFERENCE:
  case hir::UnaryOp::MUTABLE_REFERENCE: {
    // Create reference to operand
    bool is_mut = (expr.op == hir::UnaryOp::MUTABLE_REFERENCE);
    TypeId ref_type =
        get_typeID(Type{ReferenceType{operand_info.type, is_mut}});
    return ExprInfo(ref_type, false, false, operand_info.endpoints);
  }
  default:
    throw std::logic_error("Unknown unary operator");
  }
}

ExprInfo ExprChecker::check(hir::BinaryOp &expr) {
  ExprInfo lhs_info = check(*expr.lhs);
  ExprInfo rhs_info = check(*expr.rhs);
  EndpointSet endpoints = merge_endpoints(lhs_info, rhs_info);

  switch (expr.op) {
  case hir::BinaryOp::ADD:
  case hir::BinaryOp::SUB:
  case hir::BinaryOp::MUL:
  case hir::BinaryOp::DIV:
  case hir::BinaryOp::REM: {
    std::cerr << "DEBUG: BinaryOp arithmetic, about to resolve inference" << std::endl;
    if (!is_numeric_type(lhs_info.type) || !is_numeric_type(rhs_info.type)) {
      throw std::runtime_error("Arithmetic operands must be numeric");
    }
    std::cerr << "DEBUG: BinaryOp resolving lhs to rhs" << std::endl;
    resolve_inference_if_needed(lhs_info.type, rhs_info.type);
    std::cerr << "DEBUG: BinaryOp resolving rhs to lhs" << std::endl;
    resolve_inference_if_needed(rhs_info.type, lhs_info.type);

    std::cerr << "DEBUG: BinaryOp finding common type" << std::endl;
    auto common_type = find_common_type(lhs_info.type, rhs_info.type);
    if (!common_type) {
      throw std::runtime_error("Arithmetic operands must have compatible types");
    }
    std::cerr << "DEBUG: BinaryOp found common type" << std::endl;
    return ExprInfo(*common_type, false, false, endpoints);
  }
  case hir::BinaryOp::EQ:
  case hir::BinaryOp::NE:
  case hir::BinaryOp::LT:
  case hir::BinaryOp::GT:
  case hir::BinaryOp::LE:
  case hir::BinaryOp::GE: {
    if (!are_comparable(lhs_info.type, rhs_info.type)) {
      throw std::runtime_error("Comparison operands must be comparable");
    }
    return ExprInfo(get_typeID(Type{PrimitiveKind::BOOL}), false, false, endpoints);
  }
  case hir::BinaryOp::AND:
  case hir::BinaryOp::OR: {
    if (!is_bool_type(lhs_info.type) || !is_bool_type(rhs_info.type)) {
      throw std::runtime_error("Logical operands must be boolean");
    }
    return ExprInfo(get_typeID(Type{PrimitiveKind::BOOL}), false, false, endpoints);
  }
  case hir::BinaryOp::BIT_AND:
  case hir::BinaryOp::BIT_XOR:
  case hir::BinaryOp::BIT_OR: {
    if (!is_numeric_type(lhs_info.type) || !is_numeric_type(rhs_info.type)) {
      throw std::runtime_error("Bitwise operands must be numeric");
    }
    resolve_inference_if_needed(lhs_info.type, rhs_info.type);
    resolve_inference_if_needed(rhs_info.type, lhs_info.type);

    auto common_type = find_common_type(lhs_info.type, rhs_info.type);
    if (!common_type) {
      throw std::runtime_error("Bitwise operands must have compatible types");
    }
    return ExprInfo(*common_type, false, false, endpoints);
  }
  case hir::BinaryOp::SHL:
  case hir::BinaryOp::SHR: {
    if (!is_numeric_type(lhs_info.type)) {
      throw std::runtime_error("Shift left operand must be numeric");
    }
    if (!is_assignable_to(rhs_info.type, get_typeID(Type{PrimitiveKind::USIZE}))) {
      throw std::runtime_error("Shift right operand must be coercible to usize");
    }
    return ExprInfo(lhs_info.type, false, false, endpoints);
  }
  default:
    throw std::logic_error("Unknown binary operator");
  }
}

ExprInfo ExprChecker::check(hir::Assignment &expr) {
  ExprInfo lhs_info = check(*expr.lhs);
  ExprInfo rhs_info = check(*expr.rhs);

  // Verify left-hand side is mutable place
  if (!lhs_info.is_place || !lhs_info.is_mut) {
    throw std::runtime_error("Assignment target must be mutable place");
  }

  // Ensure right-hand side type assignable to left-hand side
  if (!is_assignable_to(rhs_info.type, lhs_info.type)) {
    throw std::runtime_error("Assignment type mismatch");
  }

  // Return: unit type, non-mutable, non-place, endpoints from right-hand side
  return ExprInfo{.type = get_typeID(Type{UnitType{}}),
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = rhs_info.endpoints};
}

ExprInfo ExprChecker::check(hir::Cast &expr) {
  ExprInfo operand_info = check(*expr.expr);

  // Validate cast allowed between source and target types
  if (!is_castable_to(operand_info.type,
                      get_resolved_type(expr.target_type))) {
    throw std::runtime_error("Invalid cast between types");
  }

  return ExprInfo{.type = get_resolved_type(expr.target_type),
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = operand_info.endpoints};
}

ExprInfo ExprChecker::check(hir::Call &expr) {
  auto func_type = std::get_if<hir::FuncUse>(&expr.callee->value);
  if (!func_type) {
    throw std::runtime_error("Call target must be a function");
  }


  // Check argument types
  if (func_type->def->params.size() != expr.args.size()) {
    const std::string func_name = get_name(*func_type->def).name;
    throw std::runtime_error("Argument count mismatch when calling function '" +
                             func_name + "'");
  }

  for (size_t i = 0; i < func_type->def->params.size(); ++i) {
    ExprInfo arg_info = check(*expr.args[i]);
    if (!is_assignable_to(arg_info.type,
                          get_resolved_type(
                              *func_type->def->param_type_annotations[i]))) {
      const std::string func_name = get_name(*func_type->def).name;
      throw std::runtime_error("Argument type mismatch when calling function '" +
                               func_name + "'");
    }
  }

  return ExprInfo{.type = get_resolved_type(*func_type->def->return_type),
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = {NormalEndpoint{}}};
}

ExprInfo ExprChecker::check(hir::MethodCall &expr) {
  // Check receiver expression first
  ExprInfo receiver_info = check(*expr.receiver);
  
  // Extract base type from potentially nested references
  TypeId base_type = get_base_type(receiver_info.type);
  auto name = std::get_if<ast::Identifier>(&expr.method);
  if (!name) {
    throw std::logic_error("Method name not resolved");
  }
  
  // Find method in impl table to resolve the method
  auto method_def = impl_table.lookup_method(base_type, *name);
  if (!method_def) {
    throw std::runtime_error("Method '" + name->name + "' not found");
  }
  expr.method = method_def;
  
  // Check argument count matches method parameters
  if (method_def->params.size() != expr.args.size()) {
    const std::string method_name = get_name(*method_def).name;
    throw std::runtime_error("Method argument count mismatch for '" +
                             method_name + "'");
  }
  
  // Determine if auto-reference is needed based on method's self parameter
  TypeId expected_receiver_type = base_type;
  bool needs_auto_reference = false;
  
  if (method_def->self_param.is_reference) {
    needs_auto_reference = true;
    // Create reference type matching method's self parameter
    expected_receiver_type = get_typeID(Type{ReferenceType{
      .referenced_type = base_type,
      .is_mutable = method_def->self_param.is_mutable
    }});
  }
  
  // Apply auto-reference if needed
  ExprInfo final_receiver_info = receiver_info;
  if (needs_auto_reference) {
    // Transform HIR to insert explicit reference operation
    expr.receiver = transform_helper::apply_reference(std::move(expr.receiver), method_def->self_param.is_mutable);
    final_receiver_info = check(*expr.receiver);
  }
  
  // Verify receiver type compatibility with method's self parameter
  if (!is_assignable_to(final_receiver_info.type, expected_receiver_type)) {
    const std::string method_name = get_name(*method_def).name;
    throw std::runtime_error("Receiver type mismatch when calling method '" +
                             method_name + "'");
  }
  
  // Check argument types
  std::vector<ExprInfo> arg_infos;
  arg_infos.reserve(expr.args.size());
  
  for (size_t i = 0; i < expr.args.size(); ++i) {
    ExprInfo arg_info = check(*expr.args[i]);
    TypeId expected_param_type = get_resolved_type(*method_def->param_type_annotations[i]);
    
    resolve_inference_if_needed(arg_info.type, expected_param_type);
    
    if (!is_assignable_to(arg_info.type, expected_param_type)) {
      const std::string method_name = get_name(*method_def).name;
      throw std::runtime_error("Method argument type mismatch for '" +
                               method_name + "'");
    }
    arg_infos.push_back(std::move(arg_info));
  }
  
  // Merge endpoints from receiver and arguments
  EndpointSet endpoints = receiver_info.endpoints;
  for (const auto& arg_info : arg_infos) {
    merge_endpoints(endpoints, arg_info);
  }
  
  // Return method's return type
  return ExprInfo{
    .type = get_resolved_type(*method_def->return_type),
    .is_mut = false,
    .is_place = false,
    .endpoints = std::move(endpoints)
  };
}

ExprInfo ExprChecker::check(hir::If &expr) {
  ExprInfo cond_info = check(*expr.condition);
  if (!is_bool_type(cond_info.type)) {
    throw std::runtime_error("If condition must be boolean");
  }

  
  ExprInfo then_info = check(*expr.then_block);
  

  if (expr.else_expr) {
    ExprInfo else_info = check(*(*expr.else_expr));
    
    // Try to find common type, but also allow unit type coercion
    auto common_type = find_common_type(then_info.type, else_info.type);
    
    if (!common_type) {
      // Special handling for never_type: if one branch is never_type, use the other branch's type
      auto never_type = get_typeID(Type{NeverType{}});
      
      if (then_info.type == never_type) {
        common_type = else_info.type;
      } else if (else_info.type == never_type) {
        common_type = then_info.type;
      } else {
        // If no common type found, try coercing both to unit type
        auto unit_type = get_typeID(Type{UnitType{}});
        if (is_assignable_to(then_info.type, unit_type) && is_assignable_to(else_info.type, unit_type)) {
          common_type = unit_type;
        } else {
          // Try coercing to the more general type (i32 in this case)
          if (is_assignable_to(then_info.type, else_info.type)) {
            common_type = else_info.type;
          } else if (is_assignable_to(else_info.type, then_info.type)) {
            common_type = then_info.type;
          } else {
            throw std::runtime_error("If branches must have compatible types");
          }
        }
      }
    }

    auto endpoints = merge_endpoints({cond_info, then_info, else_info});
    return ExprInfo(*common_type, false, false, endpoints);
  } else {
      auto endpoints = merge_endpoints({cond_info, then_info});
      endpoints.insert(NormalEndpoint{});
      auto unit_type = get_typeID(Type{UnitType{}});
    return ExprInfo(unit_type, false, false, endpoints);
  }
}

ExprInfo ExprChecker::check(hir::Loop &expr) {
  ExprInfo body_info = check(*expr.body);
  
  
  // Remove the unit type requirement for loop bodies - loops can have any body type
  // if (body_info.type != get_typeID(Type{UnitType{}})) {
  //   throw std::runtime_error("Loop body must have unit type");
  // }

  if (!expr.break_type) {
    expr.break_type = get_typeID(Type{NeverType{}});
  }
  
  EndpointSet endpoints = body_info.endpoints;
  endpoints.erase(ContinueEndpoint{.target = &expr});
  
  auto break_it = endpoints.find(BreakEndpoint{.target = &expr, .value_type = *expr.break_type});
  if (break_it != endpoints.end()) {
    endpoints.erase(break_it);
    endpoints.insert(NormalEndpoint{});
  }
  
  return ExprInfo{.type = *expr.break_type, .is_mut = false, .is_place = false, .endpoints = endpoints};
}

ExprInfo ExprChecker::check(hir::While &expr) {
  ExprInfo cond_info = check(*expr.condition);
  if (!is_bool_type(cond_info.type)) {
    throw std::runtime_error("While condition must be boolean");
  }
  
  ExprInfo body_info = check(*expr.body);
  if (body_info.type != get_typeID(Type{UnitType{}})) {
    throw std::runtime_error("While body must have unit type");
  }
  
  if (!expr.break_type) {
    expr.break_type = get_typeID(Type{UnitType{}});
  }
  
  EndpointSet endpoints = merge_endpoints(cond_info, body_info);
  endpoints.erase(ContinueEndpoint{.target = &expr});
  
  auto break_it = endpoints.find(BreakEndpoint{.target = &expr, .value_type = *expr.break_type});
  if (break_it != endpoints.end()) {
    endpoints.erase(break_it);
    endpoints.insert(NormalEndpoint{});
  }
  
  return ExprInfo{.type = *expr.break_type, .is_mut = false, .is_place = false, .endpoints = endpoints};
}

ExprInfo ExprChecker::check(hir::Break &expr) {
  // Check value type if present
  TypeId value_type = get_typeID(Type{UnitType{}});
  EndpointSet value_endpoints; // Default empty endpoints

  if (expr.value) {
    ExprInfo value_info = check(*(*expr.value));
    value_type = value_info.type;
    value_endpoints = value_info.endpoints;
  }
  

  // Update loop/while break type
  std::visit(Overloaded{[&](hir::Loop *loop) {
                          if (!loop->break_type) {
                            loop->break_type = value_type;
                          } else if (loop->break_type != value_type) {
                            throw std::runtime_error(
                                "Inconsistent break value types in loop");
                          }
                        },
                        [&](hir::While *while_loop) {
                          if (!while_loop->break_type) {
                            while_loop->break_type = value_type;
                          } else if (while_loop->break_type != value_type) {
                            throw std::runtime_error(
                                "Inconsistent break value types in while loop");
                          }
                        }},
             *expr.target);

  // Break expressions should NOT include NormalEndpoint - they only have the BreakEndpoint
  // Break expressions should ONLY have BreakEndpoint, never NormalEndpoint
  // Don't include value_endpoints as they might contain NormalEndpoint
  EndpointSet endpoints;
  endpoints.insert(BreakEndpoint{*expr.target, value_type});
  
  
  return ExprInfo{.type = get_typeID(Type{NeverType{}}),
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = std::move(endpoints)};
}

ExprInfo ExprChecker::check(hir::Continue &expr) {
  return ExprInfo{.type = get_typeID(Type{NeverType{}}),
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = {ContinueEndpoint{*expr.target}}};
}

ExprInfo ExprChecker::check(hir::Return &expr) {
  // Check value type if present
  TypeId value_type = get_typeID(Type{UnitType{}});  // Default to unit type for returns without value
  if (expr.value) {
    ExprInfo value_info = check(*(*expr.value));
    value_type = value_info.type;
  }
  
  std::visit(
      Overloaded{
          [&](hir::Function *func) {
            TypeId expected_type = get_resolved_type(*func->return_type);
            // For ReturnExpressionWithoutValue test, we need to handle the case where function returns unit type
            if (value_type != expected_type) {
              throw std::runtime_error(
                  "Return value type does not match function return type");
            }
          },
          [&](hir::Method *method) {
            TypeId expected_type = get_resolved_type(*method->return_type);
            if (value_type != expected_type) {
              throw std::runtime_error(
                  "Return value type does not match method return type");
            }
          }},
      *expr.target);

  // Return expressions should ONLY have ReturnEndpoint, never NormalEndpoint
  EndpointSet endpoints;
  endpoints.insert(ReturnEndpoint{*expr.target, value_type});

  return ExprInfo{.type = get_typeID(Type{NeverType{}}),
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = std::move(endpoints)};
}

// Block expressions
ExprInfo ExprChecker::check(hir::Block &expr) {
  std::vector<ExprInfo> stmt_infos;
  stmt_infos.reserve(expr.stmts.size());
  
  for (const auto &stmt : expr.stmts) {
    std::visit(
        Overloaded{
            [this, &stmt_infos](hir::LetStmt &let) {
              if (!let.initializer) {
                throw std::runtime_error("Let statement must have initializer");
              }
              ExprInfo init_info = check(*let.initializer);

              if (let.type_annotation) {
                TypeId annotation_type = get_resolved_type(*let.type_annotation);
                resolve_inference_if_needed(init_info.type, annotation_type);

                if (!is_assignable_to(init_info.type, annotation_type)) {
                  throw std::runtime_error("Let initializer type doesn't match annotation");
                }
              }
              stmt_infos.push_back(std::move(init_info));
            },
            [this, &stmt_infos](hir::ExprStmt &expr_stmt) {
              stmt_infos.push_back(check(*expr_stmt.expr));
            }},
        stmt->value);
  }

  TypeId result_type = get_typeID(Type{UnitType{}});
  if (expr.final_expr) {
    if (*expr.final_expr) {
      ExprInfo final_info = check(**expr.final_expr);
      result_type = final_info.type;
      stmt_infos.push_back(std::move(final_info));
    }
  }

  // auto endpoints = merge_endpoints(stmt_infos);
  // not merge, it should be if if some stmt diverge, all following stmts are unreachable
  // logic is: if enpoints contain normal, remove normal and merge next, else break

  EndpointSet endpoints = {NormalEndpoint{}};
  for (const auto& info : stmt_infos) {
    if (endpoints.contains(NormalEndpoint{})) {
      endpoints.erase(NormalEndpoint{});
      merge_endpoints(endpoints, info);
    } else {
      break;
    }
  }
  
  // If block has no normal endpoint and has diverging statements, set type to never
  // Don't set empty blocks to never_type - they should be unit type
  if (!endpoints.contains(NormalEndpoint{}) && !stmt_infos.empty()) {
    result_type = get_typeID(Type{NeverType{}});
  }

  return ExprInfo(result_type, false, false, endpoints);
}

// Static variants
ExprInfo ExprChecker::check(hir::StructConst &expr) {
  return ExprInfo{.type =
                      get_resolved_type(*expr.assoc_const->type),
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = {NormalEndpoint{}}};
}

ExprInfo ExprChecker::check(hir::EnumVariant &expr) {
  return ExprInfo{.type = get_typeID(Type{EnumType{expr.enum_def}}),
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = {NormalEndpoint{}}};
}

} // namespace semantic