#pragma once

#include "semantic/type/type.hpp"
#include "symbol.hpp"
#include <unordered_map>
#include <vector>

namespace semantic {

struct TypeIdHasher {
  size_t operator()(const TypeId &type_id) const {
    return std::hash<const Type*>()(type_id);
  }
};

class ImplTable {
private:
  std::unordered_map<TypeId, std::vector<SymbolId>, TypeIdHasher> type_impls;

public:
  ImplTable() = default;

  void add_impl(TypeId type, SymbolId impl_symbol) {
    type_impls[type].push_back(impl_symbol);
  }

  const std::vector<SymbolId>& get_impls_for_type(TypeId type) const {
    if (auto it = type_impls.find(type); it != type_impls.end()) {
      return it->second;
    }
    static const std::vector<SymbolId> empty_vec;
    return empty_vec;
  }

  bool has_impls_for_type(TypeId type) const {
    return type_impls.find(type) != type_impls.end();
  }

  void clear() {
    type_impls.clear();
  }
};

} // namespace semantic
