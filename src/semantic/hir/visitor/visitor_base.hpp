#pragma once

#include "semantic/hir/hir.hpp"

namespace hir {

template<typename Derived>
class HirVisitorBase {
protected:
	Derived& derived() { return *static_cast<Derived*>(this); }
	const Derived& derived() const { return *static_cast<const Derived*>(this); }

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
		for (auto& stmt : block.stmts) {
			derived().visit_stmt(stmt);
		}
		if (block.final_expr && *block.final_expr) {
			derived().visit_expr(**block.final_expr);
		}
	}

	// Items
	void visit(Function& function) {
		derived().visit_block(function.body);
	}

	void visit(StructDef&) {}
	void visit(EnumDef&) {}

	void visit(ConstDef& constant) {
		derived().visit_expr(constant.value);
	}

	// Statements
	void visit(LetStmt& stmt) {
		derived().visit_expr(stmt.initializer);
	}

	void visit(ExprStmt& stmt) {
		derived().visit_expr(stmt.expr);
	}

	// Expressions
	void visit(Literal&) {}
	void visit(Variable&) {}
	void visit(FieldAccess& access) {
		derived().visit_expr(access.base);
	}
	void visit(StructLiteral& literal) {
		for (auto& field : literal.fields) {
			derived().visit_expr(field.initializer);
		}
	}
	void visit(ArrayLiteral& literal) {
		for (auto& element : literal.elements) {
			derived().visit_expr(element);
		}
	}
	void visit(ArrayRepeat& repeat) {
		derived().visit_expr(repeat.value);
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
