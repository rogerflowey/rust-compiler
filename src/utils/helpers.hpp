
#pragma once
// numeric_parser.cp
#include <vector>
#include <sstream>
#include <cctype> // for std::isdigit
#include <string>
#include <optional>

// A struct to hold the parsed components.
// Using a struct makes the code more readable than using std::pair.
struct ParsedNumeric {
    std::string number;
    std::string type; // Will be empty if no type suffix is found.

    // Optional: Add a comparison operator for easy testing.
    bool operator==(const ParsedNumeric& other) const {
        return number == other.number && type == other.type;
    }
};

std::optional<ParsedNumeric> separateNumberAndType(const std::string& input);

// Helper function to check if a string consists only of digits.
static bool isAllDigits(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

std::optional<ParsedNumeric> separateNumberAndType(const std::string& input) {
    if (input.length() <= 1 || input[0] != '_') {
        return std::nullopt;
    }

    std::vector<std::string> parts;
    std::stringstream ss(input.substr(1));
    std::string part;

    while (std::getline(ss, part, '_')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    if (parts.empty()) {
        return std::nullopt;
    }
    ParsedNumeric result;
    std::string last_part = parts.back();

    if (!isAllDigits(last_part)) {
        result.type = last_part;
        parts.pop_back();
    }
    if (parts.empty()) {
        return std::nullopt;
    }
    
    std::stringstream number_stream;
    for (const auto& num_part : parts) {
        if (!isAllDigits(num_part)) {
            return std::nullopt;
        }
        number_stream << num_part;
    }
    result.number = number_stream.str();
    return result;
}