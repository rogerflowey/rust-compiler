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

	TypeId resolve_annotation(hir::TypeAnnotation &annotation) {
		return type_resolver.resolve(annotation);
	}

	void assign_type_to_local(hir::Local &local, TypeId type_id) {
		local.type_annotation = hir::TypeAnnotation(type_id);
	}

	void assign_type_to_binding(hir::BindingDef &binding, TypeId type_id) {
		binding.type_annotation = hir::TypeAnnotation(type_id);
		if (auto *local_ptr = std::get_if<hir::Local *>(&binding.local)) {
			if (!*local_ptr) {
				throw std::logic_error("Binding resolved without an associated local");
			}
			assign_type_to_local(**local_ptr, type_id);
		}
	}

	void assign_type_to_pattern(hir::Pattern &pattern, TypeId type_id) {
		std::visit(
			[&](auto &node) {
				using Node = std::decay_t<decltype(node)>;
				if constexpr (std::is_same_v<Node, hir::BindingDef>) {
					assign_type_to_binding(node, type_id);
				}
			},
			pattern.value
		);
	}

	void ensure_struct_field_annotations_in_sync(const hir::StructDef &struct_def) {
		if (struct_def.fields.size() != struct_def.field_type_annotations.size()) {
			throw std::logic_error("Struct field types and annotations are out of sync");
		}
	}

	[[nodiscard]] size_t evaluate_array_count(const hir::Expr &expr) {
		ConstVariant value = const_evaluator.evaluate(expr);
		if (const auto *uint_value = std::get_if<UintConst>(&value)) {
			return static_cast<size_t>(uint_value->value);
		}
		if (const auto *int_value = std::get_if<IntConst>(&value)) {
			if (int_value->value < 0) {
				throw std::logic_error("Array repeat count must be non-negative");
			}
			return static_cast<size_t>(int_value->value);
		}
		throw std::logic_error("Array repeat count must be an integer constant");
	}

public:
	using hir::HirVisitorBase<TypeConstResolver>::visit;

	void visit_program(hir::Program &program) {
		const_evaluator = ConstEvaluator{};
		hir::HirVisitorBase<TypeConstResolver>::visit_program(program);
	}

	void visit(hir::Function &function) {
		if (function.return_type) {
			resolve_annotation(*function.return_type);
		}
		hir::HirVisitorBase<TypeConstResolver>::visit(function);
	}

	void visit(hir::Method &method) {
		if (method.return_type) {
			resolve_annotation(*method.return_type);
		}
		hir::HirVisitorBase<TypeConstResolver>::visit(method);
	}

	void visit(hir::Impl &impl) {
		resolve_annotation(impl.for_type);
		hir::HirVisitorBase<TypeConstResolver>::visit(impl);
	}

	void visit(hir::BindingDef &binding) {
		if (binding.type_annotation) {
			TypeId type_id = resolve_annotation(*binding.type_annotation);
			assign_type_to_binding(binding, type_id);
		}
	}

	void visit(hir::StructDef &struct_def) {
		ensure_struct_field_annotations_in_sync(struct_def);
		for (size_t idx = 0; idx < struct_def.field_type_annotations.size(); ++idx) {
			TypeId type_id = resolve_annotation(struct_def.field_type_annotations[idx]);
			struct_def.field_type_annotations[idx] = hir::TypeAnnotation(type_id);
			struct_def.fields[idx].type = type_id;
		}
	}

	void visit(hir::ConstDef &constant) {
		if (constant.type) {
			TypeId type_id = resolve_annotation(*constant.type);
			constant.type = hir::TypeAnnotation(type_id);
		}
		if (!constant.value) {
			throw std::logic_error("Const definition missing initializer");
		}
		hir::HirVisitorBase<TypeConstResolver>::visit(constant);
		constant.const_value = const_evaluator.evaluate(*constant.value);
	}

	void visit(hir::LetStmt &stmt) {
		std::optional<TypeId> explicit_type;
		if (stmt.type_annotation) {
			TypeId type_id = resolve_annotation(*stmt.type_annotation);
			stmt.type_annotation = hir::TypeAnnotation(type_id);
			explicit_type = type_id;
		}

		hir::HirVisitorBase<TypeConstResolver>::visit(stmt);

		if (explicit_type && stmt.pattern) {
			assign_type_to_pattern(*stmt.pattern, *explicit_type);
		}
	}

	void visit(hir::Cast &cast) {
		resolve_annotation(cast.target_type);
		hir::HirVisitorBase<TypeConstResolver>::visit(cast);
	}

	void visit(hir::ArrayRepeat &repeat) {
		hir::HirVisitorBase<TypeConstResolver>::visit(repeat);
		if (auto *count_expr = std::get_if<std::unique_ptr<hir::Expr>>(&repeat.count)) {
			if (*count_expr) {
				repeat.count = evaluate_array_count(**count_expr);
			} else {
				throw std::logic_error("Array repeat count expression is null");
			}
		}
	}
};

} // namespace semantic