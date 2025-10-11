#pragma once

#include "semantic/const/evaluator.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include "semantic/type/resolver.hpp"

#include <optional>
#include <stdexcept>
#include <type_traits>

namespace semantic {

// The pass traverses the HIR, resolves every type annotation to a TypeId, and
// evaluates const-expression sites so later passes operate purely on semantic data.
class TypeConstResolver : public hir::HirVisitorBase<TypeConstResolver> {
	TypeResolver type_resolver;
	ConstEvaluator const_evaluator;

public:
	using hir::HirVisitorBase<TypeConstResolver>::visit;

	void visit_program(hir::Program &program) {
		const_evaluator = ConstEvaluator{};
		hir::HirVisitorBase<TypeConstResolver>::visit_program(program);
	}

	void visit(hir::Function &function) {
		static const TypeId unit_type = get_typeID(Type{UnitType{}});
		if (!function.return_type) {
			// Default to the unit return type
			function.return_type = hir::TypeAnnotation(unit_type);
		}
		type_resolver.resolve(*function.return_type);
		hir::HirVisitorBase<TypeConstResolver>::visit(function);
	}

	void visit(hir::Method &method) {
		static const TypeId unit_type = get_typeID(Type{UnitType{}});
		if (!method.return_type) {
			// same as function
			method.return_type = hir::TypeAnnotation(unit_type);
		}
		type_resolver.resolve(*method.return_type);
		hir::HirVisitorBase<TypeConstResolver>::visit(method);
	}

	void visit(hir::Impl &impl) {
		type_resolver.resolve(impl.for_type);
		hir::HirVisitorBase<TypeConstResolver>::visit(impl);
	}

	void visit(hir::BindingDef &binding) {
		if (binding.type_annotation) {
			TypeId resolved_type = type_resolver.resolve(*binding.type_annotation);
			binding.type_annotation = hir::TypeAnnotation(resolved_type);
			if (auto *local_ptr = std::get_if<hir::Local *>(&binding.local)) {
				if (!*local_ptr) {
					throw std::logic_error("Binding resolved without an associated local");
				}
				// Mirror the binding's resolved type onto the captured local slot.
				(*local_ptr)->type_annotation = hir::TypeAnnotation(resolved_type);
			}
		}
	}

	void visit(hir::StructDef &struct_def) {
		if (struct_def.fields.size() != struct_def.field_type_annotations.size()) {
			throw std::logic_error("Struct field types and annotations are out of sync");
		}
		for (size_t idx = 0; idx < struct_def.field_type_annotations.size(); ++idx) {
			TypeId type_id = type_resolver.resolve(struct_def.field_type_annotations[idx]);
			struct_def.field_type_annotations[idx] = hir::TypeAnnotation(type_id);
			struct_def.fields[idx].type = type_id;
		}
	}

	void visit(hir::ConstDef &constant) {
		if (constant.type) {
			TypeId type_id = type_resolver.resolve(*constant.type);
			constant.type = hir::TypeAnnotation(type_id);
		}
		if (!constant.value) {
			throw std::logic_error("Const definition missing initializer");
		}
		hir::HirVisitorBase<TypeConstResolver>::visit(constant);
		// Fold the initializer expression into a compile-time constant value.
		constant.const_value = const_evaluator.evaluate(*constant.value);
	}

	void visit(hir::LetStmt &stmt) {
		std::optional<TypeId> explicit_type;
		if (stmt.type_annotation) {
			TypeId type_id = type_resolver.resolve(*stmt.type_annotation);
			stmt.type_annotation = hir::TypeAnnotation(type_id);
			explicit_type = type_id;
		}

		hir::HirVisitorBase<TypeConstResolver>::visit(stmt);

		if (explicit_type && stmt.pattern) {
			std::visit(
				[&](auto &node) {
					using Node = std::decay_t<decltype(node)>;
					if constexpr (std::is_same_v<Node, hir::BindingDef>) {
						// Keep the binding and its local in sync with the explicit annotation.
						node.type_annotation = hir::TypeAnnotation(*explicit_type);
						if (auto *local_ptr = std::get_if<hir::Local *>(&node.local)) {
							if (!*local_ptr) {
								throw std::logic_error("Binding resolved without an associated local");
							}
							(*local_ptr)->type_annotation = hir::TypeAnnotation(*explicit_type);
						}
					}
				},
				stmt.pattern->value
			);
		}
	}

	void visit(hir::Cast &cast) {
		// Ensure the target type of the cast is fully resolved before descending.
		type_resolver.resolve(cast.target_type);
		hir::HirVisitorBase<TypeConstResolver>::visit(cast);
	}

	void visit(hir::UnaryOp &op, hir::Expr &expr) {
		hir::HirVisitorBase<TypeConstResolver>::visit(op);
		if (op.op != hir::UnaryOp::NEGATE) {
			return;
		}
		if (!op.rhs) {
			return;
		}
		auto *literal_expr = std::get_if<hir::Literal>(&op.rhs->value);
		if (!literal_expr) {
			return;
		}
		auto *int_literal = std::get_if<hir::Literal::Integer>(&literal_expr->value);
		if (!int_literal) {
			return;
		}
		int_literal->is_negative = !int_literal->is_negative;
		hir::Literal merged_literal = std::move(*literal_expr);
		expr.value = std::move(merged_literal);
	}

	void visit(hir::ArrayRepeat &repeat) {
		hir::HirVisitorBase<TypeConstResolver>::visit(repeat);
		if (auto *count_expr = std::get_if<std::unique_ptr<hir::Expr>>(&repeat.count)) {
			if (*count_expr) {
				ConstVariant value = const_evaluator.evaluate(**count_expr);
				if (const auto *uint_value = std::get_if<UintConst>(&value)) {
					// Materialize the repeat count from an unsigned constant expression.
					repeat.count = static_cast<size_t>(uint_value->value);
					return;
				}
				if (const auto *int_value = std::get_if<IntConst>(&value)) {
					if (int_value->value < 0) {
						throw std::logic_error("Array repeat count must be non-negative");
					}
					// Accept signed integer constants by reinterpreting them as sizes.
					repeat.count = static_cast<size_t>(int_value->value);
					return;
				}
				throw std::logic_error("Array repeat count must be an integer constant");
			} else {
				throw std::logic_error("Array repeat count expression is null");
			}
		}
	}
};

} // namespace semantic