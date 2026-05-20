#pragma once

#include <typeindex>
#include <unordered_set>

namespace ir3 {

class AnalysisManager;

class PreservedAnalyses {
public:
    static PreservedAnalyses none() { return PreservedAnalyses(false); }
    static PreservedAnalyses all() { return PreservedAnalyses(true); }

    template <class Analysis>
    void preserve() {
        if (preserve_all_) {
            return;
        }
        preserved_.insert(std::type_index(typeid(Analysis)));
    }

    template <class Analysis>
    bool preserves() const {
        return preserves(std::type_index(typeid(Analysis)));
    }

private:
    explicit PreservedAnalyses(bool preserve_all)
        : preserve_all_(preserve_all) {}

    bool preserves(std::type_index type) const {
        return preserve_all_ || preserved_.contains(type);
    }

    bool preserve_all_ = false;
    std::unordered_set<std::type_index> preserved_;

    friend class AnalysisManager;
};

} // namespace ir3
