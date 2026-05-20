#pragma once

#include "ir3/ir3.hpp"

#include <unordered_map>

namespace ir3::detail {

ValueId resolve_replacement(const std::unordered_map<ValueId, ValueId>& replacements,
                            ValueId value);

void rewrite_all_uses(Function& fn,
                      const std::unordered_map<ValueId, ValueId>& replacements);

inline void rewrite_all_uses(Function& fn, ValueId from, ValueId to) {
    rewrite_all_uses(fn, std::unordered_map<ValueId, ValueId>{{from, to}});
}

} // namespace ir3::detail
