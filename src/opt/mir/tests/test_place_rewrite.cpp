#include <catch2/catch_test_macros.hpp>

#include "opt/mir/analysis/escape_analysis.hpp"
#include "opt/mir/ir/basic_block.hpp"
#include "opt/mir/ir/module.hpp"
#include "opt/mir/passes/updater.hpp"
#include "opt/mir/tools/builder.hpp"
#include "opt/mir/tools/graph_mutator.hpp"

using namespace opt::mir;

static const type::TypeId i32_type =
    type::get_typeID(type::Type{type::PrimitiveKind::I32});
static const type::TypeId ptr_i32_type =
    type::get_typeID(type::Type{type::ReferenceType{i32_type, false}});

TEST_CASE("PlaceRewriter: Load indirect -> direct",
          "[opt_mir][place_rewriter]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;
  auto t0 = b.entry_token();

  auto slot_x = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");

  // %ptr = AddressOf(x)
  auto ptr_node =
      func.alloc_node(Node{AddressOfNode{Place::simple(slot_x)}, ptr_i32_type});

  // %v = Load(%ptr)  ->  should become Load(x)
  auto load_node = func.alloc_node(
      Node{LoadNode{t0, Place::from_ptr(ptr_node)},
           i32_type}); // Note: using ptr_node as base (IndexProjection not used
                       // in from_ptr, it sets base=ptr_node)

  // Pin it? LoadNode is floating. We emit a return to keep it alive/reachable?
  // Or just rely on Updater finding it in node list.
  // But Updater worklist initialization seeds all nodes.

  // We need to attach it to something returned or stored to be "meaningful"?
  // Updater runs on all nodes.

  // Run Updater
  Updater::run(func);

  // Check if LoadNode was rewritten.
  // The Node ID `load_node` should now have kind LoadNode with
  // Place::simple(slot_x).
  const auto &node = func.nodes[raw(load_node)];
  REQUIRE(std::holds_alternative<LoadNode>(node.kind));
  const auto &load = std::get<LoadNode>(node.kind);

  REQUIRE(std::holds_alternative<SlotId>(load.place.base));
  REQUIRE(std::get<SlotId>(load.place.base) == slot_x);
  REQUIRE(load.place.projections.empty());
}

TEST_CASE("PlaceRewriter: Store indirect -> direct",
          "[opt_mir][place_rewriter]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;
  auto t0 = b.entry_token();

  auto slot_x = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");
  auto c42 = b.make_constant({ConstantValue::Kind::Int, 42}, i32_type);

  // %ptr = AddressOf(x)
  auto ptr_node =
      func.alloc_node(Node{AddressOfNode{Place::simple(slot_x)}, ptr_i32_type});

  // Store(%ptr, 42) -> Store(x, 42)
  auto ptr_place = Place::from_ptr(ptr_node);
  // Manual store creation since Builder::emit_store takes SlotId usually?
  // Actually Builder::emit_store takes SlotId.
  // We need to manually create StoreInst with Place from ptr.

  auto t1 = func.alloc_token();
  auto store_id =
      func.alloc_inst(PinnedInst{StoreInst{t0, ptr_place, c42, t1}}, entry);
  func.get_block_mut(entry).inst_ids.push_back(store_id);

  // Run Updater
  Updater::run(func);

  // Check StoreInst
  const auto &inst = func.insts[raw(store_id)];
  REQUIRE(std::holds_alternative<StoreInst>(inst.kind));
  const auto &store = std::get<StoreInst>(inst.kind);

  REQUIRE(std::holds_alternative<SlotId>(store.place.base));
  REQUIRE(std::get<SlotId>(store.place.base) == slot_x);
}

TEST_CASE("PlaceRewriter: Merge projections", "[opt_mir][place_rewriter]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;
  auto t0 = b.entry_token();

  // We need a struct type to have fields.
  // Let's mock a struct type. Or just use indices blindly (type checker not
  // enforced here).

  auto slot_x = b.new_slot(Slot::Kind::StackLocal, i32_type, "x");

  // Base place: x.0
  Place base_place = Place::simple(slot_x);
  base_place.projections.push_back(FieldProjection{0});

  // %ptr = AddressOf(x.0)
  auto ptr_node =
      func.alloc_node(Node{AddressOfNode{base_place}, ptr_i32_type});

  // Load(%ptr.1) -> Load(x.0.1)
  Place load_place = Place::from_ptr(ptr_node);
  load_place.projections.push_back(FieldProjection{1});

  auto load_node = func.alloc_node(Node{LoadNode{t0, load_place}, i32_type});

  Updater::run(func);

  const auto &node = func.nodes[raw(load_node)];
  const auto &load = std::get<LoadNode>(node.kind);

  REQUIRE(std::holds_alternative<SlotId>(load.place.base));
  REQUIRE(std::get<SlotId>(load.place.base) == slot_x);
  REQUIRE(load.place.projections.size() == 2);
  REQUIRE(std::get<FieldProjection>(load.place.projections[0]).index == 0);
  REQUIRE(std::get<FieldProjection>(load.place.projections[1]).index == 1);
}

TEST_CASE("PlaceRewriter: Memcopy rewrite", "[opt_mir][place_rewriter]") {
  OptFunction func;
  Builder b(func);
  auto entry = b.new_block();
  func.entry_block = entry;
  auto t0 = b.entry_token();

  auto slot_a = b.new_slot(Slot::Kind::StackLocal, i32_type, "a");
  auto slot_b = b.new_slot(Slot::Kind::StackLocal, i32_type, "b");

  auto ptr_a =
      func.alloc_node(Node{AddressOfNode{Place::simple(slot_a)}, ptr_i32_type});
  auto ptr_b =
      func.alloc_node(Node{AddressOfNode{Place::simple(slot_b)}, ptr_i32_type});

  // Memcopy(dst=%ptr_a, src=%ptr_b) -> Memcopy(dst=a, src=b)
  auto t1 = func.alloc_token();
  auto memcpy_id = func.alloc_inst(
      PinnedInst{MemcopyInst{t0, Place::from_ptr(ptr_a), Place::from_ptr(ptr_b),
                             i32_type, t1}},
      entry);
  func.get_block_mut(entry).inst_ids.push_back(memcpy_id);

  Updater::run(func);

  const auto &inst = func.insts[raw(memcpy_id)];
  const auto &memcpy = std::get<MemcopyInst>(inst.kind);

  REQUIRE(std::holds_alternative<SlotId>(memcpy.dest.base));
  REQUIRE(std::get<SlotId>(memcpy.dest.base) == slot_a);

  REQUIRE(std::holds_alternative<SlotId>(memcpy.src.base));
  REQUIRE(std::get<SlotId>(memcpy.src.base) == slot_b);
}
