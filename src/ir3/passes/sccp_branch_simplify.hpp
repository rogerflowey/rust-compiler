#pragma once

#include "ir3/analysis/sccp.hpp"
#include "ir3/ir3.hpp"

namespace ir3 {

class SccpBranchSimplifier {
public:
    bool run(Function& fn, const SccpInfo& sccp) const;
};

} // namespace ir3
