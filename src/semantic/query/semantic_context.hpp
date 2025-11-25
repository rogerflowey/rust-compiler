#pragma once

#include "semantic/const/const.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/pass/semantic_check/expr_info.hpp"
#include "semantic/query/expectation.hpp"
#include "semantic/type/impl_table.hpp"
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <type_traits>

namespace semantic {

class ExprChecker;

class SemanticContext {
public:
    explicit SemanticContext(ImplTable& impl_table);

    TypeId type_query(hir::TypeAnnotation& annotation);
    ExprInfo expr_query(hir::Expr& expr, TypeExpectation exp = TypeExpectation::none());
    ConstVariant const_query(hir::Expr& expr, TypeId expected_type);
    ConstVariant const_query(hir::ConstDef& def);
    void bind_pattern_type(hir::Pattern& pattern, TypeId expected_type);

    ExprChecker& get_checker() { return expr_checker; }

private:
    ImplTable& impl_table;
    ExprChecker expr_checker;
    std::unordered_map<const hir::TypeAnnotation*, TypeId> type_cache;

    struct ExprKey {
        hir::Expr* expr;
        ExpectationKind kind;
        TypeId expected;
    };

    struct ExprKeyHash {
        size_t operator()(const ExprKey& key) const {
            size_t seed = std::hash<hir::Expr*>()(key.expr);
            seed ^= std::hash<int>()(static_cast<int>(key.kind)) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= std::hash<TypeId>()(key.expected) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    struct ExprKeyEqual {
        bool operator()(const ExprKey& lhs, const ExprKey& rhs) const {
            return lhs.expr == rhs.expr && lhs.kind == rhs.kind && lhs.expected == rhs.expected;
        }
    };

    std::unordered_map<ExprKey, ExprInfo, ExprKeyHash, ExprKeyEqual> expr_cache;
    std::unordered_map<const hir::ConstDef*, ConstVariant> const_cache;
    std::unordered_set<const hir::Expr*> evaluating_const_exprs;

    TypeId resolve_type_annotation(hir::TypeAnnotation& annotation);
    TypeId resolve_type_node(const hir::TypeNode& node);
    ExprInfo compute_expr(hir::Expr& expr, TypeExpectation exp);
    bool can_reuse_cached(const ExprInfo& info, TypeExpectation exp) const;
    void bind_reference_pattern(hir::ReferencePattern& pattern, TypeId expected_type);
};

} // namespace semantic

