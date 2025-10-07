
#pragma once
// numeric_parser.cp
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

// (intentionally left minimal)

inline std::optional<ParsedNumeric> separateNumberAndType(const std::string& input) {
    if (input.empty() || !std::isdigit(static_cast<unsigned char>(input[0]))) {
        return std::nullopt;
    }

    // Split into leading digits and optional alnum suffix (e.g., i32, u64, usize)
    size_t i = 0;
    while (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i]))) {
        ++i;
    }

    ParsedNumeric result;
    result.number = input.substr(0, i);
    result.type = (i < input.size()) ? input.substr(i) : std::string{};
    return result;
}


