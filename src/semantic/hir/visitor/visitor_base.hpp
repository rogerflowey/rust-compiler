#pragma once

#include "semantic/hir/hir.hpp"

namespace hir {

template<typename Derived>
class HirVisitorBase {
protected:
	Derived& derived() { return *static_cast<Derived*>(this); }
	const Derived& derived() const { return *static_cast<const Derived*>(this); }
	HirVisitorBase& base() { return *this; }
	const HirVisitorBase& base() const { return *this; }

	void visit_optional_expr(std::optional<std::unique_ptr<Expr>>& maybe_expr) {
		if (maybe_expr && *maybe_expr) {
			derived().visit_expr(**maybe_expr);
		}
	}

public:
	void visit_program(Program& program) {
		for (auto& item : program.items) {
			derived().visit_item(item);
		}
	}

	void visit_item(std::unique_ptr<Item>& item) {
		if (item) {
			derived().visit_item(*item);
		}
	}

	void visit_item(Item& item) {
		std::visit([this](auto& node) { derived().visit(node); }, item.value);
	}

	void visit_associated_item(std::unique_ptr<AssociatedItem>& item) {
		if (item) {
			derived().visit_associated_item(*item);
		}
	}

	void visit_associated_item(AssociatedItem& item) {
		std::visit([this](auto& node) { derived().visit(node); }, item.value);
	}

	void visit_stmt(std::unique_ptr<Stmt>& stmt) {
		if (stmt) {
			derived().visit_stmt(*stmt);
		}
	}

	void visit_stmt(Stmt& stmt) {
		std::visit([this](auto& node) { derived().visit(node); }, stmt.value);
	}

	void visit_expr(std::unique_ptr<Expr>& expr) {
		if (expr) {
			derived().visit_expr(*expr);
		}
	}

	void visit_expr(Expr& expr) {
		std::visit([this](auto& node) { derived().visit(node); }, expr.value);
	}

	void visit_block(std::unique_ptr<Block>& block) {
		if (block) {
			derived().visit_block(*block);
		}
	}

	void visit_block(Block& block) {
		for (auto& item : block.items) {
			derived().visit_item(item);
		}
		for (auto& stmt : block.stmts) {
			derived().visit_stmt(stmt);
		}
		if (block.final_expr && *block.final_expr) {
			derived().visit_expr(**block.final_expr);
		}
	}

	// Items
	void visit(Function& function) {
		for (auto& param : function.params) {
			derived().visit_pattern(param);
		}
		derived().visit_block(function.body);
	}

	void visit(Method& method) {
		// self_param is a simple struct, no need to visit its members.
		for (auto& param : method.params) {
			derived().visit_pattern(param);
		}
		derived().visit_block(method.body);
	}

	void visit(StructDef&) {}
	void visit(EnumDef&) {}

	void visit(ConstDef& constant) {
		derived().visit_expr(constant.value);
	}
	void visit(Trait& trait) {
		for (auto& item : trait.items) {
			derived().visit_item(item);
		}
	}
	void visit(Impl& impl) {
		for (auto& item : impl.items) {
			derived().visit_associated_item(item);
		}
	}

	// Statements
	void visit(LetStmt& stmt) {
		derived().visit_pattern(stmt.pattern);
		derived().visit_expr(stmt.initializer);
	}

	void visit(ExprStmt& stmt) {
		derived().visit_expr(stmt.expr);
	}

	void visit_pattern(Pattern& pattern) {
		std::visit([this](auto& node) { derived().visit(node); }, pattern.value);
	}

	// Patterns
	void visit(Binding&) {}
	void visit(WildCardPattern&) {}

	// Expressions
	void visit(Literal&) {}
	void visit(Variable&) {}
	void visit(TypeStatic&) {}
	void visit(Underscore&) {}
	void visit(FieldAccess& access) {
		derived().visit_expr(access.base);
	}
	void visit(StructLiteral& literal) {
		std::visit(
			[this](auto& fields_variant) {
				using T = std::decay_t<decltype(fields_variant)>;
				if constexpr (std::is_same_v<T, StructLiteral::SyntacticFields>) {
					for (auto& field : fields_variant.initializers) {
						derived().visit_expr(field.second);
					}
				} else if constexpr (std::is_same_v<T, StructLiteral::CanonicalFields>) {
					for (auto& field : fields_variant.initializers) {
						derived().visit_expr(field);
					}
				}
			},
			literal.fields
		);
	}
	void visit(StructConst&) {}
	void visit(StructStatic&) {}
	void visit(EnumVariant&) {}
	void visit(ArrayLiteral& literal) {
		for (auto& element : literal.elements) {
			derived().visit_expr(element);
		}
	}
	void visit(ArrayRepeat& repeat) {
		derived().visit_expr(repeat.value);
        if (auto* count_expr = std::get_if<std::unique_ptr<Expr>>(&repeat.count)) {
            derived().visit_expr(*count_expr);
        }
	}
	void visit(Index& index) {
		derived().visit_expr(index.base);
		derived().visit_expr(index.index);
	}
	void visit(Assignment& assignment) {
		derived().visit_expr(assignment.lhs);
		derived().visit_expr(assignment.rhs);
	}
	void visit(UnaryOp& op) {
		derived().visit_expr(op.rhs);
	}
	void visit(BinaryOp& op) {
		derived().visit_expr(op.lhs);
		derived().visit_expr(op.rhs);
	}
	void visit(Cast& cast) {
		derived().visit_expr(cast.expr);
	}
	void visit(Call& call) {
		derived().visit_expr(call.callee);
		for (auto& arg : call.args) {
			derived().visit_expr(arg);
		}
	}
	void visit(MethodCall& call) {
		derived().visit_expr(call.receiver);
		for (auto& arg : call.args) {
			derived().visit_expr(arg);
		}
	}
	void visit(Block& block_expr) {
		derived().visit_block(block_expr);
	}
	void visit(If& if_expr) {
		derived().visit_expr(if_expr.condition);
		derived().visit_block(if_expr.then_block);
		visit_optional_expr(if_expr.else_expr);
	}
	void visit(Loop& loop) {
		derived().visit_block(loop.body);
	}
	void visit(While& while_expr) {
		derived().visit_expr(while_expr.condition);
		derived().visit_block(while_expr.body);
	}
	void visit(Break& brk) {
		visit_optional_expr(brk.value);
	}
	void visit(Continue&) {}
	void visit(Return& ret) {
		visit_optional_expr(ret.value);
	}
};

} // namespace hir