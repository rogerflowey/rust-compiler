#include "semantic_context.hpp"

#include "semantic/const/evaluator.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/pass/semantic_check/expr_check.hpp"
#include "semantic/pass/semantic_check/type_compatibility.hpp"
#include "semantic/type/helper.hpp"
#include "semantic/type/type.hpp"
#include "semantic/utils.hpp"
#include <stdexcept>

namespace semantic {

namespace {

TypeId primitive_type_id(PrimitiveKind kind) {
    return get_typeID(Type{kind});
}

TypeId unit_type_id() {
    return get_typeID(Type{UnitType{}});
}

} // namespace

SemanticContext::SemanticContext(ImplTable& impl_table)
    : impl_table(impl_table), expr_checker(*this, impl_table) {}

TypeId SemanticContext::type_query(hir::TypeAnnotation& annotation) {
    auto* key = &annotation;
    if (auto it = type_cache.find(key); it != type_cache.end()) {
        return it->second;
    }

    TypeId resolved = resolve_type_annotation(annotation);
    type_cache.emplace(key, resolved);
    return resolved;
}

ExprInfo SemanticContext::expr_query(hir::Expr& expr, TypeExpectation exp) {
    ExprKey key{&expr, exp.kind, exp.expected};
    if (auto it = expr_cache.find(key); it != expr_cache.end()) {
        return it->second;
    }

    if (exp.kind != ExpectationKind::None) {
        ExprKey none_key{&expr, ExpectationKind::None, invalid_type_id};
        if (auto it = expr_cache.find(none_key); it != expr_cache.end()) {
            if (can_reuse_cached(it->second, exp)) {
                expr_cache.emplace(key, it->second);
                return it->second;
            }
        }
    }

    ExprInfo info = compute_expr(expr, exp);
    expr_cache.emplace(key, info);
    expr.expr_info = info;
    return info;
}

std::optional<ConstVariant> SemanticContext::const_query(hir::Expr& expr, TypeId expected_type) {
    auto [_, inserted] = evaluating_const_exprs.insert(&expr);
    if (!inserted) {
        return std::nullopt;
    }
    struct ConstGuard {
        SemanticContext& ctx;
        const hir::Expr* expr_ptr;
        ~ConstGuard() { ctx.evaluating_const_exprs.erase(expr_ptr); }
    } guard{*this, &expr};

    ExprInfo info = expr_query(expr, TypeExpectation::exact_const(expected_type));
    if (!info.has_type || info.type == invalid_type_id) {
        return std::nullopt;
    }
    if (expected_type != invalid_type_id && !is_assignable_to(info.type, expected_type)) {
        return std::nullopt;
    }
    return info.const_value;
}

std::optional<ConstVariant> SemanticContext::const_query(hir::ConstDef& def) {
    if (auto it = const_cache.find(&def); it != const_cache.end()) {
        return it->second;
    }

    TypeId expected_type = invalid_type_id;
    if (def.type) {
        expected_type = type_query(*def.type);
    }

    if (!def.expr) {
        throw std::logic_error("Const definition missing expression");
    }

    auto value = const_query(*def.expr, expected_type);
    def.const_value = value;
    const_cache.emplace(&def, value);
    return value;
}

void SemanticContext::bind_pattern_type(hir::Pattern& pattern, TypeId expected_type) {
    std::visit(
        [&](auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, hir::BindingDef>) {
                auto* local_ptr = std::get_if<hir::Local*>(&node.local);
                if (!local_ptr || !*local_ptr) {
                    throw std::logic_error("BindingDef does not have resolved Local*");
                }

                auto& local = **local_ptr;
                local.type_annotation = hir::TypeAnnotation(expected_type);
            } else if constexpr (std::is_same_v<Node, hir::ReferencePattern>) {
                bind_reference_pattern(node, expected_type);
            }
        },
        pattern.value);
}

TypeId SemanticContext::function_return_type(hir::Function& function) {
    return ensure_return_type_annotation(function.return_type);
}

TypeId SemanticContext::method_return_type(hir::Method& method) {
    return ensure_return_type_annotation(method.return_type);
}

TypeId SemanticContext::ensure_return_type_annotation(std::optional<hir::TypeAnnotation>& annotation) {
    if (!annotation) {
        annotation = hir::TypeAnnotation(unit_type_id());
    }
    return type_query(annotation.value());
}

TypeId SemanticContext::resolve_type_annotation(hir::TypeAnnotation& annotation) {
    if (auto* id_ptr = std::get_if<TypeId>(&annotation)) {
        return *id_ptr;
    }

    auto* node_ptr = std::get_if<std::unique_ptr<hir::TypeNode>>(&annotation);
    if (!node_ptr || !*node_ptr) {
        throw std::runtime_error("Type annotation is null");
    }

    TypeId resolved = resolve_type_node(**node_ptr);
    annotation = resolved;
    return resolved;
}

TypeId SemanticContext::resolve_type_node(const hir::TypeNode& node) {
    struct Visitor {
        SemanticContext& ctx;
        std::optional<TypeId> operator()(const std::unique_ptr<hir::DefType>& def_type) {
            auto type_def = std::get_if<TypeDef>(&def_type->def);
            if (!type_def) {
                return std::nullopt;
            }
            struct DefVisitor {
                TypeId operator()(const hir::StructDef* def) {
                    return get_typeID(Type{StructType{.symbol = def}});
                }
                TypeId operator()(const hir::EnumDef* def) {
                    return get_typeID(Type{EnumType{.symbol = def}});
                }
                TypeId operator()(const hir::Trait*) {
                    throw std::logic_error("Trait cannot be used as a concrete type");
                }
            };
            return std::visit(DefVisitor{}, *type_def);
        }
        std::optional<TypeId> operator()(const std::unique_ptr<hir::PrimitiveType>& prim_type) {
            return primitive_type_id(static_cast<PrimitiveKind>(prim_type->kind));
        }
        std::optional<TypeId> operator()(const std::unique_ptr<hir::ArrayType>& array_type) {
            auto element_type_id = ctx.type_query(array_type->element_type);
            auto size_value = ctx.const_query(*array_type->size, primitive_type_id(PrimitiveKind::USIZE));
            if (!size_value) {
                throw std::logic_error("Array size must be a constant expression");
            }
            if (auto* uint_value = std::get_if<UintConst>(&*size_value)) {
                return get_typeID(Type{ArrayType{.element_type = element_type_id, .size = uint_value->value}});
            }
            throw std::logic_error("Array size must resolve to an unsigned integer");
        }
        std::optional<TypeId> operator()(const std::unique_ptr<hir::ReferenceType>& ref_type) {
            auto referenced_type_id = ctx.type_query(ref_type->referenced_type);
            return get_typeID(Type{ReferenceType{.referenced_type = referenced_type_id, .is_mutable = ref_type->is_mutable}});
        }
        std::optional<TypeId> operator()(const std::unique_ptr<hir::UnitType>&) {
            return get_typeID(Type{UnitType{}});
        }
    };

    auto resolved = std::visit(Visitor{*this}, node.value);
    if (!resolved) {
        throw std::runtime_error("Failed to resolve type node");
    }
    return *resolved;
}

ExprInfo SemanticContext::compute_expr(hir::Expr& expr, TypeExpectation exp) {
    return expr_checker.evaluate(expr, exp);
}

bool SemanticContext::can_reuse_cached(const ExprInfo& info, TypeExpectation exp) const {
    if (!info.has_type || info.type == invalid_type_id) {
        return false;
    }
    if (exp.kind == ExpectationKind::ExactType || exp.kind == ExpectationKind::ExactConst) {
        if (!is_assignable_to(info.type, exp.expected)) {
            return false;
        }
        if (exp.kind == ExpectationKind::ExactConst && !info.const_value) {
            return false;
        }
    }
    return true;
}

void SemanticContext::bind_reference_pattern(hir::ReferencePattern& ref_pattern, TypeId expected_type) {
    if (!helper::type_helper::is_reference_type(expected_type)) {
        throw std::runtime_error("Reference pattern expects reference type");
    }

    bool expected_mutability = helper::type_helper::get_reference_mutability(expected_type);
    if (ref_pattern.is_mutable != expected_mutability) {
        throw std::runtime_error("Reference pattern mutability mismatch");
    }

    TypeId referenced_type = helper::type_helper::get_referenced_type(expected_type);
    if (ref_pattern.subpattern) {
        bind_pattern_type(*ref_pattern.subpattern, referenced_type);
    }
}

} // namespace semantic
