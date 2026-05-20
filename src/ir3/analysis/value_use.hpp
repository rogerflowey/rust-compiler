#pragma once

#include "ir3/ir3.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace ir3 {

class AnalysisManager;

struct ParamDefSite {
    std::size_t param_index = 0;
};

struct PhiDefSite {
    BlockId block = 0;
    std::size_t phi_index = 0;
};

struct InstructionDefSite {
    BlockId block = 0;
    std::size_t instruction_index = 0;
};

using ValueDefSite = std::variant<ParamDefSite, PhiDefSite, InstructionDefSite>;

struct PhiUseSite {
    BlockId block = 0;
    std::size_t phi_index = 0;
    std::size_t incoming_index = 0;
};

struct InstructionUseSite {
    BlockId block = 0;
    std::size_t instruction_index = 0;
};

struct TerminatorUseSite {
    BlockId block = 0;
};

using ValueUseSite = std::variant<PhiUseSite, InstructionUseSite, TerminatorUseSite>;

struct ValueUseEntry {
    std::optional<SsaClass> klass;
    std::optional<ValueDefSite> def;
    std::vector<ValueUseSite> uses;
};

struct ValueUseInfo {
    std::vector<ValueUseEntry> values; // indexed by value id

    const ValueUseEntry& value(ValueId id) const {
        if (id >= values.size()) {
            throw std::out_of_range("value use query references invalid value %" +
                                    std::to_string(id));
        }
        return values[id];
    }
};

struct ValueUseAnalysis {
    using Result = ValueUseInfo;

    static Result compute(const Function& fn, AnalysisManager& am);
};

} // namespace ir3
