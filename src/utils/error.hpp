#pragma once

#include <stdexcept>
#include <string>

class LexerError : public std::runtime_error {
public:
    explicit LexerError(const std::string& message) : std::runtime_error(message) {}
};
