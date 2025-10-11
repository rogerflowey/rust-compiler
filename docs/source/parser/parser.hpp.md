# Parser Main Header

## File: [`src/parser/parser.hpp`](../../src/parser/parser.hpp)

## Overview

Main parser header that includes all parser components for the RCompiler project.

## Structure

Includes all parser components in logical order:

1. **Core Infrastructure**
   - [`common.hpp`](../../src/parser/common.hpp): Common types and definitions
   - [`parser_registry.hpp`](../../src/parser/parser_registry.hpp): Parser registry
   - [`utils.hpp`](../../src/parser/utils.hpp): Parser utilities

2. **Expression Parsing**
   - [`expr_parse.hpp`](../../src/parser/expr_parse.hpp): Expression parser builder

3. **Item Parsing**
   - [`item_parse.hpp`](../../src/parser/item_parse.hpp): Item parser builder

4. **Specialized Parsers**
   - [`path_parse.hpp`](../../src/parser/path_parse.hpp): Path expression parser
   - [`pattern_parse.hpp`](../../src/parser/pattern_parse.hpp): Pattern matching parser
   - [`stmt_parse.hpp`](../../src/parser/stmt_parse.hpp): Statement parser
   - [`type_parse.hpp`](../../src/parser/type_parse.hpp): Type expression parser

## Usage

```cpp
#include "src/parser/parser.hpp"

const auto& registry = getParserRegistry();
auto expr_parser = registry.expr;
auto item_parser = registry.item;
```

## Test Coverage

Complete parser system tested by individual test files for each component.

## Implementation Notes

Serves as the public API for the parsing subsystem. Uses parser registry pattern to manage dependencies and avoid circular dependencies.