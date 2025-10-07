#pragma once

#include "semantic/hir/hir.hpp"

namespace hir {

// Forward declare HIR nodes for the visitor
struct DefType;
struct PrimitiveType;
struct ArrayType;
struct ReferenceType;
struct UnitType;
struct TypeNode;
struct Function;
struct Method;
struct StructDef;
struct EnumDef;
struct ConstDef;
struct Trait;
struct Impl;
struct LetStmt;
struct ExprStmt;
struct BindingDef;
struct WildCardPattern;
struct Literal;
struct UnresolvedIdentifier;
struct Variable;
struct ConstUse;
struct FuncUse;
struct TypeStatic;
struct Underscore;
struct FieldAccess;
struct StructLiteral;
struct StructConst;
struct StructStatic;
struct EnumVariant;
struct ArrayLiteral;
struct ArrayRepeat;
struct Index;
struct Assignment;
struct UnaryOp;
struct BinaryOp;
struct Cast;
struct Call;
struct MethodCall;
struct Block;
struct If;
struct Loop;
struct While;
struct Break;
struct Continue;
struct Return;


template<typename Derived>
class HirVisitorBase {
protected:
	Derived& derived() { return *static_cast<Derived*>(this); }
	const Derived& derived() const { return *static_cast<const Derived*>(this); }
	HirVisitorBase& base() { return *this; }
	const HirVisitorBase& base() const { return *this; }

public:
    using ExprUpdate = std::optional<ExprVariant>;

protected:

	void visit_optional_expr(std::optional<std::unique_ptr<Expr>>& maybe_expr) {
		if (maybe_expr && *maybe_expr) {
			derived().visit_expr(**maybe_expr);
		}
	}

	void visit_type_annotation(TypeAnnotation& annotation) {
		if (auto* type_node = std::get_if<std::unique_ptr<TypeNode>>(&annotation)) {
			if (*type_node) {
				derived().visit_type_node(**type_node);
			}
		}
	}

	void visit_optional_type_annotation(std::optional<TypeAnnotation>& opt_annotation) {
		if (opt_annotation) {
			derived().visit_type_annotation(*opt_annotation);
		}
	}

	void visit_pattern(std::unique_ptr<Pattern>& pattern) {
		if (pattern) {
			derived().visit_pattern(*pattern);
		}
	}

	ExprUpdate visit_expr(std::unique_ptr<Expr>& expr) {
		if (!expr) {
			return std::nullopt;
		}
		return derived().visit_expr(*expr);
	}

	ExprUpdate visit_expr(Expr& expr) {
		auto replacement = std::visit([this](auto& node) {
			return derived().visit(node);
		}, expr.value);
		if (replacement) {
			expr.value = std::move(*replacement);
		}
		return replacement;
	}

	void visit_block(std::unique_ptr<Block>& block) {
		if (block) {
			derived().visit_block(*block);
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

	// Type Nodes
	void visit_type_node(TypeNode& type_node) {
		std::visit([this](auto& node) { derived().visit(*node); }, type_node.value);
	}
	void visit(DefType&) {}
	void visit(PrimitiveType&) {}
	void visit(UnitType&) {}
	void visit(ArrayType& array_type) {
		derived().visit_type_annotation(array_type.element_type);
		derived().visit_expr(array_type.size);
	}
	void visit(ReferenceType& ref_type) {
		derived().visit_type_annotation(ref_type.referenced_type);
	}

	// Items
	void visit(Function& function) {
		for (auto& param : function.params) {
			derived().visit_pattern(param);
		}
		visit_optional_type_annotation(function.return_type);
		derived().visit_block(function.body);
	}

	void visit(Method& method) {
		// self_param is a simple struct, no need to visit its members.
		for (auto& param : method.params) {
			derived().visit_pattern(param);
		}
		visit_optional_type_annotation(method.return_type);
		derived().visit_block(method.body);
	}

	void visit(StructDef&) {}
	void visit(EnumDef&) {}

	void visit(ConstDef& constant) {
		visit_optional_type_annotation(constant.type);
		derived().visit_expr(constant.value);
	}
	void visit(Trait& trait) {
		for (auto& item : trait.items) {
			derived().visit_item(item);
		}
	}
	void visit(Impl& impl) {
		visit_type_annotation(impl.for_type);
		for (auto& item : impl.items) {
			derived().visit_associated_item(item);
		}
	}

	// Statements
	void visit(LetStmt& stmt) {
		derived().visit_pattern(stmt.pattern);
		visit_optional_type_annotation(stmt.type_annotation);
		derived().visit_expr(stmt.initializer);
	}

	void visit(ExprStmt& stmt) {
		derived().visit_expr(stmt.expr);
	}

	void visit_pattern(Pattern& pattern) {
		std::visit([this](auto& node) { derived().visit(node); }, pattern.value);
	}

	// Patterns
	void visit(BindingDef&) {}
	void visit(WildCardPattern&) {}

	// Expressions
	ExprUpdate visit(Literal&) { return std::nullopt; }
	ExprUpdate visit(UnresolvedIdentifier&) { return std::nullopt; }
	ExprUpdate visit(Variable&) { return std::nullopt; }
	ExprUpdate visit(ConstUse&) { return std::nullopt; }
	ExprUpdate visit(FuncUse&) { return std::nullopt; }
	ExprUpdate visit(TypeStatic&) { return std::nullopt; }
	ExprUpdate visit(Underscore&) { return std::nullopt; }
	ExprUpdate visit(FieldAccess& access) {
		derived().visit_expr(access.base);
		return std::nullopt;
	}
	ExprUpdate visit(StructLiteral& literal) {
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
		return std::nullopt;
	}
	ExprUpdate visit(StructConst&) { return std::nullopt; }
	ExprUpdate visit(StructStatic&) { return std::nullopt; }
	ExprUpdate visit(EnumVariant&) { return std::nullopt; }
	ExprUpdate visit(ArrayLiteral& literal) {
		for (auto& element : literal.elements) {
			derived().visit_expr(element);
		}
		return std::nullopt;
	}
	ExprUpdate visit(ArrayRepeat& repeat) {
		derived().visit_expr(repeat.value);
        if (auto* count_expr = std::get_if<std::unique_ptr<Expr>>(&repeat.count)) {
            derived().visit_expr(*count_expr);
        }
		return std::nullopt;
	}
	ExprUpdate visit(Index& index) {
		derived().visit_expr(index.base);
		derived().visit_expr(index.index);
		return std::nullopt;
	}
	ExprUpdate visit(Assignment& assignment) {
		derived().visit_expr(assignment.lhs);
		derived().visit_expr(assignment.rhs);
		return std::nullopt;
	}
	ExprUpdate visit(UnaryOp& op) {
		derived().visit_expr(op.rhs);
		return std::nullopt;
	}
	ExprUpdate visit(BinaryOp& op) {
		derived().visit_expr(op.lhs);
		derived().visit_expr(op.rhs);
		return std::nullopt;
	}
	ExprUpdate visit(Cast& cast) {
		derived().visit_expr(cast.expr);
		visit_type_annotation(cast.target_type);
		return std::nullopt;
	}
	ExprUpdate visit(Call& call) {
		derived().visit_expr(call.callee);
		for (auto& arg : call.args) {
			derived().visit_expr(arg);
		}
		return std::nullopt;
	}
	ExprUpdate visit(MethodCall& call) {
		derived().visit_expr(call.receiver);
		for (auto& arg : call.args) {
			derived().visit_expr(arg);
		}
		return std::nullopt;
	}
	ExprUpdate visit(Block& block_expr) {
		derived().visit_block(block_expr);
		return std::nullopt;
	}
	ExprUpdate visit(If& if_expr) {
		derived().visit_expr(if_expr.condition);
		derived().visit_block(if_expr.then_block);
		visit_optional_expr(if_expr.else_expr);
		return std::nullopt;
	}
	ExprUpdate visit(Loop& loop) {
		derived().visit_block(loop.body);
		return std::nullopt;
	}
	ExprUpdate visit(While& while_expr) {
		derived().visit_expr(while_expr.condition);
		derived().visit_block(while_expr.body);
		return std::nullopt;
	}
	ExprUpdate visit(Break& brk) {
		visit_optional_expr(brk.value);
		return std::nullopt;
	}
	ExprUpdate visit(Continue&) { return std::nullopt; }
	ExprUpdate visit(Return& ret) {
		visit_optional_expr(ret.value);
		return std::nullopt;
	}
};

} // namespace hir