#pragma once

#include <stdexcept>
#include <string>
#include <sstream>

class LexerError : public std::runtime_error {
public:
    explicit LexerError(const std::string& message) : std::runtime_error(message) {}
};

class SemanticError : public std::runtime_error {
public:
    explicit SemanticError(const std::string& message) : std::runtime_error(message) {}
};

// Forward declaration
namespace semantic {
    class Type;
    using TypeId = const Type*;
}

// Error reporting helper functions
namespace error_helper {

/**
 * @brief Report a semantic error with location information
 */
inline void report_error(const std::string& message) {
    throw SemanticError(message);
}

/**
 * @brief Report a type mismatch error
 */
inline void report_type_mismatch(semantic::TypeId expected_type, 
                                 semantic::TypeId actual_type) {
    std::ostringstream oss;
    oss << "Type mismatch: expected " << expected_type 
        << " but found " << actual_type;
    report_error(oss.str());
}

/**
 * @brief Report an invalid operation error
 */
inline void report_invalid_operation(const std::string& operation) {
    std::ostringstream oss;
    oss << "Invalid operation: " << operation;
    report_error(oss.str());
}

} // namespace error_helper