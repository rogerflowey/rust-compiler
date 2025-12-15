#include "mir/codegen/llvmbuilder/builder.hpp"

#include "tests/catch_gtest_compat.hpp"

#include <string>

TEST(LLVMBuilderTest, EmitsBranchesAndPhi) {
    llvmbuilder::ModuleBuilder module("demo");

    auto &fn = module.add_function(
        "select", "i32",
        {llvmbuilder::FunctionParameter{"i32", "", {}},
         llvmbuilder::FunctionParameter{"i32", "", {}},
         llvmbuilder::FunctionParameter{"i1", "", {}}});

    auto &then_block = fn.create_block("then");
    auto &else_block = fn.create_block("else");
    auto &merge_block = fn.create_block("merge");

    auto &entry = fn.entry_block();
    entry.emit_cond_br("%arg2", then_block.label(), else_block.label());

    auto then_val = then_block.emit_binary("add", "i32", "%arg0", "1", "then_add");
    then_block.emit_br(merge_block.label());

    auto else_val = else_block.emit_binary("sub", "i32", "%arg1", "1", "else_sub");
    else_block.emit_br(merge_block.label());

    auto phi = merge_block.emit_phi(
        "i32",
        {{then_val, then_block.label()}, {else_val, else_block.label()}},
        "sel");
    merge_block.emit_ret("i32", phi);

    const std::string expected =
        "; ModuleID = 'demo'\n"
        "define i32 @select(i32 %arg0, i32 %arg1, i1 %arg2) {\n"
        "entry:\n"
        "  br i1 %arg2, label %then, label %else\n"
        "then:\n"
        "  %then_add = add i32 %arg0, 1\n"
        "  br label %merge\n"
        "else:\n"
        "  %else_sub = sub i32 %arg1, 1\n"
        "  br label %merge\n"
        "merge:\n"
        "  %sel = phi i32 [ %then_add, %then ], [ %else_sub, %else ]\n"
        "  ret i32 %sel\n"
        "}\n";

    EXPECT_EQ(module.str(), expected);
}
