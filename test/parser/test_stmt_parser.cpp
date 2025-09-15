#include <gtest/gtest.h>
#include <sstream>
#include <vector>
#include <string>
#include <variant>

#include "src/lexer/lexer.hpp"
#include "src/parser/parser_registry.hpp"
#include "src/ast/stmt.hpp"
#include "src/ast/expr.hpp"
#include "src/parser/utils.hpp"

using namespace parsec;

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
    auto e = dynamic_cast<EmptyStmt*>(s.get());
    ASSERT_NE(e, nullptr);
}

TEST_F(StmtParserTest, ParsesLetWithTypeAndInit) {
    auto s = parse_stmt("let x: i32 = 1i32;");
    auto let = dynamic_cast<LetStmt*>(s.get());
    ASSERT_NE(let, nullptr);
    ASSERT_TRUE(let->type_annotation.has_value());
    ASSERT_TRUE(let->initializer.has_value());
}

TEST_F(StmtParserTest, ParsesLetWithoutInit) {
    auto s = parse_stmt("let y: bool;");
    auto let = dynamic_cast<LetStmt*>(s.get());
    ASSERT_NE(let, nullptr);
    ASSERT_TRUE(let->type_annotation.has_value());
    ASSERT_FALSE(let->initializer.has_value());
}

TEST_F(StmtParserTest, ParsesExprWithoutBlockRequiresSemicolon) {
    auto s = parse_stmt("1i32;");
    auto es = dynamic_cast<ExprStmt*>(s.get());
    ASSERT_NE(es, nullptr);
    auto lit = dynamic_cast<IntegerLiteralExpr*>(es->expr.get());
    ASSERT_NE(lit, nullptr);
    EXPECT_EQ(lit->value, 1);
}

TEST_F(StmtParserTest, ParsesExprWithBlockOptionalSemicolon) {
    {
        auto s = parse_stmt("{ 1i32 }");
        auto es = dynamic_cast<ExprStmt*>(s.get());
        ASSERT_NE(es, nullptr);
        auto blk = dynamic_cast<BlockExpr*>(es->expr.get());
        ASSERT_NE(blk, nullptr);
    }
    {
        auto s = parse_stmt("if true { };");
        auto es = dynamic_cast<ExprStmt*>(s.get());
        ASSERT_NE(es, nullptr);
        auto iff = dynamic_cast<IfExpr*>(es->expr.get());
        ASSERT_NE(iff, nullptr);
    }
}
TEST_F(StmtParserTest, ParsesItemAsStatement) {
    // A function or struct definition is a valid statement within a block.
    {
        auto s = parse_stmt("fn helper() {}");
        auto is = dynamic_cast<ItemStmt*>(s.get());
        ASSERT_NE(is, nullptr);
        auto fn = dynamic_cast<FunctionItem*>(is->item.get());
        ASSERT_NE(fn, nullptr);
        EXPECT_EQ(fn->name->getName(), "helper");
    }
    {
        auto s = parse_stmt("struct Point { x: i32, y: i32 }");
        auto is = dynamic_cast<ItemStmt*>(s.get());
        ASSERT_NE(is, nullptr);
        auto st = dynamic_cast<StructItem*>(is->item.get());
        ASSERT_NE(st, nullptr);
        EXPECT_EQ(st->name->getName(), "Point");
    }
}

TEST_F(StmtParserTest, ParsesLetWithComplexPattern) {
    // Test `let` with a pattern other than a simple identifier.
    auto s = parse_stmt("let &x: &i32 = y;");
    auto let = dynamic_cast<LetStmt*>(s.get());
    ASSERT_NE(let, nullptr);

    auto refp = dynamic_cast<ReferencePattern*>(let->pattern.get());
    ASSERT_NE(refp, nullptr);
    auto idp = dynamic_cast<IdentifierPattern*>(refp->subpattern.get());
    ASSERT_NE(idp, nullptr);
    EXPECT_EQ(idp->name->getName(), "x");

    ASSERT_TRUE(let->type_annotation.has_value());
    ASSERT_TRUE(let->initializer.has_value());
}