#include "tests/catch_gtest_compat.hpp"
#include "type/type.hpp"
#include "type/helper.hpp"
#include "semantic/pass/semantic_check/type_compatibility.hpp"
#include "semantic/tests/helpers/common.hpp"

using namespace semantic;
using namespace semantic::helper::type_helper;
using namespace test::helpers;

namespace {

class TypeCompatibilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize common types for testing
        i32_type = get_typeID(SemanticType{PrimitiveKind::I32});
        u32_type = get_typeID(SemanticType{PrimitiveKind::U32});
        isize_type = get_typeID(SemanticType{PrimitiveKind::ISIZE});
        usize_type = get_typeID(SemanticType{PrimitiveKind::USIZE});
        bool_type = get_typeID(SemanticType{PrimitiveKind::BOOL});
        char_type = get_typeID(SemanticType{PrimitiveKind::CHAR});
        string_type = get_typeID(SemanticType{PrimitiveKind::STRING});
        
        // Never type
        never_type = get_typeID(SemanticType{NeverType{}});
        
        // Unit type
        unit_type = get_typeID(SemanticType{semantic::UnitType{}});
        
        // Array types
        i32_array_5_type = get_typeID(SemanticType{semantic::ArrayType{i32_type, 5}});
        u32_array_5_type = get_typeID(SemanticType{semantic::ArrayType{u32_type, 5}});
        i32_array_10_type = get_typeID(SemanticType{semantic::ArrayType{i32_type, 10}});
        i32_array_8_type = get_typeID(SemanticType{semantic::ArrayType{i32_type, 8}});
        
        // Reference types
        i32_ref_type = get_typeID(SemanticType{semantic::ReferenceType{i32_type, false}});
        i32_mut_ref_type = get_typeID(SemanticType{semantic::ReferenceType{i32_type, true}});
        u32_ref_type = get_typeID(SemanticType{semantic::ReferenceType{u32_type, false}});
    }
    
    // Common test types
    TypeId i32_type, u32_type, isize_type, usize_type;
    TypeId bool_type, char_type, string_type;
    TypeId never_type, unit_type;
    TypeId i32_array_5_type, u32_array_5_type, i32_array_10_type;
    TypeId i32_array_8_type;
    TypeId i32_ref_type, i32_mut_ref_type, u32_ref_type;
};

// Test basic type compatibility
TEST_F(TypeCompatibilityTest, BasicTypeCompatibility) {
    // Identical types should be assignable
    EXPECT_TRUE(is_assignable_to(i32_type, i32_type));
    EXPECT_TRUE(is_assignable_to(u32_type, u32_type));
    EXPECT_TRUE(is_assignable_to(bool_type, bool_type));
    
    // Different primitive types should not be assignable
    EXPECT_FALSE(is_assignable_to(i32_type, u32_type));
    EXPECT_FALSE(is_assignable_to(i32_type, bool_type));
    EXPECT_FALSE(is_assignable_to(u32_type, bool_type));
}

// Test array type compatibility
TEST_F(TypeCompatibilityTest, ArrayTypeCompatibility) {
    // Same element type and size should be assignable
    EXPECT_TRUE(is_assignable_to(i32_array_5_type, i32_array_5_type));
    
    // Different sizes should not be assignable
    EXPECT_FALSE(is_assignable_to(i32_array_5_type, i32_array_10_type));
    
    // Incompatible element types should not be assignable
    EXPECT_FALSE(is_assignable_to(i32_array_5_type, u32_array_5_type));
}

// Test reference type compatibility
TEST_F(TypeCompatibilityTest, ReferenceTypeCompatibility) {
    // Same reference types should be assignable
    EXPECT_TRUE(is_assignable_to(i32_ref_type, i32_ref_type));
    
    // Immutable to mutable reference should not be assignable
    EXPECT_FALSE(is_assignable_to(i32_ref_type, i32_mut_ref_type));
    
    // Mutable to immutable reference should be assignable
    EXPECT_TRUE(is_assignable_to(i32_mut_ref_type, i32_ref_type));
    
    // Different base types should not be assignable
    EXPECT_FALSE(is_assignable_to(i32_ref_type, u32_ref_type));
}

// Test common type finding
TEST_F(TypeCompatibilityTest, CommonTypeFinding) {
    // Identical types should have common type
    auto common = find_common_type(i32_type, i32_type);
    ASSERT_TRUE(common.has_value());
    EXPECT_EQ(*common, i32_type);
    
    // Incompatible types should not have common type
    common = find_common_type(i32_type, u32_type);
    EXPECT_FALSE(common.has_value());
    
    common = find_common_type(i32_type, bool_type);
    EXPECT_FALSE(common.has_value());
}

// Test array common type finding
TEST_F(TypeCompatibilityTest, ArrayCommonTypeFinding) {
    // Same array types should have common type
    auto common = find_common_type(i32_array_5_type, i32_array_5_type);
    ASSERT_TRUE(common.has_value());
    EXPECT_EQ(*common, i32_array_5_type);
    
    // Different sizes should not have common type
    common = find_common_type(i32_array_5_type, i32_array_10_type);
    EXPECT_FALSE(common.has_value());
}

// Test type comparability
TEST_F(TypeCompatibilityTest, TypeComparability) {
    // Identical types should be comparable
    EXPECT_TRUE(are_comparable(i32_type, i32_type));
    EXPECT_TRUE(are_comparable(u32_type, u32_type));
    EXPECT_TRUE(are_comparable(bool_type, bool_type));
    
    // Incompatible types should not be comparable
    EXPECT_FALSE(are_comparable(i32_type, u32_type));
    EXPECT_FALSE(are_comparable(i32_type, bool_type));
}

// Test castability
TEST_F(TypeCompatibilityTest, TypeCastability) {
    // Same types should be castable
    EXPECT_TRUE(is_castable_to(i32_type, i32_type));
    EXPECT_TRUE(is_castable_to(u32_type, u32_type));
    
    // All primitive types should be castable to each other
    EXPECT_TRUE(is_castable_to(i32_type, u32_type));
    EXPECT_TRUE(is_castable_to(i32_type, bool_type));
    EXPECT_TRUE(is_castable_to(bool_type, i32_type));
    
    // Array types with same size and castable elements should be castable
    EXPECT_TRUE(is_castable_to(i32_array_5_type, u32_array_5_type));
    
    // Array types with different sizes should not be castable
    EXPECT_FALSE(is_castable_to(i32_array_5_type, i32_array_10_type));
}

// Test NeverType behavior
TEST_F(TypeCompatibilityTest, NeverTypeBehavior) {
    // Test is_never_type helper function
    EXPECT_TRUE(semantic::helper::type_helper::is_never_type(never_type));
    EXPECT_FALSE(semantic::helper::type_helper::is_never_type(i32_type));
    EXPECT_FALSE(semantic::helper::type_helper::is_never_type(unit_type));
    EXPECT_FALSE(semantic::helper::type_helper::is_never_type(i32_array_5_type));
    EXPECT_FALSE(semantic::helper::type_helper::is_never_type(i32_ref_type));
    
    // NeverType should coerce to any type
    EXPECT_TRUE(is_assignable_to(never_type, i32_type));
    EXPECT_TRUE(is_assignable_to(never_type, u32_type));
    EXPECT_TRUE(is_assignable_to(never_type, bool_type));
    EXPECT_TRUE(is_assignable_to(never_type, unit_type));
    EXPECT_TRUE(is_assignable_to(never_type, i32_array_5_type));
    EXPECT_TRUE(is_assignable_to(never_type, i32_ref_type));
    
    // But other types should not coerce to NeverType
    EXPECT_FALSE(is_assignable_to(i32_type, never_type));
    EXPECT_FALSE(is_assignable_to(unit_type, never_type));
    
    // NeverType should find common type with any type (returning the other type)
    auto common = find_common_type(never_type, i32_type);
    ASSERT_TRUE(common.has_value());
    EXPECT_EQ(*common, i32_type);
    
    common = find_common_type(u32_type, never_type);
    ASSERT_TRUE(common.has_value());
    EXPECT_EQ(*common, u32_type);
    
    common = find_common_type(never_type, never_type);
    ASSERT_TRUE(common.has_value());
    EXPECT_EQ(*common, never_type);
    
    // NeverType should be comparable with any type
    EXPECT_TRUE(are_comparable(never_type, i32_type));
    EXPECT_TRUE(are_comparable(u32_type, never_type));
    EXPECT_TRUE(are_comparable(never_type, never_type));
    
    // NeverType should be castable to any type
    EXPECT_TRUE(is_castable_to(never_type, i32_type));
    EXPECT_TRUE(is_castable_to(never_type, u32_type));
    EXPECT_TRUE(is_castable_to(never_type, bool_type));
    EXPECT_TRUE(is_castable_to(never_type, unit_type));
}

} // namespace