#pragma once

#include "semantic/const/const.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/pass/semantic_check/expr_check.hpp"
#include "semantic/pass/semantic_check/expr_info.hpp"
#include "semantic/query/expectation.hpp"
#include "type/impl_table.hpp"
#include <optional>
#include <unordered_set>

namespace semantic {

class ExprChecker;

class SemanticContext {
public:
    explicit SemanticContext(ImplTable& impl_table);

    TypeId type_query(hir::TypeAnnotation& annotation);
    ExprInfo expr_query(hir::Expr& expr, TypeExpectation exp = TypeExpectation::none());
    std::optional<ConstVariant> const_query(hir::Expr& expr, TypeId expected_type);
    std::optional<ConstVariant> const_query(hir::ConstDef& def);
    void bind_pattern_type(hir::Pattern& pattern, TypeId expected_type);
    TypeId function_return_type(hir::Function& function);
    TypeId method_return_type(hir::Method& method);

    ExprChecker& get_checker() { return expr_checker; }

private:
    ImplTable& impl_table;
    ExprChecker expr_checker;

    std::unordered_set<const hir::Expr*> evaluating_const_exprs;

    TypeId resolve_type_annotation(hir::TypeAnnotation& annotation);
    TypeId resolve_type_node(const hir::TypeNode& node);
    ExprInfo compute_expr(hir::Expr& expr, TypeExpectation exp);
    bool can_reuse_cached(const ExprInfo& info, TypeExpectation exp) const;
    void bind_reference_pattern(hir::ReferencePattern& pattern, TypeId expected_type);
    TypeId ensure_return_type_annotation(std::optional<hir::TypeAnnotation>& annotation);
};

} // namespace semantic
