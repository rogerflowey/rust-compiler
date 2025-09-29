#pragma once

#include "ast/common.hpp"
#include "semantic/type.hpp"
#include <memory>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace semantic {

struct Symbol;

struct SymbolId {
  size_t id;
  bool operator==(const SymbolId &other) const = default;
};

struct SymbolIdHasher {
  size_t operator()(const SymbolId &s) const {
    return std::hash<size_t>()(s.id);
  }
};



enum class SymbolKind { Binding, Function, Struct, Field, Enum, Const, Trait, ImplBlock };

struct UndefinedSymbol {
  SymbolKind kind;
  const ast::Item *ast_node;
};

struct BindingSymbol {
  TypeId type;
  bool is_mutable;
};

struct FunctionSymbol {
  TypeId return_type;
  std::vector<BindingSymbol> parameters;
};

struct Field {
  TypeId type;
};

struct StructSymbol {
  TypeId struct_type;
  std::unordered_map<ast::Identifier, Field> fields;
};

struct EnumSymbol {
  TypeId enum_type;
  std::vector<ast::Identifier> variants;
};

struct ConstSymbol {
  TypeId type;
};

struct TraitSymbol {
    std::unordered_map<ast::Identifier, SymbolId> items;
};

struct ImplBlockSymbol {
    TypeId for_type;
    std::optional<SymbolId> trait_symbol;
    std::unordered_map<ast::Identifier, SymbolId> methods;
};

using SymbolVariant =
    std::variant<UndefinedSymbol, FunctionSymbol, StructSymbol, BindingSymbol,
                 EnumSymbol, ConstSymbol, TraitSymbol, ImplBlockSymbol>;

struct Symbol {
  SymbolVariant value;
  Symbol(SymbolVariant &&val) : value(std::move(val)) {}
};

class SymbolTable {
  std::vector<std::unique_ptr<Symbol>> symbols;

public:
  SymbolTable() = default;

  SymbolId create_symbol(SymbolVariant &&value) {
    size_t id = symbols.size();
    auto symbol_ptr = std::make_unique<Symbol>(std::move(value));
    symbols.push_back(std::move(symbol_ptr));
    return SymbolId{id};
  }

  Symbol *get_symbol(SymbolId id) { return symbols[id.id].get(); }
  const Symbol *get_symbol(SymbolId id) const { return symbols[id.id].get(); }
};

inline bool is_undefined_symbol(const Symbol &symbol) {
  return std::holds_alternative<UndefinedSymbol>(symbol.value);
}
inline bool is_type_symbol(const Symbol &symbol) {
  return std::holds_alternative<StructSymbol>(symbol.value) ||
         std::holds_alternative<EnumSymbol>(symbol.value) || 
         std::holds_alternative<TraitSymbol>(symbol.value);
}
inline bool is_value_symbol(const Symbol &symbol) {
  return std::holds_alternative<BindingSymbol>(symbol.value) ||
         std::holds_alternative<FunctionSymbol>(symbol.value) ||
         std::holds_alternative<ConstSymbol>(symbol.value);
}

} // namespace semantic