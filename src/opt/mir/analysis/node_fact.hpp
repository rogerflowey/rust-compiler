#pragma once

#include "opt/mir/ir/nodes.hpp" // ConstantValue

#include <algorithm> // for std::set_union
#include <iterator>  // for std::back_inserter
#include <stdexcept>
#include <vector>

namespace opt::mir {

// ============================================================================
// ConstantValue equality
// ============================================================================

inline bool operator==(const ConstantValue &a, const ConstantValue &b) {
  return a.kind == b.kind && a.bits == b.bits && a.is_signed == b.is_signed;
}

inline bool operator!=(const ConstantValue &a, const ConstantValue &b) {
  return !(a == b);
}

// ============================================================================
// ConstPropFact -- constant-propagation lattice component
//
//         Top             "Not yet analyzed / unreachable definition."
//        / | \ (lattice)
//     c1  c2  c3 ...     "Exactly this compile-time constant."
//        \ | /
//       Bottom            "Multiple distinct values possible."
//
//       NotApplicable     "Type mismatch (e.g. struct vs integer)"
//
// Analysis can only move facts downward: Top -> Constant -> Bottom.
// NotApplicable is disjoint and strict.
// ============================================================================

struct ConstPropFact {
  enum class Kind { Top, Constant, Bottom, NotApplicable };

  Kind kind = Kind::Top;
  ConstantValue value{}; // meaningful only when kind == Constant

  // -- Convenience constructors -----------------------------------------------

  static ConstPropFact top() { return {Kind::Top, {}}; }

  static ConstPropFact constant(ConstantValue v) {
    return {Kind::Constant, std::move(v)};
  }

  static ConstPropFact bottom() { return {Kind::Bottom, {}}; }

  static ConstPropFact not_applicable() { return {Kind::NotApplicable, {}}; }

  // -- Predicates -------------------------------------------------------------

  [[nodiscard]] bool is_top() const { return kind == Kind::Top; }
  [[nodiscard]] bool is_constant() const { return kind == Kind::Constant; }
  [[nodiscard]] bool is_bottom() const { return kind == Kind::Bottom; }
  [[nodiscard]] bool is_not_applicable() const {
    return kind == Kind::NotApplicable;
  }

  // -- Lattice meet -----------------------------------------------------------
  //
  //   NA  ^ NA       = NA
  //   NA  ^ x        = THROW
  //   x   ^ NA       = THROW
  //   Top ^ x        = x
  //   x   ^ Top      = x
  //   Const(a) ^ Const(a)  = Const(a)
  //   Const(a) ^ Const(b)  = Bottom   (a != b)
  //   Bottom ^ x     = Bottom
  //   x ^ Bottom     = Bottom

  static ConstPropFact meet(const ConstPropFact &a, const ConstPropFact &b) {
    if (a.is_not_applicable() && b.is_not_applicable()) {
      return a;
    }
    if (a.is_not_applicable() || b.is_not_applicable()) {
      throw std::runtime_error(
          "ConstPropFact::meet between applicable and non-applicable facts");
    }

    if (a.is_top())
      return b;
    if (b.is_top())
      return a;
    if (a.is_bottom() || b.is_bottom())
      return bottom();
    // Both Constant
    if (a.value == b.value)
      return a;
    return bottom();
  }

  // -- Equality ---------------------------------------------------------------

  bool operator==(const ConstPropFact &o) const {
    if (is_not_applicable() != o.is_not_applicable()) {
      throw std::runtime_error(
          "ConstPropFact equality between applicable and non-applicable facts");
    }
    if (kind != o.kind)
      return false;
    if (kind == Kind::Constant)
      return value == o.value;
    return true;
  }

  bool operator!=(const ConstPropFact &o) const { return !(*this == o); }
};

// ============================================================================
// PointToFact -- set of possible places a pointer might point to.
//
//         Top             "Not yet analyzed / unreachable definition."
//        / | \ (lattice)
//     {p1} {p2} ...       "Exactly these places."
//        \ | /
//       Bottom            "Unknown / could be anything."
//
//       NotApplicable     "Type mismatch (e.g. integer vs pointer)"
//
// Meet: Union of sets.
// ============================================================================

struct PointToFact {
  enum class Kind { Top, Set, NotApplicable };

  Kind kind = Kind::Top;
  std::vector<Place> places; // Sorted and deduplicated
  bool points_to_external = false;

  // -- Convenience constructors -----------------------------------------------

  static PointToFact top() { return {Kind::Top, {}}; }

  static PointToFact singleton(Place p) {
    return {Kind::Set, {std::move(p)}, false};
  }

  // "Bottom" in the old sense (total unknown) corresponds to external + no
  // known locals.
  static PointToFact bottom() { return {Kind::Set, {}, true}; }

  static PointToFact not_applicable() { return {Kind::NotApplicable, {}}; }

  // -- Predicates -------------------------------------------------------------

  [[nodiscard]] bool is_top() const { return kind == Kind::Top; }

  // "Bottom" check now looks for valid external flag with no known places.
  // Note: logic should generally check points_to_external directly.
  [[nodiscard]] bool is_bottom() const {
    return kind == Kind::Set && points_to_external && places.empty();
  }

  [[nodiscard]] bool is_not_applicable() const {
    return kind == Kind::NotApplicable;
  }

  // -- Lattice meet -----------------------------------------------------------
  //
  //   NA  ^ NA       = NA
  //   Top ^ x        = x
  //   x   ^ Top      = x
  //   Set(A, extA) ^ Set(B, extB) = Set(A U B, extA | extB)

  static PointToFact meet(const PointToFact &a, const PointToFact &b) {
    if (a.is_not_applicable() && b.is_not_applicable()) {
      return a;
    }
    if (a.is_not_applicable() || b.is_not_applicable()) {
      throw std::runtime_error(
          "PointToFact::meet between applicable and non-applicable facts");
    }

    if (a.is_top())
      return b;
    if (b.is_top())
      return a;

    // Both are Sets (or what used to be Bottom)
    PointToFact result;
    result.kind = Kind::Set;
    result.points_to_external = a.points_to_external || b.points_to_external;
    result.places.reserve(a.places.size() + b.places.size());

    // Set union (assuming sorted inputs)
    std::set_union(a.places.begin(), a.places.end(), b.places.begin(),
                   b.places.end(), std::back_inserter(result.places),
                   std::less<Place>{});

    // Deduplicate if needed? set_union handles sorted ranges correctly for
    // Union. If input vectors are sorted and unique, output is sorted and
    // unique. We assume invariant is maintained.

    return result;
  }

  // -- Equality ---------------------------------------------------------------

  bool operator==(const PointToFact &o) const {
    if (is_not_applicable() != o.is_not_applicable()) {
      throw std::runtime_error(
          "PointToFact equality between applicable and non-applicable facts");
    }
    if (kind != o.kind)
      return false;
    if (kind == Kind::Set)
      return points_to_external == o.points_to_external && places == o.places;
    return true;
  }

  bool operator!=(const PointToFact &o) const { return !(*this == o); }
};

// ============================================================================
// NodeFact -- product lattice over all analysis components
//
// Each component is an independent lattice. meet() and == dispatch
// component-wise. Adding a new analysis = adding a field here + its meet/==.
// ============================================================================

struct NodeFact {
  ConstPropFact const_prop;
  PointToFact point_to;
  // Future: IntervalFact interval;
  // Future: TypeNarrowFact type_narrow;

  // -- Convenience constructors -----------------------------------------------

  static NodeFact top() { return {ConstPropFact::top(), PointToFact::top()}; }

  static NodeFact bottom() {
    return {ConstPropFact::bottom(), PointToFact::bottom()};
  }

  static NodeFact initial_of(type::TypeId type);

  // -- Lattice meet (component-wise) ------------------------------------------

  static NodeFact meet(const NodeFact &a, const NodeFact &b) {
    return {ConstPropFact::meet(a.const_prop, b.const_prop),
            PointToFact::meet(a.point_to, b.point_to)};
  }

  // -- Equality (component-wise) ----------------------------------------------

  bool operator==(const NodeFact &o) const {
    return const_prop == o.const_prop && point_to == o.point_to;
  }

  bool operator!=(const NodeFact &o) const { return !(*this == o); }
};

} // namespace opt::mir
