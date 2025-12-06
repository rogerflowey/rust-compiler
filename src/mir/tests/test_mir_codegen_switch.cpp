#include "mir/codegen/emitter.hpp"

#include "mir/mir.hpp"
#include "tests/catch_gtest_compat.hpp"
#include "type/type.hpp"

#include <string>

namespace {

mir::Constant make_int_constant(std::int64_t value, mir::TypeId type) {
    mir::Constant constant;
    constant.type = type;
    mir::IntConstant int_value;
    int_value.value = static_cast<std::uint64_t>(value < 0 ? -value : value);
    int_value.is_negative = value < 0;
    int_value.is_signed = true;
    constant.value = int_value;
    return constant;
}

mir::Statement make_define(mir::TempId dest, const mir::Constant &constant) {
    mir::DefineStatement define;
    define.dest = dest;
    mir::ConstantRValue rvalue;
    rvalue.constant = constant;
    define.rvalue.value = rvalue;
    mir::Statement stmt;
    stmt.value = define;
    return stmt;
}

} // namespace

TEST(MirEmitterTest, EmitsSwitchAndPhiControlFlow) {
    mir::MirModule module;

    const mir::TypeId int_type = type::get_typeID(type::Type{type::PrimitiveKind::I32});

    mir::MirFunction function;
    function.id = 0;
    function.name = "@switch_select";
    function.return_type = int_type;
    function.temp_types = {int_type, int_type, int_type, int_type};
    function.start_block = 0;

    // entry block: define discriminant and switch
    mir::BasicBlock entry;
    entry.statements.push_back(make_define(0, make_int_constant(0, int_type)));

    mir::SwitchIntTerminator switch_term;
    switch_term.discriminant.value = mir::TempId{0};

    mir::SwitchIntTarget case_zero;
    case_zero.match_value = make_int_constant(0, int_type);
    case_zero.block = 1;
    switch_term.targets.push_back(case_zero);

    switch_term.otherwise = 2;
    entry.terminator.value = switch_term;

    // case block 1: set value to 10 then branch to merge
    mir::BasicBlock case_block;
    case_block.statements.push_back(make_define(1, make_int_constant(10, int_type)));
    case_block.terminator.value = mir::GotoTerminator{.target = 3};

    // default block: set value to 20 then branch to merge
    mir::BasicBlock default_block;
    default_block.statements.push_back(make_define(2, make_int_constant(20, int_type)));
    default_block.terminator.value = mir::GotoTerminator{.target = 3};

    // merge block: phi over incoming values, return result
    mir::BasicBlock merge_block;
    mir::PhiNode phi;
    phi.dest = 3;
    phi.incoming.push_back(mir::PhiIncoming{.block = 1, .value = 1});
    phi.incoming.push_back(mir::PhiIncoming{.block = 2, .value = 2});
    merge_block.phis.push_back(phi);

    mir::ReturnTerminator ret;
    mir::Operand ret_operand;
    ret_operand.value = mir::TempId{3};
    ret.value = ret_operand;
    merge_block.terminator.value = ret;

    function.basic_blocks = {entry, case_block, default_block, merge_block};

    module.functions.push_back(function);

    codegen::Emitter emitter(module);
    std::string ir = emitter.emit();

    EXPECT_NE(ir.find("switch i32 %t0, label %bb2 [\n    i32 0, label %bb1\n  ]"),
              std::string::npos);
    EXPECT_NE(ir.find("%t3 = phi i32 [ %t1, %bb1 ], [ %t2, %bb2 ]"),
              std::string::npos);
    EXPECT_NE(ir.find("ret i32 %t3"), std::string::npos);
}
