#pragma once

#include "type/type.hpp"

#include <string>

namespace opt::mir {

/// Mutability of a storage location.
enum class Mutability { Immutable, Mutable };

/// Slot — represents a distinct storage location (stack variable, heap object,
/// global). Distinct SlotIds are guaranteed not to alias ("Semantic Slicing").
struct Slot {
  enum class Kind {
    StackLocal,
    Parameter,   // Caller-initialized, logically immutable unless &mut
    MutRefParam, // &mut T parameter, treated as "Live-In" local of type T
    HeapObject,
    Global,
    // Temp slots are now just StackLocal
  };

  Kind kind = Kind::StackLocal;
  type::TypeId type = type::invalid_type_id;
  std::string debug_name;
  Mutability mutability = Mutability::Immutable;
};

} // namespace opt::mir
