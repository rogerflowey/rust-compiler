#include "ir3/llvm_transcribe.hpp"
#include "semantic/type/type.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

semantic::TypeId i32_type() {
    return semantic::get_typeID(semantic::Type{semantic::PrimitiveKind::I32});
}

semantic::TypeId bool_type() {
    return semantic::get_typeID(semantic::Type{semantic::PrimitiveKind::BOOL});
}

} // namespace

TEST(Ir3LlvmTranscribeTest, EmitsScalarFunction) {
    ir3::Function function{
        .symbol = "add",
        .params = {
            ir3::Param{.value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                       .name = "a",
                       .host_type = i32_type()},
            ir3::Param{.value = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                       .name = "b",
                       .host_type = i32_type()},
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {},
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .phis = {},
                .instructions = {
                    ir3::Binary{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .op = ir3::BinaryOp::SAdd,
                        .lhs = 0,
                        .rhs = 1,
                    },
                },
                .terminator = ir3::Return{.value = 2},
            },
        },
        .next_value = 3,
    };

    const auto text = ir3::to_llvm_string(ir3::Module{.functions = {function}});

    EXPECT_NE(text.find("define i32 @\"add\"(i32 %v0, i32 %v1)"), std::string::npos);
    EXPECT_NE(text.find("%v2 = add i32 %v0, %v1"), std::string::npos);
    EXPECT_NE(text.find("ret i32 %v2"), std::string::npos);
}

TEST(Ir3LlvmTranscribeTest, EmitsSlotsLoadsStoresAndLogicalNot) {
    ir3::Place slot0{
        .base = ir3::SlotBase{.slot = 0},
        .projections = {},
        .host_type = bool_type(),
        .is_mutable = true,
    };

    ir3::Function function{
        .symbol = "not_local",
        .params = {},
        .return_class = ir3::SsaClass::I32,
        .source_return_type = bool_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = bool_type(),
                .is_mutable = true,
                .debug_name = "x",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .phis = {},
                .instructions = {
                    ir3::IConst{.result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                                .value = 1},
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot0, .value = 0},
                    ir3::Load{.result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                              .source = slot0},
                    ir3::Unary{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .op = ir3::UnaryOp::BoolNot,
                        .operand = 1,
                    },
                },
                .terminator = ir3::Return{.value = 2},
            },
        },
        .next_value = 3,
    };

    const auto text = ir3::to_llvm_string(ir3::Module{.functions = {function}});

    EXPECT_NE(text.find("%slot0 = alloca i32, align 4"), std::string::npos);
    EXPECT_NE(text.find("store i32 %v0, ptr %slot0, align 4"), std::string::npos);
    EXPECT_NE(text.find("%v1 = load i32, ptr %slot0, align 4"), std::string::npos);
    EXPECT_NE(text.find("icmp eq i32 %v1, 0"), std::string::npos);
    EXPECT_NE(text.find("%v2 = zext i1"), std::string::npos);
}

TEST(Ir3LlvmTranscribeTest, EmitsUnsignedOpsFromExplicitIr3Opcodes) {
    ir3::Function function{
        .symbol = "unsigned_ops",
        .params = {
            ir3::Param{.value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                       .name = "a",
                       .host_type = i32_type()},
            ir3::Param{.value = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                       .name = "b",
                       .host_type = i32_type()},
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {},
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .phis = {},
                .instructions = {
                    ir3::Binary{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .op = ir3::BinaryOp::UDiv,
                        .lhs = 0,
                        .rhs = 1,
                    },
                    ir3::Binary{
                        .result = ir3::Value{.id = 3, .klass = ir3::SsaClass::I32},
                        .op = ir3::BinaryOp::ULt,
                        .lhs = 0,
                        .rhs = 1,
                    },
                    ir3::Binary{
                        .result = ir3::Value{.id = 4, .klass = ir3::SsaClass::I32},
                        .op = ir3::BinaryOp::LShr,
                        .lhs = 0,
                        .rhs = 1,
                    },
                },
                .terminator = ir3::Return{.value = 2},
            },
        },
        .next_value = 5,
    };

    const auto text = ir3::to_llvm_string(ir3::Module{.functions = {function}});

    EXPECT_NE(text.find("%v2 = udiv i32 %v0, %v1"), std::string::npos);
    EXPECT_NE(text.find("icmp ult i32 %v0, %v1"), std::string::npos);
    EXPECT_NE(text.find("%v4 = lshr i32 %v0, %v1"), std::string::npos);
}
