#pragma once

#include <variant>

namespace hir {
struct Function;
struct StructDef;
struct BindingDef;
struct ConstDef;
struct EnumDef;
struct Trait;
struct Method;
struct Local;
}

using ValueDef = std::variant<hir::Local*, hir::ConstDef*, hir::Function*, hir::Method*>;
using TypeDef = std::variant<hir::StructDef*, hir::EnumDef*, hir::Trait*>;
