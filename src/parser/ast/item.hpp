#pragma once

#include "common.hpp"
#include <optional>



struct Param {
    bool is_ref;
    bool is_mut_ref;
    bool is_mut_binding;
    IdPtr name;
    TypePtr type;
};

class Function : public Item {
public:
    IdPtr name;
    std::vector<Param> params;
    std::optional<TypePtr> return_type;
    BlockExprPtr body;
    Function(IdPtr name, std::vector<Param> params, std::optional<TypePtr> return_type, BlockExprPtr body)
        : name(std::move(name)), params(std::move(params)), return_type(std::move(return_type)), body(std::move(body)) {}
};

class FunctionSignature {
public:
    IdPtr name;
    std::vector<Param> params;
    std::optional<TypePtr> return_type;
    FunctionSignature(IdPtr name, std::vector<Param> params, std::optional<TypePtr> return_type)
        : name(std::move(name)), params(std::move(params)), return_type(std::move(return_type)) {}
};

struct StructField {
    IdPtr name;
    TypePtr type;
};

class StructDef : public Item {
public:
    IdPtr name;
    std::vector<StructField> fields;
    StructDef(IdPtr name, std::vector<StructField> fields) : name(std::move(name)), fields(std::move(fields)) {}
};

struct EnumVariant {
    IdPtr name;
    std::optional<std::vector<TypePtr>> fields; 
};

class EnumDef : public Item {
public:
    IdPtr name;
    std::vector<EnumVariant> variants;
    EnumDef(IdPtr name, std::vector<EnumVariant> variants) : name(std::move(name)), variants(std::move(variants)) {}
};


class ConstItem : public Item {
public:
    IdPtr name;
    TypePtr type;
    ExprPtr value;
    ConstItem(IdPtr name, TypePtr type, ExprPtr value)
        : name(std::move(name)), type(std::move(type)), value(std::move(value)) {}
};

class StaticItem : public Item {
public:
    bool is_mutable;
    IdPtr name;
    TypePtr type;
    ExprPtr value;
    StaticItem(bool is_mutable, IdPtr name, TypePtr type, ExprPtr value)
        : is_mutable(is_mutable), name(std::move(name)), type(std::move(type)), value(std::move(value)) {}
};

class TraitDef : public Item {
public:
    IdPtr name;
    std::vector<std::unique_ptr<FunctionSignature>> functions;
    TraitDef(IdPtr name, std::vector<std::unique_ptr<FunctionSignature>> functions)
        : name(std::move(name)), functions(std::move(functions)) {}
};


class ImplBlock : public Item {
public:
    std::optional<TypePtr> trait_path;
    TypePtr type;
    std::vector<std::unique_ptr<Function>> functions;
    ImplBlock(std::optional<TypePtr> trait_path, TypePtr type, std::vector<std::unique_ptr<Function>> functions)
        : trait_path(std::move(trait_path)), type(std::move(type)), functions(std::move(functions)) {}
};


class TypeAlias : public Item {
public:
    IdPtr name;
    TypePtr type;
    TypeAlias(IdPtr name, TypePtr type) : name(std::move(name)), type(std::move(type)) {}
};