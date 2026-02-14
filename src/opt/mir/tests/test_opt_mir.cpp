#include <catch2/catch_test_macros.hpp>

#include "opt/mir/basic_block.hpp"
#include "opt/mir/builder.hpp"
#include "opt/mir/node_id.hpp"
#include "opt/mir/nodes.hpp"
#include "opt/mir/opt_mir.hpp"
#include "opt/mir/printer.hpp"
#include "opt/mir/slot.hpp"
#include "opt/mir/world_state.hpp"

#include <sstream>
#include <string>

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
  REQUIRE(func.get_block(b0).instructions.empty());
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
  REQUIRE(bb.instructions.size() == 2); // store, return

  // First instruction is Store
  REQUIRE(std::holds_alternative<StoreInst>(bb.instructions[0].kind));
  // Second instruction is Return
  REQUIRE(std::holds_alternative<ReturnInst>(bb.instructions[1].kind));
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
  REQUIRE(merge.instructions.size() == 2);
  REQUIRE(std::holds_alternative<TokenPhiInst>(merge.instructions[0].kind));
  REQUIRE(std::holds_alternative<ReturnInst>(merge.instructions[1].kind));

  // Verify the phi has 2 incoming entries
  const auto &phi = std::get<TokenPhiInst>(merge.instructions[0].kind);
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
// WorldSnapshot
// ============================================================================

TEST_CASE("WorldSnapshot write/read round-trip", "[opt_mir][world_state]") {
  WorldSnapshot ws;

  REQUIRE(ws.empty());

  auto slot_a = SlotId{0};
  auto slot_b = SlotId{1};

  SlotState state_a{};
  SlotState state_b{};

  auto ws1 = ws.write(slot_a, state_a);
  REQUIRE(ws1.size() == 1);
  REQUIRE(ws1.read(slot_a) == state_a);

  // Original is unchanged (persistent)
  REQUIRE(ws.empty());

  auto ws2 = ws1.write(slot_b, state_b);
  REQUIRE(ws2.size() == 2);
}

TEST_CASE("WorldSnapshot merge: identical snapshots",
          "[opt_mir][world_state]") {
  auto slot = SlotId{0};
  SlotState state{};

  auto ws = WorldSnapshot{}.write(slot, state);
  auto merged = WorldSnapshot::merge(ws, ws);

  REQUIRE(merged.size() == 1);
  REQUIRE(merged.read(slot) == state);
}

TEST_CASE("WorldSnapshot merge: disagreeing slots dropped",
          "[opt_mir][world_state]") {
  auto slot_a = SlotId{0};
  auto slot_b = SlotId{1};
  SlotState state{};

  // ws_left has slot_a, ws_right has slot_b
  auto ws_left = WorldSnapshot{}.write(slot_a, state);
  auto ws_right = WorldSnapshot{}.write(slot_b, state);

  auto merged = WorldSnapshot::merge(ws_left, ws_right);

  // Neither slot is in both → all dropped
  REQUIRE(merged.empty());
}
