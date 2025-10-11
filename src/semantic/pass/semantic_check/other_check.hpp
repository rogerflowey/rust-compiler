#pragma once
#include "semantic/hir/hir.hpp"

namespace semantic {

inline void overflow_int_literal_check(const hir::Literal::Integer& lit){
    auto is_unsigned = lit.suffix_type==ast::IntegerLiteralExpr::U32 || lit.suffix_type==ast::IntegerLiteralExpr::USIZE;
    if(lit.is_negative){
        if(is_unsigned){
            throw std::logic_error("Negative value provided for unsigned integer literal");
        } 
    }
    // Check for overflow
    if (is_unsigned) {
        if (lit.value > 4294967295LL) {
            throw std::logic_error("Integer literal overflows u32");
        }
    } else {
        if (lit.is_negative) {
            if (lit.value > 2147483648LL) {
                throw std::logic_error("Integer literal underflows i32");
            }
        } else {
            if (lit.value > 2147483647LL) {
                throw std::logic_error("Integer literal overflows i32");
            }
        }
    }
};

}