#include "ir3/optimize.hpp"
#include "ir3/pretty_print.hpp"
#include "ir3/llvm_transcribe.hpp"
#include "riscv/lower.hpp"
#include "riscv/machine_ir.hpp"

#include "semantic/hir/hir.hpp"
#include "semantic/type/type.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace {

semantic::TypeId i32_type() {
    return semantic::get_typeID(semantic::Type{semantic::PrimitiveKind::I32});
}

semantic::TypeId unit_type() {
    return semantic::get_typeID(semantic::Type{semantic::UnitType{}});
}

semantic::TypeId array1_i32_type() {
    return semantic::get_typeID(
        semantic::Type{semantic::ArrayType{.element_type = i32_type(), .size = 1}});
}

semantic::TypeId pair_i32_type() {
    static hir::StructDef pair_def = [] {
        hir::StructDef def;
        def.name = ast::Identifier{"Pair"};
        def.fields = {
            semantic::Field{.name = ast::Identifier{"lhs"}, .type = i32_type()},
            semantic::Field{.name = ast::Identifier{"rhs"}, .type = i32_type()},
        };
        return def;
    }();
    static semantic::TypeId type =
        semantic::get_typeID(semantic::Type{semantic::StructType{.symbol = &pair_def}});
    return type;
}

semantic::TypeId nested_pair_type() {
    static hir::StructDef nested_def = [] {
        hir::StructDef def;
        def.name = ast::Identifier{"NestedPair"};
        def.fields = {
            semantic::Field{.name = ast::Identifier{"pair"}, .type = pair_i32_type()},
            semantic::Field{.name = ast::Identifier{"tail"}, .type = i32_type()},
        };
        return def;
    }();
    static semantic::TypeId type =
        semantic::get_typeID(semantic::Type{semantic::StructType{.symbol = &nested_def}});
    return type;
}

semantic::TypeId struct_with_array_type() {
    static hir::StructDef array_def = [] {
        hir::StructDef def;
        def.name = ast::Identifier{"ArrayHolder"};
        def.fields = {
            semantic::Field{.name = ast::Identifier{"buf"}, .type = array1_i32_type()},
            semantic::Field{.name = ast::Identifier{"value"}, .type = i32_type()},
        };
        return def;
    }();
    static semantic::TypeId type =
        semantic::get_typeID(semantic::Type{semantic::StructType{.symbol = &array_def}});
    return type;
}

ir3::Place slot_place(ir3::SlotId slot,
                      semantic::TypeId host_type,
                      bool is_mutable = true) {
    return ir3::Place{
        .base = ir3::SlotBase{.slot = slot},
        .projections = {},
        .host_type = host_type,
        .is_mutable = is_mutable,
    };
}

ir3::Place field_place(ir3::SlotId slot,
                       semantic::TypeId host_type,
                       std::initializer_list<std::size_t> fields,
                       bool is_mutable = true) {
    auto place = slot_place(slot, host_type, is_mutable);
    auto current = host_type;
    for (std::size_t index : fields) {
        const auto field_type = ir3::struct_field_type(current, index);
        place.projections.push_back(
            ir3::FieldProjection{.index = index, .result_type = field_type});
        current = field_type;
    }
    place.host_type = current;
    return place;
}

ir3::Place indexed_field_place(ir3::SlotId slot,
                               semantic::TypeId host_type,
                               std::size_t field,
                               ir3::ValueId index) {
    auto place = field_place(slot, host_type, {field});
    auto* array_type = std::get_if<semantic::ArrayType>(&place.host_type->value);
    place.projections.push_back(
        ir3::IndexProjection{.index = index, .result_type = array_type->element_type});
    place.host_type = array_type->element_type;
    return place;
}

} // namespace

TEST(Ir3OptimizeTest, PrunesUnreachableBlocksAndRewritesPhiPredecessors) {
    ir3::Function function{
        .symbol = "prune_phi",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "x",
                .host_type = i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {},
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .phis = {},
                .instructions = {},
                .terminator = ir3::Jump{.target = 1},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "bb1",
                .phis = {},
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 10,
                    },
                },
                .terminator = ir3::Jump{.target = 3},
            },
            ir3::BasicBlock{
                .id = 2,
                .name = "bb2",
                .phis = {},
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .value = 20,
                    },
                },
                .terminator = ir3::Jump{.target = 3},
            },
            ir3::BasicBlock{
                .id = 3,
                .name = "bb3",
                .phis = {
                    ir3::Phi{
                        .result = ir3::Value{.id = 3, .klass = ir3::SsaClass::I32},
                        .incoming = {
                            ir3::PhiIncoming{.pred = 1, .value = 1},
                            ir3::PhiIncoming{.pred = 2, .value = 2},
                        },
                    },
                },
                .instructions = {},
                .terminator = ir3::Return{.value = 3},
            },
        },
        .entry_block = 0,
        .next_value = 4,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    ASSERT_EQ(module.functions.size(), 1u);
    const auto& optimized = module.functions.front();
    ASSERT_EQ(optimized.blocks.size(), 3u);
    EXPECT_EQ(optimized.entry_block, 0u);
    ASSERT_TRUE(std::holds_alternative<ir3::Jump>(*optimized.blocks[1].terminator));
    EXPECT_EQ(std::get<ir3::Jump>(*optimized.blocks[1].terminator).target, 2u);
    ASSERT_EQ(optimized.blocks[2].phis.size(), 1u);
    ASSERT_EQ(optimized.blocks[2].phis.front().incoming.size(), 1u);
    EXPECT_EQ(optimized.blocks[2].phis.front().incoming.front().pred, 1u);
    EXPECT_EQ(optimized.blocks[2].phis.front().incoming.front().value, 1u);
}

TEST(Ir3OptimizeTest, RemovesDeadPureInstructionChainsToFixpoint) {
    ir3::Function function{
        .symbol = "dead_chain",
        .params = {},
        .return_class = std::nullopt,
        .source_return_type = unit_type(),
        .slots = {},
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .phis = {},
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 1,
                    },
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 2,
                    },
                    ir3::Binary{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .op = ir3::BinaryOp::SAdd,
                        .lhs = 0,
                        .rhs = 1,
                    },
                    ir3::Unary{
                        .result = ir3::Value{.id = 3, .klass = ir3::SsaClass::I32},
                        .op = ir3::UnaryOp::BitNot,
                        .operand = 2,
                    },
                },
                .terminator = ir3::Return{.value = std::nullopt},
            },
        },
        .entry_block = 0,
        .next_value = 4,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    ASSERT_EQ(module.functions.front().blocks.size(), 1u);
    EXPECT_TRUE(module.functions.front().blocks.front().instructions.empty());
}

TEST(Ir3OptimizeTest, KeepsEffectfulInstructionsAndUsedBorrow) {
    ir3::Function function{
        .symbol = "effects",
        .params = {},
        .return_class = std::nullopt,
        .source_return_type = unit_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = i32_type(),
                .is_mutable = true,
                .debug_name = "scalar",
                .origin = ir3::SlotOrigin::User,
            },
            ir3::Slot{
                .id = 1,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "dst",
                .origin = ir3::SlotOrigin::Temp,
            },
            ir3::Slot{
                .id = 2,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "src",
                .origin = ir3::SlotOrigin::Temp,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .phis = {},
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 7,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = slot_place(0, i32_type()),
                        .value = 0,
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .source = slot_place(0, i32_type()),
                    },
                    ir3::Unary{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .op = ir3::UnaryOp::BitNot,
                        .operand = 1,
                    },
                    ir3::Copy{
                        .dest = slot_place(1, array1_i32_type()),
                        .source = slot_place(2, array1_i32_type()),
                    },
                    ir3::Borrow{
                        .result = ir3::Value{.id = 3, .klass = ir3::SsaClass::Ptr},
                        .is_mutable = false,
                        .source = slot_place(0, i32_type()),
                    },
                    ir3::Call{
                        .result = std::nullopt,
                        .callee = "sink_ptr",
                        .args = {3},
                    },
                },
                .terminator = ir3::Return{.value = std::nullopt},
            },
        },
        .entry_block = 0,
        .next_value = 4,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& instructions = module.functions.front().blocks.front().instructions;
    ASSERT_EQ(instructions.size(), 6u);
    EXPECT_TRUE(std::holds_alternative<ir3::IConst>(instructions[0]));
    EXPECT_TRUE(std::holds_alternative<ir3::Store>(instructions[1]));
    EXPECT_TRUE(std::holds_alternative<ir3::Load>(instructions[2]));
    EXPECT_TRUE(std::holds_alternative<ir3::Copy>(instructions[3]));
    EXPECT_TRUE(std::holds_alternative<ir3::Borrow>(instructions[4]));
    EXPECT_TRUE(std::holds_alternative<ir3::Call>(instructions[5]));
}

TEST(Ir3OptimizeTest, RunsDeadBlockEliminationBeforeDeadCodeElimination) {
    ir3::Function function{
        .symbol = "order",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "x",
                .host_type = i32_type(),
            },
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
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 1,
                    },
                },
                .terminator = ir3::Jump{.target = 1},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "bb1",
                .phis = {},
                .instructions = {},
                .terminator = ir3::Return{.value = 0},
            },
            ir3::BasicBlock{
                .id = 2,
                .name = "bb2",
                .phis = {},
                .instructions = {},
                .terminator = ir3::Return{.value = 1},
            },
        },
        .entry_block = 0,
        .next_value = 2,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    ASSERT_EQ(module.functions.front().blocks.size(), 2u);
    EXPECT_TRUE(module.functions.front().blocks.front().instructions.empty());
}

TEST(Ir3OptimizeTest, PromotesStraightLineScalarUserSlot) {
    ir3::Function function{
        .symbol = "promote_linear",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = i32_type(),
                .is_mutable = true,
                .debug_name = "x",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 7,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = slot_place(0, i32_type()),
                        .value = 0,
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .source = slot_place(0, i32_type()),
                    },
                    ir3::Unary{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .op = ir3::UnaryOp::BitNot,
                        .operand = 1,
                    },
                },
                .terminator = ir3::Return{.value = 2},
            },
        },
        .entry_block = 0,
        .next_value = 3,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    EXPECT_TRUE(optimized.slots.empty());
    ASSERT_EQ(optimized.blocks.front().instructions.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<ir3::IConst>(optimized.blocks.front().instructions[0]));
    const auto* unary = std::get_if<ir3::Unary>(&optimized.blocks.front().instructions[1]);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->operand, 0u);
}

TEST(Ir3OptimizeTest, PromotesStraightLineScalarTempSlot) {
    ir3::Function function{
        .symbol = "promote_temp",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = i32_type(),
                .is_mutable = true,
                .debug_name = "tmp0",
                .origin = ir3::SlotOrigin::Temp,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 11,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = slot_place(0, i32_type()),
                        .value = 0,
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .source = slot_place(0, i32_type()),
                    },
                },
                .terminator = ir3::Return{.value = 1},
            },
        },
        .entry_block = 0,
        .next_value = 2,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    EXPECT_TRUE(optimized.slots.empty());
    ASSERT_TRUE(optimized.blocks.front().instructions.size() == 1u);
    EXPECT_TRUE(std::holds_alternative<ir3::IConst>(optimized.blocks.front().instructions.front()));
}

TEST(Ir3OptimizeTest, PromotesDiamondSlotWithPhi) {
    ir3::Function function{
        .symbol = "promote_phi",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "cond",
                .host_type = i32_type(),
            },
            ir3::Param{
                .value = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                .name = "x",
                .host_type = i32_type(),
            },
            ir3::Param{
                .value = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                .name = "y",
                .host_type = i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = i32_type(),
                .is_mutable = true,
                .debug_name = "x",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .terminator = ir3::Branch{.condition = 0, .then_block = 1, .else_block = 2},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "bb1",
                .instructions = {
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot_place(0, i32_type()), .value = 1},
                },
                .terminator = ir3::Jump{.target = 3},
            },
            ir3::BasicBlock{
                .id = 2,
                .name = "bb2",
                .instructions = {
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot_place(0, i32_type()), .value = 2},
                },
                .terminator = ir3::Jump{.target = 3},
            },
            ir3::BasicBlock{
                .id = 3,
                .name = "bb3",
                .instructions = {
                    ir3::Load{
                        .result = ir3::Value{.id = 3, .klass = ir3::SsaClass::I32},
                        .source = slot_place(0, i32_type()),
                    },
                },
                .terminator = ir3::Return{.value = 3},
            },
        },
        .entry_block = 0,
        .next_value = 4,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    EXPECT_TRUE(optimized.slots.empty());
    EXPECT_TRUE(optimized.blocks[1].instructions.empty());
    EXPECT_TRUE(optimized.blocks[2].instructions.empty());
    EXPECT_TRUE(optimized.blocks[3].instructions.empty());
    ASSERT_EQ(optimized.blocks[3].phis.size(), 1u);
    const auto& phi = optimized.blocks[3].phis.front();
    ASSERT_EQ(phi.incoming.size(), 2u);
    EXPECT_EQ(phi.incoming[0].pred, 1u);
    EXPECT_EQ(phi.incoming[0].value, 1u);
    EXPECT_EQ(phi.incoming[1].pred, 2u);
    EXPECT_EQ(phi.incoming[1].value, 2u);
    ASSERT_TRUE(optimized.blocks[3].terminator.has_value());
    const auto* ret = std::get_if<ir3::Return>(&*optimized.blocks[3].terminator);
    ASSERT_NE(ret, nullptr);
    ASSERT_TRUE(ret->value.has_value());
    EXPECT_EQ(*ret->value, phi.result.id);
}

TEST(Ir3OptimizeTest, PromotesLoopCarriedSlot) {
    ir3::Function function{
        .symbol = "promote_loop",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "cond",
                .host_type = i32_type(),
            },
            ir3::Param{
                .value = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                .name = "init",
                .host_type = i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = i32_type(),
                .is_mutable = true,
                .debug_name = "acc",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot_place(0, i32_type()), .value = 1},
                },
                .terminator = ir3::Jump{.target = 1},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "bb1",
                .instructions = {
                    ir3::Load{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .source = slot_place(0, i32_type()),
                    },
                },
                .terminator = ir3::Branch{.condition = 0, .then_block = 2, .else_block = 3},
            },
            ir3::BasicBlock{
                .id = 2,
                .name = "bb2",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 3, .klass = ir3::SsaClass::I32},
                        .value = 1,
                    },
                    ir3::Binary{
                        .result = ir3::Value{.id = 4, .klass = ir3::SsaClass::I32},
                        .op = ir3::BinaryOp::SAdd,
                        .lhs = 2,
                        .rhs = 3,
                    },
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot_place(0, i32_type()), .value = 4},
                },
                .terminator = ir3::Jump{.target = 1},
            },
            ir3::BasicBlock{
                .id = 3,
                .name = "bb3",
                .instructions = {
                    ir3::Load{
                        .result = ir3::Value{.id = 5, .klass = ir3::SsaClass::I32},
                        .source = slot_place(0, i32_type()),
                    },
                },
                .terminator = ir3::Return{.value = 5},
            },
        },
        .entry_block = 0,
        .next_value = 6,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    EXPECT_TRUE(optimized.slots.empty());
    ASSERT_EQ(optimized.blocks[1].phis.size(), 1u);
    EXPECT_TRUE(optimized.blocks[0].instructions.empty());
    EXPECT_TRUE(optimized.blocks[1].instructions.empty());
    EXPECT_TRUE(optimized.blocks[3].instructions.empty());
    const auto* ret = std::get_if<ir3::Return>(&*optimized.blocks[3].terminator);
    ASSERT_NE(ret, nullptr);
    ASSERT_TRUE(ret->value.has_value());
}

TEST(Ir3OptimizeTest, SroaPromotesNestedStructFields) {
    ir3::Function function{
        .symbol = "sroa_nested",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = nested_pair_type(),
                .is_mutable = true,
                .debug_name = "agg",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 1,
                    },
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 2,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = field_place(0, nested_pair_type(), {0, 0}),
                        .value = 0,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = field_place(0, nested_pair_type(), {0, 1}),
                        .value = 1,
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .source = field_place(0, nested_pair_type(), {0, 1}),
                    },
                },
                .terminator = ir3::Return{.value = 2},
            },
        },
        .entry_block = 0,
        .next_value = 3,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    EXPECT_TRUE(optimized.slots.empty());
    const auto text = ir3::to_string(module);
    EXPECT_EQ(text.find("slot(%0).field(0).field(1)"), std::string::npos);
}

TEST(Ir3OptimizeTest, SroaExpandsWholeStructCopy) {
    ir3::Function function{
        .symbol = "sroa_copy",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = pair_i32_type(),
                .is_mutable = true,
                .debug_name = "src",
                .origin = ir3::SlotOrigin::User,
            },
            ir3::Slot{
                .id = 1,
                .host_type = pair_i32_type(),
                .is_mutable = true,
                .debug_name = "dst",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 5,
                    },
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 8,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = field_place(0, pair_i32_type(), {0}),
                        .value = 0,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = field_place(0, pair_i32_type(), {1}),
                        .value = 1,
                    },
                    ir3::Copy{
                        .dest = slot_place(1, pair_i32_type()),
                        .source = slot_place(0, pair_i32_type()),
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .source = field_place(1, pair_i32_type(), {1}),
                    },
                },
                .terminator = ir3::Return{.value = 2},
            },
        },
        .entry_block = 0,
        .next_value = 3,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    EXPECT_TRUE(optimized.slots.empty());
    const auto text = ir3::to_string(module);
    EXPECT_EQ(text.find("copy slot"), std::string::npos);
}

TEST(Ir3OptimizeTest, SroaKeepsBorrowedArrayFieldInMemory) {
    ir3::Function function{
        .symbol = "sroa_array_leaf",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = struct_with_array_type(),
                .is_mutable = true,
                .debug_name = "agg",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 3,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = field_place(0, struct_with_array_type(), {1}),
                        .value = 0,
                    },
                    ir3::Borrow{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::Ptr},
                        .is_mutable = false,
                        .source = field_place(0, struct_with_array_type(), {0}),
                    },
                    ir3::Call{
                        .result = std::nullopt,
                        .callee = "sink_ptr",
                        .args = {1},
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .source = field_place(0, struct_with_array_type(), {1}),
                    },
                },
                .terminator = ir3::Return{.value = 2},
            },
        },
        .entry_block = 0,
        .next_value = 3,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    ASSERT_EQ(optimized.slots.size(), 1u);
    EXPECT_EQ(optimized.slots.front().host_type, array1_i32_type());
    const auto text = ir3::to_string(module);
    EXPECT_NE(text.find("borrow imm slot(%0)"), std::string::npos);
}

TEST(Ir3OptimizeTest, SroaRejectsExactRootBorrow) {
    ir3::Function function{
        .symbol = "sroa_root_borrow",
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = pair_i32_type(),
                .is_mutable = true,
                .debug_name = "agg",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::Borrow{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::Ptr},
                        .is_mutable = false,
                        .source = slot_place(0, pair_i32_type()),
                    },
                    ir3::Call{
                        .result = std::nullopt,
                        .callee = "sink_ptr",
                        .args = {0},
                    },
                },
                .terminator = ir3::Return{.value = std::nullopt},
            },
        },
        .entry_block = 0,
        .next_value = 1,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    ASSERT_EQ(optimized.slots.size(), 1u);
    EXPECT_EQ(optimized.slots.front().host_type, pair_i32_type());
    const auto text = ir3::to_string(module);
    EXPECT_NE(text.find("borrow imm slot(%0)"), std::string::npos);
}

TEST(Ir3OptimizeTest, SroaRejectsDynamicIndexUses) {
    ir3::Function function{
        .symbol = "sroa_index",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = struct_with_array_type(),
                .is_mutable = true,
                .debug_name = "agg",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 0,
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .source = indexed_field_place(0, struct_with_array_type(), 0, 0),
                    },
                },
                .terminator = ir3::Return{.value = 1},
            },
        },
        .entry_block = 0,
        .next_value = 2,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    ASSERT_EQ(optimized.slots.size(), 1u);
    EXPECT_EQ(optimized.slots.front().host_type, struct_with_array_type());
}

TEST(Ir3OptimizeTest, LeavesBorrowedScalarSlotInMemory) {
    ir3::Function function{
        .symbol = "borrowed_slot",
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = i32_type(),
                .is_mutable = true,
                .debug_name = "x",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 9,
                    },
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot_place(0, i32_type()), .value = 0},
                    ir3::Borrow{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::Ptr},
                        .is_mutable = false,
                        .source = slot_place(0, i32_type()),
                    },
                    ir3::Call{
                        .result = std::nullopt,
                        .callee = "sink_ptr",
                        .args = {1},
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .source = slot_place(0, i32_type()),
                    },
                },
                .terminator = ir3::Return{.value = 2},
            },
        },
        .entry_block = 0,
        .next_value = 3,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    ASSERT_EQ(optimized.slots.size(), 1u);
    const auto text = ir3::to_string(module);
    EXPECT_NE(text.find("store.i32 slot(%0), %0"), std::string::npos);
    EXPECT_NE(text.find("borrow imm slot(%0)"), std::string::npos);
    EXPECT_NE(text.find("load.i32 slot(%0)"), std::string::npos);
}

TEST(Ir3OptimizeTest, LeavesReachableLoadBeforeDefSlotUntouched) {
    ir3::Function function{
        .symbol = "load_before_def",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "cond",
                .host_type = i32_type(),
            },
            ir3::Param{
                .value = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                .name = "x",
                .host_type = i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = i32_type(),
                .is_mutable = true,
                .debug_name = "x",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .terminator = ir3::Branch{.condition = 0, .then_block = 1, .else_block = 2},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "bb1",
                .instructions = {
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot_place(0, i32_type()), .value = 1},
                },
                .terminator = ir3::Jump{.target = 3},
            },
            ir3::BasicBlock{
                .id = 2,
                .name = "bb2",
                .terminator = ir3::Jump{.target = 3},
            },
            ir3::BasicBlock{
                .id = 3,
                .name = "bb3",
                .instructions = {
                    ir3::Load{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .source = slot_place(0, i32_type()),
                    },
                },
                .terminator = ir3::Return{.value = 2},
            },
        },
        .entry_block = 0,
        .next_value = 3,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    ASSERT_EQ(optimized.slots.size(), 1u);
    EXPECT_TRUE(optimized.blocks[1].instructions.size() == 1u);
    EXPECT_TRUE(optimized.blocks[3].instructions.size() == 1u);
}

TEST(Ir3OptimizeTest, RemovesPromotedSlotFromLlvmAndRiscvLowering) {
    ir3::Function function{
        .symbol = "downstream",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = i32_type(),
                .is_mutable = true,
                .debug_name = "x",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 5,
                    },
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot_place(0, i32_type()), .value = 0},
                    ir3::Load{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .source = slot_place(0, i32_type()),
                    },
                },
                .terminator = ir3::Return{.value = 1},
            },
        },
        .entry_block = 0,
        .next_value = 2,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto llvm = ir3::to_llvm_string(module);
    EXPECT_EQ(llvm.find("alloca"), std::string::npos);

    const auto machine = riscv::lower_module(module);
    ASSERT_EQ(machine.functions.size(), 1u);
    bool has_local_slot = false;
    for (const auto& object : machine.functions.front().frame_objects) {
        if (object.kind == riscv::FrameObjectKind::LocalSlot) {
            has_local_slot = true;
            break;
        }
    }
    EXPECT_FALSE(has_local_slot);
}
