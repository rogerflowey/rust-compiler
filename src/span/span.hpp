#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace span {

using FileId = uint32_t;
constexpr FileId kInvalidFileId = std::numeric_limits<FileId>::max();

struct Span {
    FileId file = kInvalidFileId;
    uint32_t start = 0;
    uint32_t end = 0;

    constexpr bool is_valid() const { return file != kInvalidFileId; }
    constexpr uint32_t length() const { return end >= start ? end - start : 0; }

    static constexpr Span invalid() { return {}; }

    static constexpr Span merge(const Span &lhs, const Span &rhs) {
        if (!lhs.is_valid()) return rhs;
        if (!rhs.is_valid()) return lhs;
        if (lhs.file != rhs.file) return rhs;
        return {lhs.file, std::min(lhs.start, rhs.start), std::max(lhs.end, rhs.end)};
    }
};

struct LineCol {
    size_t line = 0;   // 1-based
    size_t column = 0; // 1-based
};

} // namespace span
