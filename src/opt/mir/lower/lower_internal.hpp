#pragma once

#include "opt/mir/builder.hpp"
#include "opt/mir/nodes.hpp"
#include "opt/mir/opt_mir.hpp"

#include "semantic/hir/hir.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace opt::mir {

/// OptFunctionLowerer — translates a single HIR function/method into an
/// OptFunction (the optimization MIR representation).
///
/// Key differences from old MIR's FunctionLowerer:
///   - Values are NodeId (floating nodes), not TempId
///   - Side-effect ordering is explicit via TokenId chains
///   - Loads are floating nodes tethered to tokens
///   - Locals map to SlotId (StackLocal slots)
///   - Value merging at control flow joins uses slot store/load through token
///   phi
class OptFunctionLowerer {
public:
  enum class FunctionKind { Function, Method };

  OptFunctionLowerer(
      const hir::Function &function,
      const std::unordered_map<const void *, CallTarget> &func_map,
      std::string name);

  OptFunctionLowerer(
      const hir::Method &method,
      const std::unordered_map<const void *, CallTarget> &func_map,
      std::string name);

  OptFunction lower();

private:
  // ─── Loop management ──────────────────────────────────────────────
  struct LoopContext {
    BlockId header_block = invalid_block;
    BlockId exit_block = invalid_block;
    TokenId header_phi_token = invalid_token; // pre-allocated merge token
    std::optional<type::TypeId> break_type;
    std::optional<SlotId> break_result_slot;

    // Tokens flowing into the header block (entry + back-edges + continues)
    std::vector<std::pair<BlockId, TokenId>> header_incoming;
    // Tokens flowing into the exit block (condition-false + breaks)
    std::vector<std::pair<BlockId, TokenId>> exit_incoming;
  };

  // ─── State ────────────────────────────────────────────────────────
  FunctionKind function_kind_ = FunctionKind::Function;
  const hir::Function *hir_function_ = nullptr;
  const hir::Method *hir_method_ = nullptr;
  const std::unordered_map<const void *, CallTarget> &func_map_;

  OptFunction func_;
  Builder builder_{func_};

  TokenId current_token_ = invalid_token;
  std::optional<BlockId> current_block_;

  std::unordered_map<const hir::Local *, SlotId> local_slots_;
  std::vector<std::pair<const void *, LoopContext>> loop_stack_;

  // ─── Initialisation ───────────────────────────────────────────────
  void initialize(std::string name);
  void register_locals();
  SlotId register_local(const hir::Local *local);
  const hir::Block *get_body() const;
  const std::vector<std::unique_ptr<hir::Local>> &get_locals() const;

  // ─── Block / Statement lowering ───────────────────────────────────
  void lower_block(const hir::Block &block);
  std::optional<NodeId> lower_block_expr(const hir::Block &block,
                                         type::TypeId expected_type);

  // New methods for Phase 2
  // New methods for Phase 2
  Place lower_expr_place(const hir::Expr &expr);
  void lower_expr_to_place(const hir::Expr &expr, Place place);

  std::optional<NodeId> lower_field_access(const hir::FieldAccess &fa,
                                           const semantic::ExprInfo &info);
  std::optional<NodeId> lower_index(const hir::Index &idx,
                                    const semantic::ExprInfo &info);
  std::optional<NodeId> lower_struct_literal(const hir::StructLiteral &sl,
                                             const semantic::ExprInfo &info);
  std::optional<NodeId> lower_array_literal(const hir::ArrayLiteral &al,
                                            const semantic::ExprInfo &info);
  std::optional<NodeId> lower_array_repeat(const hir::ArrayRepeat &ar,
                                           const semantic::ExprInfo &info);

  // Helpers
  SlotId allocate_temp_slot(type::TypeId type, std::string name = {});
  void emit_aggregate_copy(SlotId dest, SlotId src, type::TypeId type);
  bool lower_block_statements(const hir::Block &block);
  void lower_statement(const hir::Stmt &stmt);
  void lower_let_stmt(const hir::LetStmt &let_stmt);

  // ─── Expression lowering (implemented in lower_expr.cpp) ──────────
  std::optional<NodeId> lower_expr(const hir::Expr &expr);
  NodeId lower_expr_value(const hir::Expr &expr);

  // Individual expression dispatch
  std::optional<NodeId> lower_literal(const hir::Literal &lit,
                                      const semantic::ExprInfo &info);
  std::optional<NodeId> lower_variable(const hir::Variable &var,
                                       const semantic::ExprInfo &info);
  std::optional<NodeId> lower_binary_op(const hir::BinaryOp &bin,
                                        const semantic::ExprInfo &info);
  std::optional<NodeId> lower_unary_op(const hir::UnaryOp &unary,
                                       const semantic::ExprInfo &info);
  std::optional<NodeId> lower_cast(const hir::Cast &cast,
                                   const semantic::ExprInfo &info);
  std::optional<NodeId> lower_assignment(const hir::Assignment &assign,
                                         const semantic::ExprInfo &info);
  std::optional<NodeId> lower_call(const hir::Call &call,
                                   const semantic::ExprInfo &info);
  std::optional<NodeId> lower_method_call(const hir::MethodCall &mcall,
                                          const semantic::ExprInfo &info);
  std::optional<NodeId> lower_const_use(const hir::ConstUse &cu,
                                        const semantic::ExprInfo &info);
  std::optional<NodeId> lower_enum_variant(const hir::EnumVariant &ev,
                                           const semantic::ExprInfo &info);
  std::optional<NodeId> lower_struct_const(const hir::StructConst &sc,
                                           const semantic::ExprInfo &info);

  // Control-flow expressions
  std::optional<NodeId> lower_if_expr(const hir::If &if_expr,
                                      const semantic::ExprInfo &info);
  std::optional<NodeId> lower_loop_expr(const hir::Loop &loop,
                                        const semantic::ExprInfo &info);
  std::optional<NodeId> lower_while_expr(const hir::While &while_expr,
                                         const semantic::ExprInfo &info);
  std::optional<NodeId> lower_break_expr(const hir::Break &brk);
  std::optional<NodeId> lower_continue_expr(const hir::Continue &cont);
  std::optional<NodeId> lower_return_expr(const hir::Return &ret);
  std::optional<NodeId> lower_short_circuit(const hir::BinaryOp &binary,
                                            const semantic::ExprInfo &info,
                                            bool is_and);

  // ─── Binary-op classification ─────────────────────────────────────
  static BinaryOpNode::Kind classify_binary_op(const hir::BinaryOp &binary,
                                               type::TypeId lhs_type,
                                               type::TypeId rhs_type,
                                               type::TypeId result_type);

  // ─── Constant helpers ─────────────────────────────────────────────
  NodeId make_const_int(std::uint64_t value, type::TypeId type,
                        bool is_signed = false);
  NodeId make_const_bool(bool value);

  // ─── Loop context helpers ─────────────────────────────────────────
  LoopContext &push_loop(const void *key, BlockId header, BlockId exit,
                         TokenId header_phi_token,
                         std::optional<type::TypeId> break_type);
  LoopContext &find_loop(const void *key);
  LoopContext pop_loop(const void *key);
  void finalize_loop(const LoopContext &ctx);

  // ─── Utilities ────────────────────────────────────────────────────
  bool is_reachable() const;
  void require_reachable(const char *ctx) const;
  BlockId current_block_id() const;
  void switch_to_block(BlockId block, TokenId token);
  void jump_to(BlockId target);
  SlotId require_slot(const hir::Local *local) const;
};

} // namespace opt::mir
