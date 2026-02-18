#include <catch2/catch_test_macros.hpp>

#include "opt/mir/analysis/escape_analysis.hpp"
#include "opt/mir/analysis/node_fact.hpp"
#include "opt/mir/analysis/use_list.hpp"
#include "opt/mir/analysis/world_state.hpp"
#include "opt/mir/ir/basic_block.hpp"
#include "opt/mir/ir/module.hpp"
#include "opt/mir/ir/node_id.hpp"
#include "opt/mir/ir/nodes.hpp"
#include "opt/mir/ir/slot.hpp"
#include "opt/mir/passes/const_fold.hpp"
#include "opt/mir/passes/updater.hpp"
#include "opt/mir/tools/builder.hpp"
#include "opt/mir/tools/graph_mutator.hpp"
#include "opt/mir/tools/printer.hpp"

#include <deque>
#include <sstream>
#include <string>
#include <variant>

#include "type/type.hpp"

using namespace opt::mir;

// Helper: real type id registration
static const type::TypeId i32_type =
    type::get_typeID(type::Type{type::PrimitiveKind::I32});
static const type::TypeId bool_type =
    type::get_typeID(type::Type{type::PrimitiveKind::BOOL});
static const type::TypeId ptr_i32_type =
    type::get_typeID(type::Type{type::ReferenceType{i32_type, false}});

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
  std::vector<type::TypeId> slot_types = {i32_type, i32_type};

  auto fact_a = NodeFact::initial_of(i32_type);
  auto fact_b = NodeFact::initial_of(i32_type); // Or distinct fact for testing

  // write(slot, {}, fact)
  auto ws1 = ws.write(slot_types, slot_a, {}, fact_a);
  REQUIRE(ws1.size() == 1);
  REQUIRE(ws1.read(slot_types, slot_a) == fact_a);

  // Original is unchanged (persistent)
  REQUIRE(ws.empty());

  auto ws2 = ws1.write(slot_types, slot_b, {}, fact_b);
  REQUIRE(ws2.size() == 2);
}

TEST_CASE("WorldSnapshot merge: identical snapshots",
          "[opt_mir][world_state]") {
  auto slot = SlotId{0};
  std::vector<type::TypeId> slot_types = {i32_type};
  auto fact = NodeFact::initial_of(i32_type);

  auto ws = WorldSnapshot{}.write(slot_types, slot, {}, fact);
  auto merged = WorldSnapshot::merge(ws, ws);

  REQUIRE(merged.size() == 1);
  REQUIRE(merged.read(slot_types, slot) == fact);
}

TEST_CASE("WorldSnapshot merge: slots in only one side are kept",
          "[opt_mir][world_state]") {
  auto slot_a = SlotId{0};
  auto slot_b = SlotId{1};
  std::vector<type::TypeId> slot_types = {i32_type, i32_type};
  auto fact = NodeFact::initial_of(i32_type);

  // ws_left has slot_a, ws_right has slot_b
  auto ws_left = WorldSnapshot{}.write(slot_types, slot_a, {}, fact);
  auto ws_right = WorldSnapshot{}.write(slot_types, slot_b, {}, fact);

  auto merged = WorldSnapshot::merge(ws_left, ws_right);

  // Both kept
  REQUIRE(merged.size() == 2);
}

// ============================================================================
// PointToFact lattice
// ============================================================================

TEST_CASE("PointToFact: Top meet x = x", "[opt_mir][fact]") {
  auto top = PointToFact::top();
  auto p = PointToFact::singleton(Place::simple(SlotId{0}));
  auto bot = PointToFact::bottom();

  REQUIRE(PointToFact::meet(top, top) == top);
  REQUIRE(PointToFact::meet(top, p) == p);
  REQUIRE(PointToFact::meet(p, top) == p);
  REQUIRE(PointToFact::meet(top, bot) == bot);
  REQUIRE(PointToFact::meet(bot, top) == bot);
}

TEST_CASE("PointToFact: Set union", "[opt_mir][fact]") {
  auto p1 = PointToFact::singleton(Place::simple(SlotId{0}));
  auto p2 = PointToFact::singleton(Place::simple(SlotId{1}));

  // meet(p1, p2) -> {slot0, slot1}
  auto res = PointToFact::meet(p1, p2);
  REQUIRE(res.kind == PointToFact::Kind::Set);
  REQUIRE(res.places.size() == 2);
  REQUIRE((res.places[0] == Place::simple(SlotId{0}) ||
           res.places[0] == Place::simple(SlotId{1})));
  REQUIRE((res.places[1] == Place::simple(SlotId{0}) ||
           res.places[1] == Place::simple(SlotId{1})));
}

TEST_CASE("PointToFact: External meet Place preserves both",
          "[opt_mir][fact]") {
  auto ext = PointToFact::bottom();
  auto p = PointToFact::singleton(Place::simple(SlotId{0}));

  // meet(External, {0}) -> {0} + External
  auto res1 = PointToFact::meet(ext, p);
  REQUIRE(res1.kind == PointToFact::Kind::Set);
  REQUIRE(res1.points_to_external);
  REQUIRE(res1.places.size() == 1);
  REQUIRE(res1.places[0] == Place::simple(SlotId{0}));
  REQUIRE_FALSE(res1.is_bottom()); // Not empty

  auto res2 = PointToFact::meet(p, ext);
  REQUIRE(res2.points_to_external);
  REQUIRE(res2.places.size() == 1);
}

// ============================================================================
// NodeFact product lattice
// ============================================================================

TEST_CASE("NodeFact: product meet dispatches component-wise",
          "[opt_mir][fact]") {
  // a: Const(5), PointTo(Slot0)
  NodeFact a{ConstPropFact::constant({ConstantValue::Kind::Int, 5}),
             PointToFact::singleton(Place::simple(SlotId{0}))};
  // b: Const(7), PointTo(Slot1)
  NodeFact b{ConstPropFact::constant({ConstantValue::Kind::Int, 7}),
             PointToFact::singleton(Place::simple(SlotId{1}))};

  auto result = NodeFact::meet(a, b);
  // ConstProp: 5 meet 7 -> Bottom
  REQUIRE(result.const_prop.is_bottom());
  // PointTo: {Slot0} meet {Slot1} -> {Slot0, Slot1}
  REQUIRE(result.point_to.kind == PointToFact::Kind::Set);
  REQUIRE(result.point_to.places.size() == 2);
}

TEST_CASE("NodeFact: top meet constant = constant", "[opt_mir][fact]") {
  auto top = NodeFact::top();
  NodeFact c5{ConstPropFact::constant({ConstantValue::Kind::Int, 5}),
              PointToFact::top()};

  auto result = NodeFact::meet(top, c5);
  REQUIRE(result.const_prop.is_constant());
  REQUIRE(result == c5);
}

// ============================================================================
// WorldSnapshot merge with lattice meet
// ============================================================================

TEST_CASE("WorldSnapshot merge with PointTo facts", "[opt_mir][world_state]") {
  auto slot = SlotId{0};
  std::vector<type::TypeId> slot_types = {ptr_i32_type};

  // Two snapshots with same slot but different pointer targets
  NodeFact nf_a{ConstPropFact::top(),
                PointToFact::singleton(Place::simple(SlotId{1}))};
  NodeFact nf_b{ConstPropFact::top(),
                PointToFact::singleton(Place::simple(SlotId{2}))};

  auto ws_a = WorldSnapshot{}.write(slot_types, slot, {}, nf_a);
  auto ws_b = WorldSnapshot{}.write(slot_types, slot, {}, nf_b);

  auto merged = WorldSnapshot::merge(ws_a, ws_b);

  // Result should be Union of pointer targets
  REQUIRE(merged.size() == 1);
  auto read_fact = merged.read(slot_types, slot);
  REQUIRE(read_fact.point_to.kind == PointToFact::Kind::Set);
  REQUIRE(read_fact.point_to.places.size() == 2);
}
// ============================================================================
// Updater (Solver Loop) with AddressOf
// ============================================================================

TEST_CASE("Updater: AddressOf analysis", "[opt_mir][solver]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;
  // auto t0 = b.entry_token();

  auto slot_x = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");

  // %ptr = AddressOf(x)
  // We don't have a Builder method for AddressOf yet, create manually
  auto ptr_node_id = func.alloc_node(Node{
      AddressOfNode{Place::simple(slot_x)},
      ptr_i32_type // correctly use pointer type
  });

  // To test if it works, we need to inspect the facts computed by Solver.
  // Updater usually runs rewriting. We can run Updater (which runs Solver)
  // and checking the internal state is hard from "outside" unless we expose it.

  // Alternatively, we can construct Solver manually and query it.
  // But Solver needs initialized facts.

  // Let's just create a test that uses Updater, and relies on the fact
  // existing? Actually, we can't easily check the fact unless we misuse
  // rewrites or add a custom rewriter. Or, we rely on the fact that we can
  // construct a Solver in the test.

  // Manual Solver test
  std::vector<NodeFact> node_facts(func.nodes.size(), NodeFact::top());
  std::vector<WorldSnapshot> token_facts(100, WorldSnapshot{});

  auto escape = EscapeAnalysis::run(func);
  Solver solver(func, node_facts, token_facts, escape);
  auto fact = solver.evaluate_node(ptr_node_id);

  REQUIRE(fact.point_to.kind == PointToFact::Kind::Set);
  REQUIRE(fact.point_to.places.size() == 1);
  REQUIRE(fact.point_to.places[0] == Place::simple(slot_x));
}

TEST_CASE("Updater: PointTo propagation through memory", "[opt_mir][solver]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;
  auto t0 = b.entry_token();

  auto slot_x = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");
  auto slot_ptr =
      b.new_slot(Slot::Kind::StackLocal, ptr_i32_type, "ptr_storage");

  // %ptr = AddressOf(x)
  auto ptr_node =
      func.alloc_node(Node{AddressOfNode{Place::simple(slot_x)}, ptr_i32_type});

  // store @ptr_storage, %ptr
  auto t1 = b.emit_store(entry, t0, slot_ptr, ptr_node);

  // %loaded_ptr = load @ptr_storage
  auto loaded_ptr = b.make_load(t1, slot_ptr, ptr_i32_type);

  b.emit_return(entry, t1, loaded_ptr);

  // Run Solver manually to verify propagation
  // We need to run fixpoint or at least enough iterations.
  // Let's mimic what Updater does but simplified.

  // Initialize facts
  size_t max_node = func.nodes.size();
  size_t max_token = 100; // Safe upper bound for this small test
  std::vector<NodeFact> node_facts(max_node, NodeFact::top());
  std::vector<WorldSnapshot> token_facts(max_token, WorldSnapshot{});

  // Initial token fact (empty world)
  token_facts[raw(t0)] = WorldSnapshot{};

  auto escape = EscapeAnalysis::run(func);
  Solver solver(func, node_facts, token_facts, escape);

  // 1. Eval ptr_node
  node_facts[raw(ptr_node)] = solver.evaluate_node(ptr_node);

  // 2. Eval store -> yields output token fact for t1
  // Store inst depends on t0 (empty) and ptr_node (PointTo x)
  // Store instruction ID? Builder doesn't return InstId, we need to find it.
  InstId store_inst = func.get_block(entry).inst_ids[0];

  auto store_out = solver.evaluate_inst(store_inst);
  // Store output is t1
  for (auto [t, ws] : store_out) {
    if (t == t1)
      token_facts[raw(t1)] = ws;
  }

  // 3. Eval load
  node_facts[raw(loaded_ptr)] = solver.evaluate_node(loaded_ptr);

  // Check loaded_ptr fact
  auto fact = node_facts[raw(loaded_ptr)];
  REQUIRE(fact.point_to.kind == PointToFact::Kind::Set);
  REQUIRE(fact.point_to.places.size() == 1);
  REQUIRE(fact.point_to.places[0] == Place::simple(slot_x));
}

TEST_CASE("EscapeAnalysis: marks slots with AddressOf", "[opt_mir][escape]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;

  auto slot_x = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");
  auto slot_y = b.new_slot(Slot::Kind::StackLocal, i32_type, "y");

  (void)b.make_address_of(Place::simple(slot_x), Mutability::Immutable,
                          ptr_i32_type);

  auto escape = EscapeAnalysis::run(func);
  REQUIRE(escape.escapes(slot_x));
  REQUIRE_FALSE(escape.escapes(slot_y));
}

TEST_CASE("Solver: pointer-base store clobbers escaped slots",
          "[opt_mir][solver][escape]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;

  auto t0 = b.entry_token();

  auto slot_x = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");
  b.new_slot(Slot::Kind::StackLocal, i32_type, "p");

  auto c1 = b.make_constant({ConstantValue::Kind::Int, 1}, i32_type);
  auto t1 = b.emit_store(entry, t0, slot_x, c1);

  auto ptr = b.make_address_of(Place::simple(slot_x), Mutability::Immutable,
                               ptr_i32_type);
  auto c2 = b.make_constant({ConstantValue::Kind::Int, 2}, i32_type);

  Place ptr_place = Place::from_ptr(ptr);
  auto t2 = func.alloc_token();
  auto ptr_store =
      func.alloc_inst(PinnedInst{StoreInst{t1, ptr_place, c2, t2}}, entry);
  func.get_block_mut(entry).inst_ids.push_back(ptr_store);

  std::vector<NodeFact> node_facts(func.nodes.size(), NodeFact::top());
  std::vector<WorldSnapshot> token_facts(64, WorldSnapshot{});
  token_facts[raw(t0)] = WorldSnapshot{};

  auto escape = EscapeAnalysis::run(func);
  Solver solver(func, node_facts, token_facts, escape);

  node_facts[raw(c1)] = solver.evaluate_node(c1);
  auto store1 = solver.evaluate_inst(func.get_block(entry).inst_ids[0]);
  for (auto [t, ws] : store1) {
    if (t == t1)
      token_facts[raw(t1)] = ws;
  }

  node_facts[raw(c2)] = solver.evaluate_node(c2);

  // Manually set ptr to Unknown/External to verify that it clobbers escaped
  // slots
  node_facts[raw(ptr)].point_to = PointToFact::bottom();

  auto store2 = solver.evaluate_inst(ptr_store);
  for (auto [t, ws] : store2) {
    if (t == t2)
      token_facts[raw(t2)] = ws;
  }

  std::vector<type::TypeId> slot_types;
  slot_types.reserve(func.slots.size());
  for (const auto &slot : func.slots) {
    slot_types.push_back(slot.type);
  }
  auto fact_after = token_facts[raw(t2)].read(slot_types, slot_x);
  REQUIRE(fact_after.const_prop.is_bottom());
}

// ============================================================================
// WorldSnapshot merge with lattice meet
// ============================================================================

TEST_CASE("WorldSnapshot merge: disagreeing facts produce meet, not drop",
          "[opt_mir][world_state][fact]") {
  auto slot = SlotId{0};
  std::vector<type::TypeId> slot_types = {i32_type};

  // Two snapshots with same slot but different constant values
  NodeFact nf5{ConstPropFact::constant({ConstantValue::Kind::Int, 5}),
               PointToFact::top()};
  NodeFact nf7{ConstPropFact::constant({ConstantValue::Kind::Int, 7}),
               PointToFact::top()};

  auto ws_a = WorldSnapshot{}.write(slot_types, slot, {}, nf5);
  auto ws_b = WorldSnapshot{}.write(slot_types, slot, {}, nf7);

  auto merged = WorldSnapshot::merge(ws_a, ws_b);

  // Slot is preserved (not dropped), but its fact is Bottom
  REQUIRE(merged.size() == 1);
  REQUIRE(merged.read(slot_types, slot).const_prop.is_bottom());
}

TEST_CASE("WorldSnapshot merge: agreeing facts preserved",
          "[opt_mir][world_state][fact]") {
  auto slot = SlotId{0};
  std::vector<type::TypeId> slot_types = {i32_type};

  NodeFact nf5{ConstPropFact::constant({ConstantValue::Kind::Int, 5}),
               PointToFact::top()};

  auto ws_a = WorldSnapshot{}.write(slot_types, slot, {}, nf5);
  auto ws_b = WorldSnapshot{}.write(slot_types, slot, {}, nf5);

  auto merged = WorldSnapshot::merge(ws_a, ws_b);

  REQUIRE(merged.size() == 1);
  REQUIRE(merged.read(slot_types, slot).const_prop.is_constant());
  REQUIRE(merged.read(slot_types, slot).const_prop.value.bits == 5);
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
  //     /     \ (split)
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

// ============================================================================
// GraphMutator — Beta Reduction
// ============================================================================

TEST_CASE("GraphMutator: replace_all_uses_of(NodeId) in nodes",
          "[opt_mir][graph_mutator]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;

  auto c1 = b.make_constant({ConstantValue::Kind::Int, 1}, i32_type);
  auto c2 = b.make_constant({ConstantValue::Kind::Int, 2}, i32_type);
  auto c3 = b.make_constant({ConstantValue::Kind::Int, 3}, i32_type);
  auto sum = b.make_binary(BinaryOpNode::Kind::IAdd, c1, c2, i32_type);

  auto ul = UseLists::build(func);
  GraphMutator mutator(func, ul);

  // Before: sum = c1 + c2
  REQUIRE(ul.users_of(c2).size() == 1);
  REQUIRE(ul.users_of(c3).empty());

  // Replace c2 → c3
  mutator.replace_all_uses_of(c2, c3);

  // After: sum = c1 + c3
  auto &bin = std::get<BinaryOpNode>(func.get_node(sum).kind);
  REQUIRE(bin.lhs == c1);
  REQUIRE(bin.rhs == c3);

  // Use-lists updated
  REQUIRE(ul.users_of(c2).empty());
  REQUIRE(ul.users_of(c3).size() == 1);
  REQUIRE(std::get<NodeUser>(ul.users_of(c3)[0]).id == sum);
}

TEST_CASE("GraphMutator: replace_all_uses_of(NodeId) across instructions",
          "[opt_mir][graph_mutator]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;
  auto t0 = b.entry_token();

  auto slot = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");
  auto c1 = b.make_constant({ConstantValue::Kind::Int, 1}, i32_type);
  auto c2 = b.make_constant({ConstantValue::Kind::Int, 2}, i32_type);

  // Store c1, then return c1
  auto t1 = b.emit_store(entry, t0, slot, c1);
  b.emit_return(entry, t1, c1);

  auto ul = UseLists::build(func);
  GraphMutator mutator(func, ul);

  // c1 should have 2 inst users (Store, Return)
  REQUIRE(ul.users_of(c1).size() == 2);
  REQUIRE(ul.users_of(c2).empty());

  // Replace c1 → c2
  mutator.replace_all_uses_of(c1, c2);

  // Verify instructions updated
  auto store_id = func.get_block(entry).inst_ids[0];
  auto &store = std::get<StoreInst>(func.get_inst(store_id).kind);
  REQUIRE(store.value == c2);

  auto ret_id = func.get_block(entry).inst_ids[1];
  auto &ret = std::get<ReturnInst>(func.get_inst(ret_id).kind);
  REQUIRE(*ret.value == c2);

  // Use-lists updated
  REQUIRE(ul.users_of(c1).empty());
  REQUIRE(ul.users_of(c2).size() == 2);
}

TEST_CASE("GraphMutator: replace_all_uses_of(TokenId)",
          "[opt_mir][graph_mutator]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;

  auto slot = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");
  auto t0 = b.entry_token();
  auto c1 = b.make_constant({ConstantValue::Kind::Int, 42}, i32_type);

  // Store produces t1; Load and Return consume t1
  auto t1 = b.emit_store(entry, t0, slot, c1);
  auto ld = b.make_load(t1, slot, i32_type);
  b.emit_return(entry, t1, ld);

  auto ul = UseLists::build(func);
  GraphMutator mutator(func, ul);

  // t1 has users: ld (NodeUser) and ReturnInst (InstUser)
  REQUIRE(ul.users_of(t1).size() == 2);

  // Replace t1 → t0 (bypass the store in the token chain)
  mutator.replace_all_uses_of(t1, t0);

  // Load now reads from t0
  auto &load_node = std::get<LoadNode>(func.get_node(ld).kind);
  REQUIRE(load_node.token == t0);

  // Return now uses t0
  auto ret_id = func.get_block(entry).inst_ids.back();
  auto &ret = std::get<ReturnInst>(func.get_inst(ret_id).kind);
  REQUIRE(ret.t_in == t0);

  // t1 has no users; t0 picked them up
  REQUIRE(ul.users_of(t1).empty());
  REQUIRE(ul.users_of(t0).size() >= 2); // original store user + 2 new
}

TEST_CASE("GraphMutator: replace_all_uses_of(SlotId)",
          "[opt_mir][graph_mutator]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;
  auto t0 = b.entry_token();

  auto slot_tmp = b.new_slot(Slot::Kind::StackLocal, i32_type, "tmp");
  auto slot_dst = b.new_slot(Slot::Kind::StackLocal, i32_type, "dst");
  auto c1 = b.make_constant({ConstantValue::Kind::Int, 10}, i32_type);

  // Store to tmp, then load from tmp
  auto t1 = b.emit_store(entry, t0, slot_tmp, c1);
  auto ld = b.make_load(t1, slot_tmp, i32_type);
  b.emit_return(entry, t1, ld);

  auto ul = UseLists::build(func);
  GraphMutator mutator(func, ul);

  // Replace slot_tmp → slot_dst (copy elision)
  mutator.replace_all_uses_of(slot_tmp, slot_dst);

  // Store now targets slot_dst
  auto store_id = func.get_block(entry).inst_ids[0];
  auto &store = std::get<StoreInst>(func.get_inst(store_id).kind);
  auto *store_base = std::get_if<SlotId>(&store.place.base);
  REQUIRE(store_base != nullptr);
  REQUIRE(*store_base == slot_dst);

  // Load now reads from slot_dst
  auto &load_node = std::get<LoadNode>(func.get_node(ld).kind);
  auto *load_base = std::get_if<SlotId>(&load_node.place.base);
  REQUIRE(load_base != nullptr);
  REQUIRE(*load_base == slot_dst);
}
