#pragma once

#include "ast/ast.hpp"
#include "semantic/common.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/hir/helper.hpp"
#include <optional>
#include <string>
#include <unordered_map>

namespace semantic {

class Scope {
  Scope *parent;
  bool is_boundary;

  std::unordered_map<std::string, ValueDef> item_symbols;
  std::unordered_map<std::string, ValueDef> binding_symbols;
  std::unordered_map<std::string, TypeDef> type_symbols;

public:
  Scope(Scope *parent_scope = nullptr, bool is_boundary = false)
      : parent(parent_scope), is_boundary(is_boundary) {}

  bool define_item(const ast::Identifier &name, ValueDef def) {
    auto [_, inserted] = item_symbols.try_emplace(name.name, def);
    return inserted;
  }

  void define_binding(const ast::Identifier &name, ValueDef def) {
    binding_symbols[name.name] = def;
  }

  bool define_type(const ast::Identifier &name, TypeDef def) {
    auto [_, inserted] = type_symbols.try_emplace(name.name, def);
    return inserted;
  }

  using SymbolDef = hir::helper::NamedItemPtr;

  bool define(const ast::Identifier &name, SymbolDef def) {
    struct DefineVisitor {
      Scope *self;
      const ast::Identifier &name;
      bool operator()(hir::Binding *def) const {
        self->define_binding(name, def);
        return true;
      }
      bool operator()(hir::ConstDef *def) const {
        return self->define_item(name, def);
      }
      bool operator()(hir::Function *def) const {
        return self->define_item(name, def);
      }
      bool operator()(hir::StructDef *def) const {
        return self->define_type(name, def);
      }
      bool operator()(hir::EnumDef *def) const {
        return self->define_type(name, def);
      }
      bool operator()(hir::Trait *def) const {
        return self->define_type(name, def);
      }
    };
    return std::visit(DefineVisitor{this, name}, def);
  }

  std::optional<ValueDef> lookup_value(const ast::Identifier &name) const {
    bool bindings_enabled = true;
    const Scope *current = this;

    while (current) {
      if (bindings_enabled) {
        if (auto it = current->binding_symbols.find(name.name);
            it != current->binding_symbols.end()) {
          return it->second;
        }
      }

      if (auto it = current->item_symbols.find(name.name);
          it != current->item_symbols.end()) {
        return it->second;
      }

      if (current->is_boundary) {
        bindings_enabled = false;
      }

      current = current->parent;
    }

    return std::nullopt;
  }

  std::optional<TypeDef> lookup_type(const ast::Identifier &name) const {
    const Scope *current = this;
    while (current) {
      if (auto it = current->type_symbols.find(name.name);
          it != current->type_symbols.end()) {
        return it->second;
      }
      current = current->parent;
    }
    return std::nullopt;
  }

  std::optional<ValueDef>
  lookup_value_local(const ast::Identifier &name) const {
    if (auto it = binding_symbols.find(name.name);
        it != binding_symbols.end()) {
      return it->second;
    }
    if (auto it = item_symbols.find(name.name); it != item_symbols.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  std::optional<TypeDef> lookup_type_local(const ast::Identifier &name) const {
    if (auto it = type_symbols.find(name.name); it != type_symbols.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  Scope *get_parent() const { return parent; }
};

} // namespace semantic