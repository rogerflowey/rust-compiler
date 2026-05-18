#pragma once

#include "riscv/machine_ir.hpp"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace riscv {

struct CfgInfo {
    std::vector<std::vector<BlockId>> predecessors; // indexed by block index
    std::vector<std::vector<BlockId>> successors;   // indexed by block index
    std::vector<std::size_t> rpo;                   // block indices in RPO
    std::unordered_map<BlockId, std::size_t> index_of;
};

CfgInfo compute_cfg(const MachineFunction& fn);

} // namespace riscv
