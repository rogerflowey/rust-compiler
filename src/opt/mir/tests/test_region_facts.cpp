#include <catch2/catch_test_macros.hpp>

#include "opt/mir/analysis/region_tree.hpp"
#include "opt/mir/analysis/world_state.hpp"
#include "opt/mir/ir/node_id.hpp"
#include "type/type.hpp"

using namespace opt::mir;

static type::TypeId i32_ty() {
  return type::get_typeID(type::Type{type::PrimitiveKind::I32});
}

static type::TypeId array_of(type::TypeId element, std::size_t size) {
  return type::get_typeID(type::Type{type::ArrayType{element, size}});
}

static type::TypeId make_struct(std::initializer_list<type::TypeId> fields) {
  type::StructInfo info;
  static std::size_t counter = 0;
  info.name = "test_struct_" + std::to_string(counter++);
  std::size_t index = 0;
  for (auto field_type : fields) {
    info.fields.push_back(type::StructFieldInfo{.name = "f" +
                                                         std::to_string(index++),
                                                 .type = field_type});
  }
  const auto sid =
      type::TypeContext::get_instance().register_struct(std::move(info));
  return type::get_typeID(type::Type{type::StructType{sid}});
}

static NodeFact i32_const_fact(std::uint64_t bits) {
  auto fact = NodeFact::initial_of(i32_ty());
  fact.const_prop = ConstPropFact::constant({ConstantValue::Kind::Int, bits});
  return fact;
}

// ============================================================================
// RegionTree Tests
// ============================================================================

TEST_CASE("RegionTree: basic write and read", "[opt_mir][region_tree]") {
  RegionTree tree;
  const auto root_type = make_struct({i32_ty()});

  auto c5 = i32_const_fact(5);

  // Write @root.f[0] = 5
  std::vector<Projection> path = {FieldProjection{0}};
  tree = tree.write(root_type, path, c5);

  // Read exact path
  // Note: RegionTree::read is not fully implemented in cpp (it's limited),
  // but we can check the structure manually or via WorldSnapshot tests.
  // Let's verify structure manually.

  auto &root = tree.root;
  REQUIRE(root.children.count(0));
  REQUIRE(root.children.at(0).exact_fact == c5);

  // Root should be Top (invalidated/unknown)
  REQUIRE(root.exact_fact == NodeFact::initial_of(root_type));
}

TEST_CASE("RegionTree: write_base sets mapping", "[opt_mir][region_tree]") {
  RegionTree tree;
  const auto root_type = make_struct({i32_ty()});
  SlotId src{1};

  // Write @root passes to src (Place)
  tree = tree.write_base(root_type, {}, Place::simple(src));

  REQUIRE(tree.root.base_mapping == Place::simple(src));
  REQUIRE(tree.root.children.empty());
}

// ... WorldSnapshot tests ...

TEST_CASE("WorldSnapshot: base mapping forwarding",
          "[opt_mir][world_snapshot]") {
  WorldSnapshot world;
  SlotId s_src{1};
  SlotId s_dst{2};
  const auto slot_struct = make_struct({i32_ty(), i32_ty()});
  std::vector<type::TypeId> slot_types(3, slot_struct);

  auto c42 = i32_const_fact(42);

  // Setup: src.f[1] = 42
  std::vector<Projection> p1 = {FieldProjection{1}};
  world = world.write(slot_types, s_src, p1, c42);

  // Setup: dst = memcpy(src)
  world = world.write_base(slot_types, s_dst, {}, Place::simple(s_src));

  // Verify: read dst.f[1] should resolve to src.f[1] -> 42
  auto res = world.read(slot_types, s_dst, p1);
  REQUIRE(res == c42);
}

TEST_CASE("WorldSnapshot: base mapping with offset (sub-field forwarding)",
          "[opt_mir][world_snapshot]") {
  WorldSnapshot world;
  SlotId s_src{1};
  SlotId s_dst{2};
  const auto inner = make_struct({i32_ty(), i32_ty(), i32_ty()});
  const auto src_type = make_struct({i32_ty(), inner});
  const auto dst_type = make_struct({inner, i32_ty()});
  std::vector<type::TypeId> slot_types(3, i32_ty());
  slot_types[raw(s_src)] = src_type;
  slot_types[raw(s_dst)] = dst_type;

  auto c99 = i32_const_fact(99);

  // Setup: src.g[1] = 99 (where g is field 1)
  std::vector<Projection> pg1 = {FieldProjection{1}};
  world = world.write(slot_types, s_src, pg1, c99);

  // Setup: dst.f[0] = src (whole slot)
  // maps dst.f[0] -> src
  std::vector<Projection> pf0 = {FieldProjection{0}};
  world = world.write_base(slot_types, s_dst, pf0, Place::simple(s_src));

  // Read dst.f[0].g[1] -> src.g[1] -> 99
  std::vector<Projection> pf0_g1 = {FieldProjection{0}, FieldProjection{1}};
  auto res = world.read(slot_types, s_dst, pf0_g1);
  REQUIRE(res == c99);
}

TEST_CASE("WorldSnapshot: complex sub-field mapping (dst.f = src.g)",
          "[opt_mir][world_snapshot]") {
  WorldSnapshot world;
  SlotId s_src{1};
  SlotId s_dst{2};
  const auto inner = make_struct({i32_ty(), i32_ty(), i32_ty()});
  const auto src_type = make_struct({i32_ty(), inner});
  const auto dst_type = make_struct({inner, i32_ty()});
  std::vector<type::TypeId> slot_types(3, i32_ty());
  slot_types[raw(s_src)] = src_type;
  slot_types[raw(s_dst)] = dst_type;

  auto c77 = i32_const_fact(77);

  // Setup: src.g[2] = 77 (nested in src.g)
  // Let's say src.g is field 1. Inside it, field 2 is the value.
  // src.f[1].f[2] = 77
  std::vector<Projection> p_src_path = {FieldProjection{1}, FieldProjection{2}};
  world = world.write(slot_types, s_src, p_src_path, c77);

  // Setup: dst.f[0] = src.g (i.e. src.f[1])
  // Place(src, [Field(1)])
  std::vector<Projection> p_dst_base = {FieldProjection{0}};
  Place src_place;
  src_place.base = s_src;
  src_place.projections = {FieldProjection{1}};

  world = world.write_base(slot_types, s_dst, p_dst_base, src_place);

  // Read: dst.f[0].f[2]
  // Should map to: src.f[1].f[2] -> 77
  std::vector<Projection> p_read = {FieldProjection{0}, FieldProjection{2}};
  auto res = world.read(slot_types, s_dst, p_read);
  REQUIRE(res == c77);
}

TEST_CASE("WorldSnapshot: recursive mapping chain",
          "[opt_mir][world_snapshot]") {
  WorldSnapshot world;
  SlotId s1{1}, s2{2}, s3{3};
  const auto root_type = make_struct({i32_ty()});
  std::vector<type::TypeId> slot_types(4, root_type);

  auto c7 = i32_const_fact(7);

  // s1.f[0] = 7
  std::vector<Projection> p0 = {FieldProjection{0}};
  world = world.write(slot_types, s1, p0, c7);

  // s2 = s1
  world = world.write_base(slot_types, s2, {}, Place::simple(s1));

  // s3 = s2
  world = world.write_base(slot_types, s3, {}, Place::simple(s2));

  // read s3.f[0] -> s2.f[0] -> s1.f[0] -> 7
  REQUIRE(world.read(slot_types, s3, p0) == c7);
}

TEST_CASE("WorldSnapshot: localized override of mapping",
          "[opt_mir][world_snapshot]") {
  WorldSnapshot world;
  SlotId s_src{1};
  SlotId s_dst{2};
  const auto root_type = make_struct({i32_ty(), i32_ty()});
  std::vector<type::TypeId> slot_types(3, root_type);

  auto c10 = i32_const_fact(10);
  auto c20 = i32_const_fact(20);

  std::vector<Projection> p0 = {FieldProjection{0}};
  std::vector<Projection> p1 = {FieldProjection{1}};

  // src.f[0] = 10
  // src.f[1] = 10
  world = world.write(slot_types, s_src, p0, c10);
  world = world.write(slot_types, s_src, p1, c10);

  // dst = src
  world = world.write_base(slot_types, s_dst, {}, Place::simple(s_src));

  // overwrite dst.f[1] = 20
  world = world.write(slot_types, s_dst, p1, c20);

  // dst.f[0] should still be 10 (via base)
  REQUIRE(world.read(slot_types, s_dst, p0) == c10);

  // dst.f[1] should be 20 (override)
  REQUIRE(world.read(slot_types, s_dst, p1) == c20);
}

TEST_CASE("WorldSnapshot: clobber with IndexProjection",
          "[opt_mir][world_snapshot]") {
  WorldSnapshot world;
  SlotId s{0};
  std::vector<type::TypeId> slot_types(
      1, array_of(i32_ty(), 4));

  auto c5 = i32_const_fact(5);

  // write to s[index] (unknown index) -> clobbers s
  std::vector<Projection> idx_path = {IndexProjection{NodeId{99}}};
  world = world.write(slot_types, s, idx_path,
                      NodeFact{ConstPropFact::bottom(),
                               PointToFact::not_applicable()});

  // Reading through unknown index remains conservative Bottom.
  auto res = world.read(slot_types, s, idx_path);
  REQUIRE(res.const_prop.is_bottom());
}
