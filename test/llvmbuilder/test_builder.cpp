#include "llvmbuilder/builder.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

TEST(LlvmBuilderTest, EmitsModuleWithOneFunction) {
    llvmbuilder::ModuleBuilder module("demo");
    module.set_data_layout("e-m:e-p270:32:32");
    module.set_target_triple("x86_64-unknown-linux-gnu");
    module.add_type_definition("Pair", "{ i32, i32 }");
    module.add_global("@counter = global i32 0");

    auto& fn = module.add_function("add", "i32",
                                   {{"i32", "lhs"}, {"i32", "rhs"}});
    auto& entry = fn.entry_block();
    const auto& params = fn.parameters();
    auto sum = entry.emit_binary("add", "i32", params[0].name, params[1].name, "sum");
    entry.emit_ret("i32", sum);

    const char* expected = R"(; ModuleID = 'demo'
target datalayout = "e-m:e-p270:32:32"
target triple = "x86_64-unknown-linux-gnu"

%Pair = type { i32, i32 }

@counter = global i32 0

define i32 @add(i32 %lhs, i32 %rhs) {
entry:
  %sum = add i32 %lhs, %rhs
  ret i32 %sum
}
)";

    EXPECT_EQ(module.str(), expected);
}

TEST(LlvmBuilderTest, EmitsBranchesAndPhi) {
    llvmbuilder::ModuleBuilder module;
    auto& fn = module.add_function("branchy", "i32");

    auto& entry = fn.entry_block();
    auto cond = entry.emit_icmp("eq", "i32", "0", "0", "cond");
    auto& left = fn.create_block("left");
    auto& right = fn.create_block("right");
    auto& exit = fn.create_block("exit");

    entry.emit_cond_br(cond, left.label(), right.label());

    auto left_val = left.emit_binary("add", "i32", "1", "2", "left_sum");
    left.emit_br(exit.label());

    auto right_val = right.emit_binary("mul", "i32", "3", "4", "right_prod");
    right.emit_br(exit.label());

    auto phi = exit.emit_phi("i32",
                             {{left_val, left.label()}, {right_val, right.label()}},
                             "select_val");
    exit.emit_ret("i32", phi);

    const std::string text = module.str();
    EXPECT_NE(text.find("br i1 %cond"), std::string::npos);
    EXPECT_NE(text.find("phi i32"), std::string::npos);
    EXPECT_NE(text.find("ret i32 %select_val"), std::string::npos);
}

} // namespace
