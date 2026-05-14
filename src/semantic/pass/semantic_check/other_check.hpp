#pragma once

#include "semantic/hir/hir.hpp"

#include <optional>
#include <string_view>

namespace semantic {

struct LiteralNumericError {
    std::string_view message;
};

inline std::optional<LiteralNumericError>
overflow_int_literal_check(const hir::Literal::Integer &lit) {
    const bool is_unsigned =
        lit.suffix_type == ast::IntegerLiteralExpr::U32 ||
        lit.suffix_type == ast::IntegerLiteralExpr::USIZE;

    if (lit.is_negative && is_unsigned) {
        return LiteralNumericError{
            .message = "Negative value provided for unsigned integer literal",
        };
    }

    if (is_unsigned) {
        if (lit.value > 4294967295LL) {
            return LiteralNumericError{.message = "Integer literal overflows u32"};
        }
    } else {
        if (lit.is_negative) {
            if (lit.value > 2147483648LL) {
                return LiteralNumericError{.message = "Integer literal underflows i32"};
            }
        } else {
            if (lit.value > 2147483647LL) {
                return LiteralNumericError{.message = "Integer literal overflows i32"};
            }
        }
    }

    return std::nullopt;
}

}