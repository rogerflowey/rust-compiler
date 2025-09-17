#pragma once

#include "common.hpp"

namespace ast{

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
    std::optional<SelfParamPtr> self_param;
    std::vector<std::pair<PatternPtr, TypePtr>> params;
    std::optional<TypePtr> return_type;
    std::optional<BlockExprPtr> body;
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

}