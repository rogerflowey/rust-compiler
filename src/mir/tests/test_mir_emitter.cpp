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
