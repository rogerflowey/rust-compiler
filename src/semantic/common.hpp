#pragma once

#include <variant>

namespace hir {
struct Function;
struct StructDef;
struct BindingDef;
struct ConstDef;
struct EnumDef;
struct Trait;
}

using ValueDef = std::variant<hir::BindingDef*, hir::ConstDef*, hir::Function*>;
using TypeDef = std::variant<hir::StructDef*, hir::EnumDef*, hir::Trait*>;
