#pragma once
#include "common.hpp"

#include "path_parse.hpp"
#include "expr_parse.hpp"
#include "type_parse.hpp"
#include "pattern_parse.hpp"
#include "stmt_parse.hpp"
#include "item_parse.hpp"

#include <stdexcept>

using namespace parsec;
/**
 * @struct ParserSuite
 * @brief Manages the lifecycle of all parser builders and the registry.
 * Its sole purpose is to perform the one-time initialization.
 */
struct ParserSuite {
    // Builders
    PathParserBuilder pathB;
    ExprParserBuilder exprB;
    TypeParserBuilder typeB;
    PatternParserBuilder patternB;
    StmtParserBuilder stmtB;
    ItemParserBuilder itemB;

    // The final, usable parsers are now in the registry.
    ParserRegistry registry;

    bool initialized = false;

    void init() {
        if (initialized) return;

        // === PASS 1: Create and Register all Lazy Parsers ===
        auto [path_p, set_path] = lazy<PathPtr, Token>();
        registry.path = path_p;

        auto [expr_p, set_expr] = lazy<ExprPtr, Token>();
        registry.expr = expr_p;

        auto [expr_wb_p, set_expr_wb] = lazy<ExprPtr, Token>();
        registry.exprWithBlock = expr_wb_p;

        auto [expr_lit_p, set_expr_lit] = lazy<ExprPtr, Token>();
        registry.literalExpr = expr_lit_p;

        auto [type_p, set_type] = lazy<TypePtr, Token>();
        registry.type = type_p;
        
        auto [pattern_p, set_pattern] = lazy<PatternPtr, Token>();
        registry.pattern = pattern_p;

        auto [stmt_p, set_stmt] = lazy<StmtPtr, Token>();
        registry.stmt = stmt_p;

        auto [item_p, set_item] = lazy<ItemPtr, Token>();
        registry.item = item_p;

        // === PASS 2: Finalize All Builders ===
        pathB.finalize(registry, set_path);
        typeB.finalize(registry, set_type);
        patternB.finalize(registry, set_pattern);
        stmtB.finalize(registry, set_stmt);
        itemB.finalize(registry, set_item);
        exprB.finalize(registry, set_expr, set_expr_wb, set_expr_lit);

        initialized = true;
    }
};

/**
 * @brief Global access point to the fully initialized parser registry.
 * @return A constant reference to the ParserRegistry.
 */
inline const ParserRegistry& getParserRegistry() {
    static ParserSuite suite;
    static bool once = false;
    if (!once) {
        suite.init();
        once = true;
    }
    return suite.registry;
}