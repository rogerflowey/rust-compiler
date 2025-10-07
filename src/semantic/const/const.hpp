#pragma once
// file const.hpp
// defines the struct for const values
// the check of i32/isize u32/usize and any_int is not done here , but by the type checker

#include <cstdint>
#include <string>
#include <variant>

namespace semantic{

struct UintConst{
    uint32_t value;
};
struct IntConst{
    int32_t value;
};
struct BoolConst{
    bool value;
};
struct CharConst{
    char value;
};
struct StringConst{
    std::string value;
};

using ConstVariant = std::variant<UintConst,IntConst,BoolConst,CharConst,StringConst>;

}