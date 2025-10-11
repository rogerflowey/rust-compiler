# helpers.hpp Documentation

## Overview

[`src/utils/helpers.hpp`](../../src/utils/helpers.hpp) provides utility functions for parsing numeric literals with type suffixes in the RCompiler project.

## Purpose

This header defines utilities for separating numeric literals into their numeric value and type suffix components, particularly used by the lexer when processing number tokens.

## Data Structures

### ParsedNumeric

[`ParsedNumeric`](../../src/utils/helpers.hpp:10) represents a parsed numeric literal with optional type suffix.

#### Fields
- [`number`](../../src/utils/helpers.hpp:11): The numeric part of the literal (e.g., "42" from "42i32")
- [`type`](../../src/utils/helpers.hpp:12): The type suffix (e.g., "i32" from "42i32"), empty if no suffix

#### Methods
- [`operator==()`](../../src/utils/helpers.hpp:15): Equality comparison for testing purposes

## Functions

### separateNumberAndType()

[`separateNumberAndType()`](../../src/utils/helpers.hpp:24) parses a numeric string into number and type components.

#### Parameters
- `input`: The string to parse (e.g., "123u32", "456", "789isize")

#### Return Value
Returns `std::optional<ParsedNumeric>`:
- Valid `ParsedNumeric` if input starts with a digit
- `std::nullopt` if input is empty or doesn't start with a digit

#### Behavior

1. Validates input starts with a digit
2. Scans leading digits to find the numeric part
3. Treats remaining characters as the type suffix
4. Returns structured result with both components

#### Examples
- `"123i32"` → `{number: "123", type: "i32"}`
- `"456"` → `{number: "456", type: ""}`
- `"789usize"` → `{number: "789", type: "usize"}`

## Implementation Details

### Input Validation
- Empty strings return `std::nullopt`
- Non-digit starting characters return `std::nullopt`
- Uses `static_cast<unsigned char>` for safe `std::isdigit` usage

### Parsing Logic
- Linear scan for digits
- First non-digit character begins the type suffix
- Efficient substring operations for separation

## Dependencies

- `<cctype>`: For `std::isdigit`
- `<string>`: For `std::string` operations
- `<optional>`: For `std::optional` return type

## Test Coverage

This utility is tested as part of:
- Lexer tests: Numeric literal tokenization
- Expression parser tests: Literal expression parsing
- [`test/lexer/test_lexer.cpp`](../../test/lexer/test_lexer.cpp): Numeric token parsing and validation
- [`test/parser/test_expr_parser.cpp`](../../test/parser/test_expr_parser.cpp): Integer literal parsing with suffixes

## Usage

```cpp
auto result = separateNumberAndType("42i32");
if (result) {
    std::cout << "Number: " << result->number << std::endl; // "42"
    std::cout << "Type: " << result->type << std::endl;   // "i32"
}
```

## Design Notes

The function is intentionally minimal:
- No type validation (handled by type checker)
- No numeric conversion (handled later in pipeline)
- Simple, fast parsing for lexer use
- Clear separation of concerns

The design enables the lexer to quickly split numeric literals while deferring semantic validation to later compilation phases.