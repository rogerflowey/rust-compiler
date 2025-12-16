#include "mir/lower-v2/lower_internal.hpp"

#include "semantic/expr_info_helpers.hpp"
#include "semantic/hir/helper.hpp"

#include <variant>

namespace {

mir::Constant make_int_constant(std::uint64_t value, bool is_signed, bool is_negative, mir::TypeId type) {
    mir::IntConstant int_const;
    int_const.value = value;
    int_const.is_signed = is_signed;
    int_const.is_negative = is_negative;
    mir::Constant c;
    c.type = type;
    c.value = int_const;
    return c;
}

} // namespace

namespace mir::lower_v2::detail {

LowerResult::LowerResult(Kind k, std::variant<std::monostate, Operand, Place> d)
    : kind(k), data(std::move(d)) {}

LowerResult LowerResult::operand(Operand op) { return LowerResult{Kind::Operand, std::move(op)}; }
LowerResult LowerResult::place(Place p) { return LowerResult{Kind::Place, std::move(p)}; }
LowerResult LowerResult::written() { return LowerResult{Kind::Written, std::monostate{}}; }

Operand LowerResult::as_operand(FunctionLowerer& fl, TypeId type) const {
    switch (kind) {
    case Kind::Operand:
        return std::get<Operand>(data);
    case Kind::Place: {
        Place p = std::get<Place>(data);
        return fl.load_place_value(std::move(p), type);
    }
    case Kind::Written:
        throw std::logic_error("LowerResult::as_operand called on Written value");
    }
    throw std::logic_error("Invalid LowerResult kind");
}

Place LowerResult::as_place(FunctionLowerer& fl, TypeId type) const {
    switch (kind) {
    case Kind::Place:
        return std::get<Place>(data);
    case Kind::Operand: {
        Operand op = std::get<Operand>(data);
        LocalId local = fl.create_synthetic_local(type, "<materialized>");
        Place dest = fl.make_local_place(local);
        fl.emit_assign(dest, ValueSource{std::move(op)});
        return dest;
    }
    case Kind::Written:
        throw std::logic_error("LowerResult::as_place called on Written value");
    }
    throw std::logic_error("Invalid LowerResult kind");
}

void LowerResult::write_to_dest(FunctionLowerer& fl, Place dest, TypeId type) const {
    switch (kind) {
    case Kind::Written:
        return;
    case Kind::Operand: {
        ValueSource src;
        src.source = std::get<Operand>(data);
        fl.emit_assign(std::move(dest), std::move(src));
        return;
    }
    case Kind::Place: {
        ValueSource src;
        src.source = std::get<Place>(data);
        fl.emit_assign(std::move(dest), std::move(src));
        return;
    }
    }
}

LowerResult FunctionLowerer::lower_expr(const hir::Expr& expr, std::optional<Place> maybe_dest) {
    semantic::ExprInfo info = hir::helper::get_expr_info(expr);
    return std::visit(
        [&](const auto& node) { return lower_expr_impl(node, info, maybe_dest); },
        expr.value);
}

Place FunctionLowerer::lower_place(const hir::Expr& expr) {
    semantic::ExprInfo info = hir::helper::get_expr_info(expr);
    return lower_expr(expr, std::nullopt).as_place(*this, info.type);
}

TempId FunctionLowerer::materialize_operand(const Operand& operand, TypeId type) {
    if (const auto* temp = std::get_if<TempId>(&operand.value)) {
        return *temp;
    }
    if (!current_block) {
        throw std::logic_error("Cannot materialize operand without active block");
    }
    if (type == invalid_type_id) {
        throw std::logic_error("Operand missing resolved type during materialization v2");
    }
    TypeId normalized = mir::detail::canonicalize_type_for_mir(type);
    const auto* constant = std::get_if<Constant>(&operand.value);
    if (!constant) {
        throw std::logic_error("Operand must contain a constant value");
    }
    if (constant->type != normalized) {
        throw std::logic_error("Operand type mismatch during materialization v2");
    }
    TempId dest = allocate_temp(normalized);
    ConstantRValue const_rvalue{*constant};
    RValue rvalue;
    rvalue.value = const_rvalue;
    DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
    Statement stmt;
    stmt.value = std::move(define);
    append_statement(std::move(stmt));
    return dest;
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::Literal& literal,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place>) {
    TypeId type = mir::detail::canonicalize_type_for_mir(info.type);
    Constant constant;
    constant.type = type;
    if (auto b = std::get_if<bool>(&literal.value)) {
        constant.value = BoolConstant{*b};
    } else if (auto c = std::get_if<char>(&literal.value)) {
        constant.value = CharConstant{*c};
    } else if (auto s = std::get_if<hir::Literal::String>(&literal.value)) {
        StringConstant str;
        str.data = s->value;
        str.length = s->value.size();
        str.is_cstyle = s->is_cstyle;
        constant.value = std::move(str);
    } else if (auto i = std::get_if<hir::Literal::Integer>(&literal.value)) {
        bool is_signed = mir::detail::is_signed_integer_type(type);
        constant = make_int_constant(i->value, is_signed, i->is_negative, type);
    } else {
        throw std::logic_error("Unsupported literal variant in v2 lowering");
    }
    Operand op = mir::detail::make_constant_operand(constant);
    return LowerResult::operand(std::move(op));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::Variable& variable,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place>) {
    (void)info;
    Place p = make_local_place(variable.local_id);
    return LowerResult::place(std::move(p));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::FieldAccess& field_access,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place>) {
    semantic::ExprInfo base_info = hir::helper::get_expr_info(*field_access.base);
    Place base_place = lower_expr(*field_access.base, std::nullopt).as_place(*this, base_info.type);

    std::size_t index = 0;
    if (auto idx = std::get_if<size_t>(&field_access.field)) {
        index = *idx;
    } else {
        // Resolve by name using struct info
        TypeId base_type = mir::detail::canonicalize_type_for_mir(base_info.type);
        const auto& ty = type::get_type_from_id(base_type);
        if (const auto* st = std::get_if<type::StructType>(&ty.value)) {
            const auto& struct_info = type::get_struct(st->id);
            const auto* ident = std::get_if<ast::Identifier>(&field_access.field);
            if (!ident) {
                throw std::logic_error("FieldAccess expected identifier");
            }
            bool found = false;
            for (std::size_t i = 0; i < struct_info.fields.size(); ++i) {
                if (struct_info.fields[i].name == ident->name) {
                    index = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw std::logic_error("FieldAccess field not found in struct");
            }
        }
    }

    Place projected = project_field(base_place, index);
    return LowerResult::place(std::move(projected));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::Index& index_expr,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place>) {
    semantic::ExprInfo base_info = hir::helper::get_expr_info(*index_expr.base);
    Place base_place = lower_expr(*index_expr.base, std::nullopt).as_place(*this, base_info.type);
    semantic::ExprInfo idx_info = hir::helper::get_expr_info(*index_expr.index);
    Operand idx_operand = lower_expr(*index_expr.index, std::nullopt).as_operand(*this, idx_info.type);

    Place projected = base_place;
    projected.projections.push_back(IndexProjection{std::move(idx_operand)});
    return LowerResult::place(std::move(projected));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::StructLiteral& literal,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place> dest) {
    TypeId struct_type = mir::detail::canonicalize_type_for_mir(info.type);
    Place target = dest.value_or(make_local_place(create_synthetic_local(struct_type, "<struct>")));

    const auto& type_info = type::get_type_from_id(struct_type);
    if (!std::holds_alternative<type::StructType>(type_info.value)) {
        throw std::logic_error("StructLiteral lowered with non-struct type");
    }
    const auto& struct_desc = type::get_struct(std::get<type::StructType>(type_info.value).id);

    if (auto* canonical = std::get_if<hir::StructLiteral::CanonicalFields>(&literal.fields)) {
        if (canonical->initializers.size() != struct_desc.fields.size()) {
            throw std::logic_error("Canonical struct literal field count mismatch");
        }
        for (std::size_t i = 0; i < canonical->initializers.size(); ++i) {
            const auto& field_expr = canonical->initializers[i];
            Place field_place = project_field(target, i);
            TypeId field_type = struct_desc.fields[i].type;
            LowerResult field_res = lower_expr(*field_expr, field_place);
            field_res.write_to_dest(*this, field_place, field_type);
        }
    } else {
        throw std::logic_error("Non-canonical struct literal not supported in v2 lowering");
    }

    return dest ? LowerResult::written() : LowerResult::place(std::move(target));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::ArrayLiteral& array_literal,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place> dest) {
    TypeId array_type = mir::detail::canonicalize_type_for_mir(info.type);
    const auto& ty = type::get_type_from_id(array_type);
    if (!std::holds_alternative<type::ArrayType>(ty.value)) {
        throw std::logic_error("ArrayLiteral lowered with non-array type");
    }
    const auto& arr = std::get<type::ArrayType>(ty.value);

    Place target = dest.value_or(make_local_place(create_synthetic_local(array_type, "<array>")));
    for (std::size_t i = 0; i < array_literal.elements.size(); ++i) {
        const auto& elem_expr = array_literal.elements[i];
        Place elem_place = project_index(target, i);
        LowerResult elem_res = lower_expr(*elem_expr, elem_place);
        elem_res.write_to_dest(*this, elem_place, arr.element_type);
    }

    return dest ? LowerResult::written() : LowerResult::place(std::move(target));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::ArrayRepeat& array_repeat,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place> dest) {
    TypeId array_type = mir::detail::canonicalize_type_for_mir(info.type);
    const auto& ty = type::get_type_from_id(array_type);
    if (!std::holds_alternative<type::ArrayType>(ty.value)) {
        throw std::logic_error("ArrayRepeat lowered with non-array type");
    }
    const auto& arr = std::get<type::ArrayType>(ty.value);
    std::size_t count = arr.size;

    Place target = dest.value_or(make_local_place(create_synthetic_local(array_type, "<arrayrepeat>")));
    LowerResult value_res = lower_expr(*array_repeat.value, std::nullopt);
    Operand value_operand = value_res.as_operand(*this, arr.element_type);

    for (std::size_t i = 0; i < count; ++i) {
        Place elem_place = project_index(target, i);
        emit_assign(elem_place, ValueSource{value_operand});
    }

    return dest ? LowerResult::written() : LowerResult::place(std::move(target));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::Cast& cast_expr,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place>) {
    semantic::ExprInfo inner_info = hir::helper::get_expr_info(*cast_expr.expr);
    Operand value = lower_expr(*cast_expr.expr, std::nullopt).as_operand(*this, inner_info.type);

    CastRValue rvalue{.value = std::move(value), .target_type = info.type};
    RValue wrapper;
    wrapper.value = std::move(rvalue);

    TempId dest = allocate_temp(info.type);
    DefineStatement def{.dest = dest, .rvalue = std::move(wrapper)};
    Statement stmt;
    stmt.value = std::move(def);
    append_statement(std::move(stmt));
    return LowerResult::operand(make_temp_operand(dest));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::BinaryOp& binary,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place>) {
    semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*binary.lhs);
    semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*binary.rhs);

    Operand lhs = lower_expr(*binary.lhs, std::nullopt).as_operand(*this, lhs_info.type);
    Operand rhs = lower_expr(*binary.rhs, std::nullopt).as_operand(*this, rhs_info.type);

    BinaryOpRValue rvalue;
    rvalue.kind =
        mir::detail::classify_binary_kind(binary, lhs_info.type, rhs_info.type, info.type);
    rvalue.lhs = std::move(lhs);
    rvalue.rhs = std::move(rhs);

    RValue wrapper;
    wrapper.value = std::move(rvalue);
    TempId dest = allocate_temp(info.type);
    DefineStatement def{.dest = dest, .rvalue = std::move(wrapper)};
    Statement stmt;
    stmt.value = std::move(def);
    append_statement(std::move(stmt));
    return LowerResult::operand(make_temp_operand(dest));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::Assignment& assignment,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place>) {
    semantic::ExprInfo lhs_info = hir::helper::get_expr_info(*assignment.lhs);
    semantic::ExprInfo rhs_info = hir::helper::get_expr_info(*assignment.rhs);
    Place target = lower_place(*assignment.lhs);
    LowerResult rhs_res = lower_expr(*assignment.rhs, target);
    rhs_res.write_to_dest(*this, target, rhs_info.type);
    return LowerResult::written();
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::Block& block_expr,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place> dest) {
    (void)info;
    return lower_block_expr(block_expr, dest);
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::If& if_expr,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place> dest) {
    return lower_if_expr(if_expr, info, dest);
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::Call& call_expr,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place> dest) {
    if (!call_expr.callee) {
        throw std::logic_error("Call expression missing callee during MIR lowering v2");
    }
    const auto* func_use = std::get_if<hir::FuncUse>(&call_expr.callee->value);
    if (!func_use || !func_use->def) {
        throw std::logic_error("Call expression callee is not a resolved function use");
    }

    const hir::Function* hir_fn = func_use->def;
    mir::FunctionRef target_ref = lookup_function(hir_fn);
    const MirFunctionSig& callee_sig = get_callee_sig(target_ref);

    CallTarget target;
    if (std::holds_alternative<MirFunction*>(target_ref)) {
        target.kind = CallTarget::Kind::Internal;
        target.id = std::get<MirFunction*>(target_ref)->id;
    } else {
        target.kind = CallTarget::Kind::External;
        target.id = std::get<ExternalFunction*>(target_ref)->id;
    }

    bool callee_sret = mir::is_indirect_sret(callee_sig.return_desc);
    std::optional<Place> sret_place;
    if (callee_sret) {
        Place target_place =
            dest.value_or(make_local_place(create_synthetic_local(info.type, "<call_sret>")));
        sret_place = target_place;
    }

    CallStatement call_stmt;
    call_stmt.target = target;
    call_stmt.sret_dest = sret_place;

    if (!callee_sret && !mir::is_void_semantic(callee_sig.return_desc)) {
        call_stmt.dest = allocate_temp(info.type);
    }

    for (std::size_t i = 0; i < call_expr.args.size(); ++i) {
        const auto& arg_expr = call_expr.args[i];
        semantic::ExprInfo arg_info = hir::helper::get_expr_info(*arg_expr);
        Operand arg = lower_expr(*arg_expr, std::nullopt).as_operand(*this, arg_info.type);
        call_stmt.args.push_back(ValueSource{std::move(arg)});
    }

    Statement stmt;
    stmt.value = std::move(call_stmt);
    append_statement(std::move(stmt));

    if (callee_sret) {
        if (dest) {
            return LowerResult::written();
        }
        return LowerResult::place(*sret_place);
    }

    if (!call_stmt.dest) {
        return LowerResult::written();
    }
    return LowerResult::operand(make_temp_operand(*call_stmt.dest));
}

LowerResult FunctionLowerer::lower_expr_impl(const hir::Return& return_expr,
                                             const semantic::ExprInfo& info,
                                             std::optional<Place>) {
    if (return_plan.is_sret) {
        Place dest = return_plan.return_place();
        if (return_expr.value) {
            const hir::Expr& value_expr = **return_expr.value;
            semantic::ExprInfo value_info = hir::helper::get_expr_info(value_expr);
            LowerResult res = lower_expr(value_expr, dest);
            res.write_to_dest(*this, dest, value_info.type);
        }
        emit_return(std::nullopt);
    } else {
        if (return_expr.value) {
            const hir::Expr& value_expr = **return_expr.value;
            semantic::ExprInfo value_info = hir::helper::get_expr_info(value_expr);
            Operand value = lower_expr(value_expr, std::nullopt).as_operand(*this, value_info.type);
            emit_return(std::move(value));
        } else {
            emit_return(std::nullopt);
        }
    }
    return LowerResult::written();
}

// Fallback specializations for unsupported nodes remain in header template.

} // namespace mir::lower_v2::detail
