#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace ir3::fixpoint {

enum class WorklistDiscipline { Fifo, Lifo };

template <class T,
          class Hash = std::hash<T>,
          class Eq = std::equal_to<T>>
class Worklist {
public:
    explicit Worklist(WorklistDiscipline discipline = WorklistDiscipline::Lifo)
        : discipline_(discipline) {}

    bool empty() const { return items_.empty(); }

    std::size_t size() const { return items_.size(); }

    bool push(const T& value) {
        if (!queued_.insert(value).second) {
            return false;
        }
        items_.push_back(value);
        return true;
    }

    bool push(T&& value) {
        if (!queued_.insert(value).second) {
            return false;
        }
        items_.push_back(std::move(value));
        return true;
    }

    T pop() {
        if (items_.empty()) {
            throw std::runtime_error("fixpoint worklist pop from empty queue");
        }

        T value;
        if (discipline_ == WorklistDiscipline::Fifo) {
            value = std::move(items_.front());
            items_.pop_front();
        } else {
            value = std::move(items_.back());
            items_.pop_back();
        }
        queued_.erase(value);
        return value;
    }

private:
    WorklistDiscipline discipline_;
    std::deque<T> items_;
    std::unordered_set<T, Hash, Eq> queued_;
};

} // namespace ir3::fixpoint
