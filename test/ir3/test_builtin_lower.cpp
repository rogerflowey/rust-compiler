#include "ir3/lower.hpp"
#include "semantic/pass/semantic_check/expr_info.hpp"
#include "semantic/symbol/predefined.hpp"
#include "semantic/type/type.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

semantic::TypeId i32_type() {
    return semantic::get_typeID(semantic::Type{semantic::PrimitiveKind::I32});
}

semantic::TypeId unit_type() {
    return semantic::get_typeID(semantic::Type{semantic::UnitType{}});
}

std::unique_ptr<hir::Expr> integer_literal(std::int64_t value) {
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{hir::Literal{
        .value = hir::Literal::Integer{
            .value = static_cast<std::uint64_t>(value >= 0 ? value : -value),
            .suffix_type = ast::IntegerLiteralExpr::I32,
            .is_negative = value < 0,
        },
    }});
    expr->expr_info = semantic::ExprInfo{
        .type = i32_type(),
        .has_type = true,
        .is_mut = false,
        .is_place = false,
        .endpoints = {semantic::NormalEndpoint{}},
    };
    return expr;
}

std::unique_ptr<hir::Expr> function_call(const hir::Function* callee,
                                         std::vector<std::unique_ptr<hir::Expr>> args,
                                         semantic::TypeId result_type) {
    auto callee_expr =
        std::make_unique<hir::Expr>(hir::ExprVariant{hir::FuncUse(callee)});
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{hir::Call{
        .callee = std::move(callee_expr),
        .args = std::move(args),
    }});
    expr->expr_info = semantic::ExprInfo{
        .type = result_type,
        .has_type = true,
        .is_mut = false,
        .is_place = false,
        .endpoints = {semantic::NormalEndpoint{}},
    };
    return expr;
}

hir::Function external_print_int_function() {
    hir::Function fn{};
    fn.name = ast::Identifier{"printInt"};
    fn.param_type_annotations.push_back(hir::TypeAnnotation{i32_type()});
    fn.return_type = hir::TypeAnnotation{unit_type()};
    fn.body = std::make_unique<hir::Block>();
    return fn;
}

} // namespace

TEST(Ir3BuiltinLoweringTest, DistinguishesPredefinedAndUserDefinedPrintIntSymbols) {
    auto user_print_int = external_print_int_function();

    auto body = std::make_unique<hir::Block>();
    std::vector<std::unique_ptr<hir::Expr>> builtin_args;
    builtin_args.push_back(integer_literal(7));
    body->stmts.push_back(std::make_unique<hir::Stmt>(hir::StmtVariant{hir::ExprStmt{
        .expr = function_call(&semantic::func_printInt, std::move(builtin_args), unit_type()),
    }}));
    std::vector<std::unique_ptr<hir::Expr>> user_args;
    user_args.push_back(integer_literal(11));
    body->stmts.push_back(std::make_unique<hir::Stmt>(hir::StmtVariant{hir::ExprStmt{
        .expr = function_call(&user_print_int, std::move(user_args), unit_type()),
    }}));

    hir::Function main_fn{};
    main_fn.name = ast::Identifier{"main"};
    main_fn.return_type = hir::TypeAnnotation{unit_type()};
    main_fn.body = std::move(body);

    hir::Program program;
    program.items.push_back(
        std::make_unique<hir::Item>(hir::ItemVariant{std::move(main_fn)}));

    const auto module = ir3::lower_program(program);
    ASSERT_EQ(module.functions.size(), 1u);

    std::vector<std::string> callees;
    for (const auto& block : module.functions.front().blocks) {
        for (const auto& inst : block.instructions) {
            if (const auto* call = std::get_if<ir3::Call>(&inst)) {
                callees.push_back(call->callee);
            }
        }
    }

    ASSERT_EQ(callees.size(), 2u);
    EXPECT_EQ(callees[0], "__rcomp_printInt");
    EXPECT_EQ(callees[1], "printInt");
}
