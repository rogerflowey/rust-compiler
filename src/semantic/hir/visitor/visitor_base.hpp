#pragma once

#include "semantic/hir/hir.hpp"

namespace hir {

#define DEFINE_COMPATIBLE_VISIT(NodeType)                                             \
	void visit(NodeType& node, Expr& container) {                                     \
		derived().visit(node);                                                       \
		(void)container;                                                             \
		base().visit(node);                                                          \
	}                                                                                \
	void visit(NodeType& /*node*/) {}

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
struct Local;
struct LetStmt;
struct ExprStmt;
struct BindingDef;
struct ReferencePattern;
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

private:
	template <typename Node>
	void visit_dispatch(Node& node, Expr& container) {
		auto& d = derived();
		if constexpr (requires { d.visit(node, container); }) {
			d.visit(node, container);
		} else {
			(void)container;
			d.visit(node);
		}
	}

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

	void visit_expr(std::unique_ptr<Expr>& expr) {
		if (!expr) {
			return;
		}
		derived().visit_expr(*expr);
	}

	void visit_expr(Expr& expr) {
		std::visit([this, &expr](auto& node) {
			visit_dispatch(node, expr);
		}, expr.value);
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
	void visit(Local& local) {
		visit_optional_type_annotation(local.type_annotation);
	}

	// Items
	void visit(Function& function) {
		for (auto& param : function.params) {
			derived().visit_pattern(param);
		}
		for (auto& annotation : function.param_type_annotations) {
			visit_optional_type_annotation(annotation);
		}
		visit_optional_type_annotation(function.return_type);
		derived().visit_block(function.body);
		for (auto& local : function.locals) {
			if (local) {
				derived().visit(*local);
			}
		}
	}

	void visit(Method& method) {
		// self_param is a simple struct, no need to visit its members.
		for (auto& param : method.params) {
			derived().visit_pattern(param);
		}
		for (auto& annotation : method.param_type_annotations) {
			visit_optional_type_annotation(annotation);
		}
		visit_optional_type_annotation(method.return_type);
		derived().visit_block(method.body);
		if (method.self_local) {
			derived().visit(*method.self_local);
		}
		for (auto& local : method.locals) {
			if (local) {
				derived().visit(*local);
			}
		}
	}

	void visit(StructDef& struct_def) {
		for (auto& annotation : struct_def.field_type_annotations) {
			visit_type_annotation(annotation);
		}
	}
	void visit(EnumDef&) {}

	void visit(ConstDef& constant) {
		visit_optional_type_annotation(constant.type);
		if (constant.expr) {
			derived().visit_expr(*constant.expr);
		}
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
	void visit(ReferencePattern& ref_pattern) {
		derived().visit_pattern(ref_pattern.subpattern);
	}

	// Expressions
	void visit(Literal&) {}
	void visit(UnresolvedIdentifier&) {}
	void visit(Variable&) {}
	void visit(ConstUse&) {}
	void visit(FuncUse&) {}
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
		visit_type_annotation(cast.target_type);
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