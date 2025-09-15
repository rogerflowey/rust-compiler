#pragma once

#include "common.hpp"
#include <optional>


inline auto equal(const Token& t){
    if(t.value == ""){
        throw std::runtime_error("Token value cannot be empty in equal()");
    }
    return parsec::satisfy<Token>([t](const Token& token)->bool {
        return token == t;
    },"token ["+t.value+"]");
}