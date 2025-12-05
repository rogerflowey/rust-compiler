#include "mir/codegen/type.hpp"

#include "tests/catch_gtest_compat.hpp"
#include "type/type.hpp"

#include <initializer_list>
#include <string>

namespace {

type::TypeId make_struct_type(const std::string& name,
                              std::initializer_list<type::StructFieldInfo> fields) {
    type::StructInfo info;
    info.name = name;
    info.fields.assign(fields.begin(), fields.end());
    auto& ctx = type::TypeContext::get_instance();
    type::StructId id = ctx.register_struct(info);
    return type::get_typeID(type::Type{type::StructType{.id = id}});
}

} // namespace

TEST(TypeEmitterTest, ResolvesPrimitiveReferenceAndArrayTypes) {
    codegen::TypeEmitter emitter;

    type::TypeId int_type = type::get_typeID(type::Type{type::PrimitiveKind::I32});
    EXPECT_EQ(emitter.get_type_name(int_type), "i32");

    type::ReferenceType ref_int{.referenced_type = int_type, .is_mutable = false};
    type::TypeId ref_type = type::get_typeID(type::Type{ref_int});
    EXPECT_EQ(emitter.get_type_name(ref_type), "i32*");

    type::ArrayType array_int{.element_type = int_type, .size = 4};
    type::TypeId array_type = type::get_typeID(type::Type{array_int});
    EXPECT_EQ(emitter.get_type_name(array_type), "[4 x i32]");
}

TEST(TypeEmitterTest, EmitsUnitTypeAsEmptyStruct) {
    codegen::TypeEmitter emitter;

    type::TypeId unit_type = type::get_typeID(type::Type{type::UnitType{}});
    EXPECT_EQ(emitter.get_type_name(unit_type), "%__rc_unit");
    const auto& defs = emitter.struct_definitions();
    ASSERT_EQ(defs.size(), 1u);
    EXPECT_EQ(defs.front().first, "__rc_unit");
    EXPECT_EQ(defs.front().second, "{}");
    EXPECT_EQ(emitter.get_type_name(unit_type), "%__rc_unit");
}

TEST(TypeEmitterTest, EmitsNamedStructDefinitionOnce) {
    type::TypeId int_type = type::get_typeID(type::Type{type::PrimitiveKind::I32});
    type::TypeId bool_type = type::get_typeID(type::Type{type::PrimitiveKind::BOOL});

    type::TypeId point_type = make_struct_type("Point", {
        {.name = "x", .type = int_type},
        {.name = "is_valid", .type = bool_type},
    });

    codegen::TypeEmitter emitter;
    std::string llvm_type = emitter.emit_struct_definition(point_type);
    EXPECT_EQ(llvm_type, "%Point");

    // Second call returns cached result and does not add duplicates.
    EXPECT_EQ(emitter.emit_struct_definition(point_type), "%Point");

    const auto& defs = emitter.struct_definitions();
    ASSERT_EQ(defs.size(), 1u);
    EXPECT_EQ(defs.front().first, "Point");
    EXPECT_EQ(defs.front().second, "{ i32, i1 }");
}

TEST(TypeEmitterTest, AssignsAnonymousStructName) {
    type::TypeId int_type = type::get_typeID(type::Type{type::PrimitiveKind::I32});
    type::TypeId anon_type = make_struct_type("", {
        {.name = "value", .type = int_type},
    });

    codegen::TypeEmitter emitter;
    std::string llvm_type = emitter.emit_struct_definition(anon_type);
    ASSERT_FALSE(llvm_type.empty());
    EXPECT_EQ(llvm_type.front(), '%');

    const auto& defs = emitter.struct_definitions();
    ASSERT_EQ(defs.size(), 1u);
    EXPECT_FALSE(defs.front().first.empty());
    EXPECT_NE(defs.front().first.find("anon.struct."), std::string::npos);
}
