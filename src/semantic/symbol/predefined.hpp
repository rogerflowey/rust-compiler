#pragma once
#include "symbol/scope.hpp"
#include "semantic/hir/hir.hpp"

#include <optional>

namespace semantic {

namespace {

hir::Function make_builtin_function() {
    return hir::Function{
        .params = {},
        .return_type = std::nullopt,
        .body = nullptr,
        .locals = {},
        .ast_node = nullptr
    };
}

} // namespace

// struct String {}
static hir::StructDef struct_String{
    .fields = {},
    .ast_node = nullptr
};

static hir::Function func_print = make_builtin_function();
static hir::Function func_println = make_builtin_function();
static hir::Function func_printInt = make_builtin_function();
static hir::Function func_printlnInt = make_builtin_function();
static hir::Function func_getString = make_builtin_function();
static hir::Function func_getInt = make_builtin_function();
static hir::Function func_exit = make_builtin_function();

inline Scope create_predefined_scope() {
  Scope scope;
  scope.define_type("String", &struct_String);
  scope.define_item("print", &func_print);
  scope.define_item("println", &func_println);
  scope.define_item("printInt", &func_printInt);
  scope.define_item("printlnInt", &func_printlnInt);
  scope.define_item("getString", &func_getString);
  scope.define_item("getInt", &func_getInt);
  scope.define_item("exit", &func_exit);

  return scope;
}

inline Scope& get_predefined_scope() {
    static Scope predefined_scope = create_predefined_scope();
    return predefined_scope;
}

} // namespace semantic