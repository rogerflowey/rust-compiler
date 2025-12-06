#include "mir/codegen/emitter.hpp"
#include "mir/codegen/rvalue.hpp"
#include "codegen/type.hpp"
#include "helper.hpp"
#include "type.hpp"
#include "type/helper.hpp"
#include "utils.hpp"

#include <stdexcept>
#include <variant>
#include <vector>

namespace codegen {

void Emitter::emit_function(const mir::MirFunction &function) {
  current_function_ = &function;
  current_function_code_.header_line =
      "define " + type_emitter_.get_type_name(function.return_type) + " @" +
      get_function_name(function.id) + "(";

  // parameters
  for (std::size_t i = 0; i < function.params.size(); ++i) {
    const auto &param = function.params[i];
    if (i > 0) {
      current_function_code_.header_line += ", ";
    }
    current_function_code_.header_line +=
        type_emitter_.get_type_name(param.type) + " %" + param.name;
  }
  current_function_code_.header_line += ") {";

  // emit blocks
  for (std::size_t i = 0; i < function.basic_blocks.size(); ++i) {
    if (i == current_function_->start_block) {
      emit_entry_block_prologue();
    }
    emit_block(static_cast<mir::BasicBlockId>(i));
  }

  finish_function();
  current_function_ = nullptr;
}

void Emitter::emit_entry_block_prologue() {
  // Allocate space for local variables
  for (std::size_t i = 0; i < current_function_->locals.size(); ++i) {
    const auto &local = current_function_->locals[i];
    std::string local_ptr = get_local_ptr(static_cast<mir::LocalId>(i));
    std::string llvm_type = type_emitter_.get_type_name(local.type);
    current_block_code_.stmt_lines.push_back("  " + local_ptr + " = alloca " +
                                             llvm_type);
  }
  // accept incoming parameters
  for (const auto &param : current_function_->params) {
    std::string local_ptr = get_local_ptr(param.local);
    std::string llvm_type = type_emitter_.get_type_name(param.type);
    current_block_code_.stmt_lines.push_back("  store " + llvm_type + " %" +
                                             param.name + ", " + llvm_type +
                                             "* " + local_ptr);
  }
}

TranslatedPlace Emitter::translate_place(const mir::Place &place) {
  std::string base;
  TypeId base_type;
  std::visit(
    Overloaded{[&](const mir::LocalPlace &local_place) {
           base = get_local_ptr(local_place.id);
           base_type =
             current_function_->get_local_info(local_place.id).type;
         },
         [&](const mir::GlobalPlace &global_place) {
           base = get_global(global_place.global);
           const auto &global = module.globals[global_place.global];
           base_type = std::visit(
               Overloaded{[&](const mir::StringLiteralGlobal &literal) {
                 auto char_type = type::get_typeID(
                     type::Type{type::PrimitiveKind::CHAR});
                 type::ArrayType arr;
                 arr.element_type = char_type;
                 arr.size = literal.value.length;
                 return type::get_typeID(type::Type{arr});
               }},
               global.value);
         },
         [&](const mir::PointerPlace &pointer_place) {
           base = get_temp(pointer_place.temp);
           base_type =
             type::helper::type_helper::deref(
               current_function_->get_temp_type(pointer_place.temp))
               .value();
         }},
    place.base);

  if (place.projections.empty()) {
    return TranslatedPlace{.pointer = base, .pointee_type = base_type};
  }

  TypeId gep_base_type = base_type;
  TypeId current_type = base_type;
  std::vector<std::string> gep_indices;
  gep_indices.push_back("i32 0");
  std::string result_name = make_internal_value_name(base, "proj");

  for (const auto &projection : place.projections) {
    std::visit(
        Overloaded{
            [&](const mir::FieldProjection &field_proj) {
              gep_indices.push_back("i32 " +
                                    std::to_string(field_proj.index));
              current_type =
                  type::helper::type_helper::field(current_type, field_proj.index)
                      .value();
            },
            [&](const mir::IndexProjection &index_proj) {
              auto index_type =
                  current_function_->get_temp_type(index_proj.index);
              std::string index_operand = type_emitter_.get_type_name(index_type) +
                                          " " + get_temp(index_proj.index);
              gep_indices.push_back(index_operand);
              current_type = type::helper::type_helper::array_element(current_type)
                                 .value();
            }},
        projection);
  }

  std::string type_name = type_emitter_.get_type_name(gep_base_type);
  std::string indices_str = gep_indices.front();
  for (std::size_t i = 1; i < gep_indices.size(); ++i) {
    indices_str += ", " + gep_indices[i];
  }

  current_block_code_.stmt_lines.push_back(
      "  " + result_name + " = getelementptr inbounds " + type_name + ", " +
      type_name + "* " + base + ", " + indices_str);

  return TranslatedPlace{.pointer = result_name, .pointee_type = current_type};
}

std::string Emitter::get_temp(mir::TempId temp) {
  return "%t" + std::to_string(temp);
}

std::string Emitter::get_local_ptr(mir::LocalId local) {
  return "%l" + std::to_string(local);
}

std::string Emitter::get_block_label(mir::BasicBlockId block_id) const {
  return "bb" + std::to_string(block_id);
}

std::string Emitter::get_function_name(mir::FunctionId id) const {
  if (id >= module.functions.size()) {
    throw std::out_of_range("Invalid FunctionId");
  }
  return module.functions[id].name;
}

std::string Emitter::get_global(mir::GlobalId id) const {
  return "@g" + std::to_string(id);
}

std::string Emitter::get_constant(const mir::Constant &constant) {
  if (constant.type == mir::invalid_type_id) {
    throw std::logic_error("Constant missing resolved type during codegen");
  }
  std::string type_name = type_emitter_.get_type_name(constant.type);
  std::string literal = format_constant_literal(constant);
  return type_name + " " + literal;
}

std::string Emitter::get_constant_ptr(const mir::Constant &constant) {
  // For constants that need address-of semantics (e.g., string literals)
  // This would typically involve creating a global and returning a GEP
  // For now, we throw as this requires more infrastructure
  (void)constant;
  throw std::logic_error(
      "get_constant_ptr not yet implemented for this constant type");
}

std::string Emitter::get_operand(const mir::Operand &operand) {
  return std::visit(
      Overloaded{[&](const mir::TempId &temp) -> std::string {
                   auto type = current_function_->get_temp_type(temp);
                   std::string type_name = type_emitter_.get_type_name(type);
                   return type_name + " " + get_temp(temp);
                 },
                 [&](const mir::Constant &constant) -> std::string {
                   return get_constant(constant);
                 }},
      operand.value);
}

TypedOperand Emitter::get_typed_operand(const mir::Operand &operand) {
  return std::visit(
      Overloaded{[&](const mir::TempId &temp) -> TypedOperand {
                   auto type = current_function_->get_temp_type(temp);
                   std::string type_name = type_emitter_.get_type_name(type);
                   return TypedOperand{.type_name = std::move(type_name),
                                       .value_name = get_temp(temp),
                                       .type = type};
                 },
                 [&](const mir::Constant &constant) -> TypedOperand {
                   if (constant.type == mir::invalid_type_id) {
                     throw std::logic_error(
                         "Constant operand missing resolved type during codegen");
                   }
                   std::string type_name = type_emitter_.get_type_name(constant.type);
                   return TypedOperand{.type_name = std::move(type_name),
                                       .value_name = format_constant_literal(constant),
                                       .type = constant.type};
                 }},
      operand.value);
}

std::string Emitter::format_constant_literal(const mir::Constant &constant) {
  return std::visit(
      Overloaded{[](mir::BoolConstant val) -> std::string {
                   return val.value ? "1" : "0";
                 },
                 [](mir::IntConstant val) -> std::string {
                   std::string prefix = val.is_negative ? "-" : "";
                   return prefix + std::to_string(val.value);
                 },
                 [](mir::UnitConstant) -> std::string {
                   return "zeroinitializer";
                 },
                 [](mir::CharConstant val) -> std::string {
                   return std::to_string(static_cast<unsigned int>(
                       static_cast<unsigned char>(val.value)));
                 }},
      constant.value);
}

std::string Emitter::make_internal_value_name(const std::string &base,
                                              std::string_view suffix) {
  return base + "." + std::string(suffix) +
         std::to_string(internal_name_counter_++);
}

void Emitter::append_instruction(const std::string &line) {
  current_block_code_.stmt_lines.push_back("  " + line);
}

std::string Emitter::format_typed_operand(const TypedOperand &operand) const {
  return operand.type_name + " " + operand.value_name;
}

void Emitter::emit_constant_rvalue(const std::string &dest_name, mir::TypeId dest_type,
                                   const mir::ConstantRValue &value) {
  mir::TypeId const_type = value.constant.type == mir::invalid_type_id
                               ? dest_type
                               : value.constant.type;
  if (const_type == mir::invalid_type_id) {
    throw std::logic_error("Constant rvalue missing resolved type during codegen");
  }
  std::string type_name = type_emitter_.get_type_name(const_type);
  if (std::holds_alternative<mir::UnitConstant>(value.constant.value)) {
    append_instruction(dest_name + " = undef " + type_name);
    return;
  }
  append_instruction(dest_name + " = add " + type_name + " 0, " +
                     format_constant_literal(value.constant));
}

void Emitter::emit_binary_rvalue(const std::string &dest_name,
                                 const mir::BinaryOpRValue &value) {
  auto lhs = get_typed_operand(value.lhs);
  auto rhs = get_typed_operand(value.rhs);
  auto spec = detail::classify_binary_op(value.kind);
  if (spec.is_compare) {
    append_instruction(dest_name + " = " + spec.opcode + " " + spec.predicate +
                       " " + format_typed_operand(lhs) + ", " + rhs.value_name);
    return;
  }
  append_instruction(dest_name + " = " + spec.opcode + " " +
                     format_typed_operand(lhs) + ", " + rhs.value_name);
}

void Emitter::emit_unary_rvalue(const std::string &dest_name, mir::TypeId dest_type,
                                const mir::UnaryOpRValue &value) {
  auto operand = get_typed_operand(value.operand);
  auto category = detail::classify_type(operand.type);
  switch (value.kind) {
  case mir::UnaryOpRValue::Kind::Not:
    if (category == detail::ValueCategory::Bool) {
      append_instruction(dest_name + " = xor " + operand.type_name + " " +
                         operand.value_name + ", 1");
    } else {
      append_instruction(dest_name + " = xor " + operand.type_name + " " +
                         operand.value_name + ", -1");
    }
    break;
  case mir::UnaryOpRValue::Kind::Neg:
    append_instruction(dest_name + " = sub " + operand.type_name + " 0, " +
                       operand.value_name);
    break;
  case mir::UnaryOpRValue::Kind::Deref: {
    std::string pointee_type = type_emitter_.get_type_name(dest_type);
    append_instruction(dest_name + " = load " + pointee_type + ", " + pointee_type +
                       "* " + operand.value_name);
    break;
  }
  }
}

void Emitter::emit_ref_rvalue(const std::string &dest_name, mir::TypeId dest_type,
                              const mir::RefRValue &value) {
  TranslatedPlace place = translate_place(value.place);
  if (place.pointee_type == mir::invalid_type_id) {
    throw std::logic_error("Reference place missing pointee type during codegen");
  }
  std::string pointee_type = type_emitter_.get_type_name(place.pointee_type);
  std::string dest_type_name = type_emitter_.get_type_name(dest_type);
  append_instruction(dest_name + " = getelementptr inbounds " + pointee_type +
                     ", " + dest_type_name + " " + place.pointer + ", i32 0");
}

void Emitter::emit_aggregate_rvalue(const std::string &dest_name, mir::TypeId dest_type,
                                    const mir::AggregateRValue &value) {
  std::string aggregate_type = type_emitter_.get_type_name(dest_type);
  if (value.elements.empty()) {
    append_instruction(dest_name + " = undef " + aggregate_type);
    return;
  }
  std::string current_value;
  for (std::size_t index = 0; index < value.elements.size(); ++index) {
    auto element = get_typed_operand(value.elements[index]);
    std::string target_name =
        (index + 1 == value.elements.size()) ? dest_name
                                             : make_internal_value_name(dest_name, "ins");
    std::string base_value = (index == 0) ? "undef" : current_value;
    append_instruction(target_name + " = insertvalue " + aggregate_type + " " +
                       base_value + ", " + format_typed_operand(element) + ", " +
                       std::to_string(index));
    current_value = target_name;
  }
}

void Emitter::emit_array_repeat_rvalue(const std::string &dest_name,
                                       mir::TypeId dest_type,
                                       const mir::ArrayRepeatRValue &value) {
  const auto &resolved = type::get_type_from_id(dest_type);
  const auto *array_type = std::get_if<type::ArrayType>(&resolved.value);
  if (!array_type) {
    throw std::logic_error("Array repeat lowering requires array destination type");
  }
  if (array_type->size != value.count) {
    throw std::logic_error("Array repeat count mismatch during codegen");
  }
  std::string aggregate_type = type_emitter_.get_type_name(dest_type);
  if (value.count == 0) {
    append_instruction(dest_name + " = undef " + aggregate_type);
    return;
  }
  auto element_operand = get_typed_operand(value.value);
  std::string current_value;
  for (std::size_t index = 0; index < value.count; ++index) {
    std::string target_name =
        (index + 1 == value.count) ? dest_name
                                   : make_internal_value_name(dest_name, "rep");
    std::string base_value = (index == 0) ? "undef" : current_value;
    append_instruction(target_name + " = insertvalue " + aggregate_type + " " +
                       base_value + ", " + format_typed_operand(element_operand) +
                       ", " + std::to_string(index));
    current_value = target_name;
  }
}

void Emitter::emit_cast_rvalue(const std::string &dest_name, mir::TypeId dest_type,
                               const mir::CastRValue &value) {
  mir::TypeId target_type = value.target_type == mir::invalid_type_id
                                ? dest_type
                                : value.target_type;
  if (target_type == mir::invalid_type_id) {
    throw std::logic_error("Cast rvalue missing target type during codegen");
  }
  auto operand = get_typed_operand(value.value);
  std::string target_type_name = type_emitter_.get_type_name(target_type);
  if (operand.type == target_type) {
    append_instruction(dest_name + " = add " + target_type_name + " 0, " +
                       operand.value_name);
    return;
  }
  auto from_category = detail::classify_type(operand.type);
  auto to_category = detail::classify_type(target_type);

  if (detail::is_integer_category(from_category) &&
      detail::is_integer_category(to_category)) {
    int from_bits = detail::bit_width_for_integer(operand.type);
    int to_bits = detail::bit_width_for_integer(target_type);
    if (to_bits > from_bits) {
      const char *op = from_category == detail::ValueCategory::SignedInt ? "sext"
                                                                         : "zext";
      append_instruction(dest_name + std::string(" = ") + op + " " +
                         operand.type_name + " " + operand.value_name + " to " +
                         target_type_name);
      return;
    }
    if (to_bits < from_bits) {
      append_instruction(dest_name + " = trunc " + operand.type_name + " " +
                         operand.value_name + " to " + target_type_name);
      return;
    }
    append_instruction(dest_name + " = add " + target_type_name + " 0, " +
                       operand.value_name);
    return;
  }

  if (from_category == detail::ValueCategory::Pointer &&
      to_category == detail::ValueCategory::Pointer) {
    append_instruction(dest_name + " = bitcast " + operand.type_name + " " +
                       operand.value_name + " to " + target_type_name);
    return;
  }

  throw std::logic_error("Unsupported cast combination during codegen");
}

void Emitter::emit_field_access_rvalue(const std::string &dest_name,
                                       const mir::FieldAccessRValue &value) {
  auto base_type = current_function_->get_temp_type(value.base);
  std::string base_type_name = type_emitter_.get_type_name(base_type);
  append_instruction(dest_name + " = extractvalue " + base_type_name + " " +
                     get_temp(value.base) + ", " +
                     std::to_string(value.index));
}

void Emitter::translate_rvalue(const std::string &dest_name, mir::TypeId dest_type,
                               const mir::RValue &rvalue) {
  std::visit(Overloaded{
                 [&](const mir::ConstantRValue &value) {
                   emit_constant_rvalue(dest_name, dest_type, value);
                 },
                 [&](const mir::BinaryOpRValue &value) {
                   emit_binary_rvalue(dest_name, value);
                 },
                 [&](const mir::UnaryOpRValue &value) {
                   emit_unary_rvalue(dest_name, dest_type, value);
                 },
                 [&](const mir::RefRValue &value) {
                   emit_ref_rvalue(dest_name, dest_type, value);
                 },
                 [&](const mir::AggregateRValue &value) {
                   emit_aggregate_rvalue(dest_name, dest_type, value);
                 },
                 [&](const mir::ArrayRepeatRValue &value) {
                   emit_array_repeat_rvalue(dest_name, dest_type, value);
                 },
                 [&](const mir::CastRValue &value) {
                   emit_cast_rvalue(dest_name, dest_type, value);
                 },
                 [&](const mir::FieldAccessRValue &value) {
                   emit_field_access_rvalue(dest_name, value);
                 }},
             rvalue.value);
}

} // namespace codegen