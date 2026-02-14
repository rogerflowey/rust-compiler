#include "opt/mir/validator.hpp"
#include "opt/mir/node_id.hpp"
#include "type/type.hpp"
#include <iostream>
#include <variant>

namespace opt::mir {

namespace {

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

// Renamed helper to avoid ambiguity with opt::mir::raw (if it exists)
inline std::uint32_t as_int(type::TypeId t) {
  return static_cast<std::uint32_t>(t);
}
inline std::uint32_t as_int(NodeId n) { return static_cast<std::uint32_t>(n); }

bool is_scalar(type::TypeId id) {
  if (id == type::invalid_type_id)
    return false;
  const auto &ty = type::get_type_from_id(id);
  return std::holds_alternative<type::PrimitiveKind>(ty.value) ||
         std::holds_alternative<type::ReferenceType>(ty.value) ||
         std::holds_alternative<type::EnumType>(ty.value) ||
         std::holds_alternative<type::UnitType>(ty.value);
}

} // namespace

bool validate_pure_node_types(const OptModule &mod, std::ostream *os) {
  bool valid = true;
  for (const auto &func : mod.functions) {
    for (size_t i = 0; i < func.nodes.size(); ++i) {
      NodeId id{static_cast<uint32_t>(i)};
      const auto &node = func.nodes[i];

      // Strictly ban non-scalar nodes (aggregates must be on stack/memory)
      if (!is_scalar(node.type)) {
        if (os) {
          *os << "Error: Node %" << as_int(id) << " in function @" << func.name
              << " has non-scalar type " << as_int(node.type) << "\n";
        }
        valid = false;
      }
    }
  }
  return valid;
}

} // namespace opt::mir
