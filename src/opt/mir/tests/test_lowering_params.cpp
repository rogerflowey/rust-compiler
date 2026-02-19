#include <catch2/catch_test_macros.hpp>

#include "opt/mir/ir/module.hpp"
#include "opt/mir/lower/lower.hpp"
#include "opt/mir/tools/printer.hpp"
#include "semantic/hir/hir.hpp"
#include "type/type.hpp"

#include <memory>

using namespace opt::mir;

TEST_CASE("Lowering MutRefParam creates MutRefParam slot and AddressOf access",
          "[opt_mir][lower]") {
  // Construct a minimal HIR program:
  // fn test(p: &mut i32) { p; }

  // 1. Setup types
  auto i32 = type::get_typeID(type::Type{type::PrimitiveKind::I32});
  auto mut_i32 = type::get_typeID(type::Type{type::ReferenceType{i32, true}});

  // 2. Construct Function
  hir::Program prog;
  auto func_ptr = std::make_unique<hir::Function>();
  func_ptr->sig.name.name = "test";

  // Param p
  auto local_p = std::make_unique<hir::Local>();
  local_p->name.name = "p";
  local_p->type_annotation = mut_i32;
  hir::Local *p_ptr = local_p.get();

  // We need to keep p_ptr valid, but local_p is moved into BindingDef ->
  // Pattern -> Function. Wait, BindingDef takes ownership? hir::BindingDef has
  // `std::variant<Unresolved, Local*> local`. It stores a pointer! Who owns the
  // Local? `FunctionBody` owns locals. But parameters are NOT in
  // `FunctionBody::locals`. `FunctionSignature::params` owns `Pattern`.
  // `Pattern` owns `BindingDef`.
  // `BindingDef` owns `Local*`? No, raw pointer.
  // Where is the parameter `Local` stored?
  // In `hir.hpp`: `struct BindingDef { ... std::variant<Unresolved, Local*>
  // local; ... }` It seems `BindingDef` does NOT own the `Local`. So who owns
  // parameter Locals? `FunctionSignature` does NOT own them. This might be a
  // flaw in my mocked HIR or my understanding. Let's check `hir::Local` again.
  // Params are usually part of the AST which owns them?
  // In `hir::Function`, `params` is `vector<unique_ptr<Pattern>>`.
  // The `Local` usually lives in `FunctionBody`? No, params are not in body.

  // Let's cheat and store the `Local` in a separate vector to keep it alive.
  std::vector<std::unique_ptr<hir::Local>> param_storage;
  param_storage.push_back(std::move(local_p));

  hir::BindingDef binding;
  binding.local = param_storage.back().get();

  auto pattern =
      std::make_unique<hir::Pattern>(hir::BindingDef(std::move(binding)));
  func_ptr->sig.params.push_back(std::move(pattern));

  // Body
  func_ptr->body = hir::FunctionBody{};
  func_ptr->body->block = std::make_unique<hir::Block>();

  // Stmt: p;
  auto var_expr = std::make_unique<hir::Expr>(hir::Variable{p_ptr});
  var_expr->expr_info = semantic::ExprInfo{};
  var_expr->expr_info->type = mut_i32;
  var_expr->expr_info->is_place = true;
  var_expr->expr_info->is_mut = true;

  auto stmt = std::make_unique<hir::Stmt>(hir::ExprStmt{std::move(var_expr)});
  func_ptr->body->block->stmts.push_back(std::move(stmt));

  prog.items.push_back(std::make_unique<hir::Item>(std::move(*func_ptr)));

  // 3. Lower
  // We need to catch exceptions if lowering fails due to my mock
  opt::mir::OptModule module;
  try {
    module = opt::mir::lower_program(prog);
  } catch (const std::exception &e) {
    FAIL("Lowering failed: " << e.what());
  }

  REQUIRE(module.functions.size() == 1);
  const auto &func = module.functions[0];

  // 4. Verify Slot
  bool found_mut_ref = false;
  SlotId slot_id = invalid_slot;
  for (size_t i = 0; i < func.slots.size(); ++i) {
    if (func.slots[i].kind == Slot::Kind::MutRefParam) {
      found_mut_ref = true;
      slot_id = SlotId{(uint32_t)i};
      REQUIRE(func.slots[i].type == i32); // Inner type!
      REQUIRE(func.slots[i].debug_name == "p");
    }
  }
  REQUIRE(found_mut_ref);

  // 5. Verify AddressOf Node
  // The expression `p` in the body should trigger `lower_variable`, which emits
  // `AddressOf(Slot)`.
  bool found_addr_of = false;
  for (const auto &node : func.nodes) {
    if (const auto *addr = std::get_if<AddressOfNode>(&node.kind)) {
      if (std::holds_alternative<SlotId>(addr->place.base)) {
        if (std::get<SlotId>(addr->place.base) == slot_id) {
          found_addr_of = true;
        }
      }
    }
  }
  REQUIRE(found_addr_of);
}

TEST_CASE("Lowering mutable &mut param seeds local ref from MutRefParam",
          "[opt_mir][lower]") {
  // fn test(mut p: &mut i32) { p; }
  auto i32 = type::get_typeID(type::Type{type::PrimitiveKind::I32});
  auto mut_i32 = type::get_typeID(type::Type{type::ReferenceType{i32, true}});

  hir::Program prog;
  auto func_ptr = std::make_unique<hir::Function>();
  func_ptr->sig.name.name = "test";

  auto local_p = std::make_unique<hir::Local>();
  local_p->name.name = "p";
  local_p->is_mutable = true;
  local_p->type_annotation = mut_i32;
  hir::Local *p_ptr = local_p.get();

  std::vector<std::unique_ptr<hir::Local>> param_storage;
  param_storage.push_back(std::move(local_p));

  hir::BindingDef binding;
  binding.local = param_storage.back().get();
  auto pattern =
      std::make_unique<hir::Pattern>(hir::BindingDef(std::move(binding)));
  func_ptr->sig.params.push_back(std::move(pattern));

  func_ptr->body = hir::FunctionBody{};
  func_ptr->body->block = std::make_unique<hir::Block>();

  auto var_expr = std::make_unique<hir::Expr>(hir::Variable{p_ptr});
  var_expr->expr_info = semantic::ExprInfo{};
  var_expr->expr_info->type = mut_i32;
  var_expr->expr_info->is_place = true;
  var_expr->expr_info->is_mut = true;

  auto stmt = std::make_unique<hir::Stmt>(hir::ExprStmt{std::move(var_expr)});
  func_ptr->body->block->stmts.push_back(std::move(stmt));

  prog.items.push_back(std::make_unique<hir::Item>(std::move(*func_ptr)));

  opt::mir::OptModule module;
  try {
    module = opt::mir::lower_program(prog);
  } catch (const std::exception &e) {
    FAIL("Lowering failed: " << e.what());
  }

  REQUIRE(module.functions.size() == 1);
  const auto &func = module.functions[0];

  SlotId mutref_slot = invalid_slot;
  SlotId param_slot = invalid_slot;
  for (size_t i = 0; i < func.slots.size(); ++i) {
    const SlotId sid{static_cast<std::uint32_t>(i)};
    if (func.slots[i].kind == Slot::Kind::MutRefParam) {
      mutref_slot = sid;
      REQUIRE(func.slots[i].type == i32);
    }
    if (func.slots[i].kind == Slot::Kind::Parameter &&
        func.slots[i].debug_name == "p") {
      param_slot = sid;
      REQUIRE(func.slots[i].type == mut_i32);
    }
  }
  REQUIRE(mutref_slot != invalid_slot);
  REQUIRE(param_slot != invalid_slot);

  bool found_seed_store = false;
  for (const auto &inst : func.insts) {
    if (const auto *store = std::get_if<StoreInst>(&inst.kind)) {
      if (!std::holds_alternative<SlotId>(store->place.base)) {
        continue;
      }
      if (std::get<SlotId>(store->place.base) != param_slot) {
        continue;
      }

      const Node &value_node = func.get_node(store->value);
      const auto *addr = std::get_if<AddressOfNode>(&value_node.kind);
      if (!addr || !std::holds_alternative<SlotId>(addr->place.base)) {
        continue;
      }
      if (std::get<SlotId>(addr->place.base) == mutref_slot) {
        found_seed_store = true;
        break;
      }
    }
  }
  REQUIRE(found_seed_store);
}
