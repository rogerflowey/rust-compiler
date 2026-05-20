#pragma once

#include "ir3/passes/pass.hpp"

namespace ir3 {

class DeadBlockEliminationPass : public FunctionPass {
public:
    PreservedAnalyses run(Function& fn, AnalysisManager& am) override;
};

} // namespace ir3
