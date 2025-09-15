#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>
#include <variant>

#include "src/lexer/lexer.hpp"
#include "src/parser/parser_registry.hpp"
#include "src/ast/type.hpp"
#include "src/ast/expr.hpp"

using namespace parsec;

// Build a Type parser that also requires EOF to ensure full consumption
static auto make_full_type_parser() {
	const auto& registry = getParserRegistry();
	return registry.type < equal(T_EOF);
}

// Parse a type from source and return the AST
static TypePtr parse_type(const std::string &src) {
	std::stringstream ss(src);
	Lexer lex(ss);
	const auto &tokens = lex.tokenize();
	auto full = make_full_type_parser();
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
	return std::move(std::get<TypePtr>(result));
}

class TypeParserTest : public ::testing::Test {};

TEST_F(TypeParserTest, ParsesPrimitiveTypes) {
	struct Case { const char* src; PrimitiveType::Kind kind; } cases[] = {
			{"i32", PrimitiveType::I32},
			{"u32", PrimitiveType::U32},
			{"usize", PrimitiveType::USIZE},
			{"bool", PrimitiveType::BOOL},
			{"char", PrimitiveType::CHAR},
			{"str", PrimitiveType::STRING},
	};

	for (const auto &c : cases) {
		auto ty = parse_type(c.src);
		auto prim = dynamic_cast<PrimitiveType*>(ty.get());
		ASSERT_NE(prim, nullptr) << "expected PrimitiveType for " << c.src;
		EXPECT_EQ(prim->kind, c.kind);
	}
}

TEST_F(TypeParserTest, ParsesSharedReference) {
	auto ty = parse_type("& i32");
	auto refty = dynamic_cast<ReferenceType*>(ty.get());
	ASSERT_NE(refty, nullptr);
	EXPECT_FALSE(refty->is_mutable);
	auto inner = dynamic_cast<PrimitiveType*>(refty->referenced_type.get());
	ASSERT_NE(inner, nullptr);
	EXPECT_EQ(inner->kind, PrimitiveType::I32);
}

TEST_F(TypeParserTest, ParsesMutableReference) {
	auto ty = parse_type("& mut u32");
	auto refty = dynamic_cast<ReferenceType*>(ty.get());
	ASSERT_NE(refty, nullptr);
	EXPECT_TRUE(refty->is_mutable);
	auto inner = dynamic_cast<PrimitiveType*>(refty->referenced_type.get());
	ASSERT_NE(inner, nullptr);
	EXPECT_EQ(inner->kind, PrimitiveType::U32);
}

TEST_F(TypeParserTest, ParsesSliceType) {
	// Removed feature: slice types are not supported. This test has been disabled.
	SUCCEED();
}

TEST_F(TypeParserTest, ParsesArrayTypeWithusizeExpr) {
	// Array size expression uses the literal expression parser; use a typed number
	auto ty = parse_type("[u32; 4usize]");
	auto arr = dynamic_cast<ArrayType*>(ty.get());
	ASSERT_NE(arr, nullptr);
	auto elem = dynamic_cast<PrimitiveType*>(arr->element_type.get());
	ASSERT_NE(elem, nullptr);
	EXPECT_EQ(elem->kind, PrimitiveType::U32);
	auto size_expr = dynamic_cast<IntegerLiteralExpr*>(arr->size.get());
	ASSERT_NE(size_expr, nullptr);
	EXPECT_EQ(size_expr->value, 4);
    EXPECT_EQ(size_expr->type, IntegerLiteralExpr::USIZE);
}

TEST_F(TypeParserTest, ParsesTupleType) {
	// Removed feature: tuple types are not supported (except unit). This test has been disabled.
	SUCCEED();
}

TEST_F(TypeParserTest, ParsesPathTypeIdentifier) {
	auto ty = parse_type("MyType");
	auto pty = dynamic_cast<PathType*>(ty.get());
	ASSERT_NE(pty, nullptr);
	ASSERT_NE(pty->path, nullptr);
	const auto &segs = pty->path->getSegments();
	ASSERT_EQ(segs.size(), 1u);
	ASSERT_TRUE(segs[0].id.has_value());
	EXPECT_EQ((*segs[0].id)->getName(), "MyType");
}

TEST_F(TypeParserTest, ParsesPathTypeSelf) {
	auto ty = parse_type("Self");
	auto pty = dynamic_cast<PathType*>(ty.get());
	ASSERT_NE(pty, nullptr);
	const auto &segs = pty->path->getSegments();
	ASSERT_EQ(segs.size(), 1u);
	EXPECT_EQ(segs[0].type, PathSegType::SELF);
}

TEST_F(TypeParserTest, ParsesUnitType) {
    auto ty = parse_type("()");
    auto unit = dynamic_cast<UnitType*>(ty.get());
    ASSERT_NE(unit, nullptr);
}

TEST_F(TypeParserTest, ParsesDeeplyNestedTypes) {
    // Reference to an array of mutable references to a path type
    auto ty = parse_type("&[&mut my::Type; 10usize]");

    auto r1 = dynamic_cast<ReferenceType*>(ty.get());
    ASSERT_NE(r1, nullptr);
    EXPECT_FALSE(r1->is_mutable);

    auto arr = dynamic_cast<ArrayType*>(r1->referenced_type.get());
    ASSERT_NE(arr, nullptr);

    auto r2 = dynamic_cast<ReferenceType*>(arr->element_type.get());
    ASSERT_NE(r2, nullptr);
    EXPECT_TRUE(r2->is_mutable);

    auto p = dynamic_cast<PathType*>(r2->referenced_type.get());
    ASSERT_NE(p, nullptr);
    const auto& segs = p->path->getSegmentNames();
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_EQ(segs[0], "my");
    EXPECT_EQ(segs[1], "Type");
}