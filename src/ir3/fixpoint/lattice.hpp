#pragma once

#include <utility>

namespace ir3::fixpoint {

template <class State, class JoinFn>
bool join_assign(State& dst, const State& src, JoinFn&& join) {
    State merged = join(dst, src);
    if (merged == dst) {
        return false;
    }
    dst = std::move(merged);
    return true;
}

} // namespace ir3::fixpoint
