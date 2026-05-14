#pragma once

#include "common.hpp"
#include <optional>

inline span::Span merge_span_pair(const span::Span &lhs, const span::Span &rhs) {
    return span::Span::merge(lhs, rhs);
}

template <typename Container>
inline span::Span merge_span_list(const Container &spans) {
    span::Span merged = span::Span::invalid();
    for (const auto &s : spans) {
        merged = span::Span::merge(merged, s);
    }
    return merged;
}


inline auto equal(const Token& t){
    if(t.value == ""){
        throw std::runtime_error("Token value cannot be empty in equal()");
    }
    return parsec::satisfy<Token>([t](const Token& token)->bool {
        return token == t;
    },"token ["+t.value+"]");
}