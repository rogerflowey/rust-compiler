#pragma once

#include "type/type.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

namespace mir {

using TypeId = type::TypeId;
inline constexpr TypeId invalid_type_id = type::invalid_type_id;

struct BoolConstant {
  bool value = false;
};

struct IntConstant {
  std::uint64_t value = 0;
  bool is_negative = false;
  bool is_signed = false;
};

struct CharConstant {
  char value = '\0';
};

struct StringConstant {
  std::string data;
  std::size_t length = 0;
  bool is_cstyle = false;
};

struct StringLiteralGlobal {
  StringConstant value;
};

using GlobalValue = std::variant<StringLiteralGlobal>;

struct MirGlobal {
  GlobalValue value;
};

using ConstantValue =
    std::variant<BoolConstant, IntConstant, CharConstant, StringConstant>;

struct Constant {
  TypeId type = invalid_type_id;
  ConstantValue value;
};

} // namespace mir
