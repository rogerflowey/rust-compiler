#pragma once

#include "ir3/ir3.hpp"

#include <iosfwd>
#include <string>

namespace ir3 {

void print_module(std::ostream& out, const Module& module);
std::string to_string(const Module& module);

} // namespace ir3
