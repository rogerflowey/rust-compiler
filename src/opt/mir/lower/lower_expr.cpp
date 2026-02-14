// opt/mir/lower/lower_expr.cpp — Expression lowering for opt MIR
//
// Dispatches each HIR expression variant to its concrete lowering
// function, producing floating NodeIds and threading TokenIds for
// side-effecting operations.

#include "opt/mir/lower/lower_internal.hpp"

#include "mir/lower/lower_common.hpp"
#include "mir/lower/lower_const.hpp"

#include "semantic/expr_info_helpers.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/utils.hpp"
#include "type/type.hpp"

#include <stdexcept>

namespace opt::mir {

using type::invalid_type_id;
using type::TypeId;

// ═══════════════════════════════════════════════════════════════════
// Top-level expression dispatch
// ═══════════════════════════════════════════════════════════════════

std::optional<NodeId> OptFunctionLowerer::lower_expr(const hir::Expr &expr) {
  if (!is_reachable()) {
    return std::nullopt;
  }
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);

  auto result = std::visit(
      Overloaded{
          [&](const hir::Literal &n) { return lower_literal(n, info); },
          [&](const hir::Variable &n) { return lower_variable(n, info); },
          [&](const hir::BinaryOp &n) { return lower_binary_op(n, info); },
          [&](const hir::UnaryOp &n) { return lower_unary_op(n, info); },
          [&](const hir::Cast &n) { return lower_cast(n, info); },
          [&](const hir::Assignment &n) { return lower_assignment(n, info); },
          [&](const hir::Call &n) { return lower_call(n, info); },
          [&](const hir::MethodCall &n) { return lower_method_call(n, info); },
          [&](const hir::ConstUse &n) { return lower_const_use(n, info); },
          [&](const hir::EnumVariant &n) {
            return lower_enum_variant(n, info);
          },
          [&](const hir::StructConst &n) {
            return lower_struct_const(n, info);
          },
          [&](const hir::Block &n) { return lower_block_expr(n, info.type); },
          [&](const hir::If &n) { return lower_if_expr(n, info); },
          [&](const hir::Loop &n) { return lower_loop_expr(n, info); },
          [&](const hir::While &n) { return lower_while_expr(n, info); },
          [&](const hir::Break &n) { return lower_break_expr(n); },
          [&](const hir::Continue &n) { return lower_continue_expr(n); },
          [&](const hir::Return &n) { return lower_return_expr(n); },
          [&](const hir::FuncUse &) -> std::optional<NodeId> {
            throw std::logic_error(
                "Function-as-value not yet implemented in opt MIR");
          },
          [&](const hir::FieldAccess &n) {
            return lower_field_access(n, info);
          },
          [&](const hir::Index &n) { return lower_index(n, info); },
          [&](const hir::StructLiteral &n) {
            return lower_struct_literal(n, info);
          },
          [&](const hir::ArrayLiteral &n) {
            return lower_array_literal(n, info);
          },
          [&](const hir::ArrayRepeat &n) {
            return lower_array_repeat(n, info);
          },
          [&](const hir::UnresolvedIdentifier &) -> std::optional<NodeId> {
            throw std::logic_error("UnresolvedIdentifier in opt MIR lowering");
          },
          [&](const hir::TypeStatic &) -> std::optional<NodeId> {
            throw std::logic_error("TypeStatic in opt MIR lowering");
          },
          [&](const hir::Underscore &) -> std::optional<NodeId> {
            throw std::logic_error("Underscore in opt MIR lowering");
          },
      },
      expr.value);

  return result;
}

NodeId OptFunctionLowerer::lower_expr_value(const hir::Expr &expr) {
  auto result = lower_expr(expr);
  if (!result) {
    if (!is_reachable()) {
      return invalid_node;
    }
    throw std::logic_error(
        "Expression must produce a value in opt MIR lowering");
  }
  return *result;
}

// ═══════════════════════════════════════════════════════════════════
// Scalar expressions
// ═══════════════════════════════════════════════════════════════════

std::optional<NodeId>
OptFunctionLowerer::lower_literal(const hir::Literal &lit,
                                  const semantic::ExprInfo &info) {
  // Use old MIR const lowering to get the constant value, then convert
  ::mir::Constant mc = ::mir::detail::lower_literal(lit, info.type);

  ConstantValue cv;
  std::visit(Overloaded{
                 [&](const ::mir::BoolConstant &b) {
                   cv.kind = ConstantValue::Kind::Bool;
                   cv.bits = b.value ? 1ULL : 0ULL;
                 },
                 [&](const ::mir::IntConstant &i) {
                   cv.kind = ConstantValue::Kind::Int;
                   cv.bits = i.value;
                   cv.is_signed = i.is_signed;
                 },
                 [&](const ::mir::CharConstant &c) {
                   cv.kind = ConstantValue::Kind::Char;
                   cv.bits = static_cast<std::uint64_t>(c.value);
                 },
                 [&](const ::mir::StringConstant &) {
                   throw std::logic_error(
                       "String constants not yet supported in opt MIR");
                 },
             },
             mc.value);

  return builder_.make_constant(cv, info.type);
}

std::optional<NodeId>
OptFunctionLowerer::lower_variable(const hir::Variable &var,
                                   const semantic::ExprInfo &info) {
  SlotId slot = require_slot(var.local_id);
  return builder_.make_load(current_token_, slot, info.type);
}

std::optional<NodeId>
OptFunctionLowerer::lower_const_use(const hir::ConstUse &cu,
                                    const semantic::ExprInfo &info) {
  if (!cu.def) {
    throw std::logic_error("ConstUse missing definition in opt MIR lowering");
  }
  TypeId type = info.type;
  if (type == invalid_type_id && cu.def->type) {
    type = hir::helper::get_resolved_type(*cu.def->type);
  }
  ::mir::Constant mc = ::mir::detail::lower_const_definition(*cu.def, type);

  ConstantValue cv;
  std::visit(Overloaded{
                 [&](const ::mir::BoolConstant &b) {
                   cv.kind = ConstantValue::Kind::Bool;
                   cv.bits = b.value ? 1ULL : 0ULL;
                 },
                 [&](const ::mir::IntConstant &i) {
                   cv.kind = ConstantValue::Kind::Int;
                   cv.bits = i.value;
                   cv.is_signed = i.is_signed;
                 },
                 [&](const ::mir::CharConstant &c) {
                   cv.kind = ConstantValue::Kind::Char;
                   cv.bits = static_cast<std::uint64_t>(c.value);
                 },
                 [&](const auto &) {
                   throw std::logic_error(
                       "Unsupported constant kind in opt MIR lowering");
                 },
             },
             mc.value);

  return builder_.make_constant(cv, type);
}

std::optional<NodeId>
OptFunctionLowerer::lower_enum_variant(const hir::EnumVariant &ev,
                                       const semantic::ExprInfo &info) {
  TypeId type = info.type;
  if (type == invalid_type_id) {
    throw std::logic_error("Enum variant missing type in opt MIR lowering");
  }
  ::mir::Constant mc = ::mir::detail::lower_enum_variant(ev, type);

  ConstantValue cv;
  cv.kind = ConstantValue::Kind::Int;
  auto &ic = std::get<::mir::IntConstant>(mc.value);
  cv.bits = ic.value;
  cv.is_signed = ic.is_signed;
  return builder_.make_constant(cv, type);
}

std::optional<NodeId>
OptFunctionLowerer::lower_struct_const(const hir::StructConst &sc,
                                       const semantic::ExprInfo &info) {
  if (!sc.assoc_const) {
    throw std::logic_error("StructConst missing definition in opt MIR");
  }
  TypeId type = info.type;
  if (type == invalid_type_id && sc.assoc_const->type) {
    type = hir::helper::get_resolved_type(*sc.assoc_const->type);
  }
  ::mir::Constant mc =
      ::mir::detail::lower_const_definition(*sc.assoc_const, type);

  ConstantValue cv;
  std::visit(Overloaded{
                 [&](const ::mir::BoolConstant &b) {
                   cv.kind = ConstantValue::Kind::Bool;
                   cv.bits = b.value ? 1ULL : 0ULL;
                 },
                 [&](const ::mir::IntConstant &i) {
                   cv.kind = ConstantValue::Kind::Int;
                   cv.bits = i.value;
                   cv.is_signed = i.is_signed;
                 },
                 [&](const ::mir::CharConstant &c) {
                   cv.kind = ConstantValue::Kind::Char;
                   cv.bits = static_cast<std::uint64_t>(c.value);
                 },
                 [&](const auto &) {
                   throw std::logic_error(
                       "Unsupported constant kind in opt MIR lowering");
                 },
             },
             mc.value);

  return builder_.make_constant(cv, type);
}

// ═══════════════════════════════════════════════════════════════════
// Binary / Unary / Cast
// ═══════════════════════════════════════════════════════════════════

std::optional<NodeId>
OptFunctionLowerer::lower_binary_op(const hir::BinaryOp &binary,
                                    const semantic::ExprInfo &info) {
  // Short-circuit operators are handled separately
  if (std::get_if<hir::LogicalAnd>(&binary.op)) {
    return lower_short_circuit(binary, info, /*is_and=*/true);
  }
  if (std::get_if<hir::LogicalOr>(&binary.op)) {
    return lower_short_circuit(binary, info, /*is_and=*/false);
  }

  if (!binary.lhs || !binary.rhs) {
    throw std::logic_error("Binary op missing operand in opt MIR lowering");
  }

  semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*binary.lhs);
  semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*binary.rhs);

  NodeId lhs = lower_expr_value(*binary.lhs);
  NodeId rhs = lower_expr_value(*binary.rhs);

  auto kind =
      classify_binary_op(binary, lhs_info.type, rhs_info.type, info.type);
  return builder_.make_binary(kind, lhs, rhs, info.type);
}

std::optional<NodeId>
OptFunctionLowerer::lower_unary_op(const hir::UnaryOp &unary,
                                   const semantic::ExprInfo &info) {
  if (!unary.rhs) {
    throw std::logic_error("Unary op missing operand in opt MIR lowering");
  }
  return std::visit(
      Overloaded{
          [&](const hir::UnaryNot &) -> std::optional<NodeId> {
            NodeId operand = lower_expr_value(*unary.rhs);
            return builder_.make_unary(UnaryOpNode::Kind::Not, operand,
                                       info.type);
          },
          [&](const hir::UnaryNegate &) -> std::optional<NodeId> {
            NodeId operand = lower_expr_value(*unary.rhs);
            return builder_.make_unary(UnaryOpNode::Kind::Neg, operand,
                                       info.type);
          },
          [&](const hir::Reference &ref) -> std::optional<NodeId> {
            Place p = lower_expr_place(*unary.rhs);
            return builder_.make_address_of(
                std::move(p),
                ref.is_mutable ? Mutability::Mutable : Mutability::Immutable,
                info.type);
          },
          [&](const hir::Dereference &) -> std::optional<NodeId> {
            NodeId ptr = lower_expr_value(*unary.rhs);
            return builder_.make_load(current_token_, Place::from_ptr(ptr),
                                      info.type);
          },
      },
      unary.op);
}

std::optional<NodeId>
OptFunctionLowerer::lower_cast(const hir::Cast &cast,
                               const semantic::ExprInfo &info) {
  if (!cast.expr) {
    throw std::logic_error("Cast missing operand in opt MIR lowering");
  }
  NodeId operand = lower_expr_value(*cast.expr);
  return builder_.make_cast(operand, info.type);
}

// ═══════════════════════════════════════════════════════════════════
// Assignment
// ═══════════════════════════════════════════════════════════════════

std::optional<NodeId>
OptFunctionLowerer::lower_assignment(const hir::Assignment &assign,
                                     const semantic::ExprInfo &) {
  if (!assign.lhs || !assign.rhs) {
    throw std::logic_error("Assignment missing operands in opt MIR lowering");
  }

  // Underscore assignment — lower RHS for side effects only
  if (std::get_if<hir::Underscore>(&assign.lhs->value)) {
    (void)lower_expr(*assign.rhs);
    return std::nullopt;
  }

  // General assignment
  Place dest = lower_expr_place(*assign.lhs);
  NodeId value = lower_expr_value(*assign.rhs);
  if (!is_reachable())
    return std::nullopt;

  current_token_ =
      builder_.emit_store(current_block_id(), current_token_, dest, value);
  return std::nullopt;
}

// ═══════════════════════════════════════════════════════════════════
// Calls
// ═══════════════════════════════════════════════════════════════════

std::optional<NodeId>
OptFunctionLowerer::lower_call(const hir::Call &call,
                               const semantic::ExprInfo &info) {
  if (!call.callee) {
    throw std::logic_error("Call missing callee in opt MIR lowering");
  }
  const auto *func_use = std::get_if<hir::FuncUse>(&call.callee->value);
  if (!func_use || !func_use->def) {
    throw std::logic_error("Call callee not a resolved function use");
  }

  auto it = func_map_.find(func_use->def);
  if (it == func_map_.end()) {
    throw std::logic_error("Call target not registered in opt MIR lowering");
  }
  const CallTarget &target = it->second;

  // Prepare arguments
  std::vector<CallArg> args;
  args.reserve(call.args.size());

  for (const auto &arg : call.args) {
    if (!arg)
      throw std::logic_error("Call argument missing");

    semantic::ExprInfo arg_info = hir::helper::get_expr_info(*arg);
    bool is_aggregate = ::mir::detail::is_aggregate_type(arg_info.type);

    if (is_aggregate) {
      // Aggregate passed by value (ByVal)
      // Optimization: If the argument is already a place (variable, etc.) and
      // move semantics allow it, pass the slot directly?
      // For now, to be safe and simple: Always Eval to Temp, pass Temp.
      // (Optimizations can forward this later).
      // Note: lower_expr_place handles rvalues by checking spill.

      Place p = lower_expr_place(*arg);

      // If p is a simple Slot, we can pass it directly.
      // If p is a projection (@x.f), we must copy it to a contiguous temp
      // because 'ByVal' expects a SlotId (contiguous region).

      if (std::holds_alternative<SlotId>(p.base) && p.projections.empty()) {
        args.push_back(std::get<SlotId>(p.base));
      } else {
        // Copy to temp
        SlotId temp = allocate_temp_slot(arg_info.type, "<arg_copy>");
        current_token_ =
            builder_.emit_memcopy(current_block_id(), current_token_,
                                  Place::simple(temp), p, arg_info.type);
        args.push_back(temp);
      }
    } else {
      // Scalar passed by value
      args.push_back(lower_expr_value(*arg));
    }

    if (!is_reachable())
      return std::nullopt;
  }

  // Handle Return Value
  bool ret_is_aggregate = ::mir::detail::is_aggregate_type(info.type);
  std::optional<SlotId> sret_slot;
  type::TypeId ret_type = info.type;

  if (ret_is_aggregate) {
    // SRET: Allocate destination slot
    sret_slot = allocate_temp_slot(info.type, "<sret>");
  }

  TokenId t_out = builder_.emit_call(current_block_id(), current_token_, target,
                                     args, sret_slot, ret_type);
  current_token_ = t_out;

  // Result Node
  if (ret_is_aggregate) {
    // Result is finding the value in the sret slot
    return builder_.make_load(current_token_, *sret_slot, info.type);
  } else if (!::mir::detail::is_unit_type(info.type) &&
             !::mir::detail::is_never_type(info.type)) {
    // Scalar result observed by CallResultNode
    return builder_.make_call_result(current_token_, info.type);
  }

  return std::nullopt; // Unit/Different handling
}

std::optional<NodeId>
OptFunctionLowerer::lower_method_call(const hir::MethodCall &mcall,
                                      const semantic::ExprInfo &info) {
  const hir::Method *method_def = hir::helper::get_method_def(mcall);
  if (!mcall.receiver) {
    throw std::logic_error("Method call missing receiver in opt MIR lowering");
  }

  auto it = func_map_.find(method_def);
  if (it == func_map_.end()) {
    throw std::logic_error("Method target not registered in opt MIR lowering");
  }
  const CallTarget &target = it->second;

  // Prepare arguments: Receiver + Explicit Args
  std::vector<CallArg> args;
  args.reserve(1 + mcall.args.size());

  auto process_arg = [&](const hir::Expr &arg_expr) {
    semantic::ExprInfo arg_info = hir::helper::get_expr_info(arg_expr);
    bool is_aggregate = ::mir::detail::is_aggregate_type(arg_info.type);

    if (is_aggregate) {
      // Aggregate passed by value (ByVal)
      Place p = lower_expr_place(arg_expr);
      if (std::holds_alternative<SlotId>(p.base) && p.projections.empty()) {
        args.push_back(std::get<SlotId>(p.base));
      } else {
        SlotId temp = allocate_temp_slot(arg_info.type, "<arg_copy>");
        current_token_ =
            builder_.emit_memcopy(current_block_id(), current_token_,
                                  Place::simple(temp), p, arg_info.type);
        args.push_back(temp);
      }
    } else {
      // Scalar passed by value
      args.push_back(lower_expr_value(arg_expr));
    }
  };

  // 1. Receiver
  process_arg(*mcall.receiver);
  if (!is_reachable())
    return std::nullopt;

  // 2. Explicit Args
  for (const auto &arg : mcall.args) {
    if (!arg) {
      throw std::logic_error("Method call argument missing");
    }
    process_arg(*arg);
    if (!is_reachable())
      return std::nullopt;
  }

  // Handle Return Value (SRET)
  bool ret_is_aggregate = ::mir::detail::is_aggregate_type(info.type);
  std::optional<SlotId> sret_slot;
  type::TypeId ret_type = info.type;

  if (ret_is_aggregate) {
    sret_slot = allocate_temp_slot(info.type, "<sret>");
  }

  TokenId t_out = builder_.emit_call(current_block_id(), current_token_, target,
                                     args, sret_slot, ret_type);
  current_token_ = t_out;

  // Result Node
  if (ret_is_aggregate) {
    return builder_.make_load(current_token_, *sret_slot, info.type);
  } else if (!::mir::detail::is_unit_type(info.type) &&
             !::mir::detail::is_never_type(info.type)) {
    return builder_.make_call_result(current_token_, info.type);
  }

  return std::nullopt;
}

// ═══════════════════════════════════════════════════════════════════
// Control flow: if
// ═══════════════════════════════════════════════════════════════════

std::optional<NodeId>
OptFunctionLowerer::lower_if_expr(const hir::If &if_expr,
                                  const semantic::ExprInfo &info) {
  if (!if_expr.condition) {
    throw std::logic_error("If missing condition in opt MIR lowering");
  }
  NodeId cond = lower_expr_value(*if_expr.condition);
  if (!is_reachable())
    return std::nullopt;

  bool has_else = if_expr.else_expr && *if_expr.else_expr;
  bool result_needed = !::mir::detail::is_unit_type(info.type) &&
                       !::mir::detail::is_never_type(info.type);

  BlockId then_block = builder_.new_block();
  BlockId else_block = has_else ? builder_.new_block() : invalid_block;
  BlockId merge_block = builder_.new_block();

  BlockId false_target = has_else ? else_block : merge_block;

  // Create a result slot if needed
  std::optional<SlotId> result_slot;
  if (result_needed) {
    result_slot =
        builder_.new_slot(Slot::Kind::StackLocal, info.type, "<if_result>");
  }

  // Branch
  auto [t_true, t_false] = builder_.emit_branch(
      current_block_id(), current_token_, cond, then_block, false_target);

  // Incoming tokens for merge
  std::vector<std::pair<BlockId, TokenId>> merge_incoming;

  // ── THEN ──
  switch_to_block(then_block, t_true);
  auto then_val = lower_block_expr(*if_expr.then_block, info.type);
  std::optional<BlockId> then_exit_block;
  std::optional<TokenId> then_exit_token;
  if (is_reachable()) {
    if (result_needed && then_val) {
      current_token_ = builder_.emit_store(current_block_id(), current_token_,
                                           *result_slot, *then_val);
    }
    then_exit_block = current_block_id();
    then_exit_token = current_token_;
    merge_incoming.emplace_back(*then_exit_block, *then_exit_token);
    jump_to(merge_block);
  }

  // ── ELSE ──
  if (has_else) {
    switch_to_block(else_block, t_false);
    auto else_val = lower_expr(**if_expr.else_expr);
    if (is_reachable()) {
      if (result_needed && else_val) {
        current_token_ = builder_.emit_store(current_block_id(), current_token_,
                                             *result_slot, *else_val);
      }
      merge_incoming.emplace_back(current_block_id(), current_token_);
      jump_to(merge_block);
    }
  } else {
    // No else: the false branch goes directly to merge.
    // Note: the branch was emitted from the block *before* the then arm,
    // so we record that source block (which was current when we branched).
    // t_false already carries the token from the branch instruction.
    merge_incoming.emplace_back(then_block, t_false);
  }

  // ── MERGE ──
  if (merge_incoming.empty()) {
    current_block_.reset();
    return std::nullopt;
  }

  TokenId t_merged;
  if (merge_incoming.size() == 1) {
    t_merged = merge_incoming[0].second;
  } else {
    t_merged = builder_.emit_token_phi(merge_block, merge_incoming);
  }
  switch_to_block(merge_block, t_merged);

  if (result_needed && result_slot) {
    return builder_.make_load(t_merged, *result_slot, info.type);
  }
  return std::nullopt;
}

// ═══════════════════════════════════════════════════════════════════
// Control flow: loop (infinite)
// ═══════════════════════════════════════════════════════════════════

std::optional<NodeId>
OptFunctionLowerer::lower_loop_expr(const hir::Loop &loop,
                                    const semantic::ExprInfo &info) {
  BlockId header = builder_.new_block();
  BlockId exit = builder_.new_block();

  // Pre-allocate the header's merged token
  TokenId t_header = func_.alloc_token();

  // Entry edge → header
  if (is_reachable()) {
    auto entry_block = current_block_id();
    auto entry_token = current_token_;
    jump_to(header);

    auto &ctx = push_loop(&loop, header, exit, t_header, loop.break_type);
    ctx.header_incoming.emplace_back(entry_block, entry_token);
  } else {
    push_loop(&loop, header, exit, t_header, loop.break_type);
  }

  // Lower body
  switch_to_block(header, t_header);
  (void)lower_block_expr(*loop.body, ::mir::detail::get_unit_type());

  // Back-edge
  if (is_reachable()) {
    auto &ctx = find_loop(&loop);
    ctx.header_incoming.emplace_back(current_block_id(), current_token_);
    jump_to(header);
  }

  auto ctx = pop_loop(&loop);
  finalize_loop(ctx);

  // Exit block
  if (!ctx.exit_incoming.empty()) {
    TokenId t_exit;
    if (ctx.exit_incoming.size() == 1) {
      t_exit = ctx.exit_incoming[0].second;
    } else {
      t_exit = builder_.emit_token_phi(exit, ctx.exit_incoming);
    }
    switch_to_block(exit, t_exit);

    if (ctx.break_result_slot) {
      return builder_.make_load(t_exit, *ctx.break_result_slot,
                                ctx.break_type.value_or(invalid_type_id));
    }
    return std::nullopt;
  }

  // No breaks → loop never exits (diverging)
  current_block_.reset();
  return std::nullopt;
}

// ═══════════════════════════════════════════════════════════════════
// Control flow: while
// ═══════════════════════════════════════════════════════════════════

std::optional<NodeId>
OptFunctionLowerer::lower_while_expr(const hir::While &while_expr,
                                     const semantic::ExprInfo &info) {
  BlockId header = builder_.new_block();
  BlockId body_block = builder_.new_block();
  BlockId exit = builder_.new_block();

  TokenId t_header = func_.alloc_token();

  // Entry edge → header
  BlockId entry_block_id = current_block_id();
  TokenId entry_token = current_token_;
  jump_to(header);

  auto &ctx = push_loop(&while_expr, header, exit, t_header, std::nullopt);
  ctx.header_incoming.emplace_back(entry_block_id, entry_token);

  // Header: evaluate condition
  switch_to_block(header, t_header);
  NodeId cond = lower_expr_value(*while_expr.condition);
  if (!is_reachable()) {
    auto fctx = pop_loop(&while_expr);
    finalize_loop(fctx);
    return std::nullopt;
  }

  auto [t_body, t_exit] = builder_.emit_branch(
      current_block_id(), current_token_, cond, body_block, exit);

  // Condition-false goes to exit
  find_loop(&while_expr).exit_incoming.emplace_back(current_block_id(), t_exit);

  // Body
  switch_to_block(body_block, t_body);
  (void)lower_block_expr(*while_expr.body, ::mir::detail::get_unit_type());

  // Back-edge
  if (is_reachable()) {
    find_loop(&while_expr)
        .header_incoming.emplace_back(current_block_id(), current_token_);
    jump_to(header);
  }

  auto fctx = pop_loop(&while_expr);
  finalize_loop(fctx);

  // Exit
  if (!fctx.exit_incoming.empty()) {
    TokenId t_merged;
    if (fctx.exit_incoming.size() == 1) {
      t_merged = fctx.exit_incoming[0].second;
    } else {
      t_merged = builder_.emit_token_phi(exit, fctx.exit_incoming);
    }
    switch_to_block(exit, t_merged);
  } else {
    current_block_.reset();
  }
  return std::nullopt; // while always produces unit
}

// ═══════════════════════════════════════════════════════════════════
// Control flow: break / continue / return
// ═══════════════════════════════════════════════════════════════════

namespace {
const void *
get_loop_key(const std::optional<std::variant<hir::Loop *, hir::While *>> &t) {
  if (!t)
    return nullptr;
  const void *key = nullptr;
  std::visit([&](auto *ptr) { key = ptr; }, *t);
  return key;
}
} // namespace

std::optional<NodeId>
OptFunctionLowerer::lower_break_expr(const hir::Break &brk) {
  require_reachable("lower_break_expr");
  const void *key = get_loop_key(brk.target);
  if (!key)
    throw std::logic_error("Break target missing");

  auto &ctx = find_loop(key);

  if (brk.value && ctx.break_result_slot) {
    NodeId val = lower_expr_value(**brk.value);
    if (!is_reachable())
      return std::nullopt;
    current_token_ = builder_.emit_store(current_block_id(), current_token_,
                                         *ctx.break_result_slot, val);
  } else if (brk.value) {
    // Lower for side effects if break has a value but no result slot
    (void)lower_expr(**brk.value);
    if (!is_reachable())
      return std::nullopt;
  }

  ctx.exit_incoming.emplace_back(current_block_id(), current_token_);
  jump_to(ctx.exit_block);
  return std::nullopt;
}

std::optional<NodeId>
OptFunctionLowerer::lower_continue_expr(const hir::Continue &cont) {
  require_reachable("lower_continue_expr");
  const void *key = get_loop_key(cont.target);
  if (!key)
    throw std::logic_error("Continue target missing");

  auto &ctx = find_loop(key);
  ctx.header_incoming.emplace_back(current_block_id(), current_token_);
  jump_to(ctx.header_block);
  return std::nullopt;
}

std::optional<NodeId>
OptFunctionLowerer::lower_return_expr(const hir::Return &ret) {
  require_reachable("lower_return_expr");
  std::optional<NodeId> value;
  if (ret.value) {
    value = lower_expr(**ret.value);
    if (!is_reachable())
      return std::nullopt;
  }
  builder_.emit_return(current_block_id(), current_token_, value);
  current_block_.reset();
  return std::nullopt;
}

// ═══════════════════════════════════════════════════════════════════
// Short-circuit boolean: a && b  /  a || b
// ═══════════════════════════════════════════════════════════════════

std::optional<NodeId> OptFunctionLowerer::lower_short_circuit(
    const hir::BinaryOp &binary, const semantic::ExprInfo &info, bool is_and) {
  NodeId lhs = lower_expr_value(*binary.lhs);
  if (!is_reachable())
    return std::nullopt;

  BlockId rhs_block = builder_.new_block();
  BlockId short_block = builder_.new_block();
  BlockId merge_block = builder_.new_block();

  // Result slot
  SlotId result_slot =
      builder_.new_slot(Slot::Kind::StackLocal, info.type, "<short_circuit>");

  // Branch on LHS
  //   &&: if true → evaluate RHS, if false → short-circuit (false)
  //   ||: if true → short-circuit (true), if false → evaluate RHS
  BlockId true_target = is_and ? rhs_block : short_block;
  BlockId false_target = is_and ? short_block : rhs_block;

  auto [t_true, t_false] = builder_.emit_branch(
      current_block_id(), current_token_, lhs, true_target, false_target);

  std::vector<std::pair<BlockId, TokenId>> merge_incoming;

  // Short-circuit path: store the short-circuit value
  {
    TokenId t_short = is_and ? t_false : t_true;
    switch_to_block(short_block, t_short);
    NodeId short_val = make_const_bool(!is_and); // &&→false, ||→true
    current_token_ =
        builder_.emit_store(short_block, t_short, result_slot, short_val);
    merge_incoming.emplace_back(short_block, current_token_);
    jump_to(merge_block);
  }

  // RHS path: evaluate RHS and store result
  {
    TokenId t_rhs = is_and ? t_true : t_false;
    switch_to_block(rhs_block, t_rhs);
    NodeId rhs = lower_expr_value(*binary.rhs);
    if (is_reachable()) {
      current_token_ = builder_.emit_store(current_block_id(), current_token_,
                                           result_slot, rhs);
      merge_incoming.emplace_back(current_block_id(), current_token_);
      jump_to(merge_block);
    }
  }

  if (merge_incoming.empty()) {
    current_block_.reset();
    return std::nullopt;
  }

  TokenId t_merged;
  if (merge_incoming.size() == 1) {
    t_merged = merge_incoming[0].second;
  } else {
    t_merged = builder_.emit_token_phi(merge_block, merge_incoming);
  }
  switch_to_block(merge_block, t_merged);
  return builder_.make_load(t_merged, result_slot, info.type);
}

// ═══════════════════════════════════════════════════════════════════
// Binary-op classification (maps HIR operator → opt MIR BinaryOpNode::Kind)
// ═══════════════════════════════════════════════════════════════════

BinaryOpNode::Kind OptFunctionLowerer::classify_binary_op(
    const hir::BinaryOp &binary, type::TypeId lhs_type, type::TypeId rhs_type,
    type::TypeId result_type) {
  // Delegate to old MIR classifier and convert the result.
  // Both enums have identical value sequences.
  ::mir::BinaryOpRValue::Kind old_kind = ::mir::detail::classify_binary_kind(
      binary, lhs_type, rhs_type, result_type);
  return static_cast<BinaryOpNode::Kind>(static_cast<int>(old_kind));
}

std::optional<NodeId>
OptFunctionLowerer::lower_field_access(const hir::FieldAccess &fa,
                                       const semantic::ExprInfo &info) {
  // Syntactic cleanup: if base is a Place, project it. Only load at the end.
  // We can reuse lower_expr_place logic but we need to reconstruct the
  // expression wrapper which is awkward. Better to just build the place
  // manually here to save a temp copy.
  Place p = lower_expr_place(*fa.base);
  size_t index = hir::helper::get_field_index(fa);
  p.projections.push_back(FieldProjection{index});
  return builder_.make_load(current_token_, std::move(p), info.type);
}

std::optional<NodeId>
OptFunctionLowerer::lower_index(const hir::Index &idx,
                                const semantic::ExprInfo &info) {
  Place p = lower_expr_place(*idx.base);
  NodeId index_val = lower_expr_value(*idx.index);
  p.projections.push_back(IndexProjection{index_val});
  return builder_.make_load(current_token_, std::move(p), info.type);
}

std::optional<NodeId>
OptFunctionLowerer::lower_struct_literal(const hir::StructLiteral &sl,
                                         const semantic::ExprInfo &info) {
  // Allocate temp slot for the result
  SlotId temp = allocate_temp_slot(info.type, "<struct_lit>");

  // HIR StructLiteral fields are stored in a variant (Syntactic or Canonical).
  // Semantic analysis normalizes to CanonicalFields.
  if (auto *canonical =
          std::get_if<hir::StructLiteral::CanonicalFields>(&sl.fields)) {
    size_t index = 0;
    for (const auto &expr_ptr : canonical->initializers) {
      if (!expr_ptr)
        continue;
      NodeId val = lower_expr_value(*expr_ptr);
      if (!is_reachable())
        return std::nullopt;

      Place field_place = Place::simple(temp);
      field_place.projections.push_back(FieldProjection{index});
      current_token_ = builder_.emit_store(current_block_id(), current_token_,
                                           field_place, val);
      index++;
    }
  } else {
    // Should not happen if semantic analysis pass ran
    throw std::logic_error("Syntactic struct fields found in active MIR "
                           "lowering (expected Canonical)");
  }

  // Struct update syntax (..base) is handled during desugaring/normalization
  // generally, so if it persists here, it might be an issue.
  // However, hir::StructLiteral does NOT have a base field in the definition I
  // read. So we ignore it. The canonical initializers should cover all fields.

  return builder_.make_load(current_token_, temp, info.type);
}

std::optional<NodeId>
OptFunctionLowerer::lower_array_literal(const hir::ArrayLiteral &al,
                                        const semantic::ExprInfo &info) {
  SlotId temp = allocate_temp_slot(info.type, "<array_lit>");
  type::TypeId usize_type =
      type::get_typeID(type::Type{type::PrimitiveKind::USIZE});

  for (size_t i = 0; i < al.elements.size(); ++i) {
    NodeId val = lower_expr_value(*al.elements[i]);
    if (!is_reachable())
      return std::nullopt;

    Place elem_place = Place::simple(temp);
    elem_place.projections.push_back(
        IndexProjection{make_const_int(i, usize_type)});

    current_token_ = builder_.emit_store(current_block_id(), current_token_,
                                         elem_place, val);
  }
  return builder_.make_load(current_token_, temp, info.type);
}

std::optional<NodeId>
OptFunctionLowerer::lower_array_repeat(const hir::ArrayRepeat &ar,
                                       const semantic::ExprInfo &info) {
  SlotId temp = allocate_temp_slot(info.type, "<array_repeat>");

  // Evaluate element once
  NodeId val = lower_expr_value(*ar.value);

  // Get count from type
  size_t count = 0;
  bool found_size = false;
  // type::Types are wrappers, we need to inspect the inner value
  const type::Type &ty = type::get_type_from_id(info.type);
  if (const auto *arr_ty = std::get_if<type::ArrayType>(&ty.value)) {
    count = arr_ty->size;
    found_size = true;
  }

  if (!found_size) {
    throw std::logic_error("ArrayRepeat Result type is not ArrayType");
  }

  type::TypeId usize_type =
      type::get_typeID(type::Type{type::PrimitiveKind::USIZE});

  // Unroll loop for Phase 2
  for (size_t i = 0; i < count; ++i) {
    Place elem_place = Place::simple(temp);
    elem_place.projections.push_back(
        IndexProjection{make_const_int(i, usize_type)});
    builder_.emit_store(current_block_id(), current_token_, elem_place, val);
  }

  return builder_.make_load(current_token_, temp, info.type);
}

// ═══════════════════════════════════════════════════════════════════
// Place lowering
// ═══════════════════════════════════════════════════════════════════

Place OptFunctionLowerer::lower_expr_place(const hir::Expr &expr) {
  // 1. Recursive handling of place-expressions
  if (const auto *var = std::get_if<hir::Variable>(&expr.value)) {
    return Place::simple(require_slot(var->local_id));
  }
  if (const auto *fa = std::get_if<hir::FieldAccess>(&expr.value)) {
    Place p = lower_expr_place(*fa->base);
    p.projections.push_back(FieldProjection{hir::helper::get_field_index(*fa)});
    return p;
  }
  if (const auto *idx = std::get_if<hir::Index>(&expr.value)) {
    Place p = lower_expr_place(*idx->base);
    NodeId index_val = lower_expr_value(*idx->index);
    p.projections.push_back(IndexProjection{index_val});
    return p;
  }
  if (const auto *unary = std::get_if<hir::UnaryOp>(&expr.value)) {
    if (std::get_if<hir::Dereference>(&unary->op)) {
      NodeId ptr = lower_expr_value(*unary->rhs);
      return Place::from_ptr(ptr);
    }
  }

  // 2. R-value spilling (e.g. &Call(), &StructLiteral())
  //    Create a temp slot, evaluate the expression into it, return the slot.
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);
  NodeId value = lower_expr_value(expr);
  SlotId temp = allocate_temp_slot(info.type, "<rvalue_temp>");
  current_token_ =
      builder_.emit_store(current_block_id(), current_token_, temp, value);
  return Place::simple(temp);
}

} // namespace opt::mir
