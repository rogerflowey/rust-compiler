#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "src/ast/expr.hpp"
#include "src/ast/type.hpp"
#include "src/lexer/lexer.hpp"
#include "src/parser/parser_registry.hpp"
#include "src/parser/utils.hpp"

using namespace parsec;

// Helper to safely get a pointer to the concrete node type from the variant wrapper
template <typename T, typename VariantPtr>
T* get_node(const VariantPtr& ptr) {
    if (!ptr) return nullptr;
    return std::get_if<T>(&(ptr->value));
}

// The setup is now incredibly simple.
static auto make_full_expr_parser() {
  const auto &registry = getParserRegistry();
  return registry.expr < equal(T_EOF);
}

static ExprPtr parse_expr(const std::string &src) {
  std::stringstream ss(src);
  Lexer lex(ss);
  const auto &tokens = lex.tokenize();
  auto full = make_full_expr_parser();
  auto result = run(full, tokens);
  if (std::holds_alternative<parsec::ParseError>(result)) {
    auto err = std::get<parsec::ParseError>(result);
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

    for(const auto& ctx : err.context_stack) {
        error_msg += "\n... while parsing " + ctx;
    }

    throw std::runtime_error(error_msg);
  }
  return std::move(std::get<ExprPtr>(result));
}

class ExprParserTest : public ::testing::Test {};

TEST_F(ExprParserTest, ParsesIntAndUintLiterals) {
  {
    auto e = parse_expr("123i32");
    auto i = get_node<IntegerLiteralExpr>(e);
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->value, 123);
    EXPECT_EQ(i->type, IntegerLiteralExpr::I32);
  }
  {
    auto e = parse_expr("5usize");
    auto u = get_node<IntegerLiteralExpr>(e);
    ASSERT_NE(u, nullptr);
    EXPECT_EQ(u->value, 5u);
    EXPECT_EQ(u->type, IntegerLiteralExpr::USIZE);
  }
  {
    auto e = parse_expr("42u32");
    auto u = get_node<IntegerLiteralExpr>(e);
    ASSERT_NE(u, nullptr);
    EXPECT_EQ(u->value, 42u);
    EXPECT_EQ(u->type, IntegerLiteralExpr::U32);
  }
  {
    auto e = parse_expr("100isize");
    auto i = get_node<IntegerLiteralExpr>(e);
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->value, 100);
    EXPECT_EQ(i->type, IntegerLiteralExpr::ISIZE);
  }
  {
    auto e = parse_expr("7"); // Default to i32
    auto i = get_node<IntegerLiteralExpr>(e);
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->value, 7);
    EXPECT_EQ(i->type, IntegerLiteralExpr::I32);
  }
}

TEST_F(ExprParserTest, ParsesGrouped) {
  {
    auto e = parse_expr("(1i32)");
    auto g = get_node<GroupedExpr>(e);
    ASSERT_NE(g, nullptr);
    auto i = get_node<IntegerLiteralExpr>(g->expr);
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->value, 1);
  }
}

TEST_F(ExprParserTest, ParsesArrayListAndRepeat) {
  {
    auto e = parse_expr("[]");
    auto a = get_node<ArrayInitExpr>(e);
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->elements.size(), 0u);
  }
  {
    auto e = parse_expr("[1i32, 2i32]");
    auto a = get_node<ArrayInitExpr>(e);
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->elements.size(), 2u);
    auto i0 = get_node<IntegerLiteralExpr>(a->elements[0]);
    auto i1 = get_node<IntegerLiteralExpr>(a->elements[1]);
    ASSERT_NE(i0, nullptr);
    ASSERT_NE(i1, nullptr);
    EXPECT_EQ(i0->value, 1);
    EXPECT_EQ(i1->value, 2);
  }
  {
    auto e = parse_expr("[1i32; 3i32]");
    auto r = get_node<ArrayRepeatExpr>(e);
    ASSERT_NE(r, nullptr);
    auto v = get_node<IntegerLiteralExpr>(r->value);
    auto c = get_node<IntegerLiteralExpr>(r->count);
    ASSERT_NE(v, nullptr);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(v->value, 1);
    EXPECT_EQ(c->value, 3);
  }
}

TEST_F(ExprParserTest, ParsesPostfixCallIndexFieldMethod) {
  {
    auto e = parse_expr("foo()");
    auto call = get_node<CallExpr>(e);
    ASSERT_NE(call, nullptr);
    auto callee = get_node<PathExpr>(call->callee);
    ASSERT_NE(callee, nullptr);
    ASSERT_EQ(call->args.size(), 0u);
  }
  {
    auto e = parse_expr("foo(1i32, 2i32)");
    auto call = get_node<CallExpr>(e);
    ASSERT_NE(call, nullptr);
    auto callee = get_node<PathExpr>(call->callee);
    ASSERT_NE(callee, nullptr);
    ASSERT_EQ(call->args.size(), 2u);
  }
  {
    auto e = parse_expr("arr[0i32]");
    auto idx = get_node<IndexExpr>(e);
    ASSERT_NE(idx, nullptr);
  }
  {
    auto e = parse_expr("obj.field");
    auto fld = get_node<FieldAccessExpr>(e);
    ASSERT_NE(fld, nullptr);
  }
  {
    auto e = parse_expr("obj.method(1i32)");
    auto m = get_node<MethodCallExpr>(e);
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->args.size(), 1u);
  }
  {
    auto e = parse_expr("a.b.c");
    auto f1 = get_node<FieldAccessExpr>(e);
    ASSERT_NE(f1, nullptr);
    EXPECT_EQ(f1->field_name->getName(), "c");
    auto f2 = get_node<FieldAccessExpr>(f1->object);
    ASSERT_NE(f2, nullptr);
    EXPECT_EQ(f2->field_name->getName(), "b");
    auto p = get_node<PathExpr>(f2->object);
    ASSERT_NE(p, nullptr);
  }
}

TEST_F(ExprParserTest, ParsesUnaryAndCastChain) {
  {
    auto e = parse_expr("!true");
    auto u = get_node<UnaryExpr>(e);
    ASSERT_NE(u, nullptr);
    EXPECT_EQ(u->op, UnaryExpr::NOT);
  }
  {
    auto e = parse_expr("-1i32");
    auto u = get_node<UnaryExpr>(e);
    ASSERT_NE(u, nullptr);
    EXPECT_EQ(u->op, UnaryExpr::NEGATE);
    auto i = get_node<IntegerLiteralExpr>(u->operand);
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->value, 1);
  }
  {
    auto e = parse_expr("1i32 as usize as i32");
    auto c1 = get_node<CastExpr>(e);
    ASSERT_NE(c1, nullptr);
    auto t1 = get_node<PrimitiveType>(c1->type);
    ASSERT_NE(t1, nullptr);
    // outer is i32
    EXPECT_EQ(t1->kind, PrimitiveType::I32);
    auto c0 = get_node<CastExpr>(c1->expr);
    ASSERT_NE(c0, nullptr);
    auto t0 = get_node<PrimitiveType>(c0->type);
    ASSERT_NE(t0, nullptr);
    EXPECT_EQ(t0->kind, PrimitiveType::USIZE);
  }
}

TEST_F(ExprParserTest, BinaryPrecedenceAndAssociativity) {
  // 1 + 2 * 3 => 1 + (2 * 3)
  auto e = parse_expr("1i32 + 2i32 * 3i32");
  auto add = get_node<BinaryExpr>(e);
  ASSERT_NE(add, nullptr);
  EXPECT_EQ(add->op, BinaryExpr::ADD);
  auto lhs = get_node<IntegerLiteralExpr>(add->left);
  ASSERT_NE(lhs, nullptr);
  EXPECT_EQ(lhs->value, 1);
  auto mul = get_node<BinaryExpr>(add->right);
  ASSERT_NE(mul, nullptr);
  EXPECT_EQ(mul->op, BinaryExpr::MUL);
}

TEST_F(ExprParserTest, AssignmentIsRightAssociative) {
  auto e = parse_expr("a = b = 1i32");
  auto asn_outer = get_node<AssignExpr>(e);
  ASSERT_NE(asn_outer, nullptr);
  EXPECT_EQ(asn_outer->op, AssignExpr::ASSIGN);
  auto asn_inner = get_node<AssignExpr>(asn_outer->right);
  ASSERT_NE(asn_inner, nullptr);
  EXPECT_EQ(asn_inner->op, AssignExpr::ASSIGN);
}

TEST_F(ExprParserTest, IfWhileLoopAndBlock) {
  {
    auto e = parse_expr("if true { 1i32 }");
    auto iff = get_node<IfExpr>(e);
    ASSERT_NE(iff, nullptr);
    ASSERT_NE(iff->then_branch, nullptr);
    ASSERT_FALSE(iff->else_branch.has_value());
  }
  {
    auto e = parse_expr("if true { 1i32 } else { 2i32 }");
    auto iff = get_node<IfExpr>(e);
    ASSERT_NE(iff, nullptr);
    ASSERT_NE(iff->then_branch, nullptr);
    ASSERT_TRUE(iff->then_branch->final_expr.has_value());
    auto then_i =
        get_node<IntegerLiteralExpr>(*iff->then_branch->final_expr);
    ASSERT_NE(then_i, nullptr);
    ASSERT_TRUE(iff->else_branch.has_value());
  }
  {
    auto e = parse_expr("while true { }");
    auto w = get_node<WhileExpr>(e);
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->body, nullptr);
    EXPECT_FALSE(w->body->final_expr.has_value());
    EXPECT_TRUE(w->body->statements.empty());
  }
  {
    auto e = parse_expr("loop { }");
    auto l = get_node<LoopExpr>(e);
    ASSERT_NE(l, nullptr);
    ASSERT_NE(l->body, nullptr);
  }
  {
    auto e = parse_expr("{ let x: i32 = 1i32; 2i32 }");
    auto b = get_node<BlockExpr>(e);
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->statements.size(), 1u);
    ASSERT_TRUE(b->final_expr.has_value());
    auto two = get_node<IntegerLiteralExpr>(*b->final_expr);
    ASSERT_NE(two, nullptr);
    EXPECT_EQ(two->value, 2);
  }
}

TEST_F(ExprParserTest, ParsesLiterals) {
  {
    auto e = parse_expr("true");
    auto b = get_node<BoolLiteralExpr>(e);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(b->value);
  }
  {
    auto e = parse_expr("'a'");
    auto c = get_node<CharLiteralExpr>(e);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->value, 'a');
  }
  {
    auto e = parse_expr(R"("hello")");
    auto s = get_node<StringLiteralExpr>(e);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->value, "hello");
  }
}
TEST_F(ExprParserTest, ParsesStructExpr) {
  {
    auto e = parse_expr("MyStruct {}");
    auto s = get_node<StructExpr>(e);
    ASSERT_NE(s, nullptr);
    auto p = s->path.get();
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->getSegments().size(), 1u);
    EXPECT_EQ(p->getSegmentNames()[0], "MyStruct");
    EXPECT_EQ(s->fields.size(), 0u);
  }
  {
    auto e = parse_expr("MyStruct { field1: 1i32 }");
    auto s = get_node<StructExpr>(e);
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->fields.size(), 1u);
    EXPECT_EQ(s->fields[0].name->getName(), "field1");
    auto i = get_node<IntegerLiteralExpr>(s->fields[0].value);
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->value, 1);
  }
  {
    auto e = parse_expr("MyStruct { field1: 1i32, field2: true }");
    auto s = get_node<StructExpr>(e);
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->fields.size(), 2u);
    EXPECT_EQ(s->fields[0].name->getName(), "field1");
    EXPECT_EQ(s->fields[1].name->getName(), "field2");
    auto i = get_node<IntegerLiteralExpr>(s->fields[0].value);
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->value, 1);
    auto b = get_node<BoolLiteralExpr>(s->fields[1].value);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(b->value);
  }
  {
      auto e = parse_expr("Outer { inner: Inner { x: 1i32 } }");
      auto outer = get_node<StructExpr>(e);
      ASSERT_NE(outer, nullptr);
      ASSERT_EQ(outer->fields.size(), 1u);
      EXPECT_EQ(outer->fields[0].name->getName(), "inner");
      auto inner = get_node<StructExpr>(outer->fields[0].value);
      ASSERT_NE(inner, nullptr);
      ASSERT_EQ(inner->fields.size(), 1u);
      EXPECT_EQ(inner->fields[0].name->getName(), "x");
      auto x = get_node<IntegerLiteralExpr>(inner->fields[0].value);
      ASSERT_NE(x, nullptr);
      EXPECT_EQ(x->value, 1);
  }
}
TEST_F(ExprParserTest, ComplexPostfixChain) {
  // This tests the parser's ability to handle a left-to-right chain of
  // different postfix operators: call -> field access -> index -> method call
  auto e = parse_expr("get_obj().field[0i32].process(true)");

  // Outer-most is the method call
  auto mcall = get_node<MethodCallExpr>(e);
  ASSERT_NE(mcall, nullptr);
  EXPECT_EQ(mcall->method_name->getName(), "process");
  ASSERT_EQ(mcall->args.size(), 1u);

  // Next is the index expression
  auto idx = get_node<IndexExpr>(mcall->receiver);
  ASSERT_NE(idx, nullptr);

  // Next is the field access
  auto fld = get_node<FieldAccessExpr>(idx->array);
  ASSERT_NE(fld, nullptr);
  EXPECT_EQ(fld->field_name->getName(), "field");

  // Innermost is the initial function call
  auto call = get_node<CallExpr>(fld->object);
  ASSERT_NE(call, nullptr);
  auto callee = get_node<PathExpr>(call->callee);
  ASSERT_NE(callee, nullptr);
  EXPECT_EQ(callee->path->getSegmentNames()[0], "get_obj");
}

TEST_F(ExprParserTest, PrecedenceWithUnaryAndCast) {
  // Test interaction between unary, cast, and binary operators.
  // Should parse as: `(-(1i32)) as isize * 2isize`
  auto e = parse_expr("-1i32 as isize * 2isize");
  auto mul = get_node<BinaryExpr>(e);
  ASSERT_NE(mul, nullptr);
  EXPECT_EQ(mul->op, BinaryExpr::MUL);

  auto cast = get_node<CastExpr>(mul->left);
  ASSERT_NE(cast, nullptr);
  auto ty = get_node<PrimitiveType>(cast->type);
  ASSERT_NE(ty, nullptr);
  EXPECT_EQ(ty->kind, PrimitiveType::ISIZE);

  auto neg = get_node<UnaryExpr>(cast->expr);
  ASSERT_NE(neg, nullptr);
  EXPECT_EQ(neg->op, UnaryExpr::NEGATE);
}

TEST_F(ExprParserTest, TrailingCommasInLiterals) {
  // Trailing commas are common corner cases in list-like structures.
  {
    auto e = parse_expr("[1i32, 2i32, ]");
    auto a = get_node<ArrayInitExpr>(e);
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->elements.size(), 2u);
  }
  {
    auto e = parse_expr("MyStruct { field1: 1i32, }");
    auto s = get_node<StructExpr>(e);
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->fields.size(), 1u);
  }
  {
    auto e = parse_expr("foo(1i32, )");
    auto c = get_node<CallExpr>(e);
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(c->args.size(), 1u);
  }
}

TEST_F(ExprParserTest, BlockAsExpressionValue) {
    // A block can be the value in a struct field or an argument in a call.
    {
        auto e = parse_expr("MyStruct { val: { let x = 1i32; x + 1i32 } }");
        auto s = get_node<StructExpr>(e);
        ASSERT_NE(s, nullptr);
        ASSERT_EQ(s->fields.size(), 1u);
        auto b = get_node<BlockExpr>(s->fields[0].value);
        ASSERT_NE(b, nullptr);
        ASSERT_TRUE(b->final_expr.has_value());
    }
    {
        auto e = parse_expr("if { let x = true; x } { 1i32 } else { 0i32 }");
        auto i = get_node<IfExpr>(e);
        ASSERT_NE(i, nullptr);
        auto cond_block = get_node<BlockExpr>(i->condition);
        ASSERT_NE(cond_block, nullptr);
    }
}