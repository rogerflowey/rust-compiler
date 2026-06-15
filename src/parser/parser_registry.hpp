#pragma once
#include "common.hpp"
#include "path_parse.hpp"
#include "expr_parse.hpp"
#include "type_parse.hpp"
#include "pattern_parse.hpp"
#include "stmt_parse.hpp"
#include "item_parse.hpp"
#include "handwritten_parse.hpp"

#include <stdexcept>

using namespace parsec;
using namespace ast;
/**
 * @struct ParserSuite
 * @brief Manages the lifecycle of all parser builders and the registry.
 * Its sole purpose is to perform the one-time initialization.
 */
struct ParserSuite {
    // The final, usable parsers are now in the registry.
    ParserRegistry registry;

    bool initialized = false;

    void init() {
        if (initialized) return;

        initHandwrittenParserRegistry(registry);
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
