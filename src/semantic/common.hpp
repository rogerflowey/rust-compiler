#pragma once

#include <variant>

namespace hir {
struct Function;
struct StructDef;
struct Binding;
struct ConstDef;
struct EnumDef;
struct Trait;
}

using ValueDef = std::variant<hir::Binding*, hir::ConstDef*, hir::Function*>;
using TypeDef = std::variant<hir::StructDef*, hir::EnumDef*, hir::Trait*>;
