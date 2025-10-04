#pragma once
#include "symbol/scope.hpp"
#include "semantic/hir/hir.hpp"



namespace semantic {

// fn print(s: &str) -> ()
static hir::Function func_print = {
    .params = {hir::Pattern(hir::Binding{.is_mutable = false,
                            .type = get_typeID(Type{ReferenceType{
                                .referenced_type = get_typeID(Type{PrimitiveKind::STRING}),
                                .is_mutable = false}})})},
    .return_type = get_typeID(Type{UnitType{}}),
    .body = {},
    .ast_node = nullptr,
};

// fn println(s: &str) -> ()
static hir::Function func_println = {
    .params = {hir::Pattern(hir::Binding{.is_mutable = false,
                            .type = get_typeID(Type{ReferenceType{
                                .referenced_type = get_typeID(Type{PrimitiveKind::STRING}),
                                .is_mutable = false}})})},
    .return_type = get_typeID(Type{UnitType{}}),
    .body = {},
    .ast_node = nullptr,
};

// fn printInt(n: i32) -> ()
static hir::Function func_printInt = {
    .params = {hir::Pattern(hir::Binding{.is_mutable = false, .type = get_typeID(Type{PrimitiveKind::I32})})},
    .return_type = get_typeID(Type{UnitType{}}),
    .body = {},
    .ast_node = nullptr,
};

// fn printlnInt(n: i32) -> ()
static hir::Function func_printlnInt = {
    .params = {hir::Pattern(hir::Binding{.is_mutable = false, .type = get_typeID(Type{PrimitiveKind::I32})})},
    .return_type = get_typeID(Type{UnitType{}}),
    .body = {},
    .ast_node = nullptr,
};

// struct String {}
static hir::StructDef struct_String = {
    .fields = {},
    .ast_node = nullptr
};

// fn getString() -> String
static hir::Function func_getString = {
    .params = {},
    .return_type = get_typeID(Type{StructType{.symbol = &struct_String}}),
    .body = {},
    .ast_node = nullptr,
};

// fn getInt() -> i32
static hir::Function func_getInt = {
    .params = {},
    .return_type = get_typeID(Type{PrimitiveKind::I32}),
    .body = {},
    .ast_node = nullptr,
};

// fn exit(code: i32) -> !
static hir::Function func_exit = {
    .params = {hir::Pattern(hir::Binding{.is_mutable = false, .type = get_typeID(Type{PrimitiveKind::I32})})},
    .return_type = get_typeID(Type{NeverType{}}),
    .body = {},
    .ast_node = nullptr,
};




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