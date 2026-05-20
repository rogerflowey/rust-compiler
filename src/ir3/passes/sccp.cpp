#include "ir3/passes/sccp.hpp"

#include "ir3/analysis/sccp.hpp"
#include "ir3/passes/sccp_branch_simplify.hpp"
#include "ir3/passes/sccp_value_rewrite.hpp"

namespace ir3 {

PreservedAnalyses SccpPass::run(Function& fn, AnalysisManager& am) {
    const auto& sccp = am.get<SccpAnalysis>(fn);

    SccpValueRewriter value_rewriter;
    SccpBranchSimplifier branch_simplifier;

    const bool value_changed = value_rewriter.run(fn, sccp);
    const bool branch_changed = branch_simplifier.run(fn, sccp);
    const bool changed = value_changed || branch_changed;
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace ir3
