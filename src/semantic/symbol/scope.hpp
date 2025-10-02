#pragma once

#include "ast/ast.hpp"
#include "symbol.hpp"
#include <optional>
#include <string>
#include <unordered_map>

namespace semantic {

class Scope {
  Scope *parent;

  std::unordered_map<std::string, SymbolId> item_symbols;
  std::unordered_map<std::string, SymbolId> binding_symbols;
  std::unordered_map<std::string, SymbolId> type_symbols;

public:
  Scope(Scope *parent_scope = nullptr) : parent(parent_scope) {}

  bool insert_item(const ast::Identifier &name, SymbolId id) {
    auto [_, inserted] = item_symbols.try_emplace(name.name, id);
    return inserted;
  }

  void insert_binding(const ast::Identifier &name, SymbolId id) {
    binding_symbols[name.name] = id;
  }

  bool insert_type(const ast::Identifier &name, SymbolId id) {
    auto [_, inserted] = type_symbols.try_emplace(name.name, id);
    return inserted;
  }

  std::optional<SymbolId> lookup_value(const ast::Identifier &name) const {
    const Scope *current = this;
    while (current) {
      if (auto result = current->lookup_value_local(name)) {
        return result;
      }
      current = current->parent;
    }
    return std::nullopt;
  }

  std::optional<SymbolId> lookup_type(const ast::Identifier &name) const {
    const Scope *current = this;
    while (current) {
      if (auto result = current->lookup_type_local(name)) {
        return result;
      }
      current = current->parent;
    }
    return std::nullopt;
  }

  std::optional<SymbolId> lookup_value_local(const ast::Identifier &name) const {
    // binding checked before item to shadow them
    if (auto it = binding_symbols.find(name.name); it != binding_symbols.end()) {
      return it->second;
    }
    if (auto it = item_symbols.find(name.name); it != item_symbols.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  std::optional<SymbolId> lookup_type_local(const ast::Identifier &name) const {
    if (auto it = type_symbols.find(name.name); it != type_symbols.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  Scope *get_parent() const { return parent; }
};

}