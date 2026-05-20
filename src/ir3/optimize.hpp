#pragma once

#include "ir3/ir3.hpp"

namespace ir3 {

void optimize_function(Function& fn);
void optimize_module(Module& module);

} // namespace ir3
