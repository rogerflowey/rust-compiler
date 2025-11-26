# Predefined Symbols

## Overview

Built-in types and functions automatically available in the global scope without explicit declaration.

## Built-in Types

### String Type
```cpp
static hir::StructDef struct_String{
    .fields = {},
    
};
```
Represents text data; available without import.

## Built-in Functions

### I/O Operations
```cpp
static hir::Function func_print = make_builtin_function();        // Print without newline
static hir::Function func_println = make_builtin_function();      // Print with newline
static hir::Function func_printInt = make_builtin_function();     // Print integer without newline
static hir::Function func_printlnInt = make_builtin_function();   // Print integer with newline
static hir::Function func_getString = make_builtin_function();    // Get string input
static hir::Function func_getInt = make_builtin_function();       // Get integer input
```

### Program Control
```cpp
static hir::Function func_exit = make_builtin_function();         // Exit program
```

## API

### Scope Creation
```cpp
inline Scope create_predefined_scope() {
    Scope scope;
    scope.define_type("String", &struct_String);
    scope.define_item("print", &func_print);
    // ... other definitions
    return scope;
}
```

### Singleton Access
```cpp
inline Scope& get_predefined_scope() {
    static Scope predefined_scope = create_predefined_scope();
    return predefined_scope;
}
```

## Function Signatures

```rust
fn print(s: String) -> ();
fn println(s: String) -> ();
fn printInt(i: i32) -> ();
fn printlnInt(i: i32) -> ();
fn getString() -> String;
fn getInt() -> i32;
fn exit() -> !;  // Never returns
```

## Design Decisions

### Minimal HIR Structure
Builtin functions created with minimal HIR structure since implemented natively:
```cpp
hir::Function make_builtin_function() {
    return hir::Function{
        .params = {},
        .return_type = std::nullopt,
        .body = nullptr,
        .locals = {},
        
    };
}
```

### Integration Point
Predefined scope used as parent of global scope during name resolution:
```cpp
scopes.push(Scope{&get_predefined_scope(), true});
```

## Performance Considerations

- Scope created once and cached
- Raw pointer storage (no ownership overhead)
- Identical lookup performance to user-defined symbols

## Navigation

- **[Semantic Analysis Overview](../../../docs/component-overviews/semantic-overview.md)** - High-level semantic analysis design
- **[Scope Management](scope.md)** - Symbol table and scope management
- **[Type System](../type/type.md)** - Type system implementation
- **[HIR Representation](../hir/hir.md)** - High-level Intermediate Representation