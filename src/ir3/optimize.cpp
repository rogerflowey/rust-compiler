#include "ir3/optimize.hpp"

#include "ir3/analysis/manager.hpp"
#include "ir3/passes/copy_coalesce.hpp"
#include "ir3/passes/dead_block_elim.hpp"
#include "ir3/passes/dead_code_elim.hpp"
#include "ir3/passes/inlining.hpp"
#include "ir3/passes/sroa.hpp"
#include "ir3/passes/slot_to_ssa.hpp"

#include <memory>
#include <vector>

namespace ir3 {
namespace {

std::vector<std::unique_ptr<FunctionPass>> build_passes() {
    std::vector<std::unique_ptr<FunctionPass>> passes;
    passes.push_back(std::make_unique<DeadBlockEliminationPass>());
    passes.push_back(std::make_unique<SroaPass>());
    passes.push_back(std::make_unique<CopyCoalescePass>());
    passes.push_back(std::make_unique<SlotToSsaPass>());
    passes.push_back(std::make_unique<DeadCodeEliminationPass>());
    return passes;
}

void run_function_passes(Function& fn) {
    AnalysisManager am;
    auto passes = build_passes();
    for (auto& pass : passes) {
        const auto preserved = pass->run(fn, am);
        am.invalidate(fn, preserved);
    }
}

} // namespace

void optimize_function(Function& fn) {
    run_function_passes(fn);
}

void optimize_module(Module& module) {
    for (auto& fn : module.functions) {
        run_function_passes(fn);
    }
    run_inlining(module);
    for (auto& fn : module.functions) {
        run_function_passes(fn);
    }
}

} // namespace ir3
