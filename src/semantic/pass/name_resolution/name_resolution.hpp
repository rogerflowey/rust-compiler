#pragma once
#include "ast/ast.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include "semantic/symbol/predefined.hpp"
#include "semantic/symbol/scope.hpp"
#include "semantic/type/helper.hpp"
#include "semantic/type/impl_table.hpp"
#include "semantic/type/type.hpp"
#include "semantic/utils.hpp"
#include <algorithm>
#include <iostream>
#include <optional>
#include <stack>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace semantic {

/*The plan:
 * for each scope, we first collect all item names
 * then resolve items in the scope
 * then collect binding & resolve expressions
 * finally, we pop the scope
 * for path, it should be collected and resolve the second segment after the
 * pass is finished
 */

class NameResolver : public hir::HirVisitorBase<NameResolver> {
  std::stack<Scope> scopes;
  ImplTable &impl_table;

  std::vector<std::pair<hir::Expr *, hir::TypeStatic *>> unresolved_statics;
  std::vector<std::vector<std::unique_ptr<hir::Local>> *> local_owner_stack;
  bool deferring_bindings = false;
  std::vector<hir::Local *> pending_locals;

  std::vector<std::unique_ptr<hir::Local>> *current_locals() {
    if (local_owner_stack.empty()) {
      return nullptr;
    }
    return local_owner_stack.back();
  }

  void register_local(hir::Local &local) {
    scopes.top().define_binding(local.name, &local);
  }

  hir::ExprVariant resolve_type_static(hir::TypeStatic &node) {
    auto *resolved_type = std::get_if<TypeDef>(&node.type);
    if (!resolved_type) {
      throw std::logic_error("TypeStatic node did not resolve its type");
    }
    const auto &target_name = node.name;
    return std::visit(
        Overloaded{
            [&](hir::StructDef *struct_def) -> hir::ExprVariant {
              TypeDef type_handle = struct_def;
              auto type_id = get_typeID(semantic::helper::to_type(type_handle));
              // Use new O(1) lookup API
              if (auto *constant =
                      impl_table.lookup_const(type_id, target_name)) {
                return hir::ExprVariant{hir::StructConst(
                    struct_def, constant)};
              }
              if (auto *fn = impl_table.lookup_function(type_id, target_name)) {
                return hir::ExprVariant{
                    hir::FuncUse(fn,node.ast_node)};
              }
              if (auto *method =
                      impl_table.lookup_method(type_id, target_name)) {
                std::cerr << "DEBUG: Method resolution attempted for method: "
                          << target_name.name << std::endl;
                if (!method) {
                  std::cerr << "DEBUG: Method pointer is null - AST node "
                               "corruption detected"
                            << std::endl;
                  throw std::runtime_error("Method Ast Node corrupted");
                }
                std::cerr << "DEBUG: Method resolution not supported yet"
                          << std::endl;
                throw std::runtime_error(
                    "Method resolution not supported yet");
              }
              throw std::runtime_error("Unable to resolve struct associated item " +
                                       target_name.name);
            },
            [&](hir::EnumDef *enum_def) -> hir::ExprVariant {
              auto it = std::find_if(enum_def->variants.begin(),
                                     enum_def->variants.end(),
                                     [&](const auto &variant) {
                                       return variant.name == target_name;
                                     });
              if (it != enum_def->variants.end()) {
                size_t idx = static_cast<size_t>(
                    std::distance(enum_def->variants.begin(), it));
                return hir::ExprVariant{hir::EnumVariant(enum_def, idx)};
              }
              throw std::runtime_error("Enum variant " + target_name.name +
                                       " not found");
            },
            [&](hir::Trait *) -> hir::ExprVariant {
              throw std::runtime_error(
                  "Trait associated items are not supported yet");
            }},
        *resolved_type);
  }

  void finalize_type_statics() {
    for (auto &pending : unresolved_statics) {
      if (!pending.first || !pending.second) {
        throw std::logic_error(
            "Encountered null pending static during finalization");
      }
      pending.first->value = resolve_type_static(*pending.second);
    }
    unresolved_statics.clear();
  }

public:
  NameResolver(ImplTable &impl_table) : impl_table(impl_table){};
  using hir::HirVisitorBase<NameResolver>::visit;
  using hir::HirVisitorBase<NameResolver>::visit_block;

  void define_item(hir::Item &item) {
    // Check if this is an Impl (which doesn't have a name)
    if (std::holds_alternative<hir::Impl>(item.value)) {
      return; // impl do not need define
    }

    // Get the name directly from the ItemVariant
    auto name = hir::helper::get_name(item.value);

    // Convert to SymbolDef (Impl is filtered out earlier)
    Scope::SymbolDef symbol_def = std::visit(
        [](auto &v) -> Scope::SymbolDef {
          if constexpr (std::is_same_v<std::decay_t<decltype(v)>, hir::Impl>)
            throw std::logic_error(
                "Impl should not be defined as it has no name");
          else
            return &v;
        },
        item.value);
    if (!scopes.top().define(name, symbol_def)) {
      throw std::runtime_error("Duplicate definition of " + name.name);
    }
  }

  void visit_program(hir::Program &program) {
    scopes.push(Scope{&get_predefined_scope(), true}); // the global scope

    for (auto &item : program.items) {
      define_item(*item);
    }
    // the base visit item first then stmt and expr
    base().visit_program(program);
    finalize_type_statics();
    scopes.pop();
  }

  void visit_block(hir::Block &block) {
    scopes.push(Scope{&scopes.top(), false});
    for (auto &item : block.items) {
      define_item(*item);
    }
    hir::HirVisitorBase<NameResolver>::visit_block(block);
    scopes.pop();
  }

  void visit(hir::Block &block) { visit_block(block); }

  // --- Items ---
  // Function, Trait, Impl create new scopes
  // StructDef, EnumDef, ConstDef do not
  void visit(hir::Function &func) {
    // function scope does not allow capture of outer bindings
    scopes.push(Scope{&scopes.top(), true});
    local_owner_stack.push_back(&func.locals);

    // the body will be treated as a pure block
    base().visit(func);
    local_owner_stack.pop_back();
    scopes.pop();
  }
  void visit(hir::Method &method) {
    scopes.push(Scope{&scopes.top(), true});
    local_owner_stack.push_back(&method.locals);


    // add the Self declaration
    auto self_type_def = scopes.top().lookup_type(ast::Identifier{"Self"});
    if (!self_type_def) {
      throw std::runtime_error("Method scope missing Self type");
    }
    
    // Self is stored as a TypeDef variant, extract the struct pointer from it
    auto *self_struct = std::get_if<hir::StructDef *>(&self_type_def.value());
    if (!self_struct || !*self_struct) {
      throw std::runtime_error("Self does not resolve to a struct in method");
    }

    // create Local for self
    auto self_local = std::make_unique<hir::Local>(hir::Local{
        ast::Identifier{"self"},
        method.self_param.is_mutable,
        get_typeID(Type{StructType{.symbol = *self_struct}}),
        nullptr
    });
    
    auto *self_ptr = self_local.get();
    method.self_local = std::move(self_local);
    register_local(*self_ptr);
    base().visit(method);
    local_owner_stack.pop_back();
    scopes.pop();
  }
  void visit(hir::StructDef &struct_def) {
    for (auto &annotation : struct_def.field_type_annotations) {
      visit_type_annotation(annotation);
    }
  }
  void visit(hir::Trait &trait) {
    scopes.push(Scope{&scopes.top(), true});
    base().visit(trait);
    scopes.pop();
  }
  void visit(hir::Impl &impl) {
    std::cerr << "DEBUG: visit(Impl) called" << std::endl;

    // resolve the type being implemented
    auto *type_node_ptr =
        std::get_if<std::unique_ptr<hir::TypeNode>>(&impl.for_type);
    if (!type_node_ptr || !*type_node_ptr) {
      throw std::logic_error("Impl `for_type` is missing or already resolved.");
    }
    auto *def_type_ptr =
        std::get_if<std::unique_ptr<hir::DefType>>(&(*type_node_ptr)->value);
    if (!def_type_ptr || !*def_type_ptr) {
      throw std::logic_error(
          "Impl for non-path type is **temporarily** not supported");
    }

    auto &def_type = **def_type_ptr;
    if (auto *ident = std::get_if<ast::Identifier>(&def_type.def)) {
      std::cerr << "DEBUG: Resolving impl type: " << ident->name << std::endl;
      auto type_def = scopes.top().lookup_type(*ident);
      if (!type_def) {
        throw std::runtime_error("Undefined type " + ident->name);
      }
      // Record the resolved nominal type so downstream passes see the canonical
      // handle.
      def_type.def = *type_def;
    }
    auto *resolved_def = std::get_if<TypeDef>(&def_type.def);
    if (!resolved_def) {
      throw std::logic_error("Impl for_type did not resolve to a TypeDef");
    }
  auto self_type_def = *resolved_def; // Preserve the nominal type for `Self`
  auto resolved_type_id =
    get_typeID(semantic::helper::to_type(self_type_def));
  impl.for_type = resolved_type_id;

    // register itself
    std::cerr << "DEBUG: Adding impl to impl_table, items count: " << impl.items.size() << std::endl;
    impl_table.add_impl(resolved_type_id, impl);
    std::cerr << "DEBUG: Impl added to table successfully" << std::endl;

    if (impl.trait) {
      auto &trait_variant = *impl.trait;
      if (auto *trait_ident = std::get_if<ast::Identifier>(&trait_variant)) {
        auto type_def = scopes.top().lookup_type(*trait_ident);
        if (!type_def) {
          throw std::runtime_error("Undefined trait " + trait_ident->name);
        }
        if (!std::holds_alternative<hir::Trait *>(*type_def)) {
          throw std::runtime_error(trait_ident->name + " is not a trait");
        }
        trait_variant = std::get<hir::Trait *>(*type_def);
      }
    }

    scopes.push(Scope{&scopes.top(), true});
  // add the Self declaration - register the actual TypeDef variant
  scopes.top().define_type(ast::Identifier{"Self"}, self_type_def);

    base().visit(impl);
    scopes.pop();
  }

  void visit(hir::BindingDef &binding) {
    hir::Local *local_ptr = nullptr;
    if (auto *unresolved =
            std::get_if<hir::BindingDef::Unresolved>(&binding.local)) {
      auto *locals = current_locals();
      if (!locals) {
        throw std::logic_error("Binding resolved outside of function or method "
                               "context is not supported yet");
      }
      auto local = std::make_unique<hir::Local>(
          hir::Local(unresolved->name, unresolved->is_mutable, std::nullopt, binding.ast_node));
      local_ptr = local.get();
      locals->push_back(std::move(local));
      binding.local = local_ptr;
    } else if (auto *resolved_local =
                   std::get_if<hir::Local *>(&binding.local)) {
      local_ptr = *resolved_local;
    } else {
      throw std::logic_error(
          "BindingDef.local holds an unexpected alternative");
    }

    if (!local_ptr) {
      throw std::logic_error("BindingDef failed to produce a Local*");
    }

    if (deferring_bindings) {
      pending_locals.push_back(local_ptr);
    } else {
      register_local(*local_ptr);
    }
  }
  void visit(hir::ReferencePattern &ref_pattern) {
    // Visit the subpattern
    derived().visit_pattern(ref_pattern.subpattern);
  }

  void visit(hir::LetStmt &stmt) {
  auto previous_deferring = deferring_bindings;
  auto saved_pending = std::move(pending_locals);
    deferring_bindings = true;
  pending_locals.clear();

    if (stmt.pattern) {
      hir::HirVisitorBase<NameResolver>::visit_pattern(stmt.pattern);
    }

  auto locals_to_register = std::move(pending_locals);
  pending_locals = std::move(saved_pending);
    deferring_bindings = previous_deferring;

    visit_optional_type_annotation(stmt.type_annotation);
    if (stmt.initializer) {
      hir::HirVisitorBase<NameResolver>::visit_expr(stmt.initializer);
    }

    for (auto *local : locals_to_register) {
      if (!local) {
        throw std::logic_error("Deferred local resolution produced null");
      }
      register_local(*local);
    }
  }

  // resolve names
  void visit(hir::TypeStatic &ts, hir::Expr &container) {
    auto *type_name_ptr = std::get_if<ast::Identifier>(&ts.type);
    if (type_name_ptr) {
      auto type_def = scopes.top().lookup_type(*type_name_ptr);
      if (!type_def) {
        throw std::runtime_error("Undefined type " + type_name_ptr->name);
      }
      // Replace the syntactic path with the resolved nominal type handle.
      ts.type = *type_def;
    }
    unresolved_statics.push_back({&container, &ts});
    base().visit(ts);
  }
  void visit(hir::UnresolvedIdentifier &ident, hir::Expr &container) {
    auto def = scopes.top().lookup_value(ident.name);
    if (!def) {
      throw std::runtime_error("Undefined identifier " + ident.name.name);
    }
    auto resolved = std::visit(
        Overloaded{[&](hir::Local *local) -> hir::ExprVariant {
                     if (!local) {
                       throw std::logic_error("Resolved local pointer is null");
                     }
                     return hir::ExprVariant{hir::Variable(local, ident.ast_node)};
                   },
                   [&](hir::ConstDef *constant) -> hir::ExprVariant {
                     return hir::ExprVariant{hir::ConstUse(constant, ident.ast_node)};
                   },
                   [&](hir::Function *function) -> hir::ExprVariant {
                     return hir::ExprVariant{hir::FuncUse(function, ident.ast_node)};
                   },
                   [&](hir::Method *method) -> hir::ExprVariant {
                     (void)method; // Suppress unused parameter warning
                     throw std::runtime_error(
                         "Direct method use is not supported. Methods must be "
                         "called through method call syntax.");
                   }},
        *def);
    container.value = std::move(resolved);
  }
  void visit(hir::StructLiteral &sl) {
    auto *name_ptr = std::get_if<ast::Identifier>(&sl.struct_path);
    if (!name_ptr) {
      base().visit(sl);
      return;
    }
    const auto &name = *name_ptr;

    auto def = scopes.top().lookup_type(name);
    if (!def) {
      throw std::runtime_error("Undefined struct " + name.name);
    }
    auto *struct_def = std::get_if<hir::StructDef *>(&def.value());
    if (!struct_def) {
      throw std::runtime_error(name.name + " is not a struct");
    }
    // Swap the identifier for the resolved struct definition pointer.
    sl.struct_path = *struct_def;

    // reorder the fields to canonical order
    auto syntactic_fields =
        std::get_if<hir::StructLiteral::SyntacticFields>(&sl.fields);
    if (!syntactic_fields) {
      throw std::runtime_error(
          "Struct literal fields are not in the expected format");
    }
    std::vector<std::unique_ptr<hir::Expr>> fields;
    fields.resize((*struct_def)->fields.size());
    for (auto &init : syntactic_fields->initializers) {
      auto it = std::find_if(
          (*struct_def)->fields.begin(), (*struct_def)->fields.end(),
          [&](const semantic::Field &f) { return f.name == init.first.name; });
      if (it == (*struct_def)->fields.end()) {
        throw std::runtime_error("Field " + init.first.name +
                                 " not found in struct " + name.name);
      }
      size_t index = std::distance((*struct_def)->fields.begin(), it);
      if (fields[index]) {
        throw std::runtime_error("Duplicate initialization of field " +
                                 init.first.name + " in struct " + name.name);
      }
      fields[index] = std::move(init.second);
    }
    sl.fields =
        hir::StructLiteral::CanonicalFields{.initializers = std::move(fields)};

    base().visit(sl);
  }

  void visit(hir::DefType &def_type) {
    if (auto *name = std::get_if<ast::Identifier>(&def_type.def)) {
      auto type_def = scopes.top().lookup_type(*name);
      if (!type_def) {
        throw std::runtime_error("Undefined type " + name->name);
      }
      // Attach the resolved type handle directly to the HIR node.
      def_type.def = *type_def;
    }
  }
};

} // namespace semantic