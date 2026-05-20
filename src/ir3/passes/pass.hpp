#pragma once

#include "ir3/analysis/manager.hpp"

namespace ir3 {

class FunctionPass {
public:
    virtual ~FunctionPass() = default;

    virtual PreservedAnalyses run(Function& fn, AnalysisManager& am) = 0;
};

} // namespace ir3
