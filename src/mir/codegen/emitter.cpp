#include "mir/codegen/emitter.hpp"

#include "mir/codegen/rvalue.hpp"
#include "semantic/utils.hpp"
#include "type/helper.hpp"
#include "type/type.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace {

std::string escape_string_literal(std::string_view text) {
  std::ostringstream oss;
  for (unsigned char ch : text) {
    switch (ch) {
    case '\\':
      oss << "\\5C";
      break;
    case '"':
      oss << "\\22";
      break;
    case '\n':
      oss << "\\0A";
      break;
    case '\r':
      oss << "\\0D";
      break;
    case '\t':
      oss << "\\09";
      break;
    default:
      if (std::isprint(ch)) {
        oss << static_cast<char>(ch);
      } else {
        oss << "\\";
        oss << std::oct << std::uppercase;
        oss.width(2);
        oss.fill('0');
        oss << static_cast<unsigned>(ch);
        oss << std::dec;
      }
      break;
    }
  }
  return oss.str();
}

} // namespace

namespace codegen {

Emitter::Emitter(mir::MirModule &module, std::string target_triple,
                 std::string data_layout)
    : module(module), module_("rcompiler"),
      target_triple_(std::move(target_triple)),
      data_layout_(std::move(data_layout)) {
  if (!target_triple_.empty()) {
    module_.set_target_triple(target_triple_);
  }
  if (!data_layout_.empty()) {
    module_.set_data_layout(data_layout_);
  }
}

std::string Emitter::emit() {
  emit_globals();
  for (const auto &function : module.functions) {
    emit_function(function);
  }
  emit_struct_definitions();
  return module_.str();
}

void Emitter::emit_struct_definitions() {
  for (const auto &definition : type_emitter_.struct_definitions()) {
    module_.add_type_definition(definition.first, definition.second);
  }
}

void Emitter::emit_globals() {
  for (std::size_t index = 0; index < module.globals.size(); ++index) {
    const auto &global = module.globals[index];
    std::visit(Overloaded{[&](const mir::StringLiteralGlobal &literal) {
               std::string type_name =
                   type_emitter_.get_type_name(type::get_typeID(type::Type{
                       type::ArrayType{type::get_typeID(type::Type{type::PrimitiveKind::CHAR}),
                                        literal.value.length}}));
               std::ostringstream decl;
               decl << get_global(static_cast<mir::GlobalId>(index)) << " = private "
                    << "constant " << type_name << " c\""
                    << escape_string_literal(literal.value.data) << "\"";
               module_.add_global(decl.str());
             }},
             global.value);
  }
}

void Emitter::emit_function(const mir::MirFunction &function) {
  current_function_ = &function;
  temp_names_.clear();
  local_ptrs_.clear();
  block_builders_.clear();

  std::vector<llvmbuilder::FunctionParameter> params;
  params.reserve(function.params.size());
  for (const auto &param : function.params) {
    params.push_back(
        llvmbuilder::FunctionParameter{type_emitter_.get_type_name(param.type),
                                       param.name});
  }

  current_function_builder_ =
      &module_.add_function(get_function_name(function.id),
                            type_emitter_.get_type_name(function.return_type),
                            std::move(params));

  block_builders_.emplace(function.start_block,
                          &current_function_builder_->entry_block());

  for (std::size_t id = 0; id < function.basic_blocks.size(); ++id) {
    if (id == function.start_block) {
      continue;
    }
    block_builders_.emplace(
        static_cast<mir::BasicBlockId>(id),
        &current_function_builder_->create_block(get_block_label(id)));
  }

  for (std::size_t id = 0; id < function.basic_blocks.size(); ++id) {
    emit_block(static_cast<mir::BasicBlockId>(id));
  }

  current_function_ = nullptr;
  current_block_builder_ = nullptr;
  current_function_builder_ = nullptr;
}

void Emitter::emit_block(mir::BasicBlockId block_id) {
  current_block_builder_ = block_builders_.at(block_id);
  const auto &block = current_function_->get_basic_block(block_id);

  for (const auto &phi : block.phis) {
    emit_phi_nodes(phi);
  }

  if (block_id == current_function_->start_block) {
    emit_entry_block_prologue();
  }

  for (const auto &statement : block.statements) {
    emit_statement(statement);
  }

  emit_terminator(block.terminator);
}

void Emitter::emit_entry_block_prologue() {
  auto &entry = *current_block_builder_;
  for (std::size_t idx = 0; idx < current_function_->locals.size(); ++idx) {
    const auto &local = current_function_->locals[idx];
    std::string llvm_type = type_emitter_.get_type_name(local.type);
    std::string ptr =
        entry.emit_alloca(llvm_type, std::nullopt, std::nullopt, "l" + std::to_string(idx));
    local_ptrs_.emplace(static_cast<mir::LocalId>(idx), std::move(ptr));
  }

  const auto &params = current_function_builder_->parameters();
  for (std::size_t idx = 0; idx < current_function_->params.size(); ++idx) {
    const auto &param = current_function_->params[idx];
    auto it = local_ptrs_.find(param.local);
    if (it == local_ptrs_.end()) {
      throw std::logic_error("Parameter local pointer missing during prologue");
    }
    std::string type_name = type_emitter_.get_type_name(param.type);
    entry.emit_store(type_name, params[idx].name, type_name + "*", it->second);
  }
}

void Emitter::emit_statement(const mir::Statement &statement) {
  std::visit(Overloaded{[&](const mir::DefineStatement &define) {
               emit_define(define);
             },
             [&](const mir::LoadStatement &load) { emit_load(load); },
             [&](const mir::AssignStatement &assign) { emit_assign(assign); },
             [&](const mir::CallStatement &call) { emit_call(call); }},
             statement.value);
}

void Emitter::emit_define(const mir::DefineStatement &statement) {
  mir::TypeId dest_type = current_function_->get_temp_type(statement.dest);
  auto result = translate_rvalue(dest_type, statement.rvalue, temp_hint(statement.dest));
  temp_names_[statement.dest] = std::move(result.value_name);
}

void Emitter::emit_load(const mir::LoadStatement &statement) {
  TranslatedPlace place = translate_place(statement.src);
  if (place.pointee_type == mir::invalid_type_id) {
    throw std::logic_error("Load source missing pointee type during codegen");
  }
  std::string value_type = type_emitter_.get_type_name(place.pointee_type);
  std::string name = current_block_builder_->emit_load(
      value_type, pointer_type_name(place.pointee_type), place.pointer,
      std::nullopt, temp_hint(statement.dest));
  temp_names_[statement.dest] = std::move(name);
}

void Emitter::emit_assign(const mir::AssignStatement &statement) {
  auto operand = get_typed_operand(statement.src);
  TranslatedPlace dest = translate_place(statement.dest);
  if (dest.pointee_type == mir::invalid_type_id) {
    throw std::logic_error("Assign destination missing pointee type during codegen");
  }
  current_block_builder_->emit_store(
      operand.type_name, operand.value_name,
      pointer_type_name(dest.pointee_type), dest.pointer);
}

void Emitter::emit_call(const mir::CallStatement &statement) {
  std::vector<std::pair<std::string, std::string>> args;
  args.reserve(statement.args.size());
  for (const auto &arg : statement.args) {
    auto operand = get_typed_operand(arg);
    args.emplace_back(operand.type_name, operand.value_name);
  }

  const auto &callee = module.functions.at(statement.function);
  std::string ret_type = type_emitter_.get_type_name(callee.return_type);
  auto result = current_block_builder_->emit_call(
      ret_type, get_function_name(statement.function), args,
      statement.dest ? temp_hint(*statement.dest) : std::string_view{});
  if (statement.dest && result) {
    temp_names_[*statement.dest] = *result;
  }
}

void Emitter::emit_phi_nodes(const mir::PhiNode &phi_node) {
  std::vector<std::pair<std::string, std::string>> incomings;
  incomings.reserve(phi_node.incoming.size());
  std::string type_name =
      type_emitter_.get_type_name(current_function_->get_temp_type(phi_node.dest));
  for (const auto &incoming : phi_node.incoming) {
    auto value = get_temp(incoming.value);
    incomings.emplace_back(value, block_builders_.at(incoming.block)->label());
  }
  std::string phi_name = current_block_builder_->emit_phi(
      type_name, incomings, temp_hint(phi_node.dest));
  temp_names_[phi_node.dest] = std::move(phi_name);
}

void Emitter::emit_terminator(const mir::Terminator &terminator) {
  std::visit(Overloaded{[&](const mir::GotoTerminator &goto_term) {
               current_block_builder_->emit_br(
                   block_builders_.at(goto_term.target)->label());
             },
             [&](const mir::SwitchIntTerminator &switch_term) {
               auto discr = get_typed_operand(switch_term.discriminant);
               std::vector<std::pair<std::string, std::string>> cases;
               cases.reserve(switch_term.targets.size());
               for (const auto &target : switch_term.targets) {
                 cases.emplace_back(format_constant_literal(target.match_value),
                                    block_builders_.at(target.block)->label());
               }
               current_block_builder_->emit_switch(
                   discr.type_name, discr.value_name,
                   block_builders_.at(switch_term.otherwise)->label(), cases);
             },
             [&](const mir::ReturnTerminator &ret) {
               if (ret.value.has_value()) {
                 auto operand = get_typed_operand(*ret.value);
                 current_block_builder_->emit_ret(operand.type_name,
                                                  operand.value_name);
               } else {
                 current_block_builder_->emit_ret_void();
               }
             },
             [&](const mir::UnreachableTerminator &) {
               current_block_builder_->emit_unreachable();
             }},
             terminator.value);
}

TranslatedPlace Emitter::translate_place(const mir::Place &place) {
  std::string base_pointer;
  mir::TypeId base_type;

  std::visit(Overloaded{[&](const mir::LocalPlace &local_place) {
               base_pointer = get_local_ptr(local_place.id);
               base_type = current_function_->get_local_info(local_place.id).type;
             },
             [&](const mir::GlobalPlace &global_place) {
               base_pointer = get_global(global_place.global);
               const auto &global = module.globals[global_place.global];
               base_type = std::visit(
                   Overloaded{[&](const mir::StringLiteralGlobal &literal) {
                     auto char_type =
                         type::get_typeID(type::Type{type::PrimitiveKind::CHAR});
                     type::ArrayType arr{};
                     arr.element_type = char_type;
                     arr.size = literal.value.length;
                     return type::get_typeID(type::Type{arr});
                   }},
                   global.value);
             },
             [&](const mir::PointerPlace &pointer_place) {
               base_pointer = get_temp(pointer_place.temp);
               base_type = type::helper::type_helper::deref(
                               current_function_->get_temp_type(pointer_place.temp))
                               .value();
             }},
             place.base);

  if (place.projections.empty()) {
    return TranslatedPlace{.pointer = base_pointer, .pointee_type = base_type};
  }

  mir::TypeId gep_base_type = base_type;
  mir::TypeId current_type = base_type;
  std::vector<std::pair<std::string, std::string>> indices;
  indices.emplace_back("i32", "0");

  for (const auto &projection : place.projections) {
    std::visit(Overloaded{[&](const mir::FieldProjection &field_proj) {
                 indices.emplace_back("i32", std::to_string(field_proj.index));
                 current_type = type::helper::type_helper::field(current_type,
                                                                  field_proj.index)
                                    .value();
               },
               [&](const mir::IndexProjection &index_proj) {
                 auto index_operand = get_typed_operand(mir::Operand{index_proj.index});
                 indices.emplace_back(index_operand.type_name, index_operand.value_name);
                 current_type =
                     type::helper::type_helper::array_element(current_type).value();
               }},
               projection);
  }

  std::string result = current_block_builder_->emit_getelementptr(
      type_emitter_.get_type_name(gep_base_type),
      pointer_type_name(gep_base_type), base_pointer, indices, true, "proj");

  return TranslatedPlace{.pointer = std::move(result), .pointee_type = current_type};
}

TypedOperand Emitter::translate_rvalue(mir::TypeId dest_type,
                                       const mir::RValue &rvalue,
                                       std::string_view hint) {
  return std::visit(
      Overloaded{[&](const mir::ConstantRValue &value) {
                   return emit_constant_rvalue(dest_type, value, hint);
                 },
                 [&](const mir::BinaryOpRValue &value) {
                   return emit_binary_rvalue(value, hint);
                 },
                 [&](const mir::UnaryOpRValue &value) {
                   return emit_unary_rvalue(dest_type, value, hint);
                 },
                 [&](const mir::RefRValue &value) {
                   return emit_ref_rvalue(dest_type, value);
                 },
                 [&](const mir::AggregateRValue &value) {
                   return emit_aggregate_rvalue(dest_type, value, hint);
                 },
                 [&](const mir::ArrayRepeatRValue &value) {
                   return emit_array_repeat_rvalue(dest_type, value, hint);
                 },
                 [&](const mir::CastRValue &value) {
                   return emit_cast_rvalue(dest_type, value, hint);
                 },
                 [&](const mir::FieldAccessRValue &value) {
                   return emit_field_access_rvalue(value, hint);
                 }},
      rvalue.value);
}

TypedOperand Emitter::emit_constant_rvalue(mir::TypeId dest_type,
                                           const mir::ConstantRValue &value,
                                           std::string_view hint) {
  mir::TypeId const_type =
      value.constant.type == mir::invalid_type_id ? dest_type : value.constant.type;
  if (const_type == mir::invalid_type_id) {
    throw std::logic_error("Constant rvalue missing resolved type during codegen");
  }
  std::string type_name = type_emitter_.get_type_name(const_type);
  if (std::holds_alternative<mir::UnitConstant>(value.constant.value)) {
    return TypedOperand{.type_name = std::move(type_name),
                       .value_name = "undef",
                       .type = const_type};
  }
  std::string name = current_block_builder_->emit_binary(
      "add", type_name, "0", format_constant_literal(value.constant), hint);
  return TypedOperand{.type_name = std::move(type_name),
                      .value_name = std::move(name),
                      .type = const_type};
}

TypedOperand Emitter::emit_binary_rvalue(const mir::BinaryOpRValue &value,
                                         std::string_view hint) {
  auto lhs = get_typed_operand(value.lhs);
  auto rhs = get_typed_operand(value.rhs);
  auto spec = detail::classify_binary_op(value.kind);
  if (spec.is_compare) {
    std::string name = current_block_builder_->emit_icmp(spec.predicate, lhs.type_name,
                                                         lhs.value_name, rhs.value_name,
                                                         hint);
    return TypedOperand{.type_name = "i1", .value_name = std::move(name),
                        .type = type::get_typeID(type::Type{type::PrimitiveKind::BOOL})};
  }
  std::string name = current_block_builder_->emit_binary(
      spec.opcode, lhs.type_name, lhs.value_name, rhs.value_name, hint);
  return TypedOperand{.type_name = lhs.type_name, .value_name = std::move(name),
                      .type = lhs.type};
}

TypedOperand Emitter::emit_unary_rvalue(mir::TypeId dest_type,
                                        const mir::UnaryOpRValue &value,
                                        std::string_view hint) {
  auto operand = get_typed_operand(value.operand);
  auto category = detail::classify_type(operand.type);
  switch (value.kind) {
  case mir::UnaryOpRValue::Kind::Not: {
    std::string rhs = category == detail::ValueCategory::Bool ? "1" : "-1";
    std::string name = current_block_builder_->emit_binary(
        "xor", operand.type_name, operand.value_name, rhs, hint);
    return TypedOperand{.type_name = operand.type_name, .value_name = std::move(name),
                        .type = operand.type};
  }
  case mir::UnaryOpRValue::Kind::Neg: {
    std::string name = current_block_builder_->emit_binary(
        "sub", operand.type_name, "0", operand.value_name, hint);
    return TypedOperand{.type_name = operand.type_name, .value_name = std::move(name),
                        .type = operand.type};
  }
  case mir::UnaryOpRValue::Kind::Deref: {
    std::string pointee_type = type_emitter_.get_type_name(dest_type);
    std::string name = current_block_builder_->emit_load(
        pointee_type, pointer_type_name(dest_type), operand.value_name, std::nullopt,
        hint);
    return TypedOperand{.type_name = std::move(pointee_type),
                        .value_name = std::move(name),
                        .type = dest_type};
  }
  }
  throw std::logic_error("Unhandled unary operation during codegen");
}

TypedOperand Emitter::emit_ref_rvalue(mir::TypeId dest_type,
                                      const mir::RefRValue &value) {
  TranslatedPlace place = translate_place(value.place);
  if (place.pointee_type == mir::invalid_type_id) {
    throw std::logic_error("Reference place missing pointee type during codegen");
  }
  std::string dest_type_name = type_emitter_.get_type_name(dest_type);
  return TypedOperand{.type_name = std::move(dest_type_name),
                      .value_name = place.pointer,
                      .type = dest_type};
}

TypedOperand Emitter::emit_aggregate_rvalue(mir::TypeId dest_type,
                                            const mir::AggregateRValue &value,
                                            std::string_view hint) {
  std::string aggregate_type = type_emitter_.get_type_name(dest_type);
  if (value.elements.empty()) {
    return TypedOperand{.type_name = std::move(aggregate_type),
                        .value_name = "undef",
                        .type = dest_type};
  }

  std::string current_value = "undef";
  std::string last_name;
  for (std::size_t index = 0; index < value.elements.size(); ++index) {
    auto element = get_typed_operand(value.elements[index]);
    last_name = current_block_builder_->emit_insertvalue(
        aggregate_type, current_value, element.type_name, element.value_name,
        {static_cast<unsigned>(index)}, index + 1 == value.elements.size() ? hint :
                                                      std::string_view{});
    current_value = last_name;
  }

  return TypedOperand{.type_name = std::move(aggregate_type),
                      .value_name = std::move(last_name),
                      .type = dest_type};
}

TypedOperand Emitter::emit_array_repeat_rvalue(
    mir::TypeId dest_type, const mir::ArrayRepeatRValue &value,
    std::string_view hint) {
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
    return TypedOperand{.type_name = std::move(aggregate_type),
                        .value_name = "undef",
                        .type = dest_type};
  }

  auto element_operand = get_typed_operand(value.value);
  std::string current_value = "undef";
  std::string last_name;
  for (std::size_t index = 0; index < value.count; ++index) {
    last_name = current_block_builder_->emit_insertvalue(
        aggregate_type, current_value, element_operand.type_name,
        element_operand.value_name, {static_cast<unsigned>(index)},
        index + 1 == value.count ? hint : std::string_view{});
    current_value = last_name;
  }

  return TypedOperand{.type_name = std::move(aggregate_type),
                      .value_name = std::move(last_name),
                      .type = dest_type};
}

TypedOperand Emitter::emit_cast_rvalue(mir::TypeId dest_type,
                                       const mir::CastRValue &value,
                                       std::string_view hint) {
  mir::TypeId target_type = value.target_type == mir::invalid_type_id
                                ? dest_type
                                : value.target_type;
  if (target_type == mir::invalid_type_id) {
    throw std::logic_error("Cast rvalue missing target type during codegen");
  }
  auto operand = get_typed_operand(value.value);
  std::string target_type_name = type_emitter_.get_type_name(target_type);
  if (operand.type == target_type) {
    return TypedOperand{.type_name = std::move(target_type_name),
                        .value_name = operand.value_name,
                        .type = target_type};
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
      std::string name = current_block_builder_->emit_cast(
          op, operand.type_name, operand.value_name, target_type_name, hint);
      return TypedOperand{.type_name = std::move(target_type_name),
                          .value_name = std::move(name),
                          .type = target_type};
    }
    if (to_bits < from_bits) {
      std::string name = current_block_builder_->emit_cast(
          "trunc", operand.type_name, operand.value_name, target_type_name, hint);
      return TypedOperand{.type_name = std::move(target_type_name),
                          .value_name = std::move(name),
                          .type = target_type};
    }
    return TypedOperand{.type_name = std::move(target_type_name),
                        .value_name = operand.value_name,
                        .type = target_type};
  }

  if (from_category == detail::ValueCategory::Pointer &&
      to_category == detail::ValueCategory::Pointer) {
    std::string name = current_block_builder_->emit_cast(
        "bitcast", operand.type_name, operand.value_name, target_type_name, hint);
    return TypedOperand{.type_name = std::move(target_type_name),
                        .value_name = std::move(name),
                        .type = target_type};
  }

  throw std::logic_error("Unsupported cast combination during codegen");
}

TypedOperand Emitter::emit_field_access_rvalue(
    const mir::FieldAccessRValue &value, std::string_view hint) {
  auto base_type = current_function_->get_temp_type(value.base);
  std::string base_type_name = type_emitter_.get_type_name(base_type);
  std::string name = current_block_builder_->emit_extractvalue(
      base_type_name, get_temp(value.base), {static_cast<unsigned>(value.index)},
      hint);
  auto result_type =
      type::helper::type_helper::field(base_type, value.index).value_or(base_type);
  std::string result_type_name = type_emitter_.get_type_name(result_type);
  return TypedOperand{.type_name = std::move(result_type_name),
                      .value_name = std::move(name),
                      .type = result_type};
}

std::string Emitter::get_temp(mir::TempId temp) {
  auto it = temp_names_.find(temp);
  if (it == temp_names_.end()) {
    throw std::logic_error("Temp used before definition during codegen");
  }
  return it->second;
}

std::string Emitter::get_local_ptr(mir::LocalId local) {
  auto it = local_ptrs_.find(local);
  if (it == local_ptrs_.end()) {
    throw std::out_of_range("Invalid LocalId");
  }
  return it->second;
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

std::string Emitter::pointer_type_name(mir::TypeId pointee_type) {
  return type_emitter_.get_type_name(pointee_type) + "*";
}

std::string Emitter::temp_hint(mir::TempId temp) const {
  return "t" + std::to_string(temp);
}

std::string Emitter::format_constant_literal(const mir::Constant &constant) {
  return std::visit(Overloaded{[](mir::BoolConstant val) -> std::string {
                     return val.value ? "1" : "0";
                   },
                   [](mir::IntConstant val) -> std::string {
                     std::string prefix = val.is_negative ? "-" : "";
                     return prefix + std::to_string(val.value);
                   },
                   [](mir::UnitConstant) -> std::string { return "zeroinitializer"; },
                   [](mir::CharConstant val) -> std::string {
                     return std::to_string(static_cast<unsigned int>(
                         static_cast<unsigned char>(val.value)));
                   }},
                   constant.value);
}

} // namespace codegen

