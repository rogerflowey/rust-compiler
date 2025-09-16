#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>
#include <variant>

#include "src/lexer/lexer.hpp"
#include "src/parser/parser.hpp"
#include "src/ast/item.hpp"
#include "src/ast/expr.hpp"
#include "src/ast/type.hpp"

using namespace parsec;

// Helper to safely get a pointer to the concrete node type from the variant wrapper
template <typename T, typename VariantPtr>
T* get_node(const VariantPtr& ptr) {
    if (!ptr) return nullptr;
    return std::get_if<T>(&(ptr->value));
}

static auto make_full_item_parser() {
    const auto &registry = getParserRegistry();
    return registry.item < equal(T_EOF);
}

static ItemPtr parse_item(const std::string &src) {
    std::stringstream ss(src);
    Lexer lex(ss);
    const auto &tokens = lex.tokenize();
    auto full = make_full_item_parser();
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
    return std::move(std::get<ItemPtr>(result));
}

class ItemParserTest : public ::testing::Test {};

TEST_F(ItemParserTest, ParsesFunctionNoReturnType) {
    auto it = parse_item("fn add(a: i32, b: i32) { a + b }");
    auto fn = get_node<FunctionItem>(it);
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name->name, "add"); // FIXED
    ASSERT_EQ(fn->params.size(), 2u);
    auto p0ty = get_node<PrimitiveType>(fn->params[0].second);
    auto p1ty = get_node<PrimitiveType>(fn->params[1].second);
    ASSERT_NE(p0ty, nullptr); ASSERT_NE(p1ty, nullptr);
    EXPECT_EQ(p0ty->kind, PrimitiveType::I32);
    EXPECT_EQ(p1ty->kind, PrimitiveType::I32);
    EXPECT_EQ(fn->return_type, nullptr);
    ASSERT_NE(fn->body, nullptr);
    ASSERT_TRUE(fn->body->final_expr.has_value());
}

TEST_F(ItemParserTest, ParsesFunctionWithReturnType) {
    auto it = parse_item("fn id(x: i32) -> i32 { x }");
    auto fn = get_node<FunctionItem>(it);
    ASSERT_NE(fn, nullptr);
    ASSERT_NE(fn->return_type, nullptr);
    auto rty = get_node<PrimitiveType>(fn->return_type);
    ASSERT_NE(rty, nullptr);
    EXPECT_EQ(rty->kind, PrimitiveType::I32);
}

TEST_F(ItemParserTest, ParsesFunctionNoParameters) {
    auto it = parse_item("fn answer() -> i32 { 42i32 }");
    auto fn = get_node<FunctionItem>(it);
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name->name, "answer"); // FIXED
    ASSERT_EQ(fn->params.size(), 0u);
    ASSERT_NE(fn->return_type, nullptr);
    auto rty = get_node<PrimitiveType>(fn->return_type);
    ASSERT_NE(rty, nullptr);
    EXPECT_EQ(rty->kind, PrimitiveType::I32);
    ASSERT_NE(fn->body, nullptr);
    ASSERT_TRUE(fn->body->final_expr.has_value());
}

TEST_F(ItemParserTest, ParsesFunctionEmptyBody) {
    auto it = parse_item("fn do_nothing(a: i32) {}");
    auto fn = get_node<FunctionItem>(it);
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name->name, "do_nothing"); // FIXED
    ASSERT_EQ(fn->params.size(), 1u);
    EXPECT_EQ(fn->return_type, nullptr);
    ASSERT_NE(fn->body, nullptr);
    EXPECT_TRUE(fn->body->statements.empty());
    EXPECT_FALSE(fn->body->final_expr.has_value());
}

TEST_F(ItemParserTest, ParsesStruct) {
    auto it = parse_item("struct Point { x: i32, y: i32 }");
    auto st = get_node<StructItem>(it);
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->name->name, "Point"); // FIXED
    ASSERT_EQ(st->fields.size(), 2u);
    auto fx = get_node<PrimitiveType>(st->fields[0].second);
    auto fy = get_node<PrimitiveType>(st->fields[1].second);
    ASSERT_NE(fx, nullptr); ASSERT_NE(fy, nullptr);
}

TEST_F(ItemParserTest, ParsesUnitLikeStruct) {
    auto it = parse_item("struct Unit;");
    auto st = get_node<StructItem>(it);
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->name->name, "Unit"); // FIXED
    ASSERT_TRUE(st->fields.empty());
}

TEST_F(ItemParserTest, ParsesEmptyStruct) {
    auto it = parse_item("struct Empty {}");
    auto st = get_node<StructItem>(it);
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->name->name, "Empty"); // FIXED
    ASSERT_TRUE(st->fields.empty());
}

TEST_F(ItemParserTest, ParsesEnum) {
    auto it = parse_item("enum Color { Red, Green, Blue }");
    auto en = get_node<EnumItem>(it);
    ASSERT_NE(en, nullptr);
    EXPECT_EQ(en->name->name, "Color"); // FIXED
    ASSERT_EQ(en->variants.size(), 3u);
    EXPECT_EQ(en->variants[0]->name, "Red"); // FIXED
}

TEST_F(ItemParserTest, ParsesEnumWithTrailingComma) {
    auto it = parse_item("enum Color { Red, Green, Blue, }");
    auto en = get_node<EnumItem>(it);
    ASSERT_NE(en, nullptr);
    EXPECT_EQ(en->name->name, "Color"); // FIXED
    ASSERT_EQ(en->variants.size(), 3u);
    EXPECT_EQ(en->variants[0]->name, "Red"); // FIXED
    EXPECT_EQ(en->variants[2]->name, "Blue"); // FIXED
}

TEST_F(ItemParserTest, ParsesEnumWithOneVariant) {
    auto it = parse_item("enum Singleton { Only }");
    auto en = get_node<EnumItem>(it);
    ASSERT_NE(en, nullptr);
    EXPECT_EQ(en->name->name, "Singleton"); // FIXED
    ASSERT_EQ(en->variants.size(), 1u);
    EXPECT_EQ(en->variants[0]->name, "Only"); // FIXED
}

TEST_F(ItemParserTest, ParsesEmptyEnum) {
    auto it = parse_item("enum Empty {}");
    auto en = get_node<EnumItem>(it);
    ASSERT_NE(en, nullptr);
    EXPECT_EQ(en->name->name, "Empty"); // FIXED
    ASSERT_TRUE(en->variants.empty());
}

TEST_F(ItemParserTest, ParsesConstItem) {
    auto it = parse_item("const MAX: i32 = 10i32;");
    auto ci = get_node<ConstItem>(it);
    ASSERT_NE(ci, nullptr);
    EXPECT_EQ(ci->name->name, "MAX"); // FIXED
    auto cty = get_node<PrimitiveType>(ci->type);
    ASSERT_NE(cty, nullptr);
    auto lit = get_node<IntegerLiteralExpr>(ci->value);
    ASSERT_NE(lit, nullptr);
    EXPECT_EQ(lit->value, 10);
}

TEST_F(ItemParserTest, ParsesConstItemBool) {
    auto it = parse_item("const ENABLED: bool = true;");
    auto ci = get_node<ConstItem>(it);
    ASSERT_NE(ci, nullptr);
    EXPECT_EQ(ci->name->name, "ENABLED"); // FIXED
    auto cty = get_node<PrimitiveType>(ci->type);
    ASSERT_NE(cty, nullptr);
    EXPECT_EQ(cty->kind, PrimitiveType::BOOL);
    auto lit = get_node<BoolLiteralExpr>(ci->value);
    ASSERT_NE(lit, nullptr);
    EXPECT_EQ(lit->value, true);
}

TEST_F(ItemParserTest, ParsesConstItemString) {
    auto it = parse_item("const MSG: &str = \"hello\";");
    auto ci = get_node<ConstItem>(it);
    ASSERT_NE(ci, nullptr);
    EXPECT_EQ(ci->name->name, "MSG"); // FIXED
    auto cty = get_node<ReferenceType>(ci->type);
    ASSERT_NE(cty, nullptr);
    ASSERT_FALSE(cty->is_mutable);
    auto pty = get_node<PrimitiveType>(cty->referenced_type);
    ASSERT_NE(pty, nullptr);
    EXPECT_EQ(pty->kind, PrimitiveType::STRING);
    auto lit = get_node<StringLiteralExpr>(ci->value);
    ASSERT_NE(lit, nullptr);
    EXPECT_EQ(lit->value, "hello");
}

TEST_F(ItemParserTest, ParsesEmptyTrait) {
    auto it = parse_item("trait Drawable {}");
    auto tr = get_node<TraitItem>(it);
    ASSERT_NE(tr, nullptr);
    EXPECT_TRUE(tr->items.empty());
}

TEST_F(ItemParserTest, ParsesTraitWithFunction) {
    auto it = parse_item("trait Printable { fn print(&self); }");
    auto tr = get_node<TraitItem>(it);
    ASSERT_NE(tr, nullptr);
    EXPECT_EQ(tr->name->name, "Printable"); // FIXED
    ASSERT_EQ(tr->items.size(), 1u);
    auto fn = get_node<FunctionItem>(tr->items[0]);
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name->name, "print"); // FIXED
    ASSERT_EQ(fn->params.size(), 0u);
    ASSERT_NE(fn->self_param, nullptr);
    EXPECT_FALSE(fn->self_param->is_mutable);
    EXPECT_TRUE(fn->self_param->is_reference);
}

TEST_F(ItemParserTest, ParsesInherentImplWithFunction) {
    auto it = parse_item("impl i32 { fn zero() -> i32 { 0i32 } }");
    auto im = get_node<InherentImplItem>(it);
    ASSERT_NE(im, nullptr);
    auto for_prim = get_node<PrimitiveType>(im->for_type);
    ASSERT_NE(for_prim, nullptr);
    EXPECT_EQ(for_prim->kind, PrimitiveType::I32);
    ASSERT_EQ(im->items.size(), 1u);
    auto fn = get_node<FunctionItem>(im->items[0]);
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name->name, "zero"); // FIXED
}

TEST_F(ItemParserTest, ParsesInherentImplWithMultipleFunctions) {
    auto it = parse_item("impl Point { fn new() -> Point {} fn x(&self) -> i32 {} }");
    auto im = get_node<InherentImplItem>(it);
    ASSERT_NE(im, nullptr);
    auto for_path = get_node<PathType>(im->for_type);
    ASSERT_NE(for_path, nullptr);
    EXPECT_EQ((*for_path->path->segments[0].id)->name, "Point"); // FIXED
    ASSERT_EQ(im->items.size(), 2u);
    auto fn1 = get_node<FunctionItem>(im->items[0]);
    ASSERT_NE(fn1, nullptr);
    EXPECT_EQ(fn1->name->name, "new"); // FIXED
    auto fn2 = get_node<FunctionItem>(im->items[1]);
    ASSERT_NE(fn2, nullptr);
    EXPECT_EQ(fn2->name->name, "x"); // FIXED
}

TEST_F(ItemParserTest, ParsesTraitImpl) {
    auto it = parse_item("impl Display for i32 { }");
    auto im = get_node<TraitImplItem>(it);
    ASSERT_NE(im, nullptr);
    ASSERT_NE(im->trait_name, nullptr);
    EXPECT_EQ(im->trait_name->name, "Display"); // FIXED
    auto for_prim = get_node<PrimitiveType>(im->for_type);
    ASSERT_NE(for_prim, nullptr);
    EXPECT_EQ(for_prim->kind, PrimitiveType::I32);
    EXPECT_TRUE(im->items.empty());
}

TEST_F(ItemParserTest, FunctionWithSelf) {
    auto it = parse_item(R"(
        impl T {
            fn a(self) {}
            fn b(&self) {}
            fn c(&mut self) {}
            fn d(mut self) {}
            fn g(self, other: Other) {}
        }
    )");
    auto* impl = get_node<InherentImplItem>(it);
    ASSERT_NE(impl, nullptr);
    ASSERT_EQ(impl->items.size(), 5);

    {
        auto* func = get_node<FunctionItem>(impl->items[0]);
        ASSERT_NE(func, nullptr);
        ASSERT_NE(func->self_param, nullptr);
        auto* self = func->self_param.get();
        ASSERT_NE(self, nullptr);
        ASSERT_FALSE(self->is_reference);
        ASSERT_FALSE(self->is_mutable);
    }
    {
        auto* func = get_node<FunctionItem>(impl->items[1]);
        ASSERT_NE(func, nullptr);
        ASSERT_NE(func->self_param, nullptr);
        auto* self = func->self_param.get();
        ASSERT_NE(self, nullptr);
        ASSERT_TRUE(self->is_reference);
        ASSERT_FALSE(self->is_mutable);
    }
    {
        auto* func = get_node<FunctionItem>(impl->items[2]);
        ASSERT_NE(func, nullptr);
        ASSERT_NE(func->self_param, nullptr);
        auto* self = func->self_param.get();
        ASSERT_NE(self, nullptr);
        ASSERT_TRUE(self->is_reference);
        ASSERT_TRUE(self->is_mutable);
    }
    {
        auto* func = get_node<FunctionItem>(impl->items[3]);
        ASSERT_NE(func, nullptr);
        ASSERT_NE(func->self_param, nullptr);
        auto* self = func->self_param.get();
        ASSERT_NE(self, nullptr);
        ASSERT_FALSE(self->is_reference);
        ASSERT_TRUE(self->is_mutable);
    }
    {
        auto* func = get_node<FunctionItem>(impl->items[4]);
        ASSERT_NE(func, nullptr);
        ASSERT_NE(func->self_param, nullptr);
        auto* self = func->self_param.get();
        ASSERT_NE(self, nullptr);
        ASSERT_FALSE(self->is_reference);
        ASSERT_FALSE(self->is_mutable);
        ASSERT_EQ(func->params.size(), 1);
    }
}

TEST_F(ItemParserTest, FunctionWithComplexReturnType) {
    // Test function signatures with more complex, nested types.
    {
        auto it = parse_item("fn get_ref() -> &i32 { }");
        auto fn = get_node<FunctionItem>(it);
        ASSERT_NE(fn, nullptr);
        ASSERT_NE(fn->return_type, nullptr);
        auto ref_ty = get_node<ReferenceType>(fn->return_type);
        ASSERT_NE(ref_ty, nullptr);
        EXPECT_FALSE(ref_ty->is_mutable);
    }
    {
        auto it = parse_item("fn get_arr() -> [u32; 4usize] { }");
        auto fn = get_node<FunctionItem>(it);
        ASSERT_NE(fn, nullptr);
        ASSERT_NE(fn->return_type, nullptr);
        auto arr_ty = get_node<ArrayType>(fn->return_type);
        ASSERT_NE(arr_ty, nullptr);
    }
}

TEST_F(ItemParserTest, StructWithComplexFieldsAndTrailingComma) {
    auto it = parse_item("struct Node { next: &Node, val: i32, }");
    auto st = get_node<StructItem>(it);
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->name->name, "Node"); // FIXED
    ASSERT_EQ(st->fields.size(), 2u);

    EXPECT_EQ(st->fields[0].first->name, "next"); // FIXED
    auto f0_ty = get_node<ReferenceType>(st->fields[0].second);
    ASSERT_NE(f0_ty, nullptr);
    auto inner_path = get_node<PathType>(f0_ty->referenced_type);
    ASSERT_NE(inner_path, nullptr);
    EXPECT_EQ((*inner_path->path->segments[0].id)->name, "Node"); // FIXED

    EXPECT_EQ(st->fields[1].first->name, "val"); // FIXED
    auto f1_ty = get_node<PrimitiveType>(st->fields[1].second);
    ASSERT_NE(f1_ty, nullptr);
    EXPECT_EQ(f1_ty->kind, PrimitiveType::I32);
}

TEST_F(ItemParserTest, ImplWithAssociatedConst) {
    // The spec allows items inside impl blocks. Let's test a const item.
    auto it = parse_item(R"(
        impl Point {
            const ORIGIN: Point = Point { x: 0i32, y: 0i32 };
            fn is_origin(&self) -> bool {
                self.x == 0i32 && self.y == 0i32
            }
        }
    )");
    auto im = get_node<InherentImplItem>(it);
    ASSERT_NE(im, nullptr);
    ASSERT_EQ(im->items.size(), 2u);

    auto ci = get_node<ConstItem>(im->items[0]);
    ASSERT_NE(ci, nullptr);
    EXPECT_EQ(ci->name->name, "ORIGIN"); // FIXED

    auto fn = get_node<FunctionItem>(im->items[1]);
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name->name, "is_origin"); // FIXED
}