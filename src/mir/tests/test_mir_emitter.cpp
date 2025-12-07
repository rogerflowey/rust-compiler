#include "mir/codegen/emitter.hpp"

#include "tests/catch_gtest_compat.hpp"
#include "type/type.hpp"

#include <cstdint>
#include <string>

namespace {

mir::Statement make_define(mir::TempId dest, const mir::Constant& constant) {
    mir::DefineStatement define;
    define.dest = dest;
    mir::ConstantRValue rvalue;
    rvalue.constant = constant;
    define.rvalue.value = rvalue;
    mir::Statement stmt;
    stmt.value = define;
    return stmt;
}

mir::Constant make_int_constant(std::int64_t value) {
    mir::Constant constant;
    constant.type = type::get_typeID(type::Type{type::PrimitiveKind::I32});
    mir::IntConstant int_value;
    int_value.value = static_cast<std::uint64_t>(value < 0 ? -value : value);
    int_value.is_negative = value < 0;
    int_value.is_signed = true;
    constant.value = int_value;
    return constant;
}

mir::Constant make_string_constant(const std::string& text, type::TypeId pointer_type) {
    mir::Constant constant;
    constant.type = pointer_type;
    mir::StringConstant str;
    str.data = text;
    str.length = text.size();
    str.is_cstyle = true;
    constant.value = str;
    return constant;
}

} // namespace

TEST(MirEmitterTest, EmitsDeterministicTempsAndStrings) {
    mir::MirModule module;

    mir::MirFunction function;
    function.id = 0;
    function.name = "@deterministic";

    type::TypeId int_type = type::get_typeID(type::Type{type::PrimitiveKind::I32});
    type::TypeId char_type = type::get_typeID(type::Type{type::PrimitiveKind::CHAR});
    type::ReferenceType ref_char{.referenced_type = char_type, .is_mutable = false};
    type::TypeId char_ptr_type = type::get_typeID(type::Type{ref_char});

    function.return_type = int_type;
    function.temp_types = {int_type, char_ptr_type};

    mir::BasicBlock entry;
    entry.statements.push_back(make_define(0, make_int_constant(5)));
    entry.statements.push_back(make_define(1, make_string_constant("hello", char_ptr_type)));

    mir::ReturnTerminator ret;
    mir::Operand ret_operand;
    ret_operand.value = mir::TempId{0};
    ret.value = ret_operand;
    entry.terminator.value = ret;

    function.basic_blocks.push_back(entry);
    function.start_block = 0;

    module.functions.push_back(std::move(function));

    codegen::Emitter emitter(module);
    std::string ir = emitter.emit();

    EXPECT_NE(ir.find("%t0 = add i32 0, 5"), std::string::npos);
    EXPECT_NE(ir.find("%t1"), std::string::npos);

    auto first_str = ir.find("@str.0 =");
    EXPECT_NE(first_str, std::string::npos);
    EXPECT_EQ(first_str, ir.rfind("@str.0 ="));
}

TEST(MirEmitterTest, EmitsExternalFunctionDeclarations) {
    mir::MirModule module;
    
    // Create types
    type::TypeId unit_type = type::get_typeID(type::Type{type::UnitType{}});
    type::TypeId i32_type = type::get_typeID(type::Type{type::PrimitiveKind::I32});
    type::TypeId char_type = type::get_typeID(type::Type{type::PrimitiveKind::CHAR});
    type::ReferenceType char_ptr{.referenced_type = char_type, .is_mutable = false};
    type::TypeId char_ptr_type = type::get_typeID(type::Type{char_ptr});
    
    // Add an external function: print(char*) -> unit
    mir::ExternalFunction ext_fn;
    ext_fn.id = 0;
    ext_fn.name = "print";
    ext_fn.return_type = unit_type;
    ext_fn.param_types.push_back(char_ptr_type);
    
    module.external_functions.push_back(ext_fn);
    
    // Add another external function: getInt() -> i32
    mir::ExternalFunction getint_fn;
    getint_fn.id = 1;
    getint_fn.name = "getInt";
    getint_fn.return_type = i32_type;
    
    module.external_functions.push_back(getint_fn);
    
    codegen::Emitter emitter(module);
    std::string ir = emitter.emit();
    
    // Check that external declarations are emitted
    EXPECT_NE(ir.find("declare dso_local"), std::string::npos);
    EXPECT_NE(ir.find("@print"), std::string::npos);
    EXPECT_NE(ir.find("@getInt"), std::string::npos);
}

TEST(MirEmitterTest, OptimizesArrayRepeatWithZeroInitializer) {
    mir::MirModule module;

    mir::MirFunction function;
    function.id = 0;
    function.name = "@test_zero_array";

    type::TypeId int_type = type::get_typeID(type::Type{type::PrimitiveKind::I32});
    type::TypeId array_type = type::get_typeID(type::Type{type::ArrayType{int_type, 10}});

    function.return_type = array_type;
    function.temp_types = {array_type};

    mir::BasicBlock entry;
    
    // Create array repeat with zero value: [0; 10]
    mir::ArrayRepeatRValue repeat;
    mir::Constant zero;
    zero.type = int_type;
    mir::IntConstant zero_val;
    zero_val.value = 0;
    zero_val.is_negative = false;
    zero_val.is_signed = true;
    zero.value = zero_val;
    repeat.value.value = zero;
    repeat.count = 10;

    mir::DefineStatement define;
    define.dest = 0;
    mir::RValue rvalue;
    rvalue.value = repeat;
    define.rvalue = rvalue;

    mir::Statement stmt;
    stmt.value = define;
    entry.statements.push_back(stmt);

    mir::ReturnTerminator ret;
    mir::Operand ret_operand;
    ret_operand.value = mir::TempId{0};
    ret.value = ret_operand;
    entry.terminator.value = ret;

    function.basic_blocks.push_back(entry);
    function.start_block = 0;

    module.functions.push_back(std::move(function));

    codegen::Emitter emitter(module);
    std::string ir = emitter.emit();

    // With optimization, should use zeroinitializer instead of multiple insertvalue
    // The IR should contain zeroinitializer and NOT have 10 insertvalue instructions
    EXPECT_NE(ir.find("zeroinitializer"), std::string::npos);
    
    // Count insertvalue instructions - should be significantly fewer than 10
    size_t insertvalue_count = 0;
    size_t pos = 0;
    while ((pos = ir.find("insertvalue", pos)) != std::string::npos) {
        insertvalue_count++;
        pos += 11;
    }
    // Should have 0 or very few insertvalue instructions for [0; 10]
    EXPECT_EQ(insertvalue_count, 0u);
}

TEST(MirEmitterTest, OptimizesArrayRepeatWithBoolZero) {
    mir::MirModule module;

    mir::MirFunction function;
    function.id = 0;
    function.name = "@test_bool_array";

    type::TypeId bool_type = type::get_typeID(type::Type{type::PrimitiveKind::BOOL});
    type::TypeId array_type = type::get_typeID(type::Type{type::ArrayType{bool_type, 5}});

    function.return_type = array_type;
    function.temp_types = {array_type};

    mir::BasicBlock entry;
    
    // Create array repeat with false value: [false; 5]
    mir::ArrayRepeatRValue repeat;
    mir::Constant false_val;
    false_val.type = bool_type;
    mir::BoolConstant bool_const;
    bool_const.value = false;
    false_val.value = bool_const;
    repeat.value.value = false_val;
    repeat.count = 5;

    mir::DefineStatement define;
    define.dest = 0;
    mir::RValue rvalue;
    rvalue.value = repeat;
    define.rvalue = rvalue;

    mir::Statement stmt;
    stmt.value = define;
    entry.statements.push_back(stmt);

    mir::ReturnTerminator ret;
    mir::Operand ret_operand;
    ret_operand.value = mir::TempId{0};
    ret.value = ret_operand;
    ret.value = ret_operand;
    entry.terminator.value = ret;

    function.basic_blocks.push_back(entry);
    function.start_block = 0;

    module.functions.push_back(std::move(function));

    codegen::Emitter emitter(module);
    std::string ir = emitter.emit();

    // With optimization, should use zeroinitializer for [false; 5]
    EXPECT_NE(ir.find("zeroinitializer"), std::string::npos);
}

