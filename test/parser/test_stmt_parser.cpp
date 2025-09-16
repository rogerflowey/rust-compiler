#include <gtest/gtest.h>
#include <sstream>
#include <vector>
#include <string>
#include <variant>

#include "src/lexer/lexer.hpp"
#include "src/ast/stmt.hpp"
#include "src/ast/expr.hpp"
#include "src/ast/item.hpp"
#include "src/ast/pattern.hpp"

#include "src/parser/parser.hpp"

using namespace parsec;

// Helper to safely get a pointer to the concrete node type from the variant wrapper
template <typename T, typename VariantPtr>
T* get_node(const VariantPtr& ptr) {
    if (!ptr) return nullptr;
    return std::get_if<T>(&(ptr->value));
}

static auto make_full_stmt_parser() {
    // The entire manual wiring dance is replaced by one call.
    const auto& registry = getParserRegistry();
    return registry.stmt < equal(T_EOF);
}

static StmtPtr parse_stmt(const std::string &src) {
    std::stringstream ss(src);
    Lexer lex(ss);
    const auto &tokens = lex.tokenize();
    auto full = make_full_stmt_parser();
    auto result = run(full, tokens);
    if (std::holds_alternative<ParseError>(result)) {
        auto err = std::get<ParseError>(result);
        std::string expected_str;
        for(const auto& exp : err.expected) {
            expected_str += " " + exp;
        }

        std::string found_tok_str = "EOF";
        if(err.position < tokens.size()){
            found_tok_str = tokens[err.position].value;
        }

        std::string error_msg = "Parse error at position " + std::to_string(err.position) +
                                ". Expected one of:" + expected_str +
                                ", but found '" + found_tok_str + "'.\nSource: " + src;
        throw std::runtime_error(error_msg);
    }
    return std::move(std::get<StmtPtr>(result));
}

class StmtParserTest : public ::testing::Test {};

TEST_F(StmtParserTest, ParsesEmptyStatement) {
    auto s = parse_stmt(";");
    auto e = get_node<EmptyStmt>(s);
    ASSERT_NE(e, nullptr);
}

TEST_F(StmtParserTest, ParsesLetWithTypeAndInit) {
    auto s = parse_stmt("let x: i32 = 1i32;");
    auto let = get_node<LetStmt>(s);
    ASSERT_NE(let, nullptr);
    ASSERT_TRUE(let->type_annotation.has_value());
    ASSERT_TRUE(let->initializer.has_value());
}

TEST_F(StmtParserTest, ParsesLetWithoutInit) {
    auto s = parse_stmt("let y: bool;");
    auto let = get_node<LetStmt>(s);
    ASSERT_NE(let, nullptr);
    ASSERT_TRUE(let->type_annotation.has_value());
    ASSERT_FALSE(let->initializer.has_value());
}

TEST_F(StmtParserTest, ParsesExprWithoutBlockRequiresSemicolon) {
    auto s = parse_stmt("1i32;");
    auto es = get_node<ExprStmt>(s);
    ASSERT_NE(es, nullptr);
    auto lit = get_node<IntegerLiteralExpr>(es->expr);
    ASSERT_NE(lit, nullptr);
    EXPECT_EQ(lit->value, 1);
}

TEST_F(StmtParserTest, ParsesExprWithBlockOptionalSemicolon) {
    {
        auto s = parse_stmt("{ 1i32 }");
        auto es = get_node<ExprStmt>(s);
        ASSERT_NE(es, nullptr);
        auto blk = get_node<BlockExpr>(es->expr);
        ASSERT_NE(blk, nullptr);
    }
    {
        auto s = parse_stmt("if true { };");
        auto es = get_node<ExprStmt>(s);
        ASSERT_NE(es, nullptr);
        auto iff = get_node<IfExpr>(es->expr);
        ASSERT_NE(iff, nullptr);
    }
}
TEST_F(StmtParserTest, ParsesItemAsStatement) {
    // A function or struct definition is a valid statement within a block.
    {
        auto s = parse_stmt("fn helper() {}");
        auto is = get_node<ItemStmt>(s);
        ASSERT_NE(is, nullptr);
        auto fn = get_node<FunctionItem>(is->item);
        ASSERT_NE(fn, nullptr);
        EXPECT_EQ(fn->name->name, "helper");
    }
    {
        auto s = parse_stmt("struct Point { x: i32, y: i32 }");
        auto is = get_node<ItemStmt>(s);
        ASSERT_NE(is, nullptr);
        auto st = get_node<StructItem>(is->item);
        ASSERT_NE(st, nullptr);
        EXPECT_EQ(st->name->name, "Point");
    }
}

TEST_F(StmtParserTest, ParsesLetWithComplexPattern) {
    // Test `let` with a pattern other than a simple identifier.
    auto s = parse_stmt("let &x: &i32 = y;");
    auto let = get_node<LetStmt>(s);
    ASSERT_NE(let, nullptr);

    auto refp = get_node<ReferencePattern>(let->pattern);
    ASSERT_NE(refp, nullptr);
    auto idp = get_node<IdentifierPattern>(refp->subpattern);
    ASSERT_NE(idp, nullptr);
    EXPECT_EQ(idp->name->name, "x");

    ASSERT_TRUE(let->type_annotation.has_value());
    ASSERT_TRUE(let->initializer.has_value());
}