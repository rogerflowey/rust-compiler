#include <gtest/gtest.h>
#include <sstream>

#include "parser/type_parse.hpp"
#include "src/lexer/lexer.hpp"
#include "src/parser/expr_parse.hpp"
#include "src/parser/path_parse.hpp"
#include "src/parser/pattern_parse.hpp"
#include "src/parser/stmt_parse.hpp"
#include "src/ast/expr.hpp"
#include "src/ast/type.hpp"
#include "src/parser/utils.hpp"

using namespace parsec;

// Build an expression parser that must consume EOF
static auto make_full_expr_parser() {
    // 1) Path parser is standalone
    PathParserBuilder pathB;
    auto p_path = pathB.get_parser();

    // 2) Expr parser (partial), wire path for PathExpr, but don't finalize yet
    ExprParserBuilder exprB;
    exprB.wire_path_parser(p_path);

    // 3) Patterns depend on expr literals and path
    PatternParserBuilder patB;
    patB.wire_literal_from_expr(exprB.get_literal_parser());
    patB.wire_paths(p_path);
    patB.finalize();

    // 4) Types depend on path and (optionally) expr for array sizes
    TypeParserBuilder typeB;
    typeB.wire_path_parser(p_path);
    typeB.wire_array_expr_parser(exprB.get_parser()); // uses expr lazily
    typeB.finalize();

    // 5) Statements need expr, patterns, and types
    StmtParserBuilder stmtB(exprB.get_parser(), patB.get_parser(), typeB.get_parser());
    exprB.wire_block_with_stmt(stmtB.get_parser());
    exprB.wire_control_flow();
    exprB.wire_flow_terminators();
    exprB.wire_type_parser(typeB.get_parser());
    exprB.finalize();

    auto p = exprB.get_parser();
    return p < equal(Token{TOKEN_EOF, ""});
}

static ExprPtr parse_expr(const std::string &src) {
    std::stringstream ss(src);
    Lexer lex(ss);
    const auto &tokens = lex.tokenize();
    auto full = make_full_expr_parser();
    auto result = run(full, tokens);
    if (!result) {
        std::string dump = "Tokens:";
        for (auto &t : tokens) {
            dump += " [" + std::to_string(t.type) + ":" + t.value + "]";
        }
        throw std::runtime_error(dump + " | Expr parse failed for: " + src);
    }
    return std::move(*result);
}

class ExprParserTest : public ::testing::Test {};

TEST_F(ExprParserTest, ParsesIntAndUintLiterals) {
    {
        auto e = parse_expr("12_3_i32");
        auto i = dynamic_cast<IntLiteralExpr*>(e.get());
        ASSERT_NE(i, nullptr);
        EXPECT_EQ(i->value, 123);
    }
    {
        auto e = parse_expr("5usize");
        auto u = dynamic_cast<UintLiteralExpr*>(e.get());
        ASSERT_NE(u, nullptr);
        EXPECT_EQ(u->value, 5u);
    }
}

TEST_F(ExprParserTest, ParsesGrouped) {
    auto e = parse_expr("(1i32)");
    auto g = dynamic_cast<GroupedExpr*>(e.get());
    ASSERT_NE(g, nullptr);
    auto i = dynamic_cast<IntLiteralExpr*>(g->expr.get());
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->value, 1);
}

TEST_F(ExprParserTest, ParsesArrayListAndRepeat) {
    {
        auto e = parse_expr("[1i32, 2i32]");
        auto a = dynamic_cast<ArrayInitExpr*>(e.get());
        ASSERT_NE(a, nullptr);
        ASSERT_EQ(a->elements.size(), 2u);
        auto i0 = dynamic_cast<IntLiteralExpr*>(a->elements[0].get());
        auto i1 = dynamic_cast<IntLiteralExpr*>(a->elements[1].get());
        ASSERT_NE(i0, nullptr);
        ASSERT_NE(i1, nullptr);
        EXPECT_EQ(i0->value, 1);
        EXPECT_EQ(i1->value, 2);
    }
    {
        auto e = parse_expr("[1i32; 3i32]");
        auto r = dynamic_cast<ArrayRepeatExpr*>(e.get());
        ASSERT_NE(r, nullptr);
        auto v = dynamic_cast<IntLiteralExpr*>(r->value.get());
        auto c = dynamic_cast<IntLiteralExpr*>(r->count.get());
        ASSERT_NE(v, nullptr);
        ASSERT_NE(c, nullptr);
        EXPECT_EQ(v->value, 1);
        EXPECT_EQ(c->value, 3);
    }
}

TEST_F(ExprParserTest, ParsesPostfixCallIndexFieldMethod) {
    {
        auto e = parse_expr("foo(1i32, 2i32)");
        auto call = dynamic_cast<CallExpr*>(e.get());
        ASSERT_NE(call, nullptr);
        auto callee = dynamic_cast<PathExpr*>(call->callee.get());
        ASSERT_NE(callee, nullptr);
        ASSERT_EQ(call->args.size(), 2u);
    }
    {
        auto e = parse_expr("arr[0i32]");
        auto idx = dynamic_cast<IndexExpr*>(e.get());
        ASSERT_NE(idx, nullptr);
    }
    {
        auto e = parse_expr("obj.field");
        auto fld = dynamic_cast<FieldAccessExpr*>(e.get());
        ASSERT_NE(fld, nullptr);
    }
    {
        auto e = parse_expr("obj.method(1i32)");
        auto m = dynamic_cast<MethodCallExpr*>(e.get());
        ASSERT_NE(m, nullptr);
        ASSERT_EQ(m->args.size(), 1u);
    }
}

TEST_F(ExprParserTest, ParsesUnaryAndCastChain) {
    {
        auto e = parse_expr("!true");
        auto u = dynamic_cast<UnaryExpr*>(e.get());
        ASSERT_NE(u, nullptr);
        EXPECT_EQ(u->op, UnaryExpr::NOT);
    }
    {
        auto e = parse_expr("1i32 as usize as i32");
        auto c1 = dynamic_cast<CastExpr*>(e.get());
        ASSERT_NE(c1, nullptr);
        auto t1 = dynamic_cast<PrimitiveType*>(c1->type.get());
        ASSERT_NE(t1, nullptr);
        // outer is i32
        EXPECT_EQ(t1->kind, PrimitiveType::I32);
        auto c0 = dynamic_cast<CastExpr*>(c1->expr.get());
        ASSERT_NE(c0, nullptr);
        auto t0 = dynamic_cast<PrimitiveType*>(c0->type.get());
        ASSERT_NE(t0, nullptr);
        EXPECT_EQ(t0->kind, PrimitiveType::USIZE);
    }
}

TEST_F(ExprParserTest, BinaryPrecedenceAndAssociativity) {
    // 1 + 2 * 3 => 1 + (2 * 3)
    auto e = parse_expr("1i32 + 2i32 * 3i32");
    auto add = dynamic_cast<BinaryExpr*>(e.get());
    ASSERT_NE(add, nullptr);
    EXPECT_EQ(add->op, BinaryExpr::ADD);
    auto lhs = dynamic_cast<IntLiteralExpr*>(add->left.get());
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->value, 1);
    auto mul = dynamic_cast<BinaryExpr*>(add->right.get());
    ASSERT_NE(mul, nullptr);
    EXPECT_EQ(mul->op, BinaryExpr::MUL);
}

TEST_F(ExprParserTest, AssignmentIsRightAssociative) {
    auto e = parse_expr("a = b = 1i32");
    auto asn_outer = dynamic_cast<AssignExpr*>(e.get());
    ASSERT_NE(asn_outer, nullptr);
    EXPECT_EQ(asn_outer->op, AssignExpr::ASSIGN);
    auto asn_inner = dynamic_cast<AssignExpr*>(asn_outer->right.get());
    ASSERT_NE(asn_inner, nullptr);
    EXPECT_EQ(asn_inner->op, AssignExpr::ASSIGN);
}

TEST_F(ExprParserTest, IfWhileLoopAndBlock) {
    {
        auto e = parse_expr("if true { 1i32 } else { 2i32 }");
        auto iff = dynamic_cast<IfExpr*>(e.get());
        ASSERT_NE(iff, nullptr);
        ASSERT_NE(iff->then_branch, nullptr);
        ASSERT_TRUE(iff->then_branch->final_expr.has_value());
        auto then_i = dynamic_cast<IntLiteralExpr*>(iff->then_branch->final_expr->get());
        ASSERT_NE(then_i, nullptr);
        ASSERT_TRUE(iff->else_branch.has_value());
    }
    {
        auto e = parse_expr("while true { }");
        auto w = dynamic_cast<WhileExpr*>(e.get());
        ASSERT_NE(w, nullptr);
        ASSERT_NE(w->body, nullptr);
        EXPECT_FALSE(w->body->final_expr.has_value());
        EXPECT_TRUE(w->body->statements.empty());
    }
    {
        auto e = parse_expr("loop { }");
        auto l = dynamic_cast<LoopExpr*>(e.get());
        ASSERT_NE(l, nullptr);
        ASSERT_NE(l->body, nullptr);
    }
    {
        auto e = parse_expr("{ let x: i32 = 1i32; 2i32 }");
        auto b = dynamic_cast<BlockExpr*>(e.get());
        ASSERT_NE(b, nullptr);
        ASSERT_EQ(b->statements.size(), 1u);
        ASSERT_TRUE(b->final_expr.has_value());
        auto two = dynamic_cast<IntLiteralExpr*>(b->final_expr->get());
        ASSERT_NE(two, nullptr);
        EXPECT_EQ(two->value, 2);
    }
}
