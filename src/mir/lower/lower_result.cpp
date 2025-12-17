#include "mir/lower/lower_result.hpp"

#include "mir/lower/lower_internal.hpp"

namespace mir::detail {

LowerResult LowerResult::from_operand(Operand op) {
  return LowerResult{Kind::Operand, std::move(op)};
}

LowerResult LowerResult::from_place(Place p) {
  return LowerResult{Kind::Place, std::move(p)};
}

LowerResult LowerResult::written() {
  return LowerResult{Kind::Written, std::monostate{}};
}

Operand LowerResult::as_operand(FunctionLowerer &ctx,
                                const semantic::ExprInfo &info) {
  switch (kind) {
  case Kind::Operand:
    return std::get<Operand>(data);
  case Kind::Place: {
    if (!info.has_type || info.type == invalid_type_id) {
      throw std::logic_error("as_operand requires resolved type");
    }
    return ctx.load_place_value(std::get<Place>(data), info.type);
  }
  case Kind::Written:
    throw std::logic_error("Value already written to destination");
  }
  throw std::logic_error("Unknown LowerResult kind");
}

Place LowerResult::as_place(FunctionLowerer &ctx,
                            const semantic::ExprInfo &info) {
  switch (kind) {
  case Kind::Place:
    return std::get<Place>(data);
  case Kind::Operand: {
    if (!info.has_type || info.type == invalid_type_id) {
      throw std::logic_error("as_place requires resolved type");
    }
    if (std::getenv("DEBUG_MIR_LOCALS")) {
      std::cerr << "[MIR DEBUG] as_place materializing operand to synthetic "
                   "local\n";
    }
    LocalId tmp_local = ctx.create_synthetic_local(info.type, false);
    Place tmp_place = ctx.make_local_place(tmp_local);

    AssignStatement assign{.dest = tmp_place,
                           .src = ValueSource{std::get<Operand>(data)}};
    Statement stmt;
    stmt.value = std::move(assign);
    ctx.append_statement(std::move(stmt));
    return tmp_place;
  }
  case Kind::Written:
    throw std::logic_error("Value already written to destination");
  }
  throw std::logic_error("Unknown LowerResult kind");
}

void LowerResult::write_to_dest(FunctionLowerer &ctx, Place dest,
                                const semantic::ExprInfo &) {
  switch (kind) {
  case Kind::Written:
    return;
  case Kind::Operand: {
    AssignStatement assign{.dest = std::move(dest),
                           .src = ValueSource{std::get<Operand>(data)}};
    Statement stmt;
    stmt.value = std::move(assign);
    ctx.append_statement(std::move(stmt));
    return;
  }
  case Kind::Place: {
    AssignStatement assign{.dest = std::move(dest),
                           .src = ValueSource{std::get<Place>(data)}};
    Statement stmt;
    stmt.value = std::move(assign);
    ctx.append_statement(std::move(stmt));
    return;
  }
  }
}

} // namespace mir::detail
