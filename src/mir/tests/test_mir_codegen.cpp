#include "mir/codegen/codegen.hpp"

#include "semantic/type/type.hpp"

#include "tests/catch_gtest_compat.hpp"

#include <cstdint>

namespace {

semantic::TypeId make_type(semantic::PrimitiveKind kind) {
    return semantic::get_typeID(semantic::Type{kind});
}

mir::Constant make_int_constant(semantic::TypeId type, std::uint64_t value) {
    mir::Constant constant;
    constant.type = type;
    mir::IntConstant payload;
    payload.value = value;
    payload.is_signed = true;
    payload.is_negative = false;
    constant.value = payload;
    return constant;
}

} // namespace

TEST(MirCodegenTest, EmitsConstantReturn) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    mir::MirFunction function;
    function.id = 0;
    function.name = "simple";
    function.return_type = int_type;

    mir::LocalInfo local;
    local.type = int_type;
    local.debug_name = "x";
    function.locals.push_back(local);

    function.temp_types.push_back(int_type);

    mir::BasicBlock block;

    mir::AssignStatement assign;
    assign.dest.base = mir::LocalPlace{0};
    assign.src.value = make_int_constant(int_type, 42);
    block.statements.push_back(mir::Statement{assign});

    mir::LoadStatement load;
    load.dest = 0;
    load.src.base = mir::LocalPlace{0};
    block.statements.push_back(mir::Statement{load});

    mir::ReturnTerminator ret;
    mir::Operand ret_operand;
    ret_operand.value = static_cast<mir::TempId>(0);
    ret.value = ret_operand;
    block.terminator.value = ret;

    function.basic_blocks.push_back(block);
    function.start_block = 0;

    mir::MirModule module;
    module.functions.push_back(function);

    const char* expected = R"(; ModuleID = 'rc-module'
define i32 @simple() {
entry:
  %x.slot = alloca i32
  store i32 42, ptr %x.slot
  %t0 = load i32, ptr %x.slot
  ret i32 %t0
}
)";

    EXPECT_EQ(mir::codegen::emit_llvm_ir(module), expected);
}

TEST(MirCodegenTest, EmitsParameterStore) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    mir::MirFunction function;
    function.id = 1;
    function.name = "identity";
    function.return_type = int_type;

    mir::LocalInfo param_local;
    param_local.type = int_type;
    param_local.debug_name = "x";
    function.locals.push_back(param_local);

    mir::FunctionParameter param;
    param.local = 0;
    param.type = int_type;
    param.name = "x";
    function.params.push_back(param);

    function.temp_types.push_back(int_type);

    mir::BasicBlock block;
    mir::LoadStatement load;
    load.dest = 0;
    load.src.base = mir::LocalPlace{0};
    block.statements.push_back(mir::Statement{load});

    mir::ReturnTerminator ret;
    mir::Operand ret_operand;
    ret_operand.value = static_cast<mir::TempId>(0);
    ret.value = ret_operand;
    block.terminator.value = ret;

    function.basic_blocks.push_back(block);
    function.start_block = 0;

    mir::MirModule module;
    module.functions.push_back(function);

    const char* expected = R"(; ModuleID = 'rc-module'
define i32 @identity(i32 %x) {
entry:
  %x.slot = alloca i32
  store i32 %x, ptr %x.slot
  %t0 = load i32, ptr %x.slot
  ret i32 %t0
}
)";

    EXPECT_EQ(mir::codegen::emit_llvm_ir(module), expected);
}
