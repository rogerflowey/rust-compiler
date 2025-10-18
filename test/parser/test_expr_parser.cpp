#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "src/ast/expr.hpp"
#include "src/ast/type.hpp"
#include "src/lexer/lexer.hpp"

#include "src/parser/parser.hpp"

using namespace parsec;
using namespace ast;

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
    EXPECT_EQ(i->type, IntegerLiteralExpr::NOT_SPECIFIED);
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
    EXPECT_EQ(f1->field_name->name, "c"); // FIXED
    auto f2 = get_node<FieldAccessExpr>(f1->object);
    ASSERT_NE(f2, nullptr);
    EXPECT_EQ(f2->field_name->name, "b"); // FIXED
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
    ASSERT_EQ(p->segments.size(), 1u); // FIXED
    EXPECT_EQ((*p->segments[0].id)->name, "MyStruct"); // FIXED
    EXPECT_EQ(s->fields.size(), 0u);
  }
  {
    auto e = parse_expr("MyStruct { field1: 1i32 }");
    auto s = get_node<StructExpr>(e);
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->fields.size(), 1u);
    EXPECT_EQ(s->fields[0].name->name, "field1"); // FIXED
    auto i = get_node<IntegerLiteralExpr>(s->fields[0].value);
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->value, 1);
  }
  {
    auto e = parse_expr("MyStruct { field1: 1i32, field2: true }");
    auto s = get_node<StructExpr>(e);
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->fields.size(), 2u);
    EXPECT_EQ(s->fields[0].name->name, "field1"); // FIXED
    EXPECT_EQ(s->fields[1].name->name, "field2"); // FIXED
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
      EXPECT_EQ(outer->fields[0].name->name, "inner"); // FIXED
      auto inner = get_node<StructExpr>(outer->fields[0].value);
      ASSERT_NE(inner, nullptr);
      ASSERT_EQ(inner->fields.size(), 1u);
      EXPECT_EQ(inner->fields[0].name->name, "x"); // FIXED
      auto x = get_node<IntegerLiteralExpr>(inner->fields[0].value);
      ASSERT_NE(x, nullptr);
      EXPECT_EQ(x->value, 1);
  }
}
TEST_F(ExprParserTest, ComplexPostfixChain) {
  auto e = parse_expr("get_obj().field[0i32].process(true)");

  // Outer-most is the method call
  auto mcall = get_node<MethodCallExpr>(e);
  ASSERT_NE(mcall, nullptr);
  EXPECT_EQ(mcall->method_name->name, "process"); // FIXED
  ASSERT_EQ(mcall->args.size(), 1u);

  // Next is the index expression
  auto idx = get_node<IndexExpr>(mcall->receiver);
  ASSERT_NE(idx, nullptr);

  // Next is the field access
  auto fld = get_node<FieldAccessExpr>(idx->array);
  ASSERT_NE(fld, nullptr);
  EXPECT_EQ(fld->field_name->name, "field"); // FIXED

  // Innermost is the initial function call
  auto call = get_node<CallExpr>(fld->object);
  ASSERT_NE(call, nullptr);
  auto callee = get_node<PathExpr>(call->callee);
  ASSERT_NE(callee, nullptr);
  EXPECT_EQ((*callee->path->segments[0].id)->name, "get_obj"); // FIXED
}

TEST_F(ExprParserTest, PrecedenceWithUnaryAndCast) {
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

TEST_F(ExprParserTest, BlockFinalExprAbsorbsTrailingWithBlock) {
    auto e = parse_expr("{ if true { 1i32 } }");
    auto block = get_node<BlockExpr>(e);
    ASSERT_NE(block, nullptr);
    EXPECT_TRUE(block->statements.empty());
    ASSERT_TRUE(block->final_expr.has_value());
    auto iff = get_node<IfExpr>(*block->final_expr);
    ASSERT_NE(iff, nullptr);
}

TEST_F(ExprParserTest, BlockFinalExprAbsorbsTrailingIfElseChain) {
  auto e = parse_expr(
    "{ if low == high { return a[low]; } let p: usize = partition(a, low, high); "
    "if k == p { a[p] } else if k < p { select_k(a, low, p - 1, k) } else { select_k(a, p + 1, high, k) } }"
  );
  auto block = get_node<BlockExpr>(e);
  ASSERT_NE(block, nullptr);
  ASSERT_EQ(block->statements.size(), 2u);
  ASSERT_TRUE(block->final_expr.has_value());

  auto final_if = get_node<IfExpr>(*block->final_expr);
  ASSERT_NE(final_if, nullptr);
  ASSERT_TRUE(final_if->else_branch.has_value());

  auto else_if = get_node<IfExpr>(*final_if->else_branch);
  ASSERT_NE(else_if, nullptr);
  ASSERT_TRUE(else_if->else_branch.has_value());
  auto else_block = get_node<BlockExpr>(*else_if->else_branch);
  ASSERT_NE(else_block, nullptr);
}

TEST_F(ExprParserTest, PrecedenceInteractions) {
  {
    auto e = parse_expr("!visited[i]");

    // The root of the AST should be the Unary NOT operator.
    auto unary_not = get_node<UnaryExpr>(e);
    ASSERT_NE(unary_not, nullptr);
    EXPECT_EQ(unary_not->op, UnaryExpr::NOT);

    // Its operand should be the Index expression `visited[i]`.
    auto index_expr = get_node<IndexExpr>(unary_not->operand);
    ASSERT_NE(index_expr, nullptr);

    // The array part of the index should be the path "visited".
    auto array_path = get_node<PathExpr>(index_expr->array);
    ASSERT_NE(array_path, nullptr);
    ASSERT_EQ(array_path->path->segments.size(), 1u);
    EXPECT_EQ((*array_path->path->segments[0].id)->name, "visited");

    // The index part of the index should be the path "i".
    auto index_path = get_node<PathExpr>(index_expr->index);
    ASSERT_NE(index_path, nullptr);
    ASSERT_EQ(index_path->path->segments.size(), 1u);
    EXPECT_EQ((*index_path->path->segments[0].id)->name, "i");
  }
  {
    auto e = parse_expr("foo.bar + 1i32");
    auto bin_add = get_node<BinaryExpr>(e);
    ASSERT_NE(bin_add, nullptr);
    EXPECT_EQ(bin_add->op, BinaryExpr::ADD);

    // The left-hand side should be the field access.
    auto field_access = get_node<FieldAccessExpr>(bin_add->left);
    ASSERT_NE(field_access, nullptr);
    EXPECT_EQ(field_access->field_name->name, "bar");

    // The right-hand side should be the integer literal.
    auto literal = get_node<IntegerLiteralExpr>(bin_add->right);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->value, 1);
  }
  {
    auto e = parse_expr("-x * y");
    auto bin_mul = get_node<BinaryExpr>(e);
    ASSERT_NE(bin_mul, nullptr);
    EXPECT_EQ(bin_mul->op, BinaryExpr::MUL);

    // The left-hand side should be the unary negation.
    auto unary_neg = get_node<UnaryExpr>(bin_mul->left);
    ASSERT_NE(unary_neg, nullptr);
    EXPECT_EQ(unary_neg->op, UnaryExpr::NEGATE);

    // The right-hand side should be the path "y".
    auto path_y = get_node<PathExpr>(bin_mul->right);
    ASSERT_NE(path_y, nullptr);
  }
}TEST_F(ExprParserTest, CastAndOperatorPrecedence) {
  auto e = parse_expr("*ptr as &mut u32 > 0u32");

  // The root should be the binary Greater Than operator.
  auto bin_gt = get_node<BinaryExpr>(e);
  ASSERT_NE(bin_gt, nullptr);
  EXPECT_EQ(bin_gt->op, BinaryExpr::GT);

  // The right side is the simple literal.
  auto literal_zero = get_node<IntegerLiteralExpr>(bin_gt->right);
  ASSERT_NE(literal_zero, nullptr);
  EXPECT_EQ(literal_zero->value, 0);

  auto cast_expr = get_node<CastExpr>(bin_gt->left);
  ASSERT_NE(cast_expr, nullptr);

  // The expression *being cast* is the UnaryExpr `*ptr`.
  auto unary_deref = get_node<UnaryExpr>(cast_expr->expr);
  ASSERT_NE(unary_deref, nullptr);
  EXPECT_EQ(unary_deref->op, UnaryExpr::DEREFERENCE);

  // The operand of the dereference is the path "ptr".
  auto path_ptr = get_node<PathExpr>(unary_deref->operand);
  ASSERT_NE(path_ptr, nullptr);
  
  // The type being cast to is `&mut u32`.
  auto ref_type = get_node<ReferenceType>(cast_expr->type);
  ASSERT_NE(ref_type, nullptr);
  EXPECT_TRUE(ref_type->is_mutable);
  auto referenced_primitive = get_node<PrimitiveType>(ref_type->referenced_type);
  ASSERT_NE(referenced_primitive, nullptr);
  EXPECT_EQ(referenced_primitive->kind, PrimitiveType::U32);
}

TEST_F(ExprParserTest, FullPrecedenceChain) {
  auto e = parse_expr("x && *&obj.calculate(y)[0] as i32 < 100i32");

  // 1. The lowest precedence operator is `&&`.
  auto logical_and = get_node<BinaryExpr>(e);
  ASSERT_NE(logical_and, nullptr);
  EXPECT_EQ(logical_and->op, BinaryExpr::AND);
  auto path_x = get_node<PathExpr>(logical_and->left);
  ASSERT_NE(path_x, nullptr);

  // 2. The next lowest is `<`.
  auto less_than = get_node<BinaryExpr>(logical_and->right);
  ASSERT_NE(less_than, nullptr);
  EXPECT_EQ(less_than->op, BinaryExpr::LT);
  auto literal_100 = get_node<IntegerLiteralExpr>(less_than->right);
  ASSERT_NE(literal_100, nullptr);

  // 3. Next is the `as` cast.
  auto cast_expr = get_node<CastExpr>(less_than->left);
  ASSERT_NE(cast_expr, nullptr);
  auto type_i32 = get_node<PrimitiveType>(cast_expr->type);
  ASSERT_NE(type_i32, nullptr);
  EXPECT_EQ(type_i32->kind, PrimitiveType::I32);

  // 4. Next is the prefix `*` (dereference).
  auto deref_op = get_node<UnaryExpr>(cast_expr->expr);
  ASSERT_NE(deref_op, nullptr);
  EXPECT_EQ(deref_op->op, UnaryExpr::DEREFERENCE);

  // 5. Next is the prefix `&` (reference).
  auto ref_op = get_node<UnaryExpr>(deref_op->operand);
  ASSERT_NE(ref_op, nullptr);
  EXPECT_EQ(ref_op->op, UnaryExpr::REFERENCE);

  // 6. Next is the postfix `[]` (index).
  auto index_op = get_node<IndexExpr>(ref_op->operand);
  ASSERT_NE(index_op, nullptr);
  e = parse_expr("x && *&obj.calculate(y)[0i32] as i32 < 100i32");
  logical_and = get_node<BinaryExpr>(e);
  less_than = get_node<BinaryExpr>(logical_and->right);
  cast_expr = get_node<CastExpr>(less_than->left);
  deref_op = get_node<UnaryExpr>(cast_expr->expr);
  ref_op = get_node<UnaryExpr>(deref_op->operand);
  index_op = get_node<IndexExpr>(ref_op->operand);
  ASSERT_NE(index_op, nullptr);
  auto literal_0_i32 = get_node<IntegerLiteralExpr>(index_op->index);
  ASSERT_NE(literal_0_i32, nullptr);
  EXPECT_EQ(literal_0_i32->value, 0);

  auto method_call = get_node<MethodCallExpr>(index_op->array);
  ASSERT_NE(method_call, nullptr);
  EXPECT_EQ(method_call->method_name->name, "calculate");
  ASSERT_EQ(method_call->args.size(), 1u);

  auto path_obj = get_node<PathExpr>(method_call->receiver);
  ASSERT_NE(path_obj, nullptr);
  EXPECT_EQ((*path_obj->path->segments[0].id)->name, "obj");
}

TEST_F(ExprParserTest, LogicalVsComparisonPrecedence) {
  auto e = parse_expr("a > b && c < d");

  auto logical_and = get_node<BinaryExpr>(e);
  ASSERT_NE(logical_and, nullptr);
  EXPECT_EQ(logical_and->op, BinaryExpr::AND);

  auto gt_expr = get_node<BinaryExpr>(logical_and->left);
  ASSERT_NE(gt_expr, nullptr);
  EXPECT_EQ(gt_expr->op, BinaryExpr::GT);
  auto path_a = get_node<PathExpr>(gt_expr->left);
  auto path_b = get_node<PathExpr>(gt_expr->right);
  ASSERT_NE(path_a, nullptr);
  ASSERT_NE(path_b, nullptr);

  auto lt_expr = get_node<BinaryExpr>(logical_and->right);
  ASSERT_NE(lt_expr, nullptr);
  EXPECT_EQ(lt_expr->op, BinaryExpr::LT);
  auto path_c = get_node<PathExpr>(lt_expr->left);
  auto path_d = get_node<PathExpr>(lt_expr->right);
  ASSERT_NE(path_c, nullptr);
  ASSERT_NE(path_d, nullptr);
}

TEST_F(ExprParserTest, BitwiseXorPrecedence) {
  auto e = parse_expr("a & b ^ c || d");
  auto or_op = get_node<BinaryExpr>(e);
  ASSERT_NE(or_op, nullptr);
  EXPECT_EQ(or_op->op, BinaryExpr::OR);

  auto xor_op = get_node<BinaryExpr>(or_op->left);
  ASSERT_NE(xor_op, nullptr);
  EXPECT_EQ(xor_op->op, BinaryExpr::BIT_XOR);

  auto and_op = get_node<BinaryExpr>(xor_op->left);
  ASSERT_NE(and_op, nullptr);
  EXPECT_EQ(and_op->op, BinaryExpr::BIT_AND);
}

TEST_F(ExprParserTest, BitwiseXorAssignment) {
    auto e = parse_expr("a ^= b");
    auto assign_op = get_node<AssignExpr>(e);
    ASSERT_NE(assign_op, nullptr);
    EXPECT_EQ(assign_op->op, AssignExpr::XOR_ASSIGN);
}

TEST_F(ExprParserTest, XorAssignmentIsRightAssociative) {
  auto e = parse_expr("a ^= b ^= c");
  auto outer_assign = get_node<AssignExpr>(e);
  ASSERT_NE(outer_assign, nullptr);
  EXPECT_EQ(outer_assign->op, AssignExpr::XOR_ASSIGN);

  auto inner_assign = get_node<AssignExpr>(outer_assign->right);
  ASSERT_NE(inner_assign, nullptr);
  EXPECT_EQ(inner_assign->op, AssignExpr::XOR_ASSIGN);
}
