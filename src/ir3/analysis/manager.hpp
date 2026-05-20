#pragma once

#include "ir3/analysis/preserved.hpp"
#include "ir3/ir3.hpp"

#include <any>
#include <typeindex>
#include <unordered_map>

namespace ir3 {

class AnalysisManager {
public:
    template <class Analysis>
    const typename Analysis::Result& get(const Function& fn) {
        auto& function_cache = cache_[&fn];
        const auto key = std::type_index(typeid(Analysis));
        auto it = function_cache.find(key);
        if (it == function_cache.end()) {
            it = function_cache.emplace(key, Analysis::compute(fn, *this)).first;
        }
        return std::any_cast<const typename Analysis::Result&>(it->second);
    }

    void invalidate(const Function& fn, const PreservedAnalyses& preserved) {
        if (preserved.preserve_all_) {
            return;
        }

        auto fn_it = cache_.find(&fn);
        if (fn_it == cache_.end()) {
            return;
        }

        if (preserved.preserved_.empty()) {
            cache_.erase(fn_it);
            return;
        }

        auto& function_cache = fn_it->second;
        for (auto it = function_cache.begin(); it != function_cache.end();) {
            if (preserved.preserves(it->first)) {
                ++it;
                continue;
            }
            it = function_cache.erase(it);
        }

        if (function_cache.empty()) {
            cache_.erase(fn_it);
        }
    }

    void invalidate_all(const Function& fn) { cache_.erase(&fn); }

private:
    std::unordered_map<const Function*, std::unordered_map<std::type_index, std::any>> cache_;
};

} // namespace ir3
