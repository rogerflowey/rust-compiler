#pragma once

#include "opt/mir/ir/nodes.hpp" // ConstantValue

#include <algorithm> // for std::set_union
#include <iterator>  // for std::back_inserter
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
// ConstPropFact — constant-propagation lattice component
//
//         Top             "Not yet analyzed / unreachable definition."
//        / | \
//     c₁  c₂  c₃ ...     "Exactly this compile-time constant."
//        \ | /
//       Bottom            "Multiple distinct values possible."
//
// Analysis can only move facts downward: Top → Constant → Bottom.
// ============================================================================

struct ConstPropFact {
  enum class Kind { Top, Constant, Bottom };

  Kind kind = Kind::Top;
  ConstantValue value{}; // meaningful only when kind == Constant

  // -- Convenience constructors -----------------------------------------------

  static ConstPropFact top() { return {Kind::Top, {}}; }

  static ConstPropFact constant(ConstantValue v) {
    return {Kind::Constant, std::move(v)};
  }

  static ConstPropFact bottom() { return {Kind::Bottom, {}}; }

  // -- Predicates -------------------------------------------------------------

  [[nodiscard]] bool is_top() const { return kind == Kind::Top; }
  [[nodiscard]] bool is_constant() const { return kind == Kind::Constant; }
  [[nodiscard]] bool is_bottom() const { return kind == Kind::Bottom; }

  // -- Lattice meet -----------------------------------------------------------
  //
  //   Top ⊓ x        = x
  //   x   ⊓ Top      = x
  //   Const(a) ⊓ Const(a)  = Const(a)
  //   Const(a) ⊓ Const(b)  = Bottom   (a ≠ b)
  //   Bottom ⊓ x     = Bottom
  //   x ⊓ Bottom     = Bottom

  static ConstPropFact meet(const ConstPropFact &a, const ConstPropFact &b) {
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
    if (kind != o.kind)
      return false;
    if (kind == Kind::Constant)
      return value == o.value;
    return true; // Top == Top, Bottom == Bottom
  }

  bool operator!=(const ConstPropFact &o) const { return !(*this == o); }
};

// ============================================================================
// PointToFact — set of possible places a pointer might point to.
//
//         Top             "Not yet analyzed / unreachable definition."
//        / | \
//     {p1} {p2} ...       "Exactly these places."
//        \ | /
//       Bottom            "Unknown / could be anything."
//
// Meet: Union of sets.
// ============================================================================

struct PointToFact {
  enum class Kind { Top, Set, Bottom };

  Kind kind = Kind::Top;
  std::vector<Place> places; // Sorted and deduplicated

  // -- Convenience constructors -----------------------------------------------

  static PointToFact top() { return {Kind::Top, {}}; }

  static PointToFact singleton(Place p) { return {Kind::Set, {std::move(p)}}; }

  static PointToFact bottom() { return {Kind::Bottom, {}}; }

  // -- Predicates -------------------------------------------------------------

  [[nodiscard]] bool is_top() const { return kind == Kind::Top; }
  [[nodiscard]] bool is_bottom() const { return kind == Kind::Bottom; }

  // -- Lattice meet -----------------------------------------------------------
  //
  //   Top ⊓ x        = x
  //   x   ⊓ Top      = x
  //   Set(A) ⊓ Set(B) = Set(A ∪ B)
  //   Bottom ⊓ x     = Bottom
  //   x ⊓ Bottom     = Bottom

  static PointToFact meet(const PointToFact &a, const PointToFact &b) {
    if (a.is_top())
      return b;
    if (b.is_top())
      return a;
    if (a.is_bottom() || b.is_bottom())
      return bottom();

    // Both are Sets: union
    PointToFact result;
    result.kind = Kind::Set;
    result.places.reserve(a.places.size() + b.places.size());

    // Set union (assuming sorted inputs)
    std::set_union(a.places.begin(), a.places.end(), b.places.begin(),
                   b.places.end(), std::back_inserter(result.places),
                   std::less<Place>{});

    return result;
  }

  // -- Equality ---------------------------------------------------------------

  bool operator==(const PointToFact &o) const {
    if (kind != o.kind)
      return false;
    if (kind == Kind::Set)
      return places == o.places;
    return true; // Top == Top, Bottom == Bottom
  }

  bool operator!=(const PointToFact &o) const { return !(*this == o); }
};

// ============================================================================
// NodeFact — product lattice over all analysis components
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
