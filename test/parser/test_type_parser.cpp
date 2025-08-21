#include <gtest/gtest.h>
#include <sstream>
#include <string>

#include "src/lexer/lexer.hpp"
#include "src/parser/expr_parse.hpp"
#include "src/parser/path_parse.hpp"
#include "src/parser/type_parse.hpp"
#include "src/parser/ast/type.hpp"
#include "src/parser/ast/expr.hpp"

using namespace parsec;

// Build a Type parser that also requires EOF to ensure full consumption
static auto make_full_type_parser() {
	ExprParserBuilder exprg;                // for array size expressions
	PathParserBuilder pathg;                // for path-based types
	TypeParserBuilder typeg(exprg.get_literal_parser(), pathg.get_parser());
	auto p = typeg.get_parser();
	return p < equal(Token{TOKEN_EOF, ""});
}

// Parse a type from source and return the AST
static TypePtr parse_type(const std::string &src) {
	std::stringstream ss(src);
	Lexer lex(ss);
	const auto &tokens = lex.tokenize();
	auto full = make_full_type_parser();
	auto result = run(full, tokens);
	if (!result) {
		throw std::runtime_error("Type parse failed for: " + src);
	}
	return std::move(*result);
}

class TypeParserTest : public ::testing::Test {};

TEST_F(TypeParserTest, ParsesPrimitiveTypes) {
	struct Case { const char* src; PrimitiveType::Kind kind; } cases[] = {
			{"i32", PrimitiveType::I32},
			{"u32", PrimitiveType::U32},
			{"usize", PrimitiveType::USIZE},
			{"bool", PrimitiveType::BOOL},
			{"char", PrimitiveType::CHAR},
			{"String", PrimitiveType::STRING},
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
	auto ty = parse_type("[bool]");
	auto slice = dynamic_cast<SliceType*>(ty.get());
	ASSERT_NE(slice, nullptr);
	auto elem = dynamic_cast<PrimitiveType*>(slice->element_type.get());
	ASSERT_NE(elem, nullptr);
	EXPECT_EQ(elem->kind, PrimitiveType::BOOL);
}

TEST_F(TypeParserTest, ParsesArrayTypeWithusizeExpr) {
	// Array size expression uses the literal expression parser; use a typed number
	auto ty = parse_type("[u32; 4usize]");
	auto arr = dynamic_cast<ArrayType*>(ty.get());
	ASSERT_NE(arr, nullptr);
	auto elem = dynamic_cast<PrimitiveType*>(arr->element_type.get());
	ASSERT_NE(elem, nullptr);
	EXPECT_EQ(elem->kind, PrimitiveType::U32);
	auto size_expr = dynamic_cast<UintLiteralExpr*>(arr->size.get());
	ASSERT_NE(size_expr, nullptr);
	EXPECT_EQ(size_expr->value, static_cast<unsigned long>(4));
}

TEST_F(TypeParserTest, ParsesTupleType) {
	auto ty = parse_type("(i32, bool, String)");
	auto tup = dynamic_cast<TupleType*>(ty.get());
	ASSERT_NE(tup, nullptr);
	ASSERT_EQ(tup->elements.size(), 3u);
	auto e0 = dynamic_cast<PrimitiveType*>(tup->elements[0].get());
	auto e1 = dynamic_cast<PrimitiveType*>(tup->elements[1].get());
	auto e2 = dynamic_cast<PrimitiveType*>(tup->elements[2].get());
	ASSERT_NE(e0, nullptr);
	ASSERT_NE(e1, nullptr);
	ASSERT_NE(e2, nullptr);
	EXPECT_EQ(e0->kind, PrimitiveType::I32);
	EXPECT_EQ(e1->kind, PrimitiveType::BOOL);
	EXPECT_EQ(e2->kind, PrimitiveType::STRING);
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

TEST_F(TypeParserTest, ParsesReferenceToSlice) {
	auto ty = parse_type("&[i32]");
	auto refty = dynamic_cast<ReferenceType*>(ty.get());
	ASSERT_NE(refty, nullptr);
	auto slice = dynamic_cast<SliceType*>(refty->referenced_type.get());
	ASSERT_NE(slice, nullptr);
	auto elem = dynamic_cast<PrimitiveType*>(slice->element_type.get());
	ASSERT_NE(elem, nullptr);
	EXPECT_EQ(elem->kind, PrimitiveType::I32);
}
