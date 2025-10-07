#pragma once

#include "hir/hir.hpp"
#include "semantic/type/type.hpp"
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
  std::unordered_map<TypeId, std::vector<hir::Impl*>, TypeIdHasher> type_impls;

public:
  ImplTable() = default;

  void add_impl(TypeId type, hir::Impl& impl_symbol) {
    type_impls[type].push_back(&impl_symbol);
  }

  const std::vector<hir::Impl*>& get_impls(TypeId type) const {
    if (auto it = type_impls.find(type); it != type_impls.end()) {
      return it->second;
    }
    static const std::vector<hir::Impl*> empty_vec;
    return empty_vec;
  }
};

} // namespace semantic
