#include "riscv/block_order.hpp"
#include "riscv/machine_ir.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

using namespace riscv;

TEST(BlockOrderTest, PrefersElseFallthroughAndAppendsUnreachableBlocks) {
    MachineFunction fn{
        .symbol = "cfg",
        .blocks =
            {
                MachineBlock{
                    .id = 0,
                    .terminator = BranchNonZero{
                        .condition = PhysicalRegister::S1,
                        .then_block = 1,
                        .else_block = 2,
                    },
                },
                MachineBlock{
                    .id = 1,
                    .terminator = BranchNonZero{
                        .condition = PhysicalRegister::S2,
                        .then_block = 3,
                        .else_block = 4,
                    },
                },
                MachineBlock{
                    .id = 2,
                    .terminator = Jump{.target = 3},
                },
                MachineBlock{
                    .id = 3,
                    .terminator = Return{.value = PhysicalRegister::A0},
                },
                MachineBlock{
                    .id = 4,
                    .terminator = Return{.value = PhysicalRegister::A1},
                },
                MachineBlock{
                    .id = 5,
                    .terminator = Unreachable{},
                },
            },
        .entry_block = 0,
    };

    const auto order = order_blocks_for_asm(fn);
    const std::vector<BlockId> expected = {0, 2, 3, 1, 4, 5};
    EXPECT_EQ(order, expected);
}

} // namespace
