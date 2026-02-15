#include "common/mir/utils.hpp"

#include "semantic/hir/helper.hpp"
#include "semantic/utils.hpp"

#include <sstream>
#include <stdexcept>

namespace mir {
namespace detail {
namespace {

std::string pointer_label(const char *prefix, const void *ptr) {
  std::ostringstream oss;
  oss << prefix << ptr;
  return oss.str();
}

std::string primitive_kind_to_string(type::PrimitiveKind kind) {
  switch (kind) {
  case type::PrimitiveKind::I32:
    return "i32";
  case type::PrimitiveKind::U32:
    return "u32";
  case type::PrimitiveKind::ISIZE:
    return "isize";
  case type::PrimitiveKind::USIZE:
    return "usize";
  case type::PrimitiveKind::BOOL:
    return "bool";
  case type::PrimitiveKind::CHAR:
    return "char";
  case type::PrimitiveKind::STRING:
    return "String";
  }
  return "primitive";
}

std::string make_scoped_name(const std::string &scope,
                             const std::string &base) {
  if (scope.empty()) {
    return base;
  }
  if (base.empty()) {
    return scope;
  }
  return scope + "_" + base;
}
bool is_signed_integer_kind(type::PrimitiveKind kind) {
  return kind == type::PrimitiveKind::I32 || kind == type::PrimitiveKind::ISIZE;
}

bool is_unsigned_integer_kind(type::PrimitiveKind kind) {
  return kind == type::PrimitiveKind::U32 ||
         kind == type::PrimitiveKind::USIZE ||
         kind == type::PrimitiveKind::CHAR;
}

bool is_bool_kind(type::PrimitiveKind kind) {
  return kind == type::PrimitiveKind::BOOL;
}

TypeId enum_discriminant_type() {
  static const TypeId usize_type =
      type::get_typeID(type::Type{type::PrimitiveKind::USIZE});
  return usize_type;
}

TypeId canonicalize_type_impl(TypeId type) {
  if (type == type::invalid_type_id) {
    return type;
  }

  const auto &resolved = type::get_type_from_id(type);
  return std::visit(
      Overloaded{
          [](const type::EnumType &) { return enum_discriminant_type(); },
          [&](const type::ReferenceType &reference) {
            TypeId normalized =
                canonicalize_type_impl(reference.referenced_type);
            if (normalized == reference.referenced_type) {
              return type;
            }
            type::ReferenceType updated = reference;
            updated.referenced_type = normalized;
            return type::get_typeID(type::Type{updated});
          },
          [&](const type::ArrayType &array) {
            TypeId normalized = canonicalize_type_impl(array.element_type);
            if (normalized == array.element_type) {
              return type;
            }
            type::ArrayType updated = array;
            updated.element_type = normalized;
            return type::get_typeID(type::Type{updated});
          },
          [&](const auto &) { return type; }},
      resolved.value);
}

} // namespace

TypeId get_unit_type() {
  static const TypeId unit = type::get_typeID(type::Type{type::UnitType{}});
  return unit;
}

TypeId get_bool_type() {
  static const TypeId bool_type =
      type::get_typeID(type::Type{type::PrimitiveKind::BOOL});
  return bool_type;
}

bool is_unit_type(TypeId type) {
  return type != type::invalid_type_id &&
         std::get_if<type::UnitType>(&type::get_type_from_id(type).value) !=
             nullptr;
}

bool is_never_type(TypeId type) {
  return type != type::invalid_type_id &&
         std::get_if<type::NeverType>(&type::get_type_from_id(type).value) !=
             nullptr;
}

bool is_aggregate_type(TypeId type) {
  if (type == type::invalid_type_id) {
    return false;
  }
  const auto &resolved = type::get_type_from_id(type);
  return std::get_if<type::StructType>(&resolved.value) != nullptr ||
         std::get_if<type::ArrayType>(&resolved.value) != nullptr;
}

TypeId make_ref_type(TypeId pointee) {
  if (pointee == type::invalid_type_id) {
    return type::invalid_type_id;
  }
  type::ReferenceType ref_type;
  ref_type.referenced_type = pointee;
  ref_type.is_mutable = false;
  return type::get_typeID(type::Type{ref_type});
}

Constant make_bool_constant(bool value) {
  Constant constant;
  constant.type = get_bool_type();
  constant.value = BoolConstant{value};
  return constant;
}

TypeId canonicalize_type_for_mir(TypeId type) {
  return canonicalize_type_impl(type);
}

// Operand make_constant_operand(const Constant& constant) omitted.

std::optional<type::PrimitiveKind> get_primitive_kind(TypeId type) {
  if (type == type::invalid_type_id) {
    return std::nullopt;
  }
  if (auto primitive = std::get_if<type::PrimitiveKind>(
          &type::get_type_from_id(type).value)) {
    return *primitive;
  }
  return std::nullopt;
}

bool is_signed_integer_type(TypeId type) {
  auto primitive = get_primitive_kind(type);
  return primitive && is_signed_integer_kind(*primitive);
}

bool is_unsigned_integer_type(TypeId type) {
  auto primitive = get_primitive_kind(type);
  return primitive && is_unsigned_integer_kind(*primitive);
}

bool is_bool_type(TypeId type) {
  auto primitive = get_primitive_kind(type);
  return primitive && is_bool_kind(*primitive);
}

// classify_binary_kind omitted.

std::string type_name(TypeId type) {
  if (type == type::invalid_type_id) {
    return "<invalid>";
  }
  const auto &resolved = type::get_type_from_id(type);
  if (auto primitive = std::get_if<type::PrimitiveKind>(&resolved.value)) {
    return primitive_kind_to_string(*primitive);
  }
  if (auto struct_type = std::get_if<type::StructType>(&resolved.value)) {
    const auto &info =
        type::TypeContext::get_instance().get_struct(struct_type->id);
    if (!info.name.empty()) {
      return info.name;
    }
    return "struct@" + std::to_string(struct_type->id);
  }
  if (auto enum_type = std::get_if<type::EnumType>(&resolved.value)) {
    const auto &info =
        type::TypeContext::get_instance().get_enum(enum_type->id);
    if (!info.name.empty()) {
      return info.name;
    }
    return "enum@" + std::to_string(enum_type->id);
  }
  if (auto ref_type = std::get_if<type::ReferenceType>(&resolved.value)) {
    std::string prefix = ref_type->is_mutable ? "&mut " : "&";
    return prefix + type_name(ref_type->referenced_type);
  }
  if (auto array_type = std::get_if<type::ArrayType>(&resolved.value)) {
    return "[" + type_name(array_type->element_type) + ";" +
           std::to_string(array_type->size) + "]";
  }
  if (std::get_if<type::UnitType>(&resolved.value)) {
    return "unit";
  }
  if (std::get_if<type::NeverType>(&resolved.value)) {
    return "!";
  }
  return "_";
}

std::string derive_function_name(const hir::Function &function,
                                 const std::string &scope) {
  std::string base = hir::helper::get_name(function).name;
  if (base.empty()) {
    base = pointer_label("fn@", &function);
  }
  return make_scoped_name(scope, base);
}

std::string derive_method_name(const hir::Method &method,
                               const std::string &scope) {
  std::string base = hir::helper::get_name(method).name;
  if (base.empty()) {
    base = pointer_label("method@", &method);
  }
  return make_scoped_name(scope, base);
}

void populate_abi_params(MirFunctionSig &sig) {
  // 1. If we have an indirect sret return, add the sret parameter first
  if (is_indirect_sret(sig.return_desc)) {
    AbiParam sret_param;
    sret_param.param_index = std::nullopt; // hidden parameter
    sret_param.kind = AbiParamSRet{};

    AbiParamIndex sret_index =
        static_cast<AbiParamIndex>(sig.abi_params.size());
    sig.abi_params.push_back(std::move(sret_param));

    // Update the return descriptor with the sret index
    auto &sret_desc =
        std::get<ReturnDesc::RetIndirectSRet>(sig.return_desc.kind);
    sret_desc.sret_index = sret_index;
  }

  // 2. Add semantic parameters as ABI parameters
  for (ParamIndex i = 0; i < sig.params.size(); ++i) {
    const MirParam &p = sig.params[i];

    AbiParam abi_param;
    abi_param.param_index = i;

    // For now: classify based on type
    if (is_aggregate_type(p.type)) {
      // Caller-owned byval copy: caller allocates, callee receives pointer
      // (no-escape)
      abi_param.kind = AbiParamByValCallerCopy{};
    } else {
      abi_param.kind = AbiParamDirect{};
    }

    sig.abi_params.push_back(std::move(abi_param));
  }
}

} // namespace detail
} // namespace mir
