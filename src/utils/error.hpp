#pragma once

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../span/span.hpp"
#include "type/type.hpp"

class CompilerError : public std::runtime_error {
public:
    explicit CompilerError(const std::string& message,
                           span::Span span = span::Span::invalid())
        : std::runtime_error(message), span_(span) {}

    span::Span span() const { return span_; }

protected:
    span::Span span_ = span::Span::invalid();
};

class LexerError : public CompilerError {
public:
    explicit LexerError(const std::string& message,
                        span::Span span = span::Span::invalid())
        : CompilerError(message, span) {}
};

class SemanticError : public CompilerError {
public:
    explicit SemanticError(const std::string& message,
                           span::Span span = span::Span::invalid())
        : CompilerError(message, span) {}
};

class ParserError : public CompilerError {
public:
    explicit ParserError(const std::string& message,
                         span::Span span = span::Span::invalid())
        : CompilerError(message, span) {}
};

struct Diagnostic {
    std::string message;
    span::Span span = span::Span::invalid();
    std::vector<std::string> notes;
};

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
inline void report_type_mismatch(type::TypeId expected_type,
                                 type::TypeId actual_type) {
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
