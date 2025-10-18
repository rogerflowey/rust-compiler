#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <sstream>
#include <utility>

namespace debug {

struct ContextEntry {
    std::string scope;
    std::string name;
};

class Context {
public:
    class Guard {
    public:
        Guard(Context& ctx, ContextEntry entry)
            : ctx_(&ctx), active_(true) {
            ctx_->push_entry(std::move(entry));
        }

        Guard(Guard&& other) noexcept
            : ctx_(other.ctx_), active_(other.active_) {
            other.active_ = false;
        }

        Guard& operator=(Guard&&) = delete;
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        ~Guard() {
            if (active_ && ctx_) {
                ctx_->pop_entry();
            }
        }

    private:
        Context* ctx_;
        bool active_;
    };

    static Context& instance() {
        static Context ctx;
        return ctx;
    }

    Guard push(std::string scope, std::string name) {
        return Guard(*this, ContextEntry{std::move(scope), std::move(name)});
    }

    std::string format(const std::string& message) const {
        if (stack_.empty()) {
            return message;
        }

        std::ostringstream oss;
        oss << "In ";
        for (std::size_t i = 0; i < stack_.size(); ++i) {
            if (i > 0) {
                oss << " -> ";
            }
            oss << stack_[i].scope;
            if (!stack_[i].name.empty()) {
                oss << " '" << stack_[i].name << "'";
            }
        }
        oss << ": " << message;
        return oss.str();
    }

    bool is_current(std::string_view scope, std::string_view name) const {
        if (stack_.empty()) {
            return false;
        }
        const auto& current = stack_.back();
        return current.scope == scope && current.name == name;
    }

private:
    void push_entry(ContextEntry entry) {
        stack_.push_back(std::move(entry));
    }

    void pop_entry() {
        if (!stack_.empty()) {
            stack_.pop_back();
        }
    }

    inline static thread_local std::vector<ContextEntry> stack_{};
};

inline Context::Guard push(std::string scope, std::string name) {
    return Context::instance().push(std::move(scope), std::move(name));
}

inline std::string format_with_context(const std::string& message) {
    return Context::instance().format(message);
}

inline bool is_current(std::string_view scope, std::string_view name) {
    return Context::instance().is_current(scope, name);
}

} // namespace debug
