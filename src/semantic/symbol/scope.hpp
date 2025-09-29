#pragma once
#include "ast/ast.hpp"
#include "symbol.hpp"
#include <optional>
#include <string>
#include <unordered_map>

namespace semantic {

class Scope {
  Scope *parent;
  bool is_boundary;
  std::unordered_map<std::string, SymbolId> value_symbols;
  std::unordered_map<std::string, SymbolId> type_symbols;

public:
  Scope(Scope *parent_scope = nullptr, bool is_boundary = false)
      : parent(parent_scope), is_boundary(is_boundary) {}


  bool insert_value(const ast::Identifier &name, SymbolId id) {
    auto [_, inserted] = value_symbols.try_emplace(name.name, id);
    return inserted;
  }

  bool insert_type(const ast::Identifier &name, SymbolId id) {
    auto [_, inserted] = type_symbols.try_emplace(name.name, id);
    return inserted;
  }

  std::optional<SymbolId> lookup_value(const ast::Identifier &name,
                                         const SymbolTable &table) const {
    bool enable_bindings = true;
    const Scope *current = this;

    while (current) {
      if (auto it = current->value_symbols.find(name.name);
          it != current->value_symbols.end()) {
        
        const Symbol *symbol = table.get_symbol(it->second);

        if (std::holds_alternative<BindingSymbol>(symbol->value)) {
          if (enable_bindings) {
            return it->second;
          }
        } else {
          return it->second;
        }
      }
      if (current->is_boundary) {
        enable_bindings = false;
      }

      current = current->parent;
    }

    return std::nullopt;
  }

  std::optional<SymbolId> lookup_type(const ast::Identifier &name) const {
    if (auto local_result = lookup_type_local(name)) {
      return local_result;
    }
    if (parent) {
      return parent->lookup_type(name);
    }
    return std::nullopt;
  }

  std::optional<SymbolId> lookup_value_local(const ast::Identifier &name) const {
    if (auto it = value_symbols.find(name.name); it != value_symbols.end()) {
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

} // namespace semantic