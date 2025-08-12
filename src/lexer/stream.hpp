#pragma once

#include <istream>
#include <string>

struct Position {
    int row = 1; // 1-based index
    int col = 1; // 1-based index

    std::string toString() const {
        return "Line " + std::to_string(row) + ", Column " + std::to_string(col);
    }
};

class PositionedStream {
    std::istream& input;
    mutable std::string buffer;
    Position currentPos;
    size_t buffer_pos = 0;

public:
    PositionedStream(std::istream& inputStream) : input(inputStream) {}
    char peek(size_t offset = 0) const {
        while (buffer_pos + offset >= buffer.size() && input) {
            char c = input.get();
            if (input.good()) {
                buffer += c;
            } else {
                break;
            }
        }
        if (buffer_pos + offset >= buffer.size()) {
            return EOF;
        }
        return buffer[buffer_pos + offset];
    }
    std::string peek_str(size_t length) const {
        std::string result;
        for (size_t i = 0; i < length; ++i) {
            char c = peek(i);
            if (c == EOF) break;
            result += c;
        }
        return result;
    }
    bool match(const std::string& s) const {
        for (size_t i = 0; i < s.length(); ++i) {
            if (peek(i) != s[i]) {
                return false;
            }
        }
        return true;
    }
    
    void advance(size_t n = 1) {
        for (size_t i = 0; i < n; ++i) {
            if (eof()) return;

            char c = buffer[buffer_pos];
            if (c == '\n') {
                currentPos.row++;
                currentPos.col = 1;
            } else {
                currentPos.col++;
            }
            buffer_pos++;
        }
    }
    char get() {
        char c = peek();
        advance(1);
        return c;
    }
    bool eof(size_t n = 0) const {
        return peek(n) == EOF;
    }
    const Position& getPosition() const {
        return currentPos;
    }
};