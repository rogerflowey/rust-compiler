#include "ir3/analysis/cfg.hpp"
#include "ir3/analysis/dominance.hpp"
#include "ir3/analysis/dominance_frontier.hpp"
#include "ir3/analysis/manager.hpp"
#include "ir3/analysis/slot_liveness.hpp"
#include "ir3/analysis/slot_use.hpp"
#include "semantic/type/type.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

semantic::TypeId i32_type() {
    return semantic::get_typeID(semantic::Type{semantic::PrimitiveKind::I32});
}

semantic::TypeId array1_i32_type() {
    return semantic::get_typeID(
        semantic::Type{semantic::ArrayType{.element_type = i32_type(), .size = 1}});
}

ir3::Place slot_place(ir3::SlotId slot, semantic::TypeId host_type) {
    return ir3::Place{
        .base = ir3::SlotBase{.slot = slot},
        .projections = {},
        .host_type = host_type,
        .is_mutable = true,
    };
}

ir3::Place projected_slot_place(ir3::SlotId slot) {
    return ir3::Place{
        .base = ir3::SlotBase{.slot = slot},
        .projections = {ir3::IndexProjection{.index = 0, .result_type = i32_type()}},
        .host_type = i32_type(),
        .is_mutable = true,
    };
}

} // namespace

TEST(Ir3DominanceFrontierTest, DiamondJoinHasExpectedFrontier) {
    ir3::Function fn{
        .symbol = "diamond",
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .terminator = ir3::Branch{.condition = 0, .then_block = 1, .else_block = 2},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "bb1",
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
                .terminator = ir3::Return{.value = std::nullopt},
            },
        },
        .entry_block = 0,
        .next_value = 1,
    };

    ir3::AnalysisManager am;
    const auto& frontier = am.get<ir3::DominanceFrontierAnalysis>(fn);

    EXPECT_TRUE(frontier.frontier(0).empty());
    EXPECT_EQ(frontier.frontier(1), std::vector<ir3::BlockId>({3}));
    EXPECT_EQ(frontier.frontier(2), std::vector<ir3::BlockId>({3}));
    EXPECT_TRUE(frontier.frontier(3).empty());
}

TEST(Ir3DominanceFrontierTest, LoopBackEdgePlacesHeaderInBodyFrontier) {
    ir3::Function fn{
        .symbol = "loop",
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .terminator = ir3::Jump{.target = 1},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "bb1",
                .terminator = ir3::Branch{.condition = 0, .then_block = 2, .else_block = 3},
            },
            ir3::BasicBlock{
                .id = 2,
                .name = "bb2",
                .terminator = ir3::Jump{.target = 1},
            },
            ir3::BasicBlock{
                .id = 3,
                .name = "bb3",
                .terminator = ir3::Return{.value = std::nullopt},
            },
        },
        .entry_block = 0,
        .next_value = 1,
    };

    ir3::AnalysisManager am;
    const auto& frontier = am.get<ir3::DominanceFrontierAnalysis>(fn);

    EXPECT_EQ(frontier.frontier(2), std::vector<ir3::BlockId>({1}));
}

TEST(Ir3DominanceFrontierTest, UnreachableBlockStaysOutOfFrontierQueries) {
    ir3::Function fn{
        .symbol = "unreachable",
        .blocks = {
            ir3::BasicBlock{
                .id = 0,
                .name = "bb0",
                .terminator = ir3::Return{.value = std::nullopt},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "bb1",
                .terminator = ir3::Jump{.target = 0},
            },
        },
        .entry_block = 0,
        .next_value = 0,
    };

    ir3::AnalysisManager am;
    const auto& frontier = am.get<ir3::DominanceFrontierAnalysis>(fn);

    EXPECT_TRUE(frontier.is_reachable(0));
    EXPECT_FALSE(frontier.is_reachable(1));
    EXPECT_TRUE(frontier.frontier(1).empty());
}

TEST(Ir3SlotUseTest, RootLoadStoreShapeIsPromotable) {
    ir3::Function fn{
        .symbol = "shape",
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
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot_place(0, i32_type()), .value = 0},
                    ir3::Load{.result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                              .source = slot_place(0, i32_type())},
                },
                .terminator = ir3::Return{.value = 1},
            },
        },
        .entry_block = 0,
        .next_value = 2,
    };

    ir3::AnalysisManager am;
    const auto& use = am.get<ir3::SlotUseAnalysis>(fn);

    EXPECT_TRUE(use.slot(0).has_promotable_shape());
    EXPECT_TRUE(use.slot(0).def_in_block[0]);
    EXPECT_FALSE(use.slot(0).use_before_def[0]);
}

TEST(Ir3SlotUseTest, BorrowRejectsPromotionShape) {
    ir3::Function fn{
        .symbol = "borrow",
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
                    ir3::Borrow{
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::Ptr},
                        .is_mutable = false,
                        .source = slot_place(0, i32_type()),
                    },
                },
                .terminator = ir3::Return{.value = std::nullopt},
            },
        },
        .entry_block = 0,
        .next_value = 1,
    };

    ir3::AnalysisManager am;
    const auto& use = am.get<ir3::SlotUseAnalysis>(fn);

    EXPECT_FALSE(use.slot(0).has_promotable_shape());
}

TEST(Ir3SlotUseTest, ProjectionRejectsPromotionShape) {
    ir3::Function fn{
        .symbol = "projection",
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
                        .result = ir3::Value{.id = 0, .klass = ir3::SsaClass::I32},
                        .source = projected_slot_place(0),
                    },
                },
                .terminator = ir3::Return{.value = 0},
            },
        },
        .entry_block = 0,
        .next_value = 1,
    };

    ir3::AnalysisManager am;
    const auto& use = am.get<ir3::SlotUseAnalysis>(fn);

    EXPECT_FALSE(use.slot(0).exact_root_load_store_only);
}

TEST(Ir3SlotUseTest, TempScalarRootLoadStoreShapeIsPromotable) {
    ir3::Function fn{
        .symbol = "temp_shape",
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
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot_place(0, i32_type()), .value = 0},
                    ir3::Load{.result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                              .source = slot_place(0, i32_type())},
                },
                .terminator = ir3::Return{.value = 1},
            },
        },
        .entry_block = 0,
        .next_value = 2,
    };

    ir3::AnalysisManager am;
    const auto& use = am.get<ir3::SlotUseAnalysis>(fn);

    EXPECT_TRUE(use.slot(0).has_promotable_shape());
}

TEST(Ir3SlotLivenessTest, StraightLineDefThenUseIsNotLiveInAtEntry) {
    ir3::Function fn{
        .symbol = "straight",
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
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot_place(0, i32_type()), .value = 0},
                    ir3::Load{.result = ir3::Value{.id = 1, .klass = ir3::SsaClass::I32},
                              .source = slot_place(0, i32_type())},
                },
                .terminator = ir3::Return{.value = 1},
            },
        },
        .entry_block = 0,
        .next_value = 2,
    };

    ir3::AnalysisManager am;
    const auto& live = am.get<ir3::SlotLivenessAnalysis>(fn);

    EXPECT_FALSE(live.slot(0).live_in[0]);
    EXPECT_FALSE(live.slot(0).live_out[0]);
}

TEST(Ir3SlotLivenessTest, DiamondJoinIsLiveIntoBothPredsAndEntry) {
    ir3::Function fn{
        .symbol = "diamond_live",
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
                    ir3::Load{.result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                              .source = slot_place(0, i32_type())},
                },
                .terminator = ir3::Return{.value = 2},
            },
        },
        .entry_block = 0,
        .next_value = 3,
    };

    ir3::AnalysisManager am;
    const auto& live = am.get<ir3::SlotLivenessAnalysis>(fn);

    EXPECT_TRUE(live.slot(0).live_in[3]);
    EXPECT_TRUE(live.slot(0).live_out[1]);
    EXPECT_TRUE(live.slot(0).live_in[2]);
    EXPECT_TRUE(live.slot(0).live_in[0]);
}

TEST(Ir3SlotLivenessTest, LoopKeepsSlotLiveAroundBackEdge) {
    ir3::Function fn{
        .symbol = "loop_live",
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
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot_place(0, i32_type()), .value = 1},
                },
                .terminator = ir3::Jump{.target = 1},
            },
            ir3::BasicBlock{
                .id = 1,
                .name = "bb1",
                .instructions = {
                    ir3::Load{.result = ir3::Value{.id = 2, .klass = ir3::SsaClass::I32},
                              .source = slot_place(0, i32_type())},
                },
                .terminator = ir3::Branch{.condition = 0, .then_block = 2, .else_block = 3},
            },
            ir3::BasicBlock{
                .id = 2,
                .name = "bb2",
                .instructions = {
                    ir3::Store{.klass = ir3::SsaClass::I32, .dest = slot_place(0, i32_type()), .value = 2},
                },
                .terminator = ir3::Jump{.target = 1},
            },
            ir3::BasicBlock{
                .id = 3,
                .name = "bb3",
                .terminator = ir3::Return{.value = 2},
            },
        },
        .entry_block = 0,
        .next_value = 3,
    };

    ir3::AnalysisManager am;
    const auto& live = am.get<ir3::SlotLivenessAnalysis>(fn);

    EXPECT_TRUE(live.slot(0).live_in[1]);
    EXPECT_TRUE(live.slot(0).live_out[2]);
    EXPECT_FALSE(live.slot(0).live_in[0]);
}
