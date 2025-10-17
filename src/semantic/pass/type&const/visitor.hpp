#pragma once

#include "semantic/const/evaluator.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include "semantic/type/resolver.hpp"
#include "semantic/type/helper.hpp"
#include "semantic/utils.hpp"

#include <optional>
#include <stdexcept>
#include <type_traits>

namespace semantic {

// The pass traverses the HIR, resolves every type annotation to a TypeId, and
// evaluates const-expression sites so later passes operate purely on semantic data.
class TypeConstResolver : public hir::HirVisitorBase<TypeConstResolver> {
	TypeResolver type_resolver;
	ConstEvaluator const_evaluator;

private:
	// Pattern resolution methods
	void resolve_pattern_type(hir::Pattern& pattern, TypeId expected_type);
	void resolve_reference_pattern(hir::ReferencePattern& ref_pattern, TypeId expected_type);
	void resolve_binding_def_pattern(hir::BindingDef& binding, TypeId type);

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
		
		// Process parameter type annotations
		if (function.params.size() != function.param_type_annotations.size()) {
			throw std::logic_error("Function parameters and type annotations are out of sync");
		}
		
		for (size_t i = 0; i < function.param_type_annotations.size(); ++i) {
			auto& type_annotation = function.param_type_annotations[i];
			// Check if the type annotation has a value (is not std::nullopt)
			if (type_annotation) {
				TypeId type_id = type_resolver.resolve(*type_annotation);
				function.param_type_annotations[i] = hir::TypeAnnotation(type_id);
				
				// Resolve pattern with type
				if (function.params[i]) {
					resolve_pattern_type(*function.params[i], type_id);
				}
			} else {
				throw std::runtime_error("Function parameter missing type annotation");
			}
		}
		
		hir::HirVisitorBase<TypeConstResolver>::visit(function);
	}

	void visit(hir::Method &method) {
		static const TypeId unit_type = get_typeID(Type{UnitType{}});
		if (!method.return_type) {
			// same as function
			method.return_type = hir::TypeAnnotation(unit_type);
		}
		type_resolver.resolve(*method.return_type);
		
		// Process parameter type annotations
		if (method.params.size() != method.param_type_annotations.size()) {
			throw std::logic_error("Method parameters and type annotations are out of sync");
		}
		
		for (size_t i = 0; i < method.param_type_annotations.size(); ++i) {
			auto& type_annotation = method.param_type_annotations[i];
			// Check if the type annotation has a value (is not std::nullopt)
			if (type_annotation) {
				TypeId type_id = type_resolver.resolve(*type_annotation);
				method.param_type_annotations[i] = hir::TypeAnnotation(type_id);
				
				// Resolve pattern with type
				if (method.params[i]) {
					resolve_pattern_type(*method.params[i], type_id);
				}
			} else {
				throw std::runtime_error("Method parameter missing type annotation");
			}
		}
		
		hir::HirVisitorBase<TypeConstResolver>::visit(method);
	}

	void visit(hir::Impl &impl) {
		type_resolver.resolve(impl.for_type);
		hir::HirVisitorBase<TypeConstResolver>::visit(impl);
	}

	void visit(hir::BindingDef &binding) {
		// BindingDef no longer has type_annotation field
		// Type annotations are handled at the LetStmt level
		(void)binding; // Suppress unused parameter warning
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
		
		// Always visit the expression (it's always present)
		if (constant.expr) {
			derived().visit_expr(*constant.expr);
			
			// CRITICAL: Const evaluation failures are fatal compilation errors
			// This ensures the invariant that all consts are valid after this pass
			constant.const_value = const_evaluator.evaluate(*constant.expr);
		} else {
			throw std::logic_error("Const definition missing expression");
		}
	}

	void visit(hir::LetStmt &stmt) {
		std::optional<TypeId> explicit_type;
		if (stmt.type_annotation) {
			TypeId type_id = type_resolver.resolve(*stmt.type_annotation);
			stmt.type_annotation = hir::TypeAnnotation(type_id);
			explicit_type = type_id;
		}

		// Visit pattern with resolved type
		if (stmt.pattern) {
			if (explicit_type) {
				resolve_pattern_type(*stmt.pattern, *explicit_type);
			} else {
				// No type annotation - this is an error based on our design
				throw std::runtime_error("Let statement missing type annotation");
			}
		}

		hir::HirVisitorBase<TypeConstResolver>::visit(stmt);
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
				throw std::logic_error("Array repeat count must be an uint constant");
			} else {
				throw std::logic_error("Array repeat count expression is null");
			}
		}
	}
};

// Pattern resolution method implementations
inline void TypeConstResolver::resolve_pattern_type(hir::Pattern& pattern, TypeId expected_type) {
	std::visit([&](auto& node) {
		using Node = std::decay_t<decltype(node)>;
		if constexpr (std::is_same_v<Node, hir::BindingDef>) {
			resolve_binding_def_pattern(node, expected_type);
		} else if constexpr (std::is_same_v<Node, hir::ReferencePattern>) {
			resolve_reference_pattern(node, expected_type);
		}
	}, pattern.value);
}

inline void TypeConstResolver::resolve_reference_pattern(hir::ReferencePattern& ref_pattern, TypeId expected_type) {
	// Validate expected_type is a reference type
	if (!helper::type_helper::is_reference_type(expected_type)) {
		throw std::runtime_error("Reference pattern expects reference type");
	}

	// Check mutability matches
	bool expected_mutability = helper::type_helper::get_reference_mutability(expected_type);
	if (ref_pattern.is_mutable != expected_mutability) {
		throw std::runtime_error("Reference pattern mutability mismatch");
	}

	// Get referenced type and resolve subpattern
	TypeId referenced_type = helper::type_helper::get_referenced_type(expected_type);
	if (ref_pattern.subpattern) {
		resolve_pattern_type(*ref_pattern.subpattern, referenced_type);
	}
}

inline void TypeConstResolver::resolve_binding_def_pattern(hir::BindingDef& binding, TypeId type) {
	auto* local_ptr = std::get_if<hir::Local*>(&binding.local);
	if (!local_ptr || !*local_ptr) {
		throw std::logic_error("BindingDef does not have resolved Local*");
	}
	
	auto& local = **local_ptr;
	local.type_annotation = hir::TypeAnnotation(type);
}

} // namespace semantic