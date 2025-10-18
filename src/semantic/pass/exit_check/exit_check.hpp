#pragma once

#include "semantic/hir/hir.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include <vector>

namespace semantic {

class ExitCheckVisitor : public hir::HirVisitorBase<ExitCheckVisitor> {
public:
	ExitCheckVisitor() = default;

	void check_program(hir::Program& program);

	void visit(hir::Function& function);
	void visit(hir::Method& method);
	void visit(hir::Call& call);
	void visit(hir::Impl& impl);
	void visit(hir::Trait& trait);
	
	// Template catch-all for nodes we don't specifically handle
	template<typename T>
	void visit(T& node) {
		// Use the base class implementation for default traversal
		base().visit(node);
	}

private:
	enum class ContextKind { Function, Method };

	struct Context {
		ContextKind kind;
		bool is_main = false;
		hir::Function* function = nullptr;
		std::vector<const hir::Call*> exit_calls;
	};

	std::vector<Context> context_stack_;
	size_t associated_scope_depth_ = 0;

	static bool is_main_function(const hir::Function& function, bool is_top_level);
	bool is_exit_call(const hir::Call& call) const;
	void validate_main_context(Context& ctx);
};

} // namespace semantic

