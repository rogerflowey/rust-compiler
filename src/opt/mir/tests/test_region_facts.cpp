#include <catch2/catch_test_macros.hpp>

#include "opt/mir/analysis/region_tree.hpp"
#include "opt/mir/analysis/world_state.hpp"
#include "opt/mir/ir/node_id.hpp"

using namespace opt::mir;

// ============================================================================
// RegionTree Tests
// ============================================================================

TEST_CASE("RegionTree: basic write and read", "[opt_mir][region_tree]") {
  RegionTree tree;

  auto c5 = NodeFact{ConstPropFact::constant({ConstantValue::Kind::Int, 5})};

  // Write @root.f[0] = 5
  std::vector<Projection> path = {FieldProjection{0}};
  tree = tree.write(path, c5);

  // Read exact path
  // Note: RegionTree::read is not fully implemented in cpp (it's limited),
  // but we can check the structure manually or via WorldSnapshot tests.
  // Let's verify structure manually.

  auto &root = tree.root;
  REQUIRE(root.children.count(0));
  REQUIRE(root.children.at(0).exact_fact == c5);

  // Root should be Top (invalidated/unknown)
  REQUIRE(root.exact_fact.const_prop.is_top());
}

TEST_CASE("RegionTree: write_base sets mapping", "[opt_mir][region_tree]") {
  RegionTree tree;
  SlotId src{1};

  // Write @root passes to src (Place)
  tree = tree.write_base({}, Place::simple(src));

  REQUIRE(tree.root.base_mapping == Place::simple(src));
  REQUIRE(tree.root.children.empty());
}

// ... WorldSnapshot tests ...

TEST_CASE("WorldSnapshot: base mapping forwarding",
          "[opt_mir][world_snapshot]") {
  WorldSnapshot world;
  SlotId s_src{1};
  SlotId s_dst{2};

  auto c42 = NodeFact{ConstPropFact::constant({ConstantValue::Kind::Int, 42})};

  // Setup: src.f[1] = 42
  std::vector<Projection> p1 = {FieldProjection{1}};
  world = world.write(s_src, p1, c42);

  // Setup: dst = memcpy(src)
  world = world.write_base(s_dst, {}, Place::simple(s_src));

  // Verify: read dst.f[1] should resolve to src.f[1] -> 42
  auto res = world.read(s_dst, p1);
  REQUIRE(res == c42);
}

TEST_CASE("WorldSnapshot: base mapping with offset (sub-field forwarding)",
          "[opt_mir][world_snapshot]") {
  WorldSnapshot world;
  SlotId s_src{1};
  SlotId s_dst{2};

  auto c99 = NodeFact{ConstPropFact::constant({ConstantValue::Kind::Int, 99})};

  // Setup: src.g[1] = 99 (where g is field 1)
  std::vector<Projection> pg1 = {FieldProjection{1}};
  world = world.write(s_src, pg1, c99);

  // Setup: dst.f[0] = src (whole slot)
  // maps dst.f[0] -> src
  std::vector<Projection> pf0 = {FieldProjection{0}};
  world = world.write_base(s_dst, pf0, Place::simple(s_src));

  // Read dst.f[0].g[1] -> src.g[1] -> 99
  std::vector<Projection> pf0_g1 = {FieldProjection{0}, FieldProjection{1}};
  auto res = world.read(s_dst, pf0_g1);
  REQUIRE(res == c99);
}

TEST_CASE("WorldSnapshot: complex sub-field mapping (dst.f = src.g)",
          "[opt_mir][world_snapshot]") {
  WorldSnapshot world;
  SlotId s_src{1};
  SlotId s_dst{2};

  auto c77 = NodeFact{ConstPropFact::constant({ConstantValue::Kind::Int, 77})};

  // Setup: src.g[2] = 77 (nested in src.g)
  // Let's say src.g is field 1. Inside it, field 2 is the value.
  // src.f[1].f[2] = 77
  std::vector<Projection> p_src_path = {FieldProjection{1}, FieldProjection{2}};
  world = world.write(s_src, p_src_path, c77);

  // Setup: dst.f[0] = src.g (i.e. src.f[1])
  // Place(src, [Field(1)])
  std::vector<Projection> p_dst_base = {FieldProjection{0}};
  Place src_place;
  src_place.base = s_src;
  src_place.projections = {FieldProjection{1}};

  world = world.write_base(s_dst, p_dst_base, src_place);

  // Read: dst.f[0].f[2]
  // Should map to: src.f[1].f[2] -> 77
  std::vector<Projection> p_read = {FieldProjection{0}, FieldProjection{2}};
  auto res = world.read(s_dst, p_read);
  REQUIRE(res == c77);
}

TEST_CASE("WorldSnapshot: recursive mapping chain",
          "[opt_mir][world_snapshot]") {
  WorldSnapshot world;
  SlotId s1{1}, s2{2}, s3{3};

  auto c7 = NodeFact{ConstPropFact::constant({ConstantValue::Kind::Int, 7})};

  // s1.f[0] = 7
  std::vector<Projection> p0 = {FieldProjection{0}};
  world = world.write(s1, p0, c7);

  // s2 = s1
  world = world.write_base(s2, {}, Place::simple(s1));

  // s3 = s2
  world = world.write_base(s3, {}, Place::simple(s2));

  // read s3.f[0] -> s2.f[0] -> s1.f[0] -> 7
  REQUIRE(world.read(s3, p0) == c7);
}

TEST_CASE("WorldSnapshot: localized override of mapping",
          "[opt_mir][world_snapshot]") {
  WorldSnapshot world;
  SlotId s_src{1};
  SlotId s_dst{2};

  auto c10 = NodeFact{ConstPropFact::constant({ConstantValue::Kind::Int, 10})};
  auto c20 = NodeFact{ConstPropFact::constant({ConstantValue::Kind::Int, 20})};

  std::vector<Projection> p0 = {FieldProjection{0}};
  std::vector<Projection> p1 = {FieldProjection{1}};

  // src.f[0] = 10
  // src.f[1] = 10
  world = world.write(s_src, p0, c10);
  world = world.write(s_src, p1, c10);

  // dst = src
  world = world.write_base(s_dst, {}, Place::simple(s_src));

  // overwrite dst.f[1] = 20
  world = world.write(s_dst, p1, c20);

  // dst.f[0] should still be 10 (via base)
  REQUIRE(world.read(s_dst, p0) == c10);

  // dst.f[1] should be 20 (override)
  REQUIRE(world.read(s_dst, p1) == c20);
}

TEST_CASE("WorldSnapshot: clobber with IndexProjection",
          "[opt_mir][world_snapshot]") {
  WorldSnapshot world;
  SlotId s{0};

  auto c5 = NodeFact{ConstPropFact::constant({ConstantValue::Kind::Int, 5})};

  // s.f[0] = 5
  std::vector<Projection> p0 = {FieldProjection{0}};
  world = world.write(s, p0, c5);

  // write to s[index] (unknown index) -> clobbers s
  std::vector<Projection> idx_path = {IndexProjection{NodeId{99}}};
  world = world.write(s, idx_path, NodeFact{ConstPropFact::bottom()});

  // s.f[0] should now be Bottom (inherited from parent Bottom)
  // This verifies the monotonicity fix: clobbering a parent with Bottom
  // must propagate Bottom to its children/fields.
  auto res = world.read(s, p0);
  REQUIRE(res.const_prop.is_bottom());
}
