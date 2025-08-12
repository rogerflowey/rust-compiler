#include "common.hpp"
#include "parsec.hpp"
#include <optional>


inline auto equal(const Token& t){
    return parsec::satisfy<Token>([t](const Token& token) {
        return token == t;
    });
}