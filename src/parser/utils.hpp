#pragma once

#include "common.hpp"
#include <optional>


inline auto equal(const Token& t){
    return parsec::satisfy<Token>([t](const Token& token) {
        return token == t;
    });
}