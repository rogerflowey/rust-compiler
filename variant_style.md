# Variant Style Modernization - Files to Update

## Overview
This document tracks places in `src/mir/` folder that should be refactored to use `Overloaded` helper with `std::visit` instead of `std::get<>`, `std::holds_alternative()`, or raw `std::visit` with lambda expressions.

**Total occurrences found: 46 across 7 files**

---

## Files Requiring Updates

### 1. src/mir/codegen/emitter.cpp
**Count: 19 occurrences**

Patterns to refactor:
- `std::holds_alternative<>` checks
- `std::get<>` extractions
- Raw `std::visit([&](const auto&) {...})` lambda patterns

Should replace with `std::visit(Overloaded{...})` pattern.

---

### 2. src/mir/lower/lower_expr.cpp
**Count: 11 occurrences**

Patterns to refactor:
- `std::visit([this, &info](const auto& node) {...})` - needs Overloaded conversion
- `std::holds_alternative<hir::*>()` checks - should use visit-based approach
- Multiple standalone `std::visit` calls that could be consolidated with Overloaded

---

### 3. src/mir/lower/lower_common.cpp
**Count: 6 occurrences**

Patterns to refactor:
- `std::holds_alternative<type::*>()` checks for type system variants
- `std::visit(Overloaded{...})` patterns (already correct, but verify consistency)
- Mixed approaches that should be unified

---

### 4. src/mir/codegen/llvmbuilder/type_formatter.cpp
**Count: 5 occurrences**

Patterns to refactor:
- Series of `std::holds_alternative<type::*>()` checks (UnitType, NeverType, UnderscoreType, StructType, EnumType)
- Should be consolidated into single `std::visit(Overloaded{...})` visitor

---

### 5. src/mir/lower/lower_const.cpp
**Count: 2 occurrences**

Patterns to refactor:
- `std::visit([&](const auto& value)` - raw lambda pattern
- `std::visit([&](const auto& variant)` - raw lambda pattern
- Both should use Overloaded style for consistency

---

### 6. src/mir/lower/lower.cpp
**Count: 2 occurrences**

Patterns to refactor:
- `std::visit([this](const auto& node)` - raw lambda
- `std::visit([this, &value](const auto& pat)` - raw lambda
- Should use Overloaded pattern with explicit overloads

---

### 7. src/mir/codegen/rvalue.cpp
**Count: 1 occurrence**

Patterns to refactor:
- `std::holds_alternative<type::ReferenceType>()` check
- Can be replaced with visit-based type checking

---

## Refactoring Priority

1. **High Priority (many occurrences, critical paths)**:
   - `src/mir/codegen/emitter.cpp` (19)
   - `src/mir/lower/lower_expr.cpp` (11)

2. **Medium Priority (moderate occurrences)**:
   - `src/mir/lower/lower_common.cpp` (6)
   - `src/mir/codegen/llvmbuilder/type_formatter.cpp` (5)

3. **Low Priority (few occurrences)**:
   - `src/mir/lower/lower_const.cpp` (2)
   - `src/mir/lower/lower.cpp` (2)
   - `src/mir/codegen/rvalue.cpp` (1)

---

## Refactoring Pattern

### Current Pattern (to replace):
```cpp
// Pattern 1: holds_alternative checks
if (std::holds_alternative<TypeA>(variant)) {
    const auto& value = std::get<TypeA>(variant);
    // use value
}

// Pattern 2: raw visit with lambda
std::visit([&](const auto& node) {
    // dispatch based on node type
}, variant);
```

### Target Pattern (Overloaded style):
```cpp
std::visit(Overloaded{
    [&](const TypeA& value) { /* handle TypeA */ },
    [&](const TypeB& value) { /* handle TypeB */ },
    // ... other overloads
}, variant);
```

---

## Notes
- Exclude `src/mir/tests/` directory as per requirements
- Focus on consistency across all files
- Ensure captures (`this`, `&info`, etc.) are properly preserved
- Verify type safety after each refactoring
