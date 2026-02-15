#pragma once

#include "opt/mir/node_id.hpp"
#include "opt/mir/nodes.hpp"
#include "opt/mir/opt_mir.hpp"
#include "opt/mir/slot.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace opt::mir {

/// Builder — convenience API for constructing an OptFunction graph.
///
/// Usage:
///   OptFunction func;
///   Builder b(func);
///   auto entry = b.new_block();
///   func.entry_block = entry;
///   auto t0 = b.entry_token();
///   auto c  = b.make_constant({ConstantValue::Kind::Int, 42}, i32_type);
///   auto ld = b.make_load(t0, Place::simple(some_slot), i32_type);
///   auto sum = b.make_binary(BinaryOpNode::Kind::IAdd, c, ld, i32_type);
///   auto t1 = b.emit_store(entry, t0, Place::simple(some_slot), sum);
///   b.emit_return(entry, t1, sum);
class Builder {
public:
  explicit Builder(OptFunction &func) : func_(func) {}

  // ── Floating node creation ──────────────────────────────────────────

  NodeId make_constant(ConstantValue val, type::TypeId ty) {
    return func_.alloc_node(Node{ConstantNode{std::move(val)}, ty});
  }

  NodeId make_binary(BinaryOpNode::Kind op, NodeId lhs, NodeId rhs,
                     type::TypeId ty) {
    return func_.alloc_node(Node{BinaryOpNode{op, lhs, rhs}, ty});
  }

  NodeId make_unary(UnaryOpNode::Kind op, NodeId operand, type::TypeId ty) {
    return func_.alloc_node(Node{UnaryOpNode{op, operand}, ty});
  }

  NodeId make_load(TokenId token, Place place, type::TypeId ty) {
    return func_.alloc_node(Node{LoadNode{token, std::move(place)}, ty});
  }

  /// Convenience: load from a simple slot (no projections).
  NodeId make_load(TokenId token, SlotId slot, type::TypeId ty) {
    return make_load(token, Place::simple(slot), ty);
  }

  NodeId make_cast(NodeId operand, type::TypeId target_type) {
    return func_.alloc_node(Node{CastNode{operand, target_type}, target_type});
  }

  NodeId make_address_of(Place place, Mutability mut, type::TypeId ptr_ty) {
    return func_.alloc_node(Node{AddressOfNode{std::move(place), mut}, ptr_ty});
  }

  NodeId make_call_result(TokenId call_token, type::TypeId type) {
    return func_.alloc_node(Node{CallResultNode{call_token}, type});
  }

  // ── Pinned instruction emission ─────────────────────────────────────

  /// Emit a Store and return the output token.
  TokenId emit_store(BlockId block, TokenId t_in, Place place, NodeId value) {
    auto t_out = func_.alloc_token();
    auto &bb = func_.get_block_mut(block);
    bb.instructions.push_back(
        PinnedInst{StoreInst{t_in, std::move(place), value, t_out}});
    return t_out;
  }

  /// Convenience: store to a simple slot (no projections).
  TokenId emit_store(BlockId block, TokenId t_in, SlotId slot, NodeId value) {
    return emit_store(block, t_in, Place::simple(slot), value);
  }

  /// Emit a Memcopy between places.
  TokenId emit_memcopy(BlockId block, TokenId t_in, Place dest, Place src,
                       type::TypeId type) {
    auto t_out = func_.alloc_token();
    auto &bb = func_.get_block_mut(block);
    bb.instructions.push_back(PinnedInst{
        MemcopyInst{t_in, std::move(dest), std::move(src), type, t_out}});
    return t_out;
  }

  /// Emit a Branch. Returns (t_true, t_false). Also wires CFG edges.
  std::pair<TokenId, TokenId> emit_branch(BlockId block, TokenId t_in,
                                          NodeId cond, BlockId bb_true,
                                          BlockId bb_false) {
    auto t_true = func_.alloc_token();
    auto t_false = func_.alloc_token();

    auto &bb = func_.get_block_mut(block);
    bb.instructions.push_back(
        PinnedInst{BranchInst{t_in, cond, t_true, t_false, bb_true, bb_false}});

    // Wire CFG edges
    add_edge(block, bb_true);
    add_edge(block, bb_false);

    return {t_true, t_false};
  }

  /// Emit an unconditional Jump. Also wires CFG edge.
  void emit_jump(BlockId block, TokenId t_in, BlockId target) {
    auto &bb = func_.get_block_mut(block);
    bb.instructions.push_back(PinnedInst{JumpInst{t_in, target}});
    add_edge(block, target);
  }

  /// Emit a TokenPhi merge and return the merged token.
  TokenId emit_token_phi(BlockId block,
                         std::vector<std::pair<BlockId, TokenId>> incoming) {
    auto t_out = func_.alloc_token();
    std::vector<TokenPhiIncoming> entries;
    entries.reserve(incoming.size());
    for (auto &[bid, tid] : incoming) {
      entries.push_back(TokenPhiIncoming{bid, tid});
    }
    auto &bb = func_.get_block_mut(block);
    bb.instructions.push_back(
        PinnedInst{TokenPhiInst{std::move(entries), t_out}});
    return t_out;
  }

  /// Emit a Return instruction.
  void emit_return(BlockId block, TokenId t_in,
                   std::optional<NodeId> value = {}) {
    auto &bb = func_.get_block_mut(block);
    bb.instructions.push_back(PinnedInst{ReturnInst{t_in, value}});
  }

  /// Emit a Call instruction. Returns t_out.
  TokenId emit_call(BlockId block, TokenId t_in, CallTarget target,
                    std::vector<CallArg> args,
                    std::optional<SlotId> sret_slot = {},
                    type::TypeId result_type = type::invalid_type_id) {
    auto t_out = func_.alloc_token();
    auto &bb = func_.get_block_mut(block);
    bb.instructions.push_back(
        PinnedInst{CallInst{t_in, std::move(target), std::move(args), t_out,
                            sret_slot, result_type}});
    return t_out;
  }

  // ── Block management ────────────────────────────────────────────────

  BlockId new_block() { return func_.alloc_block(); }

  // ── Slot management ─────────────────────────────────────────────────

  SlotId new_slot(Slot::Kind kind, type::TypeId ty, std::string name = {},
                  Mutability mut = Mutability::Immutable) {
    return func_.alloc_slot(Slot{kind, ty, std::move(name), mut});
  }

  // ── Token management ────────────────────────────────────────────────

  /// Get (or allocate) the initial entry token for the function.
  TokenId entry_token() {
    if (entry_token_ == invalid_token) {
      entry_token_ = func_.alloc_token();
    }
    return entry_token_;
  }

  TokenId new_token() { return func_.alloc_token(); }

private:
  OptFunction &func_;
  TokenId entry_token_ = invalid_token;

  void add_edge(BlockId from, BlockId to) {
    auto &src = func_.get_block_mut(from);
    auto &dst = func_.get_block_mut(to);
    src.successors.push_back(to);
    dst.predecessors.push_back(from);
  }
};

} // namespace opt::mir
