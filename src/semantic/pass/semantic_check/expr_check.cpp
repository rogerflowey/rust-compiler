#include "expr_check.hpp"
#include "temp_ref_desugger.hpp"
#include "hir/helper.hpp"
#include "hir/hir.hpp"
#include "semantic/const/evaluator.hpp"
#include "pass/semantic_check/expr_info.hpp"
#include "pass/semantic_check/other_check.hpp"
#include "semantic/query/semantic_context.hpp"
#include "semantic/type/helper.hpp"
#include "type/type.hpp"
#include "type_compatibility.hpp"
#include "utils.hpp"
#include "utils/error.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

using namespace semantic::helper::type_helper;
using namespace hir::helper;

namespace semantic {

namespace {

std::string get_function_display_name(const hir::Function &fn) {
  if (fn.ast_node && fn.ast_node->name) {
    return fn.ast_node->name->name;
  }
  return std::string{"<function>"};
}

std::string get_const_display_name(const hir::ConstDef &def) {
  if (def.ast_node && def.ast_node->name) {
    return def.ast_node->name->name;
  }
  return std::string{"<const>"};
}

} // namespace

ExprInfo ExprChecker::check(hir::Expr& expr) {
  return check(expr, TypeExpectation::none());
}

ExprInfo ExprChecker::check(hir::Expr& expr, TypeExpectation exp) {
  return context.expr_query(expr, exp);
}

ExprChecker::ContextGuard::ContextGuard(std::string kind, std::string name)
    : guard(debug::Context::instance().push(std::move(kind), std::move(name))) {
}

ExprChecker::ContextGuard ExprChecker::enter_context(std::string kind,
                                                     std::string name) {
  return ContextGuard(std::move(kind), std::move(name));
}

std::string ExprChecker::format_error(const std::string &message) const {
  return debug::format_with_context(message);
}

[[noreturn]] void
ExprChecker::throw_in_context(const std::string &message) const {
  throw std::runtime_error(format_error(message));
}

ExprInfo ExprChecker::evaluate(hir::Expr &expr, TypeExpectation exp) {
  struct CurrentExprGuard {
    ExprChecker &checker;
    hir::Expr *previous;
    CurrentExprGuard(ExprChecker &checker, hir::Expr *current)
        : checker(checker), previous(checker.current_expr) {
      checker.current_expr = current;
    }
    ~CurrentExprGuard() { checker.current_expr = previous; }
  } guard(*this, &expr);

  return std::visit(
      [this, &exp](auto &&arg) -> ExprInfo { return this->check(arg, exp); },
      expr.value);
}

ExprChecker::FunctionScopeGuard
ExprChecker::enter_function_scope(hir::Function &function) {
  hir::Function *previous_function = current_function;
  hir::Method *previous_method = current_method;
  size_t previous_counter = temp_local_counter;

  current_function = &function;
  current_method = nullptr;
  temp_local_counter = 0;

  return FunctionScopeGuard(*this, previous_function, previous_method,
                            previous_counter);
}

ExprChecker::MethodScopeGuard
ExprChecker::enter_method_scope(hir::Method &method) {
  hir::Function *previous_function = current_function;
  hir::Method *previous_method = current_method;
  size_t previous_counter = temp_local_counter;

  current_function = nullptr;
  current_method = &method;
  temp_local_counter = 0;

  return MethodScopeGuard(*this, previous_function, previous_method,
                          previous_counter);
}

hir::Expr &ExprChecker::current_expr_ref() {
  if (!current_expr) {
    throw std::logic_error("Current expression context not set");
  }
  return *current_expr;
}

void ExprChecker::replace_current_expr(hir::ExprVariant new_expr) {
  auto &expr = current_expr_ref();
  expr.value = std::move(new_expr);
  expr.expr_info.reset();
}

hir::Local *ExprChecker::create_temporary_local(bool is_mutable,
                                                TypeId type) {
  auto temp_identifier = generate_temp_identifier();
  auto local = std::make_unique<hir::Local>();
  local->name = std::move(temp_identifier);
  local->is_mutable = is_mutable;
  local->type_annotation = type;
  local->def_site = nullptr;

  auto *local_ptr = local.get();
  if (current_function) {
    current_function->locals.push_back(std::move(local));
  } else if (current_method) {
    current_method->locals.push_back(std::move(local));
  } else {
    throw std::logic_error(
        "Temporary local created outside of function or method context");
  }
  return local_ptr;
}

ast::Identifier ExprChecker::generate_temp_identifier() {
  const std::string name = "_temp" + std::to_string(temp_local_counter++);
  return ast::Identifier{name};
}

// Literal expressions
ExprInfo ExprChecker::check(hir::Literal &expr, TypeExpectation exp) {
  return std::visit(
      Overloaded{[&](hir::Literal::Integer &integer) -> ExprInfo {
                   TypeId type = invalid_type_id;
                   bool has_type = true;

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
                     if (exp.has_expected &&
                         is_integer_type(exp.expected)) {
                       type = exp.expected;
                     } else {
                       has_type = false;
                     }
                     break;
                   }

                   overflow_int_literal_check(integer);

                   return ExprInfo{.type = type,
                                   .has_type = has_type,
                                   .is_mut = false,
                                   .is_place = false,
                                   .endpoints = {NormalEndpoint{}},
                                   .const_value = detail::LiteralVisitor{}(integer)};
                 },
                 [](bool &value) -> ExprInfo {
                   return ExprInfo{.type =
                                       get_typeID(Type{PrimitiveKind::BOOL}),
                                   .has_type = true,
                                   .is_mut = false,
                                   .is_place = false,
                                   .endpoints = {NormalEndpoint{}},
                                   .const_value = BoolConst{value}};
                 },
                 [](char &value) -> ExprInfo {
                   return ExprInfo{.type =
                                       get_typeID(Type{PrimitiveKind::CHAR}),
                                   .has_type = true,
                                   .is_mut = false,
                                   .is_place = false,
                                   .endpoints = {NormalEndpoint{}},
                                   .const_value = CharConst{value}};
                 },
                 [](hir::Literal::String &value) -> ExprInfo {
                   return ExprInfo{.type = get_typeID(Type{ReferenceType{
                                       get_typeID(Type{PrimitiveKind::STRING}),
                                       false}}),
                                   .has_type = true,
                                   .is_mut = false,
                                   .is_place = false,
                                   .endpoints = {NormalEndpoint{}},
                                   .const_value = StringConst{value.value}};
                 }},
      expr.value);
}

ExprInfo ExprChecker::check(hir::Underscore &expr, TypeExpectation) {
  (void)expr; // Suppress unused parameter warning
  return ExprInfo{.type = get_typeID(Type{UnderscoreType{}}),
                  .has_type = true,
                  .is_mut = true,
                  .is_place = true,
                  .endpoints = {NormalEndpoint{}}};
}

// Reference expressions
ExprInfo ExprChecker::check(hir::Variable &expr, TypeExpectation) {
  if (!expr.local_id->type_annotation) {
    throw_in_context("Variable missing resolved type");
  }

  TypeId resolved_type =
      context.type_query(*expr.local_id->type_annotation);
  return ExprInfo{.type = resolved_type,
                  .has_type = true,
                  .is_mut = expr.local_id->is_mutable,
                  .is_place = true,
                  .endpoints = {NormalEndpoint{}}};
}

ExprInfo ExprChecker::check(hir::ConstUse &expr, TypeExpectation) {
  // Basic validation
  if (!expr.def) {
    throw std::logic_error("Const definition is null");
  }

  const std::string const_name = get_const_display_name(*expr.def);

  // Get const's declared type
  TypeId declared_type = context.type_query(*expr.def->type);

  // Perform type validation on const expression
  if (expr.def->expr) {
    // Check the original expression's type matches declared type
    ExprInfo expr_info;
    auto evaluate = [&]() {
      if (debug::is_current("const", const_name)) {
        return check(*expr.def->expr,
                     TypeExpectation::exact(declared_type));
      }
      auto guard = enter_context("const", const_name);
      return check(*expr.def->expr, TypeExpectation::exact(declared_type));
    };
    expr_info = evaluate();

    if (!expr_info.has_type) {
      throw_in_context("Cannot infer type for const '" + const_name +
                       "'; add a type annotation or literal suffix");
    }

    // Validate type compatibility using existing type checking infrastructure
    if (!is_assignable_to(expr_info.type, declared_type)) {
      throw_in_context("Const '" + const_name +
                       "' expression type doesn't match declared type");
    }

    // Note: We could store the validated expression info for debugging,
    // but this is optional and not required for the core functionality
  } else {
    throw std::logic_error("Const '" + const_name +
                           "' definition missing expression");
  }

  std::optional<ConstVariant> const_value;
  const_value = context.const_query(*expr.def);

  return ExprInfo{.type = declared_type,
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = {NormalEndpoint{}},
                  .const_value = const_value};
}

ExprInfo ExprChecker::check(hir::FuncUse &expr, TypeExpectation) {
  (void)expr; // Suppress unused parameter warning
  // Functions are not first-class values
  throw_in_context("Function used as value (functions are not first-class)");
}

ExprInfo ExprChecker::check(hir::FieldAccess &expr, TypeExpectation) {
  ExprInfo base_info = check(*expr.base, TypeExpectation::none());
  if (is_reference_type(base_info.type)) {
    expr.base = transform_helper::apply_dereference(std::move(expr.base));
    base_info = check(*expr.base, TypeExpectation::none());
  }

  auto struct_type = std::get_if<StructType>(&base_info.type->value);
  if (!struct_type) {
    throw_in_context("Field access base must be a struct");
  }

  // resolve field
  auto name_ptr = std::get_if<ast::Identifier>(&expr.field);
  if (!name_ptr) {
    throw std::logic_error("Field access already resolved");
  }
  auto field_id = struct_type->symbol->find_field(*name_ptr);
  if (!field_id) {
    throw_in_context("Field '" + name_ptr->name + "' not found in struct '" +
                     get_name(*struct_type->symbol).name + "'");
  }
  expr.field = *field_id;
  TypeId type =
      context.type_query(struct_type->symbol->field_type_annotations[*field_id]);
  struct_type->symbol->fields[*field_id].type = type;
  return ExprInfo{.type = type,
                  .has_type = true,
                  .is_mut = base_info.is_mut,
                  .is_place = true,
                  .endpoints = base_info.endpoints};
}

ExprInfo ExprChecker::check(hir::Index &expr, TypeExpectation) {
  ExprInfo base_info = check(*expr.base, TypeExpectation::none());
  if (is_reference_type(base_info.type)) {
    expr.base = transform_helper::apply_dereference(std::move(expr.base));
    base_info = check(*expr.base, TypeExpectation::none());
  }

  auto array_type = std::get_if<ArrayType>(&base_info.type->value);
  if (!array_type) {
    throw_in_context("Index base must be an array");
  }

  TypeId usize_type = get_typeID(Type{PrimitiveKind::USIZE});
  ExprInfo index_info =
      check(*expr.index, TypeExpectation::exact(usize_type));
  if (!index_info.has_type) {
    throw_in_context("Cannot infer type for array index; expected usize");
  }
  if (!is_assignable_to(index_info.type, usize_type)) {
    throw_in_context("Index must be coercible to usize");
  }

  EndpointSet endpoints = sequence_endpoints(base_info, index_info);
  return ExprInfo{.type = array_type->element_type,
                  .has_type = true,
                  .is_mut = base_info.is_mut,
                  .is_place = true,
                  .endpoints = std::move(endpoints)};
}

ExprInfo ExprChecker::check(hir::StructLiteral &expr, TypeExpectation) {
  auto struct_def = get_struct_def(expr);
  const auto &fields = get_canonical_fields(expr).initializers;

  if (fields.size() != struct_def->fields.size()) {
    throw_in_context("Struct literal for '" + get_name(*struct_def).name +
                     "' field count mismatch");
  }

  std::vector<ExprInfo> field_infos;
  field_infos.reserve(fields.size());

  for (size_t i = 0; i < fields.size(); ++i) {
    TypeId expected_type =
        context.type_query(struct_def->field_type_annotations[i]);
    struct_def->fields[i].type = expected_type;
    ExprInfo field_info =
        check(*fields[i], TypeExpectation::exact(expected_type));
    if (!field_info.has_type) {
      const auto &field_name = struct_def->fields[i].name.name;
      throw_in_context("Cannot infer type for field '" + field_name +
                       "' in struct '" + get_name(*struct_def).name + "'");
    }

    if (!is_assignable_to(field_info.type, expected_type)) {
      throw_in_context("Struct literal field type mismatch for '" +
                       get_name(*struct_def).name + "'");
    }
    field_infos.push_back(std::move(field_info));
  }

  return ExprInfo{.type = get_typeID(Type{StructType{struct_def}}),
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = sequence_endpoints(field_infos)};
}

ExprInfo ExprChecker::check(hir::ArrayLiteral &expr, TypeExpectation exp) {
  if (expr.elements.empty()) {
    throw_in_context("Array literal cannot be empty");
  }

  std::vector<ExprInfo> elem_infos;
  elem_infos.reserve(expr.elements.size());
  for (const auto &elem : expr.elements) {
    elem_infos.push_back(check(*elem, TypeExpectation::none()));
  }

  auto expected_array = [&]() -> const ArrayType * {
    if (!exp.has_expected) {
      return nullptr;
    }
    return std::get_if<ArrayType>(&exp.expected->value);
  }();

  std::optional<TypeId> element_type;
  auto merge_type = [&](TypeId other) {
    if (!element_type) {
      element_type = other;
      return true;
    }
    auto common = find_common_type(*element_type, other);
    if (!common) {
      return false;
    }
    element_type = *common;
    return true;
  };

  for (const auto &info : elem_infos) {
    if (info.has_type) {
      if (!merge_type(info.type)) {
        throw_in_context("Array literal elements must have compatible types");
      }
    }
  }

  if (expected_array) {
    if (!element_type) {
      element_type = expected_array->element_type;
    } else if (!merge_type(expected_array->element_type)) {
      throw_in_context("Array literal does not satisfy expected element type");
    }
  }

  if (element_type) {
    for (size_t i = 0; i < elem_infos.size(); ++i) {
      if (!elem_infos[i].has_type) {
        elem_infos[i] =
            check(*expr.elements[i], TypeExpectation::exact(*element_type));
      }
      if (!elem_infos[i].has_type || !merge_type(elem_infos[i].type)) {
        element_type.reset();
        break;
      }
    }
  }

  EndpointSet endpoints = sequence_endpoints(elem_infos);

  if (!element_type) {
    return ExprInfo{.type = invalid_type_id,
                    .has_type = false,
                    .is_mut = false,
                    .is_place = false,
                    .endpoints = std::move(endpoints)};
  }

  TypeId array_type = get_typeID(
      Type{ArrayType{*element_type, expr.elements.size()}});
  return ExprInfo{.type = array_type,
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = std::move(endpoints)};
}

ExprInfo ExprChecker::check(hir::ArrayRepeat &expr, TypeExpectation exp) {
  const ArrayType *expected_array = nullptr;
  if (exp.has_expected) {
    expected_array = std::get_if<ArrayType>(&exp.expected->value);
  }

  TypeExpectation value_expectation = TypeExpectation::none();
  if (expected_array) {
    value_expectation =
        TypeExpectation::exact(expected_array->element_type);
  }

  ExprInfo value_info = check(*expr.value, value_expectation);
  if (!value_info.has_type && value_expectation.has_expected) {
    throw_in_context("Cannot infer type for repeated array element");
  }

  if (!value_info.has_type) {
    return ExprInfo{.type = invalid_type_id,
                    .has_type = false,
                    .is_mut = false,
                    .is_place = false,
                    .endpoints = value_info.endpoints};
  }

  auto resolve_count = [&]() -> size_t {
    if (auto count_ptr = std::get_if<size_t>(&expr.count)) {
      return *count_ptr;
    }

    auto count_expr_ptr = std::get_if<std::unique_ptr<hir::Expr>>(&expr.count);
    if (!count_expr_ptr || !*count_expr_ptr) {
      throw_in_context("Array repeat count expression is missing");
    }

    ConstVariant const_value = context.const_query(**count_expr_ptr,
                                                   get_typeID(Type{PrimitiveKind::USIZE}));
    if (auto *uint_value = std::get_if<UintConst>(&const_value)) {
      expr.count = static_cast<size_t>(uint_value->value);
      return std::get<size_t>(expr.count);
    }

    throw_in_context("Array repeat count must be a usize constant");
  };

  size_t count = resolve_count();
  TypeId array_type =
      get_typeID(Type{ArrayType{value_info.type, count}});
  return ExprInfo{.type = array_type,
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = value_info.endpoints};
}

// Operations
ExprInfo ExprChecker::check(hir::UnaryOp &expr, TypeExpectation exp) {
  const TypeId bool_type = get_typeID(Type{PrimitiveKind::BOOL});
  TypeExpectation operand_expectation = TypeExpectation::none();

  auto is_numeric_expectation = [&](const TypeExpectation &expectation) {
    return expectation.has_expected &&
           helper::type_helper::is_numeric_type(expectation.expected);
  };

  switch (expr.op) {
  case hir::UnaryOp::NEGATE:
    if (is_numeric_expectation(exp)) {
      operand_expectation = exp;
    }
    break;
  case hir::UnaryOp::NOT:
    if (exp.has_expected && is_bool_type(exp.expected)) {
      operand_expectation = TypeExpectation::exact(bool_type);
    } else if (is_numeric_expectation(exp)) {
      operand_expectation = exp;
    }
    break;
  case hir::UnaryOp::REFERENCE:
  case hir::UnaryOp::MUTABLE_REFERENCE:
    if (exp.has_expected) {
      if (const auto *ref_type =
              std::get_if<ReferenceType>(&exp.expected->value)) {
        operand_expectation =
            TypeExpectation::exact(ref_type->referenced_type);
      }
    }
    break;
  default:
    break;
  }

  ExprInfo operand_info = check(*expr.rhs, operand_expectation);
  auto unresolved = [&](bool as_place = false) {
    return ExprInfo{.type = invalid_type_id,
                    .has_type = false,
                    .is_mut = false,
                    .is_place = as_place,
                    .endpoints = operand_info.endpoints};
  };

  auto ensure_operand_type = [&](const std::string &msg) {
    if (!operand_info.has_type) {
      if (operand_expectation.has_expected) {
        throw_in_context(msg);
      }
      throw_in_context("Cannot infer type for operand; " + msg);
    }
  };

  switch (expr.op) {
  case hir::UnaryOp::NOT: {
    std::optional<ConstVariant> folded;
    if (operand_info.const_value) {
      folded = std::visit(detail::UnaryOpVisitor{expr.op}, *operand_info.const_value);
    }
    if (!operand_info.has_type) {
      return unresolved();
    }
    if (is_bool_type(operand_info.type)) {
      return ExprInfo{.type = bool_type,
                      .has_type = true,
                      .is_mut = false,
                      .is_place = false,
                      .endpoints = operand_info.endpoints,
                      .const_value = folded};
    }
    if (is_numeric_type(operand_info.type)) {
      return ExprInfo{.type = operand_info.type,
                      .has_type = true,
                      .is_mut = false,
                      .is_place = false,
                      .endpoints = operand_info.endpoints,
                      .const_value = folded};
    }
    throw_in_context("NOT operand must be boolean or integer");
  }
  case hir::UnaryOp::NEGATE: {
    std::optional<ConstVariant> folded;
    if (operand_info.const_value) {
      folded = std::visit(detail::UnaryOpVisitor{expr.op}, *operand_info.const_value);
    }
    if (!operand_info.has_type) {
      return unresolved();
    }
    if (!is_numeric_type(operand_info.type)) {
      throw_in_context("NEGATE operand must be numeric");
    }
    return ExprInfo{.type = operand_info.type,
                    .has_type = true,
                    .is_mut = false,
                    .is_place = false,
                    .endpoints = operand_info.endpoints,
                    .const_value = folded};
  }
  case hir::UnaryOp::DEREFERENCE: {
    ensure_operand_type("Cannot infer referenced type for dereference");
    if (!is_reference_type(operand_info.type)) {
      throw_in_context("DEREFERENCE operand must be reference");
    }
    TypeId referenced_type = get_referenced_type(operand_info.type);
    return ExprInfo{.type = referenced_type,
                    .has_type = true,
                    .is_mut = get_reference_mutability(operand_info.type),
                    .is_place = true,
                    .endpoints = operand_info.endpoints};
  }
  case hir::UnaryOp::REFERENCE:
  case hir::UnaryOp::MUTABLE_REFERENCE: {
    ensure_operand_type("Cannot infer type for referenced value");

    if (!operand_info.is_place) {
      return TempRefDesugger::desugar_reference_to_temporary(expr,
                                                             operand_info,
                                                             *this);
    }

    bool is_mut = (expr.op == hir::UnaryOp::MUTABLE_REFERENCE);
    if (is_mut && !operand_info.is_mut) {
      throw_in_context("Cannot borrow immutable value as mutable");
    }

    TypeId ref_type =
        get_typeID(Type{ReferenceType{operand_info.type, is_mut}});
    return ExprInfo{.type = ref_type,
                    .has_type = true,
                    .is_mut = false,
                    .is_place = false,
                    .endpoints = operand_info.endpoints};
  }
  default:
    throw std::logic_error("Unknown unary operator");
  }
}

ExprInfo ExprChecker::check(hir::BinaryOp &expr, TypeExpectation exp) {
  auto eval_operand = [&](std::unique_ptr<hir::Expr> &node) {
    return check(*node, TypeExpectation::none());
  };

  ExprInfo lhs_info = eval_operand(expr.lhs);
  ExprInfo rhs_info = eval_operand(expr.rhs);
  EndpointSet endpoints = sequence_endpoints(lhs_info, rhs_info);
  std::optional<ConstVariant> folded;

  auto recheck_with = [&](ExprInfo &info, std::unique_ptr<hir::Expr> &node,
                          TypeId type) {
    if (!info.has_type) {
      info = check(*node, TypeExpectation::exact(type));
    }
  };

  auto unresolved = [&]() {
    return ExprInfo{.type = invalid_type_id,
                    .has_type = false,
                    .is_mut = false,
                    .is_place = false,
                    .endpoints = endpoints};
  };

  auto is_numeric_op = [&]() {
    using Op = hir::BinaryOp;
    switch (expr.op) {
    case Op::ADD:
    case Op::SUB:
    case Op::MUL:
    case Op::DIV:
    case Op::REM:
    case Op::BIT_AND:
    case Op::BIT_XOR:
    case Op::BIT_OR:
    case Op::SHL:
    case Op::SHR:
      return true;
    default:
      return false;
    }
  }();

  if (lhs_info.has_type && is_numeric_type(lhs_info.type)) {
    recheck_with(rhs_info, expr.rhs, lhs_info.type);
  }
  if (rhs_info.has_type && is_numeric_type(rhs_info.type)) {
    recheck_with(lhs_info, expr.lhs, rhs_info.type);
  }

  if (lhs_info.const_value && rhs_info.const_value) {
    try {
      folded = std::visit(detail::BinaryOpVisitor{expr.op}, *lhs_info.const_value,
                          *rhs_info.const_value);
    } catch (...) {
      folded.reset();
    }
  }

  if (is_numeric_op && exp.has_expected &&
      helper::type_helper::is_numeric_type(exp.expected)) {
    recheck_with(lhs_info, expr.lhs, exp.expected);
    recheck_with(rhs_info, expr.rhs, exp.expected);
  }

  switch (expr.op) {
  case hir::BinaryOp::ADD:
  case hir::BinaryOp::SUB:
  case hir::BinaryOp::MUL:
  case hir::BinaryOp::DIV:
  case hir::BinaryOp::REM:
  case hir::BinaryOp::BIT_AND:
  case hir::BinaryOp::BIT_XOR:
  case hir::BinaryOp::BIT_OR: {
    if (!lhs_info.has_type || !rhs_info.has_type) {
      return unresolved();
    }
    if (!is_numeric_type(lhs_info.type) || !is_numeric_type(rhs_info.type)) {
      throw_in_context("Arithmetic operands must be numeric");
    }
    auto common_type = find_common_type(lhs_info.type, rhs_info.type);
    if (!common_type) {
      throw_in_context("Arithmetic operands must have compatible types");
    }
    return ExprInfo{.type = *common_type,
                    .has_type = true,
                    .is_mut = false,
                    .is_place = false,
                    .endpoints = endpoints,
                    .const_value = folded};
  }
  case hir::BinaryOp::EQ:
  case hir::BinaryOp::NE:
  case hir::BinaryOp::LT:
  case hir::BinaryOp::GT:
  case hir::BinaryOp::LE:
  case hir::BinaryOp::GE: {
    if (!lhs_info.has_type || !rhs_info.has_type) {
      return unresolved();
    }
    if (!are_comparable(lhs_info.type, rhs_info.type)) {
      throw_in_context("Comparison operands must be comparable");
    }
    return ExprInfo{.type = get_typeID(Type{PrimitiveKind::BOOL}),
                    .has_type = true,
                    .is_mut = false,
                    .is_place = false,
                    .endpoints = endpoints,
                    .const_value = folded};
  }
  case hir::BinaryOp::AND:
  case hir::BinaryOp::OR: {
    recheck_with(lhs_info, expr.lhs, get_typeID(Type{PrimitiveKind::BOOL}));
    recheck_with(rhs_info, expr.rhs, get_typeID(Type{PrimitiveKind::BOOL}));
    if (!lhs_info.has_type || !rhs_info.has_type) {
      return unresolved();
    }
    if (!is_bool_type(lhs_info.type) || !is_bool_type(rhs_info.type)) {
      throw_in_context("Logical operands must be boolean");
    }
    return ExprInfo{.type = get_typeID(Type{PrimitiveKind::BOOL}),
                    .has_type = true,
                    .is_mut = false,
                    .is_place = false,
                    .endpoints = endpoints,
                    .const_value = folded};
  }
  case hir::BinaryOp::SHL:
  case hir::BinaryOp::SHR: {
    if (!lhs_info.has_type) {
      return unresolved();
    }
    if (!is_numeric_type(lhs_info.type)) {
      throw_in_context("Shift operand must be numeric");
    }
    if (!rhs_info.has_type) {
      return unresolved();
    }
    if (!is_numeric_type(rhs_info.type)) {
      throw_in_context("Shift amount must be numeric");
    }
    return ExprInfo{.type = lhs_info.type,
                    .has_type = true,
                    .is_mut = false,
                    .is_place = false,
                    .endpoints = endpoints,
                    .const_value = folded};
  }
  default:
    throw std::logic_error("Unknown binary operator");
  }
}

ExprInfo ExprChecker::check(hir::Assignment &expr, TypeExpectation) {
  ExprInfo lhs_info = check(*expr.lhs, TypeExpectation::none());
  if (!lhs_info.is_place || !lhs_info.is_mut) {
    throw_in_context("Assignment target must be mutable place");
  }

  ExprInfo rhs_info = check(*expr.rhs,
                            TypeExpectation::exact(lhs_info.type));
  if (!rhs_info.has_type) {
    throw_in_context("Cannot infer type for assignment right-hand side");
  }
  if (!is_assignable_to(rhs_info.type, lhs_info.type)) {
    throw_in_context("Assignment type mismatch");
  }

  return ExprInfo{.type = get_typeID(Type{UnitType{}}),
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = rhs_info.endpoints};
}

ExprInfo ExprChecker::check(hir::Cast &expr, TypeExpectation exp) {
  (void)exp; // Cast result expectation is unused for now

  TypeId target_type = context.type_query(expr.target_type);
  ExprInfo operand_info = check(*expr.expr, TypeExpectation::none());

  if (!operand_info.has_type) {
    operand_info = check(*expr.expr, TypeExpectation::exact(target_type));
  }
  if (!operand_info.has_type) {
    throw_in_context("Cannot infer type for cast operand");
  }

  // Validate cast allowed between source and target types
  if (!is_castable_to(operand_info.type, target_type)) {
    throw_in_context("Invalid cast between types");
  }

  return ExprInfo{.type = target_type,
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = operand_info.endpoints};
}

ExprInfo ExprChecker::check(hir::Call &expr, TypeExpectation) {
  auto func_type = std::get_if<hir::FuncUse>(&expr.callee->value);
  if (!func_type) {
    throw_in_context("Call target must be a function");
  }

  // Check argument types
  const std::string func_name = get_function_display_name(*func_type->def);

  if (func_type->def->params.size() != expr.args.size()) {
    throw_in_context("Argument count mismatch when calling function '" +
                     func_name + "'");
  }

  std::vector<ExprInfo> arg_infos;
  arg_infos.reserve(expr.args.size());
  for (size_t i = 0; i < func_type->def->params.size(); ++i) {
    TypeId param_type =
        context.type_query(*func_type->def->param_type_annotations[i]);
    ExprInfo arg_info =
        check(*expr.args[i], TypeExpectation::exact(param_type));
    if (!arg_info.has_type) {
      throw_in_context("Cannot infer type for argument " +
                       std::to_string(i) + " when calling function '" +
                       func_name + "'");
    }
    if (!is_assignable_to(arg_info.type, param_type)) {
      throw_in_context("Argument type mismatch when calling function '" +
                       func_name + "'");
    }
    arg_infos.push_back(std::move(arg_info));
  }

  EndpointSet endpoints = sequence_endpoints(arg_infos);

  return ExprInfo{.type = context.type_query(*func_type->def->return_type),
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = std::move(endpoints)};
}

ExprInfo ExprChecker::check(hir::MethodCall &expr, TypeExpectation) {
  // Check receiver expression first
  ExprInfo receiver_info = check(*expr.receiver, TypeExpectation::none());
  if (!receiver_info.has_type) {
    auto literal = std::get_if<hir::Literal>(&expr.receiver->value);
    if (literal) {
      if (std::holds_alternative<hir::Literal::Integer>(literal->value)) {
        TypeId default_int = get_typeID(Type{PrimitiveKind::I32});
        receiver_info = check(*expr.receiver,
                              TypeExpectation::exact(default_int));
      }
    }
  }
  if (!receiver_info.has_type) {
    throw_in_context("Cannot infer type for method receiver");
  }

  // Extract base type from potentially nested references
  TypeId base_type = get_base_type(receiver_info.type);
  auto name = std::get_if<ast::Identifier>(&expr.method);
  if (!name) {
    throw std::logic_error("Method name not resolved");
  }

  // Find method in impl table to resolve the method
  auto method_def = impl_table.lookup_method(base_type, *name);
  if (!method_def) {
    throw_in_context("Method '" + name->name + "' not found");
  }
  expr.method = method_def;

  // Check argument count matches method parameters
  if (method_def->params.size() != expr.args.size()) {
    const std::string method_name = get_name(*method_def).name;
    throw_in_context("Method argument count mismatch for '" + method_name +
                     "'");
  }

  // Determine if auto-reference is needed based on method's self parameter
  TypeId expected_receiver_type = base_type;
  bool needs_auto_reference = false;

  if (method_def->self_param.is_reference) {
    // Create reference type matching method's self parameter
    expected_receiver_type = get_typeID(
        Type{ReferenceType{.referenced_type = base_type,
                           .is_mutable = method_def->self_param.is_mutable}});

    // Only create an auto-reference when the receiver is not already assignable
    needs_auto_reference =
        !is_assignable_to(receiver_info.type, expected_receiver_type);
  }

  // Apply auto-reference if needed
  ExprInfo final_receiver_info = receiver_info;
  if (needs_auto_reference) {
    // Transform HIR to insert explicit reference operation
    expr.receiver = transform_helper::apply_reference(
        std::move(expr.receiver), method_def->self_param.is_mutable);
    final_receiver_info = check(*expr.receiver, TypeExpectation::none());
  }

  // Verify receiver type compatibility with method's self parameter
  if (!is_assignable_to(final_receiver_info.type, expected_receiver_type)) {
    const std::string method_name = get_name(*method_def).name;
    throw_in_context("Receiver type mismatch when calling method '" +
                     method_name + "'");
  }

  // Check argument types
  std::vector<ExprInfo> eval_infos;
  eval_infos.reserve(expr.args.size() + 1);
  eval_infos.emplace_back(std::move(final_receiver_info));

  for (size_t i = 0; i < expr.args.size(); ++i) {
    TypeId expected_param_type =
        context.type_query(*method_def->param_type_annotations[i]);
    ExprInfo arg_info =
        check(*expr.args[i], TypeExpectation::exact(expected_param_type));
    if (!arg_info.has_type) {
      const std::string method_name = get_name(*method_def).name;
      throw_in_context("Cannot infer type for argument " +
                       std::to_string(i) + " when calling method '" +
                       method_name + "'");
    }

    if (!is_assignable_to(arg_info.type, expected_param_type)) {
      const std::string method_name = get_name(*method_def).name;
      throw_in_context("Method argument type mismatch for '" + method_name +
                       "'");
    }
  eval_infos.emplace_back(std::move(arg_info));
  }

  // Merge endpoints from receiver and arguments respecting evaluation order
  EndpointSet endpoints = sequence_endpoints(eval_infos);

  // Return method's return type
  return ExprInfo{.type = context.type_query(*method_def->return_type),
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = std::move(endpoints)};
}

ExprInfo ExprChecker::check(hir::If &expr, TypeExpectation exp) {
  TypeId bool_type = get_typeID(Type{PrimitiveKind::BOOL});
  ExprInfo cond_info =
      check(*expr.condition, TypeExpectation::exact(bool_type));
  if (!cond_info.has_type || !is_bool_type(cond_info.type)) {
    throw_in_context("If condition must be boolean");
  }

  ExprInfo then_info = check(*expr.then_block, exp);

  if (expr.else_expr) {
    ExprInfo else_info = check(*(*expr.else_expr), exp);

    if (!then_info.has_type || !else_info.has_type) {
      if (exp.has_expected) {
        throw_in_context("Cannot infer type for if expression branch");
      }
      auto endpoints = merge_endpoints({cond_info, then_info, else_info});
      return ExprInfo{.type = invalid_type_id,
                      .has_type = false,
                      .is_mut = false,
                      .is_place = false,
                      .endpoints = endpoints};
    }

    auto common_type = find_common_type(then_info.type, else_info.type);
    if (!common_type) {
      if (helper::type_helper::is_never_type(then_info.type)) {
        common_type = else_info.type;
      } else if (helper::type_helper::is_never_type(else_info.type)) {
        common_type = then_info.type;
      } else {
        auto unit_type = get_typeID(Type{UnitType{}});
        if (is_assignable_to(then_info.type, unit_type) &&
            is_assignable_to(else_info.type, unit_type)) {
          common_type = unit_type;
        } else if (is_assignable_to(then_info.type, else_info.type)) {
          common_type = else_info.type;
        } else if (is_assignable_to(else_info.type, then_info.type)) {
          common_type = then_info.type;
        } else {
          throw_in_context("If branches must have compatible types");
        }
      }
    }

    auto endpoints = merge_endpoints({cond_info, then_info, else_info});
    return ExprInfo{.type = *common_type,
                    .has_type = true,
                    .is_mut = false,
                    .is_place = false,
                    .endpoints = endpoints};
  }

  auto unit_type = get_typeID(Type{UnitType{}});
  if (exp.has_expected && exp.expected != unit_type) {
    throw_in_context("If expression without else must produce unit type");
  }
  auto endpoints = merge_endpoints({cond_info, then_info});
  endpoints.insert(NormalEndpoint{});
  return ExprInfo{.type = unit_type,
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = endpoints};
}

ExprInfo ExprChecker::check(hir::Loop &expr, TypeExpectation exp) {
  ExprInfo body_info = check(*expr.body, TypeExpectation::none());

  EndpointSet endpoints;
  const bool body_has_normal = body_info.has_normal_endpoint();
  bool has_break = false;
  for (const auto &endpoint : body_info.endpoints) {
    std::visit(
        Overloaded{[&](const NormalEndpoint &) {
                     // Loop body finishing normally just continues
                   },
                   [&](const ContinueEndpoint &cont_ep) {
                     auto loop_target = std::get_if<hir::Loop *>(&cont_ep.target);
                     if (!loop_target || *loop_target != &expr) {
                       endpoints.insert(endpoint);
                     }
                   },
                   [&](const BreakEndpoint &break_ep) {
                     auto loop_target = std::get_if<hir::Loop *>(&break_ep.target);
                     if (loop_target && *loop_target == &expr) {
                       has_break = true;
                       endpoints.insert(NormalEndpoint{});
                     } else {
                       endpoints.insert(endpoint);
                     }
                   },
                   [&](const auto &) { endpoints.insert(endpoint); }},
        endpoint);
  }

  if (body_has_normal) {
    endpoints.insert(NormalEndpoint{});
  }

  TypeId never_type = get_typeID(Type{NeverType{}});
  TypeId loop_type = never_type;

  if (has_break) {
    if (!expr.break_type) {
      throw_in_context("Loop break type missing despite break expressions");
    }
    loop_type = *expr.break_type;
    if (exp.has_expected &&
        !is_assignable_to(loop_type, exp.expected)) {
      throw_in_context("Loop result type not assignable to expected type");
    }
  } else {
    expr.break_type = never_type;
    // Diverging loops can satisfy any expectation since they never
    // produce a value through normal completion.
  }

  return ExprInfo{.type = loop_type,
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = endpoints};
}

ExprInfo ExprChecker::check(hir::While &expr, TypeExpectation exp) {
  TypeId bool_type = get_typeID(Type{PrimitiveKind::BOOL});
  ExprInfo cond_info =
      check(*expr.condition, TypeExpectation::exact(bool_type));
  if (!cond_info.has_type || !is_bool_type(cond_info.type)) {
    throw_in_context("While condition must be boolean");
  }

  auto unit_type = get_typeID(Type{UnitType{}});
  if (exp.has_expected && exp.expected != unit_type) {
    throw_in_context("While expression must have unit type");
  }

  ExprInfo body_info = check(*expr.body, TypeExpectation::exact(unit_type));
  if (!is_assignable_to(body_info.type, unit_type)) {
    throw_in_context("While body must be assignable to unit type");
  }

  expr.break_type = unit_type;

  EndpointSet endpoints = merge_endpoints(cond_info, body_info);
  endpoints.erase(ContinueEndpoint{.target = &expr});

  auto break_it = endpoints.find(
      BreakEndpoint{.target = &expr, .value_type = *expr.break_type});
  if (break_it != endpoints.end()) {
    endpoints.erase(break_it);
    endpoints.insert(NormalEndpoint{});
  }

  return ExprInfo{.type = *expr.break_type,
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = endpoints};
}

ExprInfo ExprChecker::check(hir::Break &expr, TypeExpectation) {
  auto unit_type = get_typeID(Type{UnitType{}});
  TypeExpectation value_expectation = TypeExpectation::none();

  std::visit(Overloaded{[&](hir::Loop *loop) {
                          if (loop->break_type) {
                            value_expectation =
                                TypeExpectation::exact(*loop->break_type);
                          }
                        },
                        [&](hir::While *while_loop) {
                          if (while_loop->break_type) {
                            value_expectation = TypeExpectation::exact(
                                *while_loop->break_type);
                          }
                        }},
             *expr.target);

  ExprInfo value_info{.type = unit_type,
                      .has_type = true,
                      .is_mut = false,
                      .is_place = false,
                      .endpoints = {NormalEndpoint{}}};
  if (expr.value) {
    value_info = check(*(*expr.value), value_expectation);
    if (!value_info.has_type) {
      throw_in_context("Cannot infer type for break value");
    }
  } else if (value_expectation.has_expected &&
             value_expectation.expected != unit_type) {
    throw_in_context("Break requires a value");
  }

  TypeId value_type = value_info.type;

  std::visit(Overloaded{[&](hir::Loop *loop) {
                          if (!loop->break_type) {
                            loop->break_type = value_type;
                          } else if (!is_assignable_to(value_type,
                                                        *loop->break_type)) {
                            throw_in_context("Break value type not assignable to "
                                             "loop break type");
                          }
                        },
                        [&](hir::While *while_loop) {
                          if (!while_loop->break_type) {
                            while_loop->break_type = value_type;
                          } else if (!is_assignable_to(
                                         value_type, *while_loop->break_type)) {
                            throw_in_context("Break value type not assignable to "
                                             "while loop break type");
                          }
                        }},
             *expr.target);

  EndpointSet endpoints = value_info.endpoints;
  endpoints.erase(NormalEndpoint{});
  endpoints.insert(BreakEndpoint{*expr.target, value_type});

  return ExprInfo{.type = get_typeID(Type{NeverType{}}),
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = std::move(endpoints)};
}

ExprInfo ExprChecker::check(hir::Continue &expr, TypeExpectation) {
  return ExprInfo{.type = get_typeID(Type{NeverType{}}),
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = {ContinueEndpoint{*expr.target}}};
}

ExprInfo ExprChecker::check(hir::Return &expr, TypeExpectation) {
  TypeId target_return_type = get_typeID(Type{UnitType{}});
  std::visit(
      Overloaded{[&](hir::Function *func) {
                   target_return_type = context.type_query(*func->return_type);
                 },
                 [&](hir::Method *method) {
                   target_return_type = context.type_query(*method->return_type);
                 }},
      *expr.target);

  ExprInfo value_info{.type = get_typeID(Type{UnitType{}}),
                      .has_type = true,
                      .is_mut = false,
                      .is_place = false,
                      .endpoints = {NormalEndpoint{}}};
  if (expr.value) {
    value_info = check(*(*expr.value),
                       TypeExpectation::exact(target_return_type));
    if (!value_info.has_type) {
      throw_in_context("Cannot infer type for return value");
    }
  }

  if (!is_assignable_to(value_info.type, target_return_type)) {
    throw_in_context(
        "Return value type does not match declared return type");
  }

  EndpointSet endpoints = value_info.endpoints;
  endpoints.erase(NormalEndpoint{});
  endpoints.insert(ReturnEndpoint{*expr.target, value_info.type});

  return ExprInfo{.type = get_typeID(Type{NeverType{}}),
                  .has_type = true,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = std::move(endpoints)};
}

// Block expressions
ExprInfo ExprChecker::check(hir::Block &expr, TypeExpectation exp) {
  std::vector<ExprInfo> stmt_infos;
  stmt_infos.reserve(expr.stmts.size() + 1);

  for (const auto &stmt : expr.stmts) {
    std::visit(
        Overloaded{[this, &stmt_infos](hir::LetStmt &let) {
                     if (!let.initializer) {
                       throw_in_context("Let statement must have initializer");
                     }
                     TypeExpectation init_expectation = TypeExpectation::none();
                     TypeId resolved_type = invalid_type_id;
                     if (let.type_annotation) {
                       resolved_type =
                           context.type_query(*let.type_annotation);
                       init_expectation =
                           TypeExpectation::exact(resolved_type);
                     }

                     ExprInfo init_info = check(*let.initializer, init_expectation);
                     if (let.type_annotation) {
                       if (!init_info.has_type ||
                           !is_assignable_to(init_info.type, resolved_type)) {
                         throw_in_context("Let initializer type doesn't match "
                                          "annotation");
                       }
                     } else {
                       if (!init_info.has_type) {
                         throw_in_context("Cannot infer type for let initializer");
                       }
                       resolved_type = init_info.type;
                       let.type_annotation = hir::TypeAnnotation(resolved_type);
                     }

                     if (let.pattern) {
                       context.bind_pattern_type(*let.pattern, resolved_type);
                     }
                     stmt_infos.push_back(std::move(init_info));
                   },
                   [this, &stmt_infos](hir::ExprStmt &expr_stmt) {
                     stmt_infos.push_back(
                         check(*expr_stmt.expr, TypeExpectation::none()));
                   }},
        stmt->value);
  }

  TypeId unit_type = get_typeID(Type{UnitType{}});
  TypeId result_type = unit_type;
  bool has_type = true;
  bool missing_final_expr = !(expr.final_expr && *expr.final_expr);

  if (expr.final_expr && *expr.final_expr) {
    ExprInfo final_info = check(**expr.final_expr, exp);
    has_type = final_info.has_type;
    result_type = final_info.type;
    stmt_infos.push_back(std::move(final_info));
  } else {
    result_type = unit_type;
  }

  if (!has_type) {
    if (exp.has_expected) {
      throw_in_context("Cannot infer block result type");
    }
    result_type = invalid_type_id;
  }

  EndpointSet endpoints = sequence_endpoints(stmt_infos);
  if (!endpoints.contains(NormalEndpoint{}) && !stmt_infos.empty()) {
    result_type = get_typeID(Type{NeverType{}});
    has_type = true;
  }

  std::optional<ConstVariant> const_value;
  if (!stmt_infos.empty() && endpoints.contains(NormalEndpoint{})) {
    const auto &last = stmt_infos.back();
    if (last.const_value) {
      const_value = last.const_value;
    }
  }

  if (missing_final_expr && exp.has_expected && exp.expected != unit_type &&
      endpoints.contains(NormalEndpoint{})) {
    throw_in_context("Block without final expression must be unit type");
  }

  return ExprInfo{.type = result_type,
                  .has_type = has_type,
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = endpoints,
                  .const_value = const_value};
}

// Static variants
ExprInfo ExprChecker::check(hir::StructConst &expr, TypeExpectation) {
  return ExprInfo{.type = context.type_query(*expr.assoc_const->type),
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = {NormalEndpoint{}}};
}

ExprInfo ExprChecker::check(hir::EnumVariant &expr, TypeExpectation) {
  return ExprInfo{.type = get_typeID(Type{EnumType{expr.enum_def}}),
                  .is_mut = false,
                  .is_place = false,
                  .endpoints = {NormalEndpoint{}}};
}

} // namespace semantic