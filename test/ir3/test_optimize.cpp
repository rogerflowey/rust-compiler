#include "ir3/analysis/manager.hpp"
#include "ir3/optimize.hpp"
#include "ir3/passes/load_forwarding.hpp"
#include "ir3/pretty_print.hpp"
#include "ir3/llvm_transcribe.hpp"
#include "riscv/lower.hpp"
#include "riscv/machine_ir.hpp"

#include "semantic/hir/hir.hpp"
#include "semantic/type/type.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

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

ir3::Place indexed_slot_place(ir3::SlotId slot,
                              semantic::TypeId host_type,
                              ir3::ValueId index,
                              bool is_mutable = true) {
    auto place = slot_place(slot, host_type, is_mutable);
    auto* array_type = std::get_if<semantic::ArrayType>(&host_type->value);
    place.projections.push_back(
        ir3::IndexProjection{.index = index, .result_type = array_type->element_type});
    place.host_type = array_type->element_type;
    return place;
}

ir3::Place deref_field_place(ir3::ValueId ptr,
                             semantic::TypeId host_type,
                             std::initializer_list<std::size_t> fields,
                             bool is_mutable = true) {
    ir3::Place place{
        .base = ir3::DerefBase{
            .ptr = ptr,
            .pointee_type = host_type,
            .is_mutable = is_mutable,
        },
        .projections = {},
        .host_type = host_type,
        .is_mutable = is_mutable,
    };

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

ir3::Place deref_place(ir3::ValueId ptr,
                       semantic::TypeId host_type,
                       bool is_mutable = true) {
    return deref_field_place(ptr, host_type, {}, is_mutable);
}

ir3::Place deref_indexed_place(ir3::ValueId ptr,
                               semantic::TypeId host_type,
                               ir3::ValueId index,
                               bool is_mutable = true) {
    auto place = deref_place(ptr, host_type, is_mutable);
    auto* array_type = std::get_if<semantic::ArrayType>(&place.host_type->value);
    place.projections.push_back(
        ir3::IndexProjection{.index = index, .result_type = array_type->element_type});
    place.host_type = array_type->element_type;
    return place;
}

std::size_t copy_count(const ir3::Function& function) {
    std::size_t count = 0;
    for (const auto& block : function.blocks) {
        for (const auto& inst : block.instructions) {
            if (std::holds_alternative<ir3::Copy>(inst)) {
                ++count;
            }
        }
    }
    return count;
}

template <class T>
std::size_t instruction_count(const ir3::Function& function) {
    std::size_t count = 0;
    for (const auto& block : function.blocks) {
        for (const auto& inst : block.instructions) {
            if (std::holds_alternative<T>(inst)) {
                ++count;
            }
        }
    }
    return count;
}

bool instruction_has_deref_place(const ir3::Instruction& inst) {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ir3::Load>) {
                return std::holds_alternative<ir3::DerefBase>(value.source.base);
            } else if constexpr (std::is_same_v<T, ir3::Store>) {
                return std::holds_alternative<ir3::DerefBase>(value.dest.base);
            } else if constexpr (std::is_same_v<T, ir3::Copy>) {
                return std::holds_alternative<ir3::DerefBase>(value.dest.base) ||
                       std::holds_alternative<ir3::DerefBase>(value.source.base);
            } else if constexpr (std::is_same_v<T, ir3::Borrow>) {
                return std::holds_alternative<ir3::DerefBase>(value.source.base);
            }
            return false;
        },
        inst);
}

bool contains_deref_place(const ir3::Function& function) {
    for (const auto& block : function.blocks) {
        for (const auto& inst : block.instructions) {
            if (instruction_has_deref_place(inst)) {
                return true;
            }
        }
    }
    return false;
}

std::size_t call_count(const ir3::Function& function) {
    std::size_t count = 0;
    for (const auto& block : function.blocks) {
        for (const auto& inst : block.instructions) {
            if (std::holds_alternative<ir3::Call>(inst)) {
                ++count;
            }
        }
    }
    return count;
}

bool contains_callee(const ir3::Function& function, std::string_view callee) {
    for (const auto& block : function.blocks) {
        for (const auto& inst : block.instructions) {
            const auto* call = std::get_if<ir3::Call>(&inst);
            if (call && call->callee == callee) {
                return true;
            }
        }
    }
    return false;
}

std::size_t block_instruction_count(const ir3::Function& function, ir3::BlockId block_id) {
    return function.blocks[block_id].instructions.size();
}

void run_load_forwarding(ir3::Function& function) {
    ir3::AnalysisManager am;
    ir3::LoadForwardingPass pass;
    pass.run(function, am);
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
    EXPECT_TRUE(optimized.blocks[2].phis.empty());
    ASSERT_EQ(optimized.blocks[2].instructions.size(), 1u);
    const auto* iconst = std::get_if<ir3::IConst>(&optimized.blocks[2].instructions.front());
    ASSERT_NE(iconst, nullptr);
    EXPECT_EQ(iconst->result.id, 3u);
    EXPECT_EQ(iconst->value, 10);
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
    ASSERT_EQ(instructions.size(), 4u);
    EXPECT_TRUE(std::holds_alternative<ir3::IConst>(instructions[0]));
    EXPECT_TRUE(std::holds_alternative<ir3::Store>(instructions[1]));
    EXPECT_TRUE(std::holds_alternative<ir3::Borrow>(instructions[2]));
    EXPECT_TRUE(std::holds_alternative<ir3::Call>(instructions[3]));
}

TEST(Ir3OptimizeTest, CanonicalizesRootBorrowedDerefTraffic) {
    ir3::Function function{
        .symbol = "ptr_root_scalar",
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
                        .value = 17,
                    },
                    ir3::Borrow{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::Ptr},
                        .is_mutable = true,
                        .source = slot_place(0, i32_type()),
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = deref_place(1, i32_type()),
                        .value = 0,
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .source = deref_place(1, i32_type()),
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
    ASSERT_EQ(optimized.blocks.size(), 1u);
    ASSERT_EQ(optimized.blocks.front().instructions.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<ir3::IConst>(optimized.blocks.front().instructions.front()));
    EXPECT_EQ(instruction_count<ir3::Borrow>(optimized), 0u);
    EXPECT_EQ(instruction_count<ir3::Load>(optimized), 0u);
    EXPECT_EQ(instruction_count<ir3::Store>(optimized), 0u);
    EXPECT_FALSE(contains_deref_place(optimized));

    const auto* ret = std::get_if<ir3::Return>(&*optimized.blocks.front().terminator);
    ASSERT_NE(ret, nullptr);
    ASSERT_TRUE(ret->value.has_value());
    EXPECT_EQ(*ret->value, 0u);
}

TEST(Ir3OptimizeTest, CanonicalizesProjectedBorrowAndExposesFieldTraffic) {
    ir3::Function function{
        .symbol = "ptr_field_scalar",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = pair_i32_type(),
                .is_mutable = true,
                .debug_name = "pair",
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
                        .value = 29,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = field_place(0, pair_i32_type(), {1}),
                        .value = 0,
                    },
                    ir3::Borrow{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::Ptr},
                        .is_mutable = false,
                        .source = field_place(0, pair_i32_type(), {1}),
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .source = deref_place(1, i32_type(), false),
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
    ASSERT_EQ(optimized.blocks.size(), 1u);
    ASSERT_EQ(optimized.blocks.front().instructions.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<ir3::IConst>(optimized.blocks.front().instructions.front()));
    EXPECT_EQ(instruction_count<ir3::Borrow>(optimized), 0u);
    EXPECT_EQ(instruction_count<ir3::Load>(optimized), 0u);
    EXPECT_EQ(instruction_count<ir3::Store>(optimized), 0u);
    EXPECT_FALSE(contains_deref_place(optimized));
}

TEST(Ir3OptimizeTest, CanonicalizesBorrowedAggregateCopy) {
    ir3::Function function{
        .symbol = "ptr_root_copy",
        .source_return_type = unit_type(),
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
                .debug_name = "mid",
                .origin = ir3::SlotOrigin::Temp,
            },
            ir3::Slot{
                .id = 2,
                .host_type = pair_i32_type(),
                .is_mutable = true,
                .debug_name = "dst",
                .origin = ir3::SlotOrigin::Temp,
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
                    ir3::Copy{
                        .dest = slot_place(1, pair_i32_type()),
                        .source = deref_place(0, pair_i32_type(), false),
                    },
                    ir3::Copy{
                        .dest = slot_place(2, pair_i32_type()),
                        .source = slot_place(1, pair_i32_type()),
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
    EXPECT_EQ(instruction_count<ir3::Borrow>(optimized), 0u);
    EXPECT_EQ(copy_count(optimized), 0u);
    EXPECT_LT(optimized.slots.size(), 3u);
    EXPECT_FALSE(contains_deref_place(optimized));
}

TEST(Ir3OptimizeTest, ConcatenatesBorrowedSourceAndDerefSuffixProjections) {
    ir3::Function function{
        .symbol = "ptr_projection_suffix",
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
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 41,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = indexed_field_place(0, struct_with_array_type(), 0, 0),
                        .value = 1,
                    },
                    ir3::Borrow{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::Ptr},
                        .is_mutable = false,
                        .source = field_place(0, struct_with_array_type(), {0}),
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 3, .klass = ir3::SsaClass::I32},
                        .source = deref_indexed_place(2, array1_i32_type(), 0, false),
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
    ASSERT_EQ(instruction_count<ir3::Borrow>(optimized), 0u);
    ASSERT_EQ(instruction_count<ir3::Store>(optimized), 1u);
    ASSERT_EQ(instruction_count<ir3::Load>(optimized), 1u);
    EXPECT_FALSE(contains_deref_place(optimized));

    const auto& instructions = optimized.blocks.front().instructions;
    const auto* store = std::get_if<ir3::Store>(&instructions[2]);
    ASSERT_NE(store, nullptr);
    const auto* store_base = std::get_if<ir3::SlotBase>(&store->dest.base);
    ASSERT_NE(store_base, nullptr);
    EXPECT_EQ(store_base->slot, 0u);
    ASSERT_EQ(store->dest.projections.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<ir3::FieldProjection>(store->dest.projections[0]));
    EXPECT_TRUE(std::holds_alternative<ir3::IndexProjection>(store->dest.projections[1]));
    EXPECT_EQ(std::get<ir3::FieldProjection>(store->dest.projections[0]).index, 0u);
    EXPECT_EQ(std::get<ir3::IndexProjection>(store->dest.projections[1]).index, 0u);

    const auto* load = std::get_if<ir3::Load>(&instructions[3]);
    ASSERT_NE(load, nullptr);
    const auto* load_base = std::get_if<ir3::SlotBase>(&load->source.base);
    ASSERT_NE(load_base, nullptr);
    EXPECT_EQ(load_base->slot, 0u);
    ASSERT_EQ(load->source.projections.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<ir3::FieldProjection>(load->source.projections[0]));
    EXPECT_TRUE(std::holds_alternative<ir3::IndexProjection>(load->source.projections[1]));
    EXPECT_EQ(std::get<ir3::FieldProjection>(load->source.projections[0]).index, 0u);
    EXPECT_EQ(std::get<ir3::IndexProjection>(load->source.projections[1]).index, 0u);
}

TEST(Ir3OptimizeTest, KeepsEscapingBorrowButCanonicalizesLocalDerefUse) {
    ir3::Function function{
        .symbol = "ptr_escape",
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
                        .value = 9,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = slot_place(0, i32_type()),
                        .value = 0,
                    },
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
                        .source = deref_place(1, i32_type(), false),
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
    EXPECT_EQ(instruction_count<ir3::Borrow>(optimized), 1u);
    EXPECT_EQ(call_count(optimized), 1u);
    ASSERT_EQ(instruction_count<ir3::Load>(optimized), 1u);
    EXPECT_FALSE(contains_deref_place(optimized));

    const auto& instructions = optimized.blocks.front().instructions;
    const auto* load = std::get_if<ir3::Load>(&instructions.back());
    ASSERT_NE(load, nullptr);
    const auto* base = std::get_if<ir3::SlotBase>(&load->source.base);
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->slot, 0u);
    EXPECT_TRUE(load->source.projections.empty());
}

TEST(Ir3OptimizeTest, DoesNotPropagatePointerFactsThroughCast) {
    ir3::Function function{
        .symbol = "ptr_cast_boundary",
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
                        .value = 12,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = slot_place(0, i32_type()),
                        .value = 0,
                    },
                    ir3::Borrow{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::Ptr},
                        .is_mutable = false,
                        .source = slot_place(0, i32_type()),
                    },
                    ir3::Cast{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::Ptr},
                        .operand = 1,
                        .op = ir3::CastOp::PtrToPtr,
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 3, .klass = ir3::SsaClass::I32},
                        .source = deref_place(2, i32_type(), false),
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
    EXPECT_EQ(instruction_count<ir3::Borrow>(optimized), 1u);
    EXPECT_EQ(instruction_count<ir3::Cast>(optimized), 1u);
    ASSERT_EQ(instruction_count<ir3::Load>(optimized), 1u);
    EXPECT_TRUE(contains_deref_place(optimized));

    const auto& instructions = optimized.blocks.front().instructions;
    const auto* load = std::get_if<ir3::Load>(&instructions.back());
    ASSERT_NE(load, nullptr);
    const auto* base = std::get_if<ir3::DerefBase>(&load->source.base);
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->ptr, 2u);
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
    ASSERT_EQ(optimized.blocks.front().instructions.size(), 1u);
    const auto* iconst = std::get_if<ir3::IConst>(&optimized.blocks.front().instructions[0]);
    ASSERT_NE(iconst, nullptr);
    EXPECT_EQ(iconst->result.id, 2u);
    EXPECT_EQ(iconst->value, -8);
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

TEST(Ir3OptimizeTest, CopyCoalescesAggregateRootSlots) {
    ir3::Function function{
        .symbol = "copy_coalesce_root",
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "src",
                .origin = ir3::SlotOrigin::User,
            },
            ir3::Slot{
                .id = 1,
                .host_type = array1_i32_type(),
                .is_mutable = false,
                .debug_name = "mid",
                .origin = ir3::SlotOrigin::Temp,
            },
            ir3::Slot{
                .id = 2,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "out",
                .origin = ir3::SlotOrigin::Temp,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::Copy{
                        .dest = slot_place(1, array1_i32_type(), false),
                        .source = slot_place(0, array1_i32_type()),
                    },
                    ir3::Copy{
                        .dest = slot_place(2, array1_i32_type()),
                        .source = slot_place(1, array1_i32_type(), false),
                    },
                },
                .terminator = ir3::Return{.value = std::nullopt},
            },
        },
        .entry_block = 0,
        .next_value = 0,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    ASSERT_EQ(optimized.slots.size(), 1u);
    EXPECT_EQ(copy_count(optimized), 0u);
    EXPECT_TRUE(optimized.slots.front().is_mutable);
}

TEST(Ir3OptimizeTest, CopyCoalescingRejectsDestinationMentionedBeforeCopy) {
    ir3::Function function{
        .symbol = "copy_dst_before",
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "dst",
                .origin = ir3::SlotOrigin::User,
            },
            ir3::Slot{
                .id = 1,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "src",
                .origin = ir3::SlotOrigin::User,
            },
            ir3::Slot{
                .id = 2,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "other",
                .origin = ir3::SlotOrigin::Temp,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::Copy{
                        .dest = slot_place(2, array1_i32_type()),
                        .source = slot_place(0, array1_i32_type()),
                    },
                    ir3::Copy{
                        .dest = slot_place(0, array1_i32_type()),
                        .source = slot_place(1, array1_i32_type()),
                    },
                },
                .terminator = ir3::Return{.value = std::nullopt},
            },
        },
        .entry_block = 0,
        .next_value = 0,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    EXPECT_EQ(optimized.slots.size(), 3u);
    EXPECT_EQ(copy_count(optimized), 2u);
}

TEST(Ir3OptimizeTest, CopyCoalescingRejectsSourceMentionedAfterCopy) {
    ir3::Function function{
        .symbol = "copy_src_after",
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "src",
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
                .debug_name = "other",
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
                        .value = 0,
                    },
                    ir3::Copy{
                        .dest = slot_place(1, array1_i32_type()),
                        .source = slot_place(0, array1_i32_type()),
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .source = indexed_slot_place(0, array1_i32_type(), 0),
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
    EXPECT_EQ(optimized.slots.size(), 3u);
    EXPECT_EQ(copy_count(optimized), 1u);
}

TEST(Ir3OptimizeTest, CopyCoalescingRejectsBorrowedSlots) {
    ir3::Function function{
        .symbol = "copy_borrow",
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "src",
                .origin = ir3::SlotOrigin::User,
            },
            ir3::Slot{
                .id = 1,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "dst",
                .origin = ir3::SlotOrigin::Temp,
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
                        .source = slot_place(0, array1_i32_type()),
                    },
                    ir3::Call{
                        .result = std::nullopt,
                        .callee = "sink_ptr",
                        .args = {0},
                    },
                    ir3::Copy{
                        .dest = slot_place(1, array1_i32_type()),
                        .source = slot_place(0, array1_i32_type()),
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
    EXPECT_EQ(optimized.slots.size(), 2u);
    EXPECT_EQ(copy_count(optimized), 1u);
}

TEST(Ir3OptimizeTest, CopyCoalescingRestartsToFixpoint) {
    ir3::Function function{
        .symbol = "copy_chain",
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "a",
                .origin = ir3::SlotOrigin::User,
            },
            ir3::Slot{
                .id = 1,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "b",
                .origin = ir3::SlotOrigin::Temp,
            },
            ir3::Slot{
                .id = 2,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "c",
                .origin = ir3::SlotOrigin::Temp,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::Copy{
                        .dest = slot_place(1, array1_i32_type()),
                        .source = slot_place(0, array1_i32_type()),
                    },
                    ir3::Copy{
                        .dest = slot_place(2, array1_i32_type()),
                        .source = slot_place(1, array1_i32_type()),
                    },
                },
                .terminator = ir3::Return{.value = std::nullopt},
            },
        },
        .entry_block = 0,
        .next_value = 0,
    };

    ir3::Module module{.functions = {function}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions.front();
    EXPECT_EQ(optimized.slots.size(), 1u);
    EXPECT_EQ(copy_count(optimized), 0u);
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

TEST(Ir3OptimizeTest, ForwardsStoreToLoadWithinBlock) {
    ir3::Function function{
        .symbol = "forward_store_load",
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

    const auto& optimized = module.functions.front();
    EXPECT_EQ(instruction_count<ir3::Load>(optimized), 0u);
    EXPECT_EQ(block_instruction_count(optimized, 0), 1u);
    ASSERT_TRUE(std::holds_alternative<ir3::IConst>(optimized.blocks[0].instructions[0]));
    const auto* ret = std::get_if<ir3::Return>(&*optimized.blocks[0].terminator);
    ASSERT_NE(ret, nullptr);
    ASSERT_TRUE(ret->value.has_value());
    EXPECT_EQ(*ret->value, 0u);
}

TEST(Ir3OptimizeTest, ForwardsRepeatedFieldLoadWithinBlock) {
    ir3::Function function{
        .symbol = "forward_field_load",
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = pair_i32_type(),
                .is_mutable = true,
                .debug_name = "pair",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::Load{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .source = field_place(0, pair_i32_type(), {1}),
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .source = field_place(0, pair_i32_type(), {1}),
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
    EXPECT_EQ(instruction_count<ir3::Load>(optimized), 1u);
    const auto* ret = std::get_if<ir3::Return>(&*optimized.blocks[0].terminator);
    ASSERT_NE(ret, nullptr);
    ASSERT_TRUE(ret->value.has_value());
    EXPECT_EQ(*ret->value, 0u);
}

TEST(Ir3OptimizeTest, ForwardsJoinLoadWhenPredecessorsAgree) {
    ir3::Function function{
        .symbol = "forward_join_same",
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
                .instructions = {
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot_place(0, i32_type()), .value = 1},
                },
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

    run_load_forwarding(function);

    const auto& optimized = function;
    EXPECT_EQ(block_instruction_count(optimized, 3), 0u);
    const auto* ret = std::get_if<ir3::Return>(&*optimized.blocks[3].terminator);
    ASSERT_NE(ret, nullptr);
    ASSERT_TRUE(ret->value.has_value());
    EXPECT_EQ(*ret->value, 1u);
}

TEST(Ir3OptimizeTest, LeavesJoinLoadWhenPredecessorsDisagree) {
    ir3::Function function{
        .symbol = "forward_join_diff",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "cond",
                .host_type = i32_type(),
            },
            ir3::Param{
                .value = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                .name = "lhs",
                .host_type = i32_type(),
            },
            ir3::Param{
                .value = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                .name = "rhs",
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

    run_load_forwarding(function);

    const auto& optimized = function;
    EXPECT_EQ(block_instruction_count(optimized, 3), 1u);
    EXPECT_EQ(instruction_count<ir3::Load>(optimized), 1u);
}

TEST(Ir3OptimizeTest, PreservesFieldValueAcrossSiblingFieldStore) {
    ir3::Function function{
        .symbol = "forward_sibling_fields",
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = pair_i32_type(),
                .is_mutable = true,
                .debug_name = "pair",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::Load{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .source = field_place(0, pair_i32_type(), {0}),
                    },
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 5,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = field_place(0, pair_i32_type(), {1}),
                        .value = 1,
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .source = field_place(0, pair_i32_type(), {0}),
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
    EXPECT_EQ(instruction_count<ir3::Load>(optimized), 1u);
    const auto* ret = std::get_if<ir3::Return>(&*optimized.blocks[0].terminator);
    ASSERT_NE(ret, nullptr);
    ASSERT_TRUE(ret->value.has_value());
    EXPECT_EQ(*ret->value, 0u);
}

TEST(Ir3OptimizeTest, BorrowBlocksForwarding) {
    ir3::Function function{
        .symbol = "forward_borrow_barrier",
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
                    ir3::Load{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .source = slot_place(0, i32_type()),
                    },
                    ir3::Borrow{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::Ptr},
                        .is_mutable = false,
                        .source = slot_place(0, i32_type()),
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

    run_load_forwarding(function);

    const auto& optimized = function;
    EXPECT_EQ(instruction_count<ir3::Load>(optimized), 2u);
}

TEST(Ir3OptimizeTest, CallBlocksForwarding) {
    ir3::Function function{
        .symbol = "forward_call_barrier",
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
                    ir3::Load{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .source = slot_place(0, i32_type()),
                    },
                    ir3::Call{
                        .result = std::nullopt,
                        .callee = "opaque",
                        .args = {},
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
    EXPECT_EQ(instruction_count<ir3::Load>(optimized), 2u);
}

TEST(Ir3OptimizeTest, IndexedPlaceRemainsUntouched) {
    ir3::Function function{
        .symbol = "forward_indexed_skip",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "idx",
                .host_type = i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = array1_i32_type(),
                .is_mutable = true,
                .debug_name = "arr",
                .origin = ir3::SlotOrigin::User,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::Load{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .source = indexed_slot_place(0, array1_i32_type(), 0),
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .source = indexed_slot_place(0, array1_i32_type(), 0),
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
    EXPECT_EQ(instruction_count<ir3::Load>(optimized), 2u);
}

TEST(Ir3OptimizeTest, ForwardsRepeatedDerefFieldLoad) {
    ir3::Function function{
        .symbol = "forward_deref_field",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::Ptr},
                .name = "ptr",
                .host_type = pair_i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::Load{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .source = deref_field_place(0, pair_i32_type(), {1}, false),
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .source = deref_field_place(0, pair_i32_type(), {1}, false),
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
    EXPECT_EQ(instruction_count<ir3::Load>(optimized), 1u);
    const auto* ret = std::get_if<ir3::Return>(&*optimized.blocks[0].terminator);
    ASSERT_NE(ret, nullptr);
    ASSERT_TRUE(ret->value.has_value());
    EXPECT_EQ(*ret->value, 1u);
}

TEST(Ir3OptimizeTest, DerefStoreKillsCachedFacts) {
    ir3::Function function{
        .symbol = "forward_deref_store_kill",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::Ptr},
                .name = "ptr",
                .host_type = pair_i32_type(),
            },
            ir3::Param{
                .value = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                .name = "rhs",
                .host_type = i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::Load{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .source = deref_field_place(0, pair_i32_type(), {0}, true),
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = deref_field_place(0, pair_i32_type(), {1}, true),
                        .value = 1,
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 3, .klass = ir3::SsaClass::I32},
                        .source = deref_field_place(0, pair_i32_type(), {0}, true),
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
    EXPECT_EQ(instruction_count<ir3::Load>(optimized), 2u);
}

TEST(Ir3OptimizeTest, InlinesSimpleScalarCall) {
    ir3::Function callee{
        .symbol = "add1",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "x",
                .host_type = i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "entry",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 1,
                    },
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
        .entry_block = 0,
        .next_value = 3,
    };

    ir3::Function caller{
        .symbol = "caller",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "entry",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 7,
                    },
                    ir3::Call{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .callee = "add1",
                        .args = {0},
                    },
                },
                .terminator = ir3::Return{.value = 1},
            },
        },
        .entry_block = 0,
        .next_value = 2,
    };

    ir3::Module module{.functions = {callee, caller}};
    ir3::optimize_module(module);

    ASSERT_EQ(module.functions.size(), 2u);
    EXPECT_EQ(call_count(module.functions[1]), 0u);
    EXPECT_FALSE(contains_callee(module.functions[1], "add1"));
}

TEST(Ir3OptimizeTest, InlinesSplitBlockAndRewritesSuccessorPhiPredecessor) {
    ir3::Function callee{
        .symbol = "inc",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "x",
                .host_type = i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "entry",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 1,
                    },
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
        .entry_block = 0,
        .next_value = 3,
    };

    ir3::Function caller{
        .symbol = "phi_caller",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "cond",
                .host_type = i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "entry",
                .terminator = ir3::Branch{.condition = 0, .then_block = 1, .else_block = 2},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "callsite",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 5,
                    },
                    ir3::Call{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .callee = "inc",
                        .args = {1},
                    },
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
                },
                .terminator = ir3::Jump{.target = 3},
            },
            ir3::BasicBlock{
                .id = 2,
                .name = "other",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 5, .klass = ir3::SsaClass::I32},
                        .value = 9,
                    },
                },
                .terminator = ir3::Jump{.target = 3},
            },
            ir3::BasicBlock{
                .id = 3,
                .name = "join",
                .phis = {
                    ir3::Phi{
                        .result = ir3::Value{.id = 6, .klass = ir3::SsaClass::I32},
                        .incoming = {
                            ir3::PhiIncoming{.pred = 1, .value = 4},
                            ir3::PhiIncoming{.pred = 2, .value = 5},
                        },
                    },
                },
                .terminator = ir3::Return{.value = 6},
            },
        },
        .entry_block = 0,
        .next_value = 7,
    };

    ir3::Module module{.functions = {callee, caller}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions[1];
    EXPECT_EQ(call_count(optimized), 0u);
    ASSERT_EQ(optimized.blocks[3].phis.size(), 1u);
    const auto& phi = optimized.blocks[3].phis.front();
    ASSERT_EQ(phi.incoming.size(), 2u);
    EXPECT_EQ(phi.incoming[1].pred, 2u);
    EXPECT_NE(phi.incoming[0].pred, 1u);
}

TEST(Ir3OptimizeTest, InlinesCalleeWithReachableUnreachablePath) {
    ir3::Function callee{
        .symbol = "branch_or_trap",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "cond",
                .host_type = i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "entry",
                .terminator = ir3::Branch{.condition = 0, .then_block = 1, .else_block = 2},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "ret",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 7,
                    },
                },
                .terminator = ir3::Return{.value = 1},
            },
            ir3::BasicBlock{
                .id = 2,
                .name = "trap",
                .terminator = ir3::Unreachable{},
            },
        },
        .entry_block = 0,
        .next_value = 2,
    };

    ir3::Function caller{
        .symbol = "unreachable_caller",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                .name = "cond",
                .host_type = i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "entry",
                .instructions = {
                    ir3::Call{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .callee = "branch_or_trap",
                        .args = {0},
                    },
                },
                .terminator = ir3::Return{.value = 1},
            },
        },
        .entry_block = 0,
        .next_value = 2,
    };

    ir3::Module module{.functions = {callee, caller}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions[1];
    EXPECT_EQ(call_count(optimized), 0u);
    bool has_unreachable = false;
    for (const auto& block : optimized.blocks) {
        if (block.terminator &&
            std::holds_alternative<ir3::Unreachable>(*block.terminator)) {
            has_unreachable = true;
            break;
        }
    }
    EXPECT_TRUE(has_unreachable);
}

TEST(Ir3OptimizeTest, InlinesAggregateReturnThroughHiddenPointerArgument) {
    ir3::Function callee{
        .symbol = "make_pair",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::Ptr},
                .name = "return_addr",
                .host_type = pair_i32_type(),
            },
        },
        .return_class = std::nullopt,
        .source_return_type = pair_i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "entry",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 4,
                    },
                    ir3::IConst{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .value = 8,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = deref_field_place(0, pair_i32_type(), {0}),
                        .value = 1,
                    },
                    ir3::Store{
                        .klass = ir3::SsaClass::I32,
                        .dest = deref_field_place(0, pair_i32_type(), {1}),
                        .value = 2,
                    },
                },
                .terminator = ir3::Return{.value = std::nullopt},
            },
        },
        .entry_block = 0,
        .next_value = 3,
    };

    ir3::Function caller{
        .symbol = "aggregate_caller",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = pair_i32_type(),
                .is_mutable = true,
                .debug_name = "out",
                .origin = ir3::SlotOrigin::Temp,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "entry",
                .instructions = {
                    ir3::Borrow{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::Ptr},
                        .is_mutable = true,
                        .source = slot_place(0, pair_i32_type()),
                    },
                    ir3::Call{
                        .result = std::nullopt,
                        .callee = "make_pair",
                        .args = {0},
                    },
                    ir3::Load{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .source = field_place(0, pair_i32_type(), {1}),
                    },
                },
                .terminator = ir3::Return{.value = 1},
            },
        },
        .entry_block = 0,
        .next_value = 2,
    };

    ir3::Module module{.functions = {callee, caller}};
    ir3::optimize_module(module);

    const auto& optimized = module.functions[1];
    EXPECT_FALSE(contains_callee(optimized, "make_pair"));

    const auto machine = riscv::lower_module(module);
    ASSERT_EQ(machine.functions.size(), 2u);
}

TEST(Ir3OptimizeTest, SkipsSelfRecursiveCallee) {
    ir3::Function callee{
        .symbol = "recur",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "entry",
                .instructions = {
                    ir3::Call{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .callee = "recur",
                        .args = {},
                    },
                },
                .terminator = ir3::Return{.value = 0},
            },
        },
        .entry_block = 0,
        .next_value = 1,
    };

    ir3::Function caller{
        .symbol = "recursive_caller",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "entry",
                .instructions = {
                    ir3::Call{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .callee = "recur",
                        .args = {},
                    },
                },
                .terminator = ir3::Return{.value = 0},
            },
        },
        .entry_block = 0,
        .next_value = 1,
    };

    ir3::Module module{.functions = {callee, caller}};
    ir3::optimize_module(module);

    EXPECT_TRUE(contains_callee(module.functions[1], "recur"));
}

TEST(Ir3OptimizeTest, InlinesBottomUpAcrossCallChain) {
    ir3::Function leaf{
        .symbol = "leaf",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "entry",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 3,
                    },
                },
                .terminator = ir3::Return{.value = 0},
            },
        },
        .entry_block = 0,
        .next_value = 1,
    };

    ir3::Function mid{
        .symbol = "mid",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "entry",
                .instructions = {
                    ir3::Call{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .callee = "leaf",
                        .args = {},
                    },
                },
                .terminator = ir3::Return{.value = 0},
            },
        },
        .entry_block = 0,
        .next_value = 1,
    };

    ir3::Function top{
        .symbol = "top",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "entry",
                .instructions = {
                    ir3::Call{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .callee = "mid",
                        .args = {},
                    },
                },
                .terminator = ir3::Return{.value = 0},
            },
        },
        .entry_block = 0,
        .next_value = 1,
    };

    ir3::Module module{.functions = {leaf, mid, top}};
    ir3::optimize_module(module);

    EXPECT_EQ(call_count(module.functions[1]), 0u);
    EXPECT_EQ(call_count(module.functions[2]), 0u);
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

TEST(Ir3OptimizeTest, SccpRewritesStraightLineValuesAndSimplifiesBranch) {
    ir3::Function function{
        .symbol = "sccp_straight",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 7,
                    },
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 8,
                    },
                    ir3::Binary{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .op = ir3::BinaryOp::SAdd,
                        .lhs = 0,
                        .rhs = 1,
                    },
                    ir3::Unary{
                        .result = ir3::Value{.id = 3, .klass = ir3::SsaClass::I32},
                        .op = ir3::UnaryOp::BoolNot,
                        .operand = 2,
                    },
                },
                .terminator = ir3::Branch{.condition = 3, .then_block = 1, .else_block = 2},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "bb1",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 4, .klass = ir3::SsaClass::I32},
                        .value = 1,
                    },
                },
                .terminator = ir3::Return{.value = 4},
            },
            ir3::BasicBlock{
                .id = 2,
                .name = "bb2",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 5, .klass = ir3::SsaClass::I32},
                        .value = 2,
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
    ASSERT_EQ(optimized.blocks.size(), 2u);
    EXPECT_TRUE(optimized.blocks.front().instructions.empty());
    EXPECT_EQ(instruction_count<ir3::Binary>(optimized), 0u);
    EXPECT_EQ(instruction_count<ir3::Unary>(optimized), 0u);

    const auto* ret = std::get_if<ir3::Return>(&*optimized.blocks[1].terminator);
    ASSERT_NE(ret, nullptr);
    ASSERT_TRUE(ret->value.has_value());
    EXPECT_EQ(*ret->value, 5u);
}

TEST(Ir3OptimizeTest, SccpRewritesConstantPhiAfterDeadEdgePrune) {
    ir3::Function function{
        .symbol = "sccp_phi",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .value = 1,
                    },
                },
                .terminator = ir3::Branch{.condition = 0, .then_block = 1, .else_block = 2},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "bb1",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .value = 42,
                    },
                },
                .terminator = ir3::Jump{.target = 3},
            },
            ir3::BasicBlock{
                .id = 2,
                .name = "bb2",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .value = 99,
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

    const auto& optimized = module.functions.front();
    ASSERT_EQ(optimized.blocks.size(), 3u);
    EXPECT_TRUE(optimized.blocks[2].phis.empty());
    ASSERT_FALSE(optimized.blocks[2].instructions.empty());
    const auto* iconst = std::get_if<ir3::IConst>(&optimized.blocks[2].instructions.front());
    ASSERT_NE(iconst, nullptr);
    EXPECT_EQ(iconst->result.id, 3u);
    EXPECT_EQ(iconst->value, 42);
}

TEST(Ir3OptimizeTest, SccpUsesSlotToSsaExposedCondition) {
    ir3::Function function{
        .symbol = "sccp_slot_to_ssa",
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {
            ir3::Slot{
                .id = 0,
                .host_type = i32_type(),
                .is_mutable = true,
                .debug_name = "cond",
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
                .terminator = ir3::Branch{.condition = 1, .then_block = 1, .else_block = 2},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "bb1",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .value = 11,
                    },
                },
                .terminator = ir3::Return{.value = 2},
            },
            ir3::BasicBlock{
                .id = 2,
                .name = "bb2",
                .instructions = {
                    ir3::IConst{
                        .result = ir3::Value{.id = 3, .klass = ir3::SsaClass::I32},
                        .value = 22,
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
    ASSERT_EQ(optimized.blocks.size(), 2u);
    EXPECT_EQ(instruction_count<ir3::Load>(optimized), 0u);
    EXPECT_EQ(instruction_count<ir3::Store>(optimized), 0u);
    const auto* ret = std::get_if<ir3::Return>(&*optimized.blocks[1].terminator);
    ASSERT_NE(ret, nullptr);
    ASSERT_TRUE(ret->value.has_value());
    EXPECT_EQ(*ret->value, 3u);
}

TEST(Ir3OptimizeTest, SccpLeavesOpaqueComputationNonConstant) {
    ir3::Function function{
        .symbol = "sccp_negative",
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
                    ir3::Call{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .callee = "opaque",
                        .args = {},
                    },
                    ir3::Borrow{
                        .result = ir3::Value{.id = 3, .klass = ir3::SsaClass::Ptr},
                        .is_mutable = false,
                        .source = slot_place(0, i32_type()),
                    },
                    ir3::Cast{
                        .result = ir3::Value{.id = 4, .klass = ir3::SsaClass::Ptr},
                        .operand = 3,
                        .op = ir3::CastOp::PtrToPtr,
                    },
                    ir3::Binary{
                        .result = ir3::Value{.id = 5, .klass = ir3::SsaClass::I32},
                        .op = ir3::BinaryOp::SAdd,
                        .lhs = 1,
                        .rhs = 2,
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
    EXPECT_EQ(instruction_count<ir3::Call>(optimized), 1u);
    EXPECT_EQ(instruction_count<ir3::Binary>(optimized), 1u);
    EXPECT_FALSE(std::holds_alternative<ir3::IConst>(optimized.blocks.front().instructions.back()));
}

TEST(Ir3OptimizeTest, RemovesDefensiveAggregateCopyForReadonlyCalleeParam) {
    ir3::Function readonly{
        .symbol = "readonly_pair",
        .params = {
            ir3::Param{
                .value = ir3::Value{.id = 0, .klass = ir3::SsaClass::Ptr},
                .name = "arg0",
                .host_type = pair_i32_type(),
            },
        },
        .return_class = ir3::SsaClass::I32,
        .source_return_type = i32_type(),
        .slots = {},
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::Load{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .source = deref_field_place(0, pair_i32_type(), {0}, false),
                    },
                    ir3::Call{
                        .result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                        .callee = "readonly_pair",
                        .args = {0},
                    },
                },
                .terminator = ir3::Return{.value = 1},
            },
        },
        .entry_block = 0,
        .next_value = 3,
    };

    ir3::Function caller{
        .symbol = "caller",
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
                .debug_name = "tmp",
                .origin = ir3::SlotOrigin::Temp,
            },
        },
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .instructions = {
                    ir3::Copy{
                        .dest = slot_place(1, pair_i32_type()),
                        .source = slot_place(0, pair_i32_type()),
                    },
                    ir3::Borrow{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::Ptr},
                        .is_mutable = false,
                        .source = slot_place(1, pair_i32_type()),
                    },
                    ir3::Call{
                        .result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                        .callee = "readonly_pair",
                        .args = {0},
                    },
                },
                .terminator = ir3::Return{.value = 1},
            },
        },
        .entry_block = 0,
        .next_value = 2,
    };

    ir3::Module module{.functions = {readonly, caller}};
    ir3::optimize_module(module);

    ASSERT_EQ(module.functions.size(), 2u);
    const auto& optimized = module.functions[1];
    EXPECT_EQ(copy_count(optimized), 0u);
    ASSERT_EQ(instruction_count<ir3::Borrow>(optimized), 1u);

    const auto& insts = optimized.blocks.front().instructions;
    auto borrow_it = std::find_if(insts.begin(), insts.end(), [](const ir3::Instruction& inst) {
        return std::holds_alternative<ir3::Borrow>(inst);
    });
    ASSERT_NE(borrow_it, insts.end());
    const auto& borrow = std::get<ir3::Borrow>(*borrow_it);
    const auto* base = std::get_if<ir3::SlotBase>(&borrow.source.base);
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->slot, 0u);
}
