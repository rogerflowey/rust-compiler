#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

#include "opt/mir/analysis/node_fact.hpp"
#include "type/type.hpp"

using namespace opt::mir;

// Helper to get type IDs for testing
static type::TypeId get_type_id(type::TypeVariant v) {
  return type::get_typeID(type::Type{std::move(v)});
}

TEST_CASE("ConstPropFact: strict type checking", "[opt_mir][fact][type]") {
  auto na = ConstPropFact::not_applicable();
  auto top = ConstPropFact::top();
  auto c5 = ConstPropFact::constant({ConstantValue::Kind::Int, 5});
  auto bot = ConstPropFact::bottom();

  SECTION("NA meet NA is NA") {
    REQUIRE(ConstPropFact::meet(na, na).is_not_applicable());
  }

  SECTION("NA meet Valid throws") {
    REQUIRE_THROWS_AS(ConstPropFact::meet(na, top), std::runtime_error);
    REQUIRE_THROWS_AS(ConstPropFact::meet(top, na), std::runtime_error);
    REQUIRE_THROWS_AS(ConstPropFact::meet(na, c5), std::runtime_error);
    REQUIRE_THROWS_AS(ConstPropFact::meet(c5, na), std::runtime_error);
    REQUIRE_THROWS_AS(ConstPropFact::meet(na, bot), std::runtime_error);
  }

  SECTION("Equality checks with NA") {
    REQUIRE(na == na);
    REQUIRE_THROWS_AS(na == top, std::runtime_error);
    REQUIRE_THROWS_AS(c5 == na, std::runtime_error);
  }
}

TEST_CASE("PointToFact: strict type checking", "[opt_mir][fact][type]") {
  auto na = PointToFact::not_applicable();
  auto top = PointToFact::top();
  auto p = PointToFact::singleton(Place::simple(SlotId{0}));
  auto bot = PointToFact::bottom();

  SECTION("NA meet NA is NA") {
    REQUIRE(PointToFact::meet(na, na).is_not_applicable());
  }

  SECTION("NA meet Valid throws") {
    REQUIRE_THROWS_AS(PointToFact::meet(na, top), std::runtime_error);
    REQUIRE_THROWS_AS(PointToFact::meet(top, na), std::runtime_error);
    REQUIRE_THROWS_AS(PointToFact::meet(na, p), std::runtime_error);
    REQUIRE_THROWS_AS(PointToFact::meet(p, na), std::runtime_error);
    REQUIRE_THROWS_AS(PointToFact::meet(na, bot), std::runtime_error);
  }

  SECTION("Equality checks with NA") {
    REQUIRE(na == na);
    REQUIRE_THROWS_AS(na == top, std::runtime_error);
    REQUIRE_THROWS_AS(p == na, std::runtime_error);
  }
}

TEST_CASE("NodeFact: initial_of respects types", "[opt_mir][fact][type]") {
  // Setup types
  auto i32 = get_type_id(type::PrimitiveKind::I32);
  auto ref_i32 = get_type_id(type::ReferenceType{i32, false});

  // Dummy struct
  type::StructType st{999}; // Just an ID
  auto struct_ty = get_type_id(st);

  SECTION("Primitive (Int) -> Const:Top, PointTo:NA") {
    auto fact = NodeFact::initial_of(i32);
    REQUIRE(fact.const_prop.is_top());
    REQUIRE(fact.point_to.is_not_applicable());
  }

  SECTION("Reference -> Const:NA, PointTo:Top") {
    auto fact = NodeFact::initial_of(ref_i32);
    REQUIRE(fact.const_prop.is_not_applicable());
    REQUIRE(fact.point_to.is_top());
  }

  SECTION("Struct -> Const:NA, PointTo:NA") {
    // Neither applicable
    auto fact = NodeFact::initial_of(struct_ty);
    REQUIRE(fact.const_prop.is_not_applicable());
    REQUIRE(fact.point_to.is_not_applicable());
  }
}

TEST_CASE("NodeFact: meet propagates checks", "[opt_mir][fact][type]") {
  auto i32 = get_type_id(type::PrimitiveKind::I32);
  auto ref_i32 = get_type_id(type::ReferenceType{i32, false});

  auto f_int = NodeFact::initial_of(i32);
  auto f_ref = NodeFact::initial_of(ref_i32);

  // Meet valid with valid (same type) -> OK
  auto f_int_2 = f_int;
  REQUIRE_NOTHROW(NodeFact::meet(f_int, f_int_2));

  // Meet Int with Ref -> Should Throw (mismatched NA states)
  // Int: Const=Top, Pt=NA
  // Ref: Const=NA, Pt=Top
  // Meet: Const(Top, NA) -> Throw, Pt(NA, Top) -> Throw
  REQUIRE_THROWS_AS(NodeFact::meet(f_int, f_ref), std::runtime_error);
}
