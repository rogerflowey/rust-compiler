#include <gtest/gtest.h>
#include <stdexcept>
#include <optional>



#include "src/semantic/type/type.hpp"
#include "src/semantic/pass/semantic_check/type_compatibility.hpp"

/**
 * @brief Test suite for type compatibility helpers
 * 
 * This test suite verifies the behavior of critical helper functions in
 * type_compatibility.hpp that are essential for expression checking.
 * 
 * Test Scenarios:
 * 1. Inference Type Detection
 *    - Verifies is_inference_type() correctly identifies __ANYINT__ and __ANYUINT__
 *    - Ensures non-inference types are rejected
 * 
 * 2. Inference Type Coercion
 *    - Tests can_inference_coerce_to() with valid coercions (__ANYINT__ -> I32/ISIZE)
 *    - Tests can_inference_coerce_to() with valid coercions (__ANYUINT__ -> U32/USIZE/__ANYINT__)
 *    - Verifies invalid coercions are rejected
 * 
 * 3. Type Coercion (try_coerce_to)
 *    - Identical types should always succeed
 *    - Inference to concrete type coercion
 *    - Array type coercion with size matching and element compatibility
 *    - Reference type coercion with mutability rules
 *    - Invalid coercion attempts
 * 
 * 4. Common Type Finding (find_common_type)
 *    - Identical types return themselves
 *    - Inference placeholder resolution (__ANYUINT__ + __ANYINT__ -> __ANYINT__)
 *    - Array common types with compatible elements and matching sizes
 *    - Cases where no common type exists
 * 
 * 5. Assignment Compatibility (is_assignable_to)
 *    - Identical types are assignable
 *    - Coercible types are assignable
 *    - Non-coercible types are not assignable
 * 
 * 6. Cast Validation (is_castable_to)
 *    - Same types are always castable
 *    - All primitive types can be cast to each other
 *    - Reference type casting with underlying type compatibility
 *    - Array casting with element compatibility and size matching
 * 
 * 7. Type Comparability (are_comparable)
 *    - Identical types are comparable
 *    - Types with common types are comparable
 *    - Non-comparable type pairs
 * 
 * 8. Inference Type Resolution
 *    - resolve_inference_type() with valid target types
 *    - resolve_inference_type() throws on incompatible types
 *    - resolve_inference_if_needed() updates source type when needed
 *    - resolve_inference_if_needed() leaves non-inference types unchanged
 */

namespace {

// Bring all the needed symbols into scope for the test
using namespace semantic;

class TypeCompatibilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize common primitive types
        i32_type = get_typeID(Type{PrimitiveKind::I32});
        u32_type = get_typeID(Type{PrimitiveKind::U32});
        isize_type = get_typeID(Type{PrimitiveKind::ISIZE});
        usize_type = get_typeID(Type{PrimitiveKind::USIZE});
        bool_type = get_typeID(Type{PrimitiveKind::BOOL});
        char_type = get_typeID(Type{PrimitiveKind::CHAR});
        string_type = get_typeID(Type{PrimitiveKind::STRING});
        
        // Initialize inference types
        anyint_type = get_typeID(Type{PrimitiveKind::__ANYINT__});
        anyuint_type = get_typeID(Type{PrimitiveKind::__ANYUINT__});
        
        // Initialize array types
        i32_array_5_type = get_typeID(Type{ArrayType{i32_type, 5}});
        u32_array_5_type = get_typeID(Type{ArrayType{u32_type, 5}});
        i32_array_10_type = get_typeID(Type{ArrayType{i32_type, 10}});
        
        // Initialize reference types
        i32_ref_type = get_typeID(Type{ReferenceType{i32_type, false}});
        i32_mut_ref_type = get_typeID(Type{ReferenceType{i32_type, true}});
        u32_ref_type = get_typeID(Type{ReferenceType{u32_type, false}});
    }
    
    // Primitive types
    TypeId i32_type, u32_type, isize_type, usize_type;
    TypeId bool_type, char_type, string_type;
    
    // Inference types
    TypeId anyint_type, anyuint_type;
    
    // Array types
    TypeId i32_array_5_type, u32_array_5_type, i32_array_10_type;
    
    // Reference types
    TypeId i32_ref_type, i32_mut_ref_type, u32_ref_type;
};

// Test 1: Inference Type Detection
TEST_F(TypeCompatibilityTest, IsInferenceType) {
    // Positive cases
    EXPECT_TRUE(is_inference_type(anyint_type));
    EXPECT_TRUE(is_inference_type(anyuint_type));
    
    // Negative cases
    EXPECT_FALSE(is_inference_type(i32_type));
    EXPECT_FALSE(is_inference_type(u32_type));
    EXPECT_FALSE(is_inference_type(bool_type));
    EXPECT_FALSE(is_inference_type(i32_array_5_type));
    EXPECT_FALSE(is_inference_type(i32_ref_type));
}

// Test 2: Inference Type Coercion
TEST_F(TypeCompatibilityTest, CanInferenceCoerceTo) {
    // __ANYINT__ coercion
    EXPECT_TRUE(can_inference_coerce_to(PrimitiveKind::__ANYINT__, PrimitiveKind::I32));
    EXPECT_TRUE(can_inference_coerce_to(PrimitiveKind::__ANYINT__, PrimitiveKind::ISIZE));
    EXPECT_FALSE(can_inference_coerce_to(PrimitiveKind::__ANYINT__, PrimitiveKind::U32));
    EXPECT_FALSE(can_inference_coerce_to(PrimitiveKind::__ANYINT__, PrimitiveKind::USIZE));
    EXPECT_FALSE(can_inference_coerce_to(PrimitiveKind::__ANYINT__, PrimitiveKind::BOOL));
    
    // __ANYUINT__ coercion
    EXPECT_TRUE(can_inference_coerce_to(PrimitiveKind::__ANYUINT__, PrimitiveKind::U32));
    EXPECT_TRUE(can_inference_coerce_to(PrimitiveKind::__ANYUINT__, PrimitiveKind::USIZE));
    EXPECT_TRUE(can_inference_coerce_to(PrimitiveKind::__ANYUINT__, PrimitiveKind::__ANYINT__));
    EXPECT_TRUE(can_inference_coerce_to(PrimitiveKind::__ANYUINT__, PrimitiveKind::I32));
    EXPECT_TRUE(can_inference_coerce_to(PrimitiveKind::__ANYUINT__, PrimitiveKind::ISIZE));
    EXPECT_FALSE(can_inference_coerce_to(PrimitiveKind::__ANYUINT__, PrimitiveKind::BOOL));
    
    // Non-inference types
    EXPECT_FALSE(can_inference_coerce_to(PrimitiveKind::I32, PrimitiveKind::U32));
    EXPECT_FALSE(can_inference_coerce_to(PrimitiveKind::U32, PrimitiveKind::I32));
}

// Test 3: Type Coercion (try_coerce_to)
TEST_F(TypeCompatibilityTest, TryCoerceTo) {
    // Identical types
    EXPECT_EQ(try_coerce_to(i32_type, i32_type), i32_type);
    EXPECT_EQ(try_coerce_to(anyint_type, anyint_type), anyint_type);
    
    // Inference to concrete
    EXPECT_EQ(try_coerce_to(anyint_type, i32_type), i32_type);
    EXPECT_EQ(try_coerce_to(anyint_type, isize_type), isize_type);
    EXPECT_EQ(try_coerce_to(anyuint_type, u32_type), u32_type);
    EXPECT_EQ(try_coerce_to(anyuint_type, usize_type), usize_type);
    EXPECT_EQ(try_coerce_to(anyuint_type, anyint_type), anyint_type);
    
    // Invalid inference coercion
    EXPECT_EQ(try_coerce_to(anyint_type, u32_type), std::nullopt);
    EXPECT_EQ(try_coerce_to(anyuint_type, i32_type), i32_type);
    
    // Array coercion - same size, compatible elements
    EXPECT_EQ(try_coerce_to(i32_array_5_type, i32_array_5_type), i32_array_5_type);
    EXPECT_EQ(try_coerce_to(i32_array_5_type, u32_array_5_type), std::nullopt); // Different element types
    
    // Array coercion - different sizes
    EXPECT_EQ(try_coerce_to(i32_array_5_type, i32_array_10_type), std::nullopt);
    
    // Reference coercion - same mutability
    EXPECT_EQ(try_coerce_to(i32_ref_type, i32_ref_type), i32_ref_type);
    
    // Reference coercion - mutable to immutable succeeds (implementation allows this)
    EXPECT_EQ(try_coerce_to(i32_mut_ref_type, i32_ref_type), i32_ref_type);
    
    // Reference coercion - immutable to mutable fails (implementation doesn't allow this)
    EXPECT_EQ(try_coerce_to(i32_ref_type, i32_mut_ref_type), std::nullopt);
    
    // Different reference types
    EXPECT_EQ(try_coerce_to(i32_ref_type, u32_ref_type), std::nullopt);
}

// Test 4: Common Type Finding (find_common_type)
TEST_F(TypeCompatibilityTest, FindCommonType) {
    // Identical types
    EXPECT_EQ(find_common_type(i32_type, i32_type), i32_type);
    EXPECT_EQ(find_common_type(anyint_type, anyint_type), anyint_type);
    
    // Inference placeholder resolution
    EXPECT_EQ(find_common_type(anyuint_type, anyint_type), anyint_type);
    EXPECT_EQ(find_common_type(anyint_type, anyuint_type), anyint_type);
    
    // Coercible types
    EXPECT_EQ(find_common_type(anyint_type, i32_type), i32_type);
    EXPECT_EQ(find_common_type(anyuint_type, u32_type), u32_type);
    
    // Non-coercible primitive types
    EXPECT_EQ(find_common_type(i32_type, u32_type), std::nullopt);
    EXPECT_EQ(find_common_type(i32_type, bool_type), std::nullopt);
    
    // Array common types
    EXPECT_EQ(find_common_type(i32_array_5_type, i32_array_5_type), i32_array_5_type);
    EXPECT_EQ(find_common_type(i32_array_5_type, u32_array_5_type), std::nullopt); // Different elements
    EXPECT_EQ(find_common_type(i32_array_5_type, i32_array_10_type), std::nullopt); // Different sizes
}

// Test 5: Assignment Compatibility (is_assignable_to)
TEST_F(TypeCompatibilityTest, IsAssignableFrom) {
    // Identical types
    EXPECT_TRUE(is_assignable_to(i32_type, i32_type));
    EXPECT_TRUE(is_assignable_to(anyint_type, anyint_type));
    
    // Coercible types
    EXPECT_TRUE(is_assignable_to(anyint_type, i32_type));
    EXPECT_TRUE(is_assignable_to(anyuint_type, u32_type));
    
    // Non-coercible types
    EXPECT_FALSE(is_assignable_to(i32_type, u32_type));
    EXPECT_FALSE(is_assignable_to(anyint_type, u32_type));
    
    // Array types
    EXPECT_TRUE(is_assignable_to(i32_array_5_type, i32_array_5_type));
    EXPECT_FALSE(is_assignable_to(i32_array_5_type, u32_array_5_type));
    EXPECT_FALSE(is_assignable_to(i32_array_5_type, i32_array_10_type));
    
    // Reference types
    EXPECT_TRUE(is_assignable_to(i32_ref_type, i32_ref_type));
    EXPECT_FALSE(is_assignable_to(i32_ref_type, i32_mut_ref_type)); // immutable to mutable fails
    EXPECT_TRUE(is_assignable_to(i32_mut_ref_type, i32_ref_type)); // mutable to immutable succeeds
}

// Test 6: Cast Validation (is_castable_to)
TEST_F(TypeCompatibilityTest, IsCastableTo) {
    // Same types
    EXPECT_TRUE(is_castable_to(i32_type, i32_type));
    EXPECT_TRUE(is_castable_to(anyint_type, anyint_type));
    
    // All primitive types can be cast to each other
    EXPECT_TRUE(is_castable_to(i32_type, u32_type));
    EXPECT_TRUE(is_castable_to(u32_type, i32_type));
    EXPECT_TRUE(is_castable_to(i32_type, bool_type));
    EXPECT_TRUE(is_castable_to(bool_type, i32_type));
    EXPECT_TRUE(is_castable_to(anyint_type, u32_type));
    EXPECT_TRUE(is_castable_to(anyuint_type, i32_type));
    
    // Reference types
    EXPECT_TRUE(is_castable_to(i32_ref_type, i32_ref_type));
    EXPECT_TRUE(is_castable_to(i32_ref_type, u32_ref_type)); // Different underlying types
    EXPECT_TRUE(is_castable_to(i32_mut_ref_type, i32_ref_type));
    EXPECT_TRUE(is_castable_to(i32_ref_type, i32_mut_ref_type));
    
    // Array types
    EXPECT_TRUE(is_castable_to(i32_array_5_type, i32_array_5_type));
    EXPECT_TRUE(is_castable_to(i32_array_5_type, u32_array_5_type)); // Different element types
    EXPECT_FALSE(is_castable_to(i32_array_5_type, i32_array_10_type)); // Different sizes
    
    // Mixed types
    EXPECT_FALSE(is_castable_to(i32_type, i32_ref_type));
    EXPECT_FALSE(is_castable_to(i32_ref_type, i32_type));
    EXPECT_FALSE(is_castable_to(i32_type, i32_array_5_type));
    EXPECT_FALSE(is_castable_to(i32_array_5_type, i32_type));
}

// Test 7: Type Comparability (are_comparable)
TEST_F(TypeCompatibilityTest, AreComparable) {
    // Identical types
    EXPECT_TRUE(are_comparable(i32_type, i32_type));
    EXPECT_TRUE(are_comparable(anyint_type, anyint_type));
    
    // Types with common types
    EXPECT_TRUE(are_comparable(anyint_type, i32_type));
    EXPECT_TRUE(are_comparable(anyuint_type, u32_type));
    EXPECT_TRUE(are_comparable(anyuint_type, anyint_type));
    
    // Non-comparable types
    EXPECT_FALSE(are_comparable(i32_type, u32_type));
    EXPECT_FALSE(are_comparable(i32_type, bool_type));
    EXPECT_FALSE(are_comparable(anyint_type, u32_type));
    
    // Array types
    EXPECT_TRUE(are_comparable(i32_array_5_type, i32_array_5_type));
    EXPECT_FALSE(are_comparable(i32_array_5_type, u32_array_5_type));
    EXPECT_FALSE(are_comparable(i32_array_5_type, i32_array_10_type));
}

// Test 8: Inference Type Resolution
TEST_F(TypeCompatibilityTest, ResolveInferenceType) {
    // Valid resolutions
    EXPECT_EQ(resolve_inference_type(anyint_type, i32_type), i32_type);
    EXPECT_EQ(resolve_inference_type(anyint_type, isize_type), isize_type);
    EXPECT_EQ(resolve_inference_type(anyuint_type, u32_type), u32_type);
    EXPECT_EQ(resolve_inference_type(anyuint_type, usize_type), usize_type);
    // This should work since __ANYUINT__ can coerce to __ANYINT__
    EXPECT_EQ(resolve_inference_type(anyuint_type, anyint_type), anyint_type);
    
    // Invalid resolutions should throw
    EXPECT_THROW(resolve_inference_type(anyint_type, u32_type), std::logic_error);
    EXPECT_THROW(resolve_inference_type(anyint_type, bool_type), std::logic_error);
}

TEST_F(TypeCompatibilityTest, ResolveInferenceIfNeeded) {
    // Resolution should happen when source is inference type
    TypeId source = anyint_type;
    resolve_inference_if_needed(source, i32_type);
    EXPECT_EQ(source, i32_type);
    
    source = anyuint_type;
    resolve_inference_if_needed(source, u32_type);
    EXPECT_EQ(source, u32_type);
    
    source = anyuint_type;
    resolve_inference_if_needed(source, anyint_type);
    EXPECT_EQ(source, anyint_type); // anyuint can coerce to anyint, so source becomes anyint_type
    
    // Non-inference types should remain unchanged
    source = i32_type;
    resolve_inference_if_needed(source, u32_type);
    EXPECT_EQ(source, i32_type);
    
    source = u32_type;
    resolve_inference_if_needed(source, i32_type);
    EXPECT_EQ(source, u32_type);
    
    // Array types should remain unchanged (not primitive)
    source = i32_array_5_type;
    resolve_inference_if_needed(source, u32_array_5_type);
    EXPECT_EQ(source, i32_array_5_type);
}

} // anonymous namespace