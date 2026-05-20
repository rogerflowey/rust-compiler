#pragma once

#include "ir3/ir3.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ir3 {

class AnalysisManager;

enum class SccpConstKind { Unknown, Constant, Overdefined };

struct SccpConst {
    SccpConstKind kind = SccpConstKind::Unknown;
    std::uint32_t bits = 0;

    static SccpConst unknown() { return SccpConst{}; }

    static SccpConst constant(std::uint32_t bits) {
        return SccpConst{.kind = SccpConstKind::Constant, .bits = bits};
    }

    static SccpConst overdefined() {
        return SccpConst{.kind = SccpConstKind::Overdefined};
    }

    bool is_unknown() const { return kind == SccpConstKind::Unknown; }
    bool is_constant() const { return kind == SccpConstKind::Constant; }
    bool is_overdefined() const { return kind == SccpConstKind::Overdefined; }

    bool operator==(const SccpConst& other) const {
        return kind == other.kind && bits == other.bits;
    }
};

struct SccpInfo {
    std::vector<SccpConst> values;               // indexed by value id
    std::vector<bool> executable_blocks;         // indexed by block id
    std::vector<std::vector<bool>> executable_edges; // indexed by block id then successor index

    const SccpConst& value(ValueId id) const {
        if (id >= values.size()) {
            throw std::out_of_range("SCCP query references invalid value %" +
                                    std::to_string(id));
        }
        return values[id];
    }

    bool is_block_executable(BlockId block) const {
        if (block >= executable_blocks.size()) {
            throw std::out_of_range("SCCP query references invalid block bb" +
                                    std::to_string(block));
        }
        return executable_blocks[block];
    }

    bool is_edge_executable(BlockId block, std::size_t succ_index) const {
        if (block >= executable_edges.size() ||
            succ_index >= executable_edges[block].size()) {
            throw std::out_of_range("SCCP query references invalid edge");
        }
        return executable_edges[block][succ_index];
    }
};

struct SccpAnalysis {
    using Result = SccpInfo;

    static Result compute(const Function& fn, AnalysisManager& am);
};

} // namespace ir3
