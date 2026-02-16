// test_lower.cpp — Minimal tests for the opt MIR lowering pass.
//
// Since constructing well-formed HIR fragments is complex (requires
// semantic passes), these tests focus on:
//   1. Verifying the lowering API compiles and links
//   2. Lowering an empty program produces an empty module
//   3. Basic structural checks

#include <catch2/catch_test_macros.hpp>

#include "opt/mir/lower/lower.hpp"
#include "opt/mir/ir/module.hpp"
#include "opt/mir/tools/printer.hpp"
#include "semantic/hir/hir.hpp"

TEST_CASE("Lower empty program", "[opt_mir][lower]") {
  hir::Program program;
  auto module = opt::mir::lower_program(program);
  REQUIRE(module.functions.empty());
}

TEST_CASE("lower_program returns OptModule", "[opt_mir][lower]") {
  hir::Program program;
  opt::mir::OptModule module = opt::mir::lower_program(program);
  // Just verifying the types are correct and it compiles
  CHECK(module.functions.size() == 0);
}
