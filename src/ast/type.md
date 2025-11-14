# AST Type Reference

## Type Node Architecture

Type nodes represent compile-time type specifications and annotations within the AST. The type system uses a variant-based design that enables type-safe operations while supporting complex type constructs like function types, tuple types, and parameterized types.

## Critical Design Decisions

### Variant-Based Type Representation

```cpp
using Type = std::variant<
    // Basic types
    BoolType, IntType, FloatType, CharType, StringType,
    // Composite types
    TupleType, ArrayType, SliceType,
    // Function types
    FunctionType,
    // User-defined types
    StructType, EnumType, UnionType,
    // Generic types
    GenericType, TraitType,
    // Type parameters and references
    TypeParamType, ReferenceType, PointerType,
    // Special types
    VoidType, NeverType, SelfType,
    // Error handling
    ErrorType
>;
```

**Design Rationale**: The variant approach provides:
- **Type Safety**: Compile-time guarantee of handling all type variants
- **Memory Efficiency**: Single allocation for any type representation
- **Pattern Matching**: Modern C++ visitation for type operations
- **Extensibility**: Easy addition of new type constructs

**Trade-off**: Requires visitor pattern for type operations, but eliminates dynamic dispatch overhead.

### Basic Type Hierarchy

```cpp
struct BoolType { /* No additional data */ };
struct IntType { 
    std::optional<size_t> bit_width;  // 8, 16, 32, 64, 128
    bool is_signed;
};
struct FloatType { 
    std::optional<size_t> bit_width;  // 32, 64
};
struct CharType { /* Unicode character type */ };
struct StringType { /* String literal type */ };
```

**Primitive Type Strategy**: Basic types provide:
- **Minimal Overhead**: Empty structs for simple types
- **Optional Parameters**: Bit width and signedness for numeric types
- **Semantic Clarity**: Distinct types for each primitive category
- **Extension Points**: Optional parameters enable type refinements

**Use Cases**: `bool`, `i32`, `u64`, `f32`, `char`, `str` with optional bit width specifications.

### Composite Type Design

```cpp
struct TupleType {
    std::vector<TypePtr> elements;
};

struct ArrayType {
    TypePtr element_type;
    std::optional<size_t> size;  // None for unsized arrays
};

struct SliceType {
    TypePtr element_type;
};
```

**Complex Type Construction**: Composite types enable:
- **Tuple Types**: Heterogeneous collections `(i32, bool, String)`
- **Array Types**: Fixed-size homogeneous collections `[i32; 5]`
- **Slice Types**: Dynamic view types `&[i32]` or `&str`
- **Nesting Support**: Types can contain other types recursively

**Size Handling**: Arrays support optional size for both fixed `[T; N]` and unsized `[T]` variants.

### Function Type Architecture

```cpp
struct FunctionType {
    std::vector<TypePtr> parameters;
    std::optional<TypePtr> return_type;
    bool is_variadic;
};
```

**Function Signature Modeling**: Function types capture:
- **Parameter Types**: Ordered list of parameter type specifications
- **Return Types**: Optional return type (void functions have none)
- **Variadic Support**: Flag for variable argument functions
- **Higher-Order Functions**: Functions can take/return other functions

**Type Safety**: Function types enable proper type checking of function calls and assignments.

### User-Defined Type Support

```cpp
struct StructType {
    Identifier name;
    std::vector<StructField> fields;
    std::vector<TypePtr> type_parameters;  // Generic parameters
};

struct EnumType {
    Identifier name;
    std::vector<EnumVariant> variants;
    std::vector<TypePtr> type_parameters;  // Generic parameters
};

struct UnionType {
    Identifier name;
    std::vector<TypePtr> variants;
    std::vector<TypePtr> type_parameters;  // Generic parameters
};
```

**Custom Type Definitions**: User-defined types support:
- **Named Types**: Struct, enum, and union definitions with identifiers
- **Field Specifications**: Struct fields with names and types
- **Variant Definitions**: Enum variants with optional data types
- **Generic Parameters**: Type parameters for generic type definitions

**Field and Variant Types**:
```cpp
struct StructField {
    Identifier name;
    TypePtr type;
    Visibility visibility;  // public, private, etc.
};

struct EnumVariant {
    Identifier name;
    std::optional<TypePtr> data_type;  // None for unit variants
};
```

### Generic and Trait Types

```cpp
struct GenericType {
    Identifier name;
    std::vector<TraitConstraint> constraints;
};

struct TraitType {
    Identifier name;
    std::vector<TraitMethod> methods;
    std::vector<TypePtr> type_parameters;
};
```

**Generic Programming Support**: Generic types enable:
- **Type Parameters**: Named generic types with constraints
- **Trait Constraints**: Bounds on generic type parameters
- **Trait Definitions**: Interface specifications with methods
- **Monomorphization**: Foundation for generic instantiation

**Constraint System**:
```cpp
struct TraitConstraint {
    Identifier trait_name;
    std::vector<TypePtr> type_arguments;
};
```

### Reference and Pointer Types

```cpp
struct ReferenceType {
    TypePtr referenced_type;
    bool is_mutable;  // &T vs &mut T
};

struct PointerType {
    TypePtr pointed_type;
    bool is_mutable;  // *const T vs *T
};
```

**Memory Access Types**: Reference and pointer types provide:
- **Borrowing Semantics**: Immutable vs mutable references
- **Pointer Operations**: Raw pointer types for unsafe operations
- **Type Safety**: Compile-time checking of reference validity
- **Lifetime Integration**: Foundation for lifetime analysis

### Special Type Categories

```cpp
struct VoidType { /* Unit type with no values */ };
struct NeverType { /* Bottom type, no values possible */ };
struct SelfType { /* Self type in trait implementations */ };
struct TypeParamType { 
    Identifier name;
    std::optional<size_t> index;  // Position in type parameter list
};
```

**Special Purpose Types**: Special types handle:
- **Void Type**: Return type for functions with no return value
- **Never Type**: Type of expressions that never return (panic, diverge)
- **Self Type**: Self reference in trait implementations
- **Type Parameters**: Unresolved type parameter references

## Performance Characteristics

### Memory Usage Analysis

- **Variant Overhead**: 8 bytes for discriminant + max(sizeof(member types))
- **Pointer Storage**: `std::unique_ptr` adds 8 bytes per nested type
- **Vector Storage**: Dynamic allocation for collections of types

**Typical Type Size**: 24-64 bytes depending on complexity, significantly smaller than inheritance-based approaches.

### Type Comparison Optimization

```cpp
// Type comparison uses structural equality
bool types_equal(const Type& lhs, const Type& rhs);
```

**Efficient Comparison**: Type system provides:
- **Structural Equality**: Types are equal if their structures match
- **Canonical Forms**: Normalized representations for comparison
- **Hash Support**: Efficient type hashing for type tables
- **Caching**: Memoized type comparisons for performance

## Integration Constraints

### Parser Interface Requirements

The parser constructs types with these guarantees:

1. **Move Semantics**: All type construction supports move operations
2. **Memory Safety**: No raw pointers or manual memory management
3. **Error Recovery**: Error types maintain valid structure
4. **Syntax Validation**: Type syntax is validated during parsing

### Semantic Analysis Expectations

Semantic analysis relies on:

1. **Type Resolution**: All type references are resolved to definitions
2. **Generic Instantiation**: Generic types are properly instantiated
3. **Trait Checking**: Trait constraints are validated
4. **Type Inference**: Type information supports inference algorithms

### Code Generation Requirements

Types transform to code with these properties:

1. **Layout Information**: Type size and alignment for code generation
2. **Method Dispatch**: Virtual table layout for trait objects
3. **Memory Layout**: Field offsets and padding information
4. **Calling Conventions**: Function type ABI information

## Component Specifications

### Core Type Categories

- **Primitive Types**: Bool, Int, Float, Char, String with optional parameters
- **Composite Types**: Tuple, Array, Slice for complex data structures
- **Function Types**: Parameter and return type specifications
- **User-Defined Types**: Struct, Enum, Union for custom data types
- **Generic Types**: Type parameters and trait constraints
- **Reference Types**: Borrowing and pointer semantics
- **Special Types**: Void, Never, Self, TypeParam for special cases

### Supporting Types

- **`TypePtr`**: `std::unique_ptr<Type>` for ownership semantics
- **`StructField`**: Field specifications for struct types
- **`EnumVariant`**: Variant specifications for enum types
- **`TraitConstraint`**: Type parameter constraints
- **`TraitMethod`**: Method specifications for trait types

### Type Operations

- **Equality**: Structural type comparison
- **Subtyping**: Type compatibility checking
- **Inference**: Type variable unification
- **Resolution**: Name to type definition mapping
- **Instantiation**: Generic type specialization

## Related Documentation

- **High-Level Overview**: [../../docs/component-overviews/ast-overview.md](../../docs/component-overviews/ast-overview.md) - AST architecture and design overview
- **AST Architecture**: [./README.md](./README.md) - Variant-based node design
- **Type System**: [../../semantic/type/type.md](../../semantic/type/type.md) - Semantic type system
- **Expression Types**: [./expr.md](./expr.md) - Type annotations in expressions
- **Item Types**: [./item.md](./item.md) - Type definitions in items
- **Type Parsing**: [../parser/type_parse.md](../parser/type_parse.md) - Type syntax parsing
- **Visitor Pattern**: [./visitor/visitor_base.md](./visitor/visitor_base.md) - Type traversal and transformation