#pragma once

#include "type/type.hpp"

namespace semantic {

enum class ExpectationKind {
    None,
    ExactType,
    ExactConst,
};

struct TypeExpectation {
    ExpectationKind kind = ExpectationKind::None;
    TypeId expected = invalid_type_id;
    bool has_expected = false;

    TypeExpectation() = default;
    TypeExpectation(ExpectationKind kind, TypeId expected)
        : kind(kind), expected(expected), has_expected(kind != ExpectationKind::None) {}

    static TypeExpectation none() { return {}; }
    static TypeExpectation exact(TypeId t) { return {ExpectationKind::ExactType, t}; }
    static TypeExpectation exact_const(TypeId t) { return {ExpectationKind::ExactConst, t}; }

    [[nodiscard]] bool has_expected_type() const { return kind != ExpectationKind::None; }
    [[nodiscard]] bool requires_const_value() const { return kind == ExpectationKind::ExactConst; }
};

} // namespace semantic

