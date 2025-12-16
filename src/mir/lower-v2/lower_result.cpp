#include "mir/lower-v2/lower_result.hpp"
#include "mir/lower-v2/lower_internal.hpp"

namespace mir::lower_v2 {

// === Constructors ===

LowerResult LowerResult::from_operand(Operand op) {
    LowerResult result(Kind::Operand, op);
    return result;
}

LowerResult LowerResult::from_place(Place p) {
    LowerResult result(Kind::Place, p);
    return result;
}

LowerResult LowerResult::written() {
    LowerResult result(Kind::Written);
    return result;
}

// === Accessors ===

const Operand& LowerResult::as_operand_unchecked() const {
    return std::get<Operand>(data_);
}

const Place& LowerResult::as_place_unchecked() const {
    return std::get<Place>(data_);
}

// === Universal Adapters ===

Operand LowerResult::as_operand(FunctionLowerer& ctx, const semantic::ExprInfo& info) {
    switch (kind_) {
        case Kind::Operand:
            return as_operand_unchecked();

        case Kind::Place: {
            // Load the place into a temporary
            Place p = as_place_unchecked();
            TempId loaded_temp = ctx.allocate_temp(info.type);
            
            LoadStatement load_stmt{.dest = loaded_temp, .src = p};
            Statement stmt;
            stmt.value = std::move(load_stmt);
            ctx.append_statement(std::move(stmt));
            
            return ctx.make_temp_operand(loaded_temp);
        }

        case Kind::Written:
            throw std::logic_error(
                "LowerResult::as_operand called on Written result - "
                "logic error: caller expected value but got destination write");
    }

    throw std::logic_error("Unreachable");
}

Place LowerResult::as_place(FunctionLowerer& ctx, const semantic::ExprInfo& info) {
    switch (kind_) {
        case Kind::Place:
            return as_place_unchecked();

        case Kind::Operand: {
            // Allocate a temporary local and assign the operand to it
            Operand op = as_operand_unchecked();
            LocalId temp_local = ctx.create_synthetic_local(info.type, false);
            Place temp_place = ctx.make_local_place(temp_local);

            // Emit: temp_local = op
            AssignStatement assign{.dest = temp_place, .src = ValueSource{op}};
            Statement stmt;
            stmt.value = std::move(assign);
            ctx.append_statement(std::move(stmt));

            return temp_place;
        }

        case Kind::Written:
            throw std::logic_error(
                "LowerResult::as_place called on Written result - "
                "logic error: caller expected place but result was written to destination");
    }

    throw std::logic_error("Unreachable");
}

void LowerResult::write_to_dest(FunctionLowerer& ctx, Place dest, const semantic::ExprInfo& info) {
    switch (kind_) {
        case Kind::Written:
            // No-op: The expression already wrote to dest via dest_hint
            return;

        case Kind::Operand: {
            // Emit: dest = operand
            Operand op = as_operand_unchecked();
            AssignStatement assign{.dest = dest, .src = ValueSource{op}};
            Statement stmt;
            stmt.value = std::move(assign);
            ctx.append_statement(std::move(stmt));
            return;
        }

        case Kind::Place: {
            // Load and assign
            Place src = as_place_unchecked();
            Operand loaded = ctx.load_place_value(src, info.type);
            AssignStatement assign{.dest = dest, .src = ValueSource{loaded}};
            Statement stmt;
            stmt.value = std::move(assign);
            ctx.append_statement(std::move(stmt));
            return;
        }
    }

    throw std::logic_error("Unreachable");
}

}  // namespace mir::lower_v2

