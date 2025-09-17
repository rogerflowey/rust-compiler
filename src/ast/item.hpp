#pragma once

#include "common.hpp"

// --- Concrete Item Nodes ---
struct FunctionItem {
    struct SelfParam {
        bool is_reference;
        bool is_mutable;
        explicit SelfParam(bool is_reference, bool is_mutable)
            : is_reference(is_reference), is_mutable(is_mutable) {}
    };
    using SelfParamPtr = std::unique_ptr<SelfParam>;
    
    IdPtr name;
    SelfParamPtr self_param;
    std::vector<std::pair<PatternPtr, TypePtr>> params;
    TypePtr return_type;
    BlockExprPtr body;
};

struct StructItem {
    IdPtr name;
    std::vector<std::pair<IdPtr, TypePtr>> fields;
};

struct EnumItem {
    IdPtr name;
    std::vector<IdPtr> variants;
};

struct ConstItem {
    IdPtr name;
    TypePtr type;
    ExprPtr value;
};

struct TraitItem {
  IdPtr name;
  std::vector<ItemPtr> items;
};

struct TraitImplItem {
  IdPtr trait_name;
  TypePtr for_type;
  std::vector<ItemPtr> items;
};

struct InherentImplItem {
    TypePtr for_type;
    std::vector<ItemPtr> items;
};

// --- Variant and Wrapper ---
using ItemVariant = std::variant<
    FunctionItem,
    StructItem,
    EnumItem,
    ConstItem,
    TraitItem,
    TraitImplItem,
    InherentImplItem
>;

// Complete the forward-declared type from common.hpp
struct Item {
    ItemVariant value;
};