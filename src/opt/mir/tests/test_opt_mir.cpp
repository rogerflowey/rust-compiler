#include <catch2/catch_test_macros.hpp>

#include "opt/mir/ir/basic_block.hpp"
#include "opt/mir/tools/builder.hpp"
#include "opt/mir/passes/const_fold.hpp"
#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/ir/node_id.hpp"
#include "opt/mir/ir/nodes.hpp"
#include "opt/mir/ir/module.hpp"
#include "opt/mir/tools/printer.hpp"
#include "opt/mir/ir/slot.hpp"
#include "opt/mir/passes/updater.hpp"
#include "opt/mir/analysis/use_list.hpp"
#include "opt/mir/analysis/world_state.hpp"

#include <deque>
#include <sstream>
#include <string>
#include <variant>

using namespace opt::mir;

// Helper: fake type id (we don't depend on a real TypeContext in these tests)
static constexpr type::TypeId i32_type = type::TypeId{0};
static constexpr type::TypeId bool_type = type::TypeId{1};

// ============================================================================
// ID Types
// ============================================================================

TEST_CASE("NodeId, TokenId, SlotId, BlockId are distinct types",
          "[opt_mir][ids]") {
  auto n = NodeId{0};
  auto t = TokenId{0};
  auto s = SlotId{0};
  auto b = BlockId{0};

  REQUIRE(raw(n) == 0);
  REQUIRE(raw(t) == 0);
  REQUIRE(raw(s) == 0);
  REQUIRE(raw(b) == 0);

  // Sentinels
  REQUIRE(raw(invalid_node) == UINT32_MAX);
  REQUIRE(raw(invalid_token) == UINT32_MAX);
  REQUIRE(raw(invalid_slot) == UINT32_MAX);
  REQUIRE(raw(invalid_block) == UINT32_MAX);
}

// ============================================================================
// Arena allocation
// ============================================================================

TEST_CASE("OptFunction alloc_node returns sequential IDs", "[opt_mir][arena]") {
  OptFunction func;
  func.name = "test";

  auto id0 = func.alloc_node(
      Node{ConstantNode{{ConstantValue::Kind::Int, 10}}, i32_type});
  auto id1 = func.alloc_node(
      Node{ConstantNode{{ConstantValue::Kind::Int, 20}}, i32_type});
  auto id2 = func.alloc_node(
      Node{ConstantNode{{ConstantValue::Kind::Int, 30}}, i32_type});

  REQUIRE(raw(id0) == 0);
  REQUIRE(raw(id1) == 1);
  REQUIRE(raw(id2) == 2);
  REQUIRE(func.nodes.size() == 3);
}

TEST_CASE("Token allocation increments", "[opt_mir][token]") {
  OptFunction func;

  auto t0 = func.alloc_token();
  auto t1 = func.alloc_token();
  auto t2 = func.alloc_token();

  REQUIRE(raw(t0) == 0);
  REQUIRE(raw(t1) == 1);
  REQUIRE(raw(t2) == 2);
}

TEST_CASE("Slot allocation stores metadata", "[opt_mir][slot]") {
  OptFunction func;

  auto s0 = func.alloc_slot(Slot{Slot::Kind::StackLocal, i32_type, "x"});
  auto s1 = func.alloc_slot(Slot{Slot::Kind::Global, bool_type, "flag"});

  REQUIRE(raw(s0) == 0);
  REQUIRE(raw(s1) == 1);
  REQUIRE(func.get_slot(s0).debug_name == "x");
  REQUIRE(func.get_slot(s1).kind == Slot::Kind::Global);
}

TEST_CASE("Block allocation creates empty blocks", "[opt_mir][block]") {
  OptFunction func;

  auto b0 = func.alloc_block();
  auto b1 = func.alloc_block();

  REQUIRE(raw(b0) == 0);
  REQUIRE(raw(b1) == 1);
  REQUIRE(func.get_block(b0).inst_ids.empty());
  REQUIRE(func.get_block(b0).id == b0);
}

// ============================================================================
// Builder: simple function (x + 42 → store → return)
// ============================================================================

TEST_CASE("Builder: simple add-store-return", "[opt_mir][builder]") {
  OptFunction func;
  func.name = "add42";
  Builder b(func);

  auto entry = b.new_block();
  func.entry_block = entry;

  auto slot_x = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");
  auto t0 = b.entry_token();

  // Load x
  auto ld = b.make_load(t0, slot_x, i32_type);

  // Constant 42
  auto c42 = b.make_constant({ConstantValue::Kind::Int, 42}, i32_type);

  // Add
  auto sum = b.make_binary(BinaryOpNode::Kind::IAdd, ld, c42, i32_type);

  // Store result back to x
  auto t1 = b.emit_store(entry, t0, slot_x, sum);

  // Return with the sum
  b.emit_return(entry, t1, sum);

  // Verify structure
  REQUIRE(func.nodes.size() == 3); // load, constant, add
  REQUIRE(func.blocks.size() == 1);
  REQUIRE(func.slots.size() == 1);

  const auto &bb = func.get_block(entry);
  REQUIRE(bb.inst_ids.size() == 2); // store, return

  // First instruction is Store
  REQUIRE(
      std::holds_alternative<StoreInst>(func.get_inst(bb.inst_ids[0]).kind));
  // Second instruction is Return
  REQUIRE(
      std::holds_alternative<ReturnInst>(func.get_inst(bb.inst_ids[1]).kind));
}

// ============================================================================
// Builder: branch + merge
// ============================================================================

TEST_CASE("Builder: branch and merge", "[opt_mir][builder]") {
  OptFunction func;
  func.name = "if_else";
  Builder b(func);

  auto bb_entry = b.new_block();
  auto bb_true = b.new_block();
  auto bb_false = b.new_block();
  auto bb_merge = b.new_block();
  func.entry_block = bb_entry;

  auto t0 = b.entry_token();
  auto cond = b.make_constant({ConstantValue::Kind::Bool, 1}, bool_type);

  // Branch
  auto [t_true, t_false] = b.emit_branch(bb_entry, t0, cond, bb_true, bb_false);

  // True block: jump to merge
  b.emit_jump(bb_true, t_true, bb_merge);

  // False block: jump to merge
  b.emit_jump(bb_false, t_false, bb_merge);

  // Merge block: phi + return
  auto t_merge =
      b.emit_token_phi(bb_merge, {{bb_true, t_true}, {bb_false, t_false}});
  b.emit_return(bb_merge, t_merge);

  // Verify CFG edges
  REQUIRE(func.get_block(bb_entry).successors.size() == 2);
  REQUIRE(func.get_block(bb_true).predecessors.size() == 1);
  REQUIRE(func.get_block(bb_false).predecessors.size() == 1);
  REQUIRE(func.get_block(bb_merge).predecessors.size() == 2);

  // Verify merge block has phi + return
  const auto &merge = func.get_block(bb_merge);
  REQUIRE(merge.inst_ids.size() == 2);
  REQUIRE(std::holds_alternative<TokenPhiInst>(
      func.get_inst(merge.inst_ids[0]).kind));
  REQUIRE(std::holds_alternative<ReturnInst>(
      func.get_inst(merge.inst_ids[1]).kind));

  // Verify the phi has 2 incoming entries
  const auto &phi =
      std::get<TokenPhiInst>(func.get_inst(merge.inst_ids[0]).kind);
  REQUIRE(phi.incoming.size() == 2);
}

// ============================================================================
// Printer
// ============================================================================

TEST_CASE("Printer output contains expected keywords", "[opt_mir][printer]") {
  OptFunction func;
  func.name = "printer_test";
  Builder b(func);

  auto entry = b.new_block();
  func.entry_block = entry;

  auto slot_x = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");
  auto t0 = b.entry_token();
  auto c1 = b.make_constant({ConstantValue::Kind::Int, 7}, i32_type);
  auto t1 = b.emit_store(entry, t0, slot_x, c1);
  b.emit_return(entry, t1, c1);

  auto text = Printer::to_string(func);

  REQUIRE(text.find("function @printer_test") != std::string::npos);
  REQUIRE(text.find("slots:") != std::string::npos);
  REQUIRE(text.find("stack_local") != std::string::npos);
  REQUIRE(text.find("\"x\"") != std::string::npos);
  REQUIRE(text.find("arena:") != std::string::npos);
  REQUIRE(text.find("Constant(7)") != std::string::npos);
  REQUIRE(text.find("Store") != std::string::npos);
  REQUIRE(text.find("Return") != std::string::npos);
  REQUIRE(text.find("[entry]") != std::string::npos);
}

// ============================================================================
// WorldSnapshot (updated to use SlotFact)
// ============================================================================

TEST_CASE("WorldSnapshot write/read round-trip", "[opt_mir][world_state]") {
  WorldSnapshot ws;

  REQUIRE(ws.empty());

  auto slot_a = SlotId{0};
  auto slot_b = SlotId{1};

  auto fact_a = SlotFact::top();
  auto fact_b = SlotFact::top();

  auto ws1 = ws.write(slot_a, fact_a);
  REQUIRE(ws1.size() == 1);
  REQUIRE(ws1.read(slot_a) == fact_a);

  // Original is unchanged (persistent)
  REQUIRE(ws.empty());

  auto ws2 = ws1.write(slot_b, fact_b);
  REQUIRE(ws2.size() == 2);
}

TEST_CASE("WorldSnapshot merge: identical snapshots",
          "[opt_mir][world_state]") {
  auto slot = SlotId{0};
  auto fact = SlotFact::top();

  auto ws = WorldSnapshot{}.write(slot, fact);
  auto merged = WorldSnapshot::merge(ws, ws);

  REQUIRE(merged.size() == 1);
  REQUIRE(merged.read(slot) == fact);
}

TEST_CASE("WorldSnapshot merge: slots in only one side are kept",
          "[opt_mir][world_state]") {
  auto slot_a = SlotId{0};
  auto slot_b = SlotId{1};
  auto fact = SlotFact::top();

  // ws_left has slot_a, ws_right has slot_b
  auto ws_left = WorldSnapshot{}.write(slot_a, fact);
  auto ws_right = WorldSnapshot{}.write(slot_b, fact);

  auto merged = WorldSnapshot::merge(ws_left, ws_right);

  // Both kept: meet(fact, Top) = fact
  REQUIRE(merged.size() == 2);
}

// ============================================================================
// ConstPropFact lattice
// ============================================================================

TEST_CASE("ConstPropFact: Top meet x = x", "[opt_mir][fact]") {
  auto top = ConstPropFact::top();
  auto c5 = ConstPropFact::constant({ConstantValue::Kind::Int, 5});
  auto bot = ConstPropFact::bottom();

  REQUIRE(ConstPropFact::meet(top, top) == top);
  REQUIRE(ConstPropFact::meet(top, c5) == c5);
  REQUIRE(ConstPropFact::meet(c5, top) == c5);
  REQUIRE(ConstPropFact::meet(top, bot) == bot);
  REQUIRE(ConstPropFact::meet(bot, top) == bot);
}

TEST_CASE("ConstPropFact: same constant meet = same constant",
          "[opt_mir][fact]") {
  auto c5a = ConstPropFact::constant({ConstantValue::Kind::Int, 5});
  auto c5b = ConstPropFact::constant({ConstantValue::Kind::Int, 5});

  REQUIRE(ConstPropFact::meet(c5a, c5b) == c5a);
  REQUIRE(ConstPropFact::meet(c5a, c5b).is_constant());
}

TEST_CASE("ConstPropFact: different constants meet = Bottom",
          "[opt_mir][fact]") {
  auto c5 = ConstPropFact::constant({ConstantValue::Kind::Int, 5});
  auto c7 = ConstPropFact::constant({ConstantValue::Kind::Int, 7});

  auto result = ConstPropFact::meet(c5, c7);
  REQUIRE(result.is_bottom());
}

TEST_CASE("ConstPropFact: Bottom meet anything = Bottom", "[opt_mir][fact]") {
  auto bot = ConstPropFact::bottom();
  auto c5 = ConstPropFact::constant({ConstantValue::Kind::Int, 5});

  REQUIRE(ConstPropFact::meet(bot, c5).is_bottom());
  REQUIRE(ConstPropFact::meet(c5, bot).is_bottom());
  REQUIRE(ConstPropFact::meet(bot, bot).is_bottom());
}

// ============================================================================
// NodeFact product lattice
// ============================================================================

TEST_CASE("NodeFact: product meet dispatches component-wise",
          "[opt_mir][fact]") {
  NodeFact a{ConstPropFact::constant({ConstantValue::Kind::Int, 5})};
  NodeFact b{ConstPropFact::constant({ConstantValue::Kind::Int, 7})};

  auto result = NodeFact::meet(a, b);
  REQUIRE(result.const_prop.is_bottom());
}

TEST_CASE("NodeFact: top meet constant = constant", "[opt_mir][fact]") {
  auto top = NodeFact::top();
  NodeFact c5{ConstPropFact::constant({ConstantValue::Kind::Int, 5})};

  auto result = NodeFact::meet(top, c5);
  REQUIRE(result.const_prop.is_constant());
  REQUIRE(result == c5);
}

// ============================================================================
// WorldSnapshot merge with lattice meet
// ============================================================================

TEST_CASE("WorldSnapshot merge: disagreeing facts produce meet, not drop",
          "[opt_mir][world_state][fact]") {
  auto slot = SlotId{0};

  // Two snapshots with same slot but different constant values
  NodeFact nf5{ConstPropFact::constant({ConstantValue::Kind::Int, 5})};
  NodeFact nf7{ConstPropFact::constant({ConstantValue::Kind::Int, 7})};

  auto ws_a = WorldSnapshot{}.write(slot, SlotFact{nf5});
  auto ws_b = WorldSnapshot{}.write(slot, SlotFact{nf7});

  auto merged = WorldSnapshot::merge(ws_a, ws_b);

  // Slot is preserved (not dropped), but its fact is Bottom
  REQUIRE(merged.size() == 1);
  REQUIRE(merged.read(slot).value_fact.const_prop.is_bottom());
}

TEST_CASE("WorldSnapshot merge: agreeing facts preserved",
          "[opt_mir][world_state][fact]") {
  auto slot = SlotId{0};

  NodeFact nf5{ConstPropFact::constant({ConstantValue::Kind::Int, 5})};

  auto ws_a = WorldSnapshot{}.write(slot, SlotFact{nf5});
  auto ws_b = WorldSnapshot{}.write(slot, SlotFact{nf5});

  auto merged = WorldSnapshot::merge(ws_a, ws_b);

  REQUIRE(merged.size() == 1);
  REQUIRE(merged.read(slot).value_fact.const_prop.is_constant());
  REQUIRE(merged.read(slot).value_fact.const_prop.value.bits == 5);
}

// ============================================================================
// UseLists
// ============================================================================

TEST_CASE("UseLists: node users tracked for BinaryOp", "[opt_mir][use_list]") {
  OptFunction func;
  func.name = "use_list_test";
  Builder b(func);

  auto entry = b.new_block();
  func.entry_block = entry;

  auto c1 = b.make_constant({ConstantValue::Kind::Int, 1}, i32_type);
  auto c2 = b.make_constant({ConstantValue::Kind::Int, 2}, i32_type);
  auto sum = b.make_binary(BinaryOpNode::Kind::IAdd, c1, c2, i32_type);

  auto t0 = b.entry_token();
  b.emit_return(entry, t0, sum);

  auto ul = UseLists::build(func);

  // c1 and c2 are both used by sum (which is a NodeUser)
  auto c1_users = ul.users_of(c1);
  REQUIRE(c1_users.size() == 1);
  REQUIRE(std::holds_alternative<NodeUser>(c1_users[0]));
  REQUIRE(std::get<NodeUser>(c1_users[0]).id == sum);

  auto c2_users = ul.users_of(c2);
  REQUIRE(c2_users.size() == 1);
  REQUIRE(std::holds_alternative<NodeUser>(c2_users[0]));
  REQUIRE(std::get<NodeUser>(c2_users[0]).id == sum);

  // sum has no users (it's returned, but return inst uses the value, wait)
  // ReturnInst uses 'sum' (NodeId).
  // So 'sum' SHOULD have a user: the ReturnInst.
  // InstUser{return_inst_id}
  auto sum_users = ul.users_of(sum);
  REQUIRE(sum_users.size() == 1);
  REQUIRE(std::holds_alternative<InstUser>(sum_users[0]));
  auto return_inst = func.get_block(entry).inst_ids.back();
  REQUIRE(std::get<InstUser>(sum_users[0]).id == return_inst);
}

TEST_CASE("UseLists: token users tracked (Node and Inst)",
          "[opt_mir][use_list]") {
  OptFunction func;
  func.name = "token_consumer_test";
  Builder b(func);

  auto entry = b.new_block();
  func.entry_block = entry;

  auto slot = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");
  auto t0 = b.entry_token();

  // LoadNode uses t0
  auto ld = b.make_load(t0, slot, i32_type);

  // StoreInst uses t0 (and produces t1)
  auto c1 = b.make_constant({ConstantValue::Kind::Int, 42}, i32_type);
  auto t1 = b.emit_store(entry, t0, slot, c1);

  // ReturnInst uses t1
  b.emit_return(entry, t1, ld);

  auto ul = UseLists::build(func);

  // 1. Check t0 users. Should be:
  // - ld (NodeUser)
  // - store (InstUser)
  auto t0_users = ul.users_of(t0);
  REQUIRE(t0_users.size() == 2);

  bool found_load = false;
  bool found_store = false;

  // Identify store instruction
  auto store_inst = func.get_block(entry).inst_ids[0]; // First inst

  for (const auto &u : t0_users) {
    if (auto *nu = std::get_if<NodeUser>(&u)) {
      if (nu->id == ld)
        found_load = true;
    } else if (auto *iu = std::get_if<InstUser>(&u)) {
      if (iu->id == store_inst)
        found_store = true;
    }
  }
  REQUIRE(found_load);
  REQUIRE(found_store);

  // 2. Check t1 users.
  // t1 is used by ReturnInst.
  auto t1_users = ul.users_of(t1);
  REQUIRE(t1_users.size() == 1);
  REQUIRE(std::holds_alternative<InstUser>(t1_users[0]));
  auto return_inst = func.get_block(entry).inst_ids.back();
  REQUIRE(std::get<InstUser>(t1_users[0]).id == return_inst);
}

TEST_CASE("UseLists: incremental updates", "[opt_mir][use_list]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;

  auto c1 = b.make_constant({ConstantValue::Kind::Int, 1}, i32_type); // Node 0
  auto c2 = b.make_constant({ConstantValue::Kind::Int, 2}, i32_type); // Node 1
  auto c3 = b.make_constant({ConstantValue::Kind::Int, 3}, i32_type); // Node 2

  // sum = c1 + c2
  auto sum =
      b.make_binary(BinaryOpNode::Kind::IAdd, c1, c2, i32_type); // Node 3

  auto ul = UseLists::build(func);

  // Initial check
  REQUIRE(ul.users_of(c1).size() == 1);
  REQUIRE(std::get<NodeUser>(ul.users_of(c1)[0]).id == sum);
  REQUIRE(ul.users_of(c2).size() == 1);
  REQUIRE(std::get<NodeUser>(ul.users_of(c2)[0]).id == sum);
  REQUIRE(ul.users_of(c3).empty());

  // Update sum to be c1 + c3
  auto &sum_node = func.nodes[raw(sum)];
  auto &bin_op = std::get<BinaryOpNode>(sum_node.kind);
  bin_op.rhs = c3;

  // Notify UseLists
  ul.notify_node_updated(sum, func);

  // Check updates
  // c1 still used by sum
  REQUIRE(ul.users_of(c1).size() == 1);
  REQUIRE(std::get<NodeUser>(ul.users_of(c1)[0]).id == sum);

  // c2 no longer used
  REQUIRE(ul.users_of(c2).empty());

  // c3 now used by sum
  REQUIRE(ul.users_of(c3).size() == 1);
  REQUIRE(std::get<NodeUser>(ul.users_of(c3)[0]).id == sum);

  // Remove sum
  ul.notify_node_removed(sum);

  // c1, c2, c3 should have no users from sum
  REQUIRE(ul.users_of(c1).empty());
  REQUIRE(ul.users_of(c3).empty());
}

// ============================================================================
// Constant Folding Helpers
// ============================================================================

TEST_CASE("ConstFold: integers", "[opt_mir][solver]") {
  ConstantValue v3{ConstantValue::Kind::Int, 3};
  ConstantValue v5{ConstantValue::Kind::Int, 5};

  auto sum = try_fold_binary(BinaryOpNode::Kind::IAdd, v3, v5);
  REQUIRE(sum.has_value());
  REQUIRE(sum->bits == 8);

  auto prod = try_fold_binary(BinaryOpNode::Kind::IMul, v3, v5);
  REQUIRE(prod.has_value());
  REQUIRE(prod->bits == 15);
}

TEST_CASE("ConstFold: divide by zero", "[opt_mir][solver]") {
  ConstantValue v10{ConstantValue::Kind::Int, 10};
  ConstantValue v0{ConstantValue::Kind::Int, 0};

  auto res = try_fold_binary(BinaryOpNode::Kind::IDiv, v10, v0);
  REQUIRE_FALSE(res.has_value());
}

// ============================================================================
// Updater (Solver Loop)
// ============================================================================

TEST_CASE("Updater: simple constant prop (3 + 5 -> 8)", "[opt_mir][solver]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;
  auto t0 = b.entry_token();

  auto c3 = b.make_constant({ConstantValue::Kind::Int, 3}, i32_type);
  auto c5 = b.make_constant({ConstantValue::Kind::Int, 5}, i32_type);
  auto add = b.make_binary(BinaryOpNode::Kind::IAdd, c3, c5, i32_type);
  b.emit_return(entry, t0, add);

  Updater::run(func);

  // 'add' node should have been rewritten to Constant(8)
  const auto &node = func.get_node(add);
  REQUIRE(std::holds_alternative<ConstantNode>(node.kind));
  REQUIRE(std::get<ConstantNode>(node.kind).value.bits == 8);
}

TEST_CASE("Updater: multi-hop const prop ( (3+5) * 2 )", "[opt_mir][solver]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;
  auto t0 = b.entry_token();

  auto c3 = b.make_constant({ConstantValue::Kind::Int, 3}, i32_type);
  auto c5 = b.make_constant({ConstantValue::Kind::Int, 5}, i32_type);
  auto sum = b.make_binary(BinaryOpNode::Kind::IAdd, c3, c5, i32_type); // 8
  auto c2 = b.make_constant({ConstantValue::Kind::Int, 2}, i32_type);
  auto prod = b.make_binary(BinaryOpNode::Kind::IMul, sum, c2, i32_type); // 16
  b.emit_return(entry, t0, prod);

  Updater::run(func);

  // Verify 'sum' is 8
  const auto &n_sum = func.get_node(sum);
  REQUIRE(std::holds_alternative<ConstantNode>(n_sum.kind));
  REQUIRE(std::get<ConstantNode>(n_sum.kind).value.bits == 8);

  // Verify 'prod' is 16
  const auto &n_prod = func.get_node(prod);
  REQUIRE(std::holds_alternative<ConstantNode>(n_prod.kind));
  REQUIRE(std::get<ConstantNode>(n_prod.kind).value.bits == 16);
}

TEST_CASE("Updater: store-load forwarding", "[opt_mir][solver]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;
  auto t0 = b.entry_token();

  auto slot = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");
  auto c42 = b.make_constant({ConstantValue::Kind::Int, 42}, i32_type);

  // store @x, 42
  auto t1 = b.emit_store(entry, t0, slot, c42);

  // load @x (t1)
  auto ld = b.make_load(t1, slot, i32_type);

  // return ld
  b.emit_return(entry, t1, ld);

  Updater::run(func);

  // Load should be rewritten to Constant(42)
  const auto &n_ld = func.get_node(ld);
  REQUIRE(std::holds_alternative<ConstantNode>(n_ld.kind));
  REQUIRE(std::get<ConstantNode>(n_ld.kind).value.bits == 42);
}

TEST_CASE("Updater: branch merge (diamond)", "[opt_mir][solver]") {
  //      entry
  //     /     \
  //   true   false
  //     \     /
  //      merge
  //
  // In true: store @x, 10
  // In false: store @x, 10
  // In merge: load @x -> expects 10 (Forwarding through Phi)

  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  auto b_true = b.new_block();
  auto b_false = b.new_block();
  auto merge = b.new_block();
  func.entry_block = entry;

  auto slot = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");
  auto t0 = b.entry_token();
  auto cond = b.make_constant({ConstantValue::Kind::Bool, 1}, bool_type);

  auto [t_true_in, t_false_in] =
      b.emit_branch(entry, t0, cond, b_true, b_false);

  // True path
  auto c10 = b.make_constant({ConstantValue::Kind::Int, 10}, i32_type);
  auto t_true_out = b.emit_store(b_true, t_true_in, slot, c10);
  b.emit_jump(b_true, t_true_out, merge);

  // False path
  auto t_false_out = b.emit_store(b_false, t_false_in, slot, c10);
  b.emit_jump(b_false, t_false_out, merge);

  // Merge
  auto t_merge =
      b.emit_token_phi(merge, {{b_true, t_true_out}, {b_false, t_false_out}});

  auto ld = b.make_load(t_merge, slot, i32_type);
  b.emit_return(merge, t_merge, ld);

  Updater::run(func);

  // The load should observe that x is 10 on both paths, so it's 10.
  const auto &n_ld = func.get_node(ld);
  REQUIRE(std::holds_alternative<ConstantNode>(n_ld.kind));
  REQUIRE(std::get<ConstantNode>(n_ld.kind).value.bits == 10);
}

TEST_CASE("Updater: conflicting branch merge", "[opt_mir][solver]") {
  // Same as above but true=10, false=20 -> Result Bottom (Load not folded)
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  auto b_true = b.new_block();
  auto b_false = b.new_block();
  auto merge = b.new_block();
  func.entry_block = entry;

  auto slot = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");
  auto t0 = b.entry_token();
  auto cond = b.make_constant({ConstantValue::Kind::Bool, 1}, bool_type);

  auto [t_true_in, t_false_in] =
      b.emit_branch(entry, t0, cond, b_true, b_false);

  // True path: 10
  auto c10 = b.make_constant({ConstantValue::Kind::Int, 10}, i32_type);
  auto t_true_out = b.emit_store(b_true, t_true_in, slot, c10);
  b.emit_jump(b_true, t_true_out, merge);

  // False path: 20
  auto c20 = b.make_constant({ConstantValue::Kind::Int, 20}, i32_type);
  auto t_false_out = b.emit_store(b_false, t_false_in, slot, c20);
  b.emit_jump(b_false, t_false_out, merge);

  auto t_merge =
      b.emit_token_phi(merge, {{b_true, t_true_out}, {b_false, t_false_out}});

  auto ld = b.make_load(t_merge, slot, i32_type);
  b.emit_return(merge, t_merge, ld);

  Updater::run(func);

  // Load should NOT be folded (it remains a LoadNode)
  const auto &n_ld = func.get_node(ld);
  REQUIRE(std::holds_alternative<LoadNode>(n_ld.kind));
}
