#pragma once

#include "common.hpp"

class SelfParam {
public:
    bool is_reference;
    bool is_mutable;
    explicit SelfParam(bool is_reference, bool is_mutable): is_reference(is_reference), is_mutable(is_mutable) {}
    virtual ~SelfParam() = default;
};

using SelfParamPtr = std::unique_ptr<SelfParam>;


class FunctionItem : public Item {
public:
    IdPtr name;
    SelfParamPtr self_param; // may be nullptr
    std::vector<std::pair<IdPtr, TypePtr>> params;
    TypePtr return_type; // may be nullptr if no explicit return type
    BlockExprPtr body;

    FunctionItem(IdPtr name, SelfParamPtr self_param, std::vector<std::pair<IdPtr, TypePtr>> params, TypePtr return_type, BlockExprPtr body)
        : name(std::move(name)), self_param(std::move(self_param)), params(std::move(params)), return_type(std::move(return_type)), body(std::move(body)) {}
};
class StructItem : public Item {
public:
    IdPtr name;
    std::vector<std::pair<IdPtr, TypePtr>> fields;

    StructItem(IdPtr name, std::vector<std::pair<IdPtr, TypePtr>> fields)
        : name(std::move(name)), fields(std::move(fields)) {}
};
class EnumItem : public Item {
public:
    IdPtr name;
    std::vector<IdPtr> variants;

    EnumItem(IdPtr name, std::vector<IdPtr> variants)
        : name(std::move(name)), variants(std::move(variants)) {}
};
class ConstItem : public Item {
public:
    IdPtr name;
    TypePtr type;
    ExprPtr value;

    ConstItem(IdPtr name, TypePtr type, ExprPtr value)
        : name(std::move(name)), type(std::move(type)), value(std::move(value)) {}
};
class TraitItem : public Item {
public:
  IdPtr name;
  std::vector<ItemPtr> items;

  TraitItem(IdPtr name, std::vector<ItemPtr> items) : name(std::move(name)), items(std::move(items)) {}
};
class ImplItem : public Item {
public:
  TypePtr for_type;
  std::vector<ItemPtr> items;

  ImplItem(TypePtr for_type, std::vector<ItemPtr> items)
      : for_type(std::move(for_type)), items(std::move(items)) {}
  
  virtual ~ImplItem() = default;
};

class TraitImplItem : public ImplItem {
public:
  IdPtr trait_name;

  TraitImplItem(IdPtr trait_name, TypePtr for_type, std::vector<ItemPtr> items)
      : ImplItem(std::move(for_type), std::move(items)), trait_name(std::move(trait_name)) {}
};

class InherentImplItem : public ImplItem {
public:
    InherentImplItem(TypePtr for_type, std::vector<ItemPtr> items)
        : ImplItem(std::move(for_type), std::move(items)) {}
};
