#pragma once

#include "mir/mir.hpp"
#include "semantic/pass/semantic_check/expr_info.hpp"

#include <optional>
#include <variant>

namespace mir::detail {

class FunctionLowerer;

class LowerResult {
public:
  enum class Kind { Operand, Place, Written };

  static LowerResult from_operand(Operand op);
  static LowerResult from_place(Place p);
  static LowerResult written();

  Operand as_operand(FunctionLowerer &ctx, const semantic::ExprInfo &info);
  Place as_place(FunctionLowerer &ctx, const semantic::ExprInfo &info);
  void write_to_dest(FunctionLowerer &ctx, Place dest,
                     const semantic::ExprInfo &info);

private:
  Kind kind;
  std::variant<std::monostate, Operand, Place> data;

  LowerResult(Kind k, std::variant<std::monostate, Operand, Place> d)
      : kind(k), data(std::move(d)) {}
};

} // namespace mir::detail
