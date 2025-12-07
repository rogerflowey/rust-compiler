#include "mir/codegen/emitter.hpp"

#include "mir/codegen/rvalue.hpp"
#include "semantic/utils.hpp"
#include "type/helper.hpp"
#include "type/type.hpp"

#include <cctype>
#include <optional>
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

/**
 * @brief Check if an operand is a zero constant
 * @return true if operand is a constant with zero/false value for its type
 */
bool is_const_zero(const mir::Operand &operand) {
  const auto *constant = std::get_if<mir::Constant>(&operand.value);
  if (!constant) {
    return false;
  }

  return std::visit(
      [](const auto &val) -> bool {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, mir::BoolConstant>) {
          return !val.value;
        } else if constexpr (std::is_same_v<T, mir::IntConstant>) {
          return val.value == 0;
        } else if constexpr (std::is_same_v<T, mir::CharConstant>) {
          return val.value == 0;
        } else {
          // StringConstant is never zero
          return false;
        }
      },
      constant->value);
}

} // namespace

namespace codegen {

Emitter::Emitter(mir::MirModule &module, std::string target_triple,
                 std::string data_layout)
    : mir_module_(module), module_("rcompiler"),
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
  
  // Emit external function declarations first
  for (const auto &external_function : mir_module_.external_functions) {
    emit_external_declaration(external_function);
  }
  
  // Emit internal function definitions
  for (const auto &function : mir_module_.functions) {
    emit_function(function);
  }
  return module_.str();
}

void Emitter::emit_globals() {
  for (std::size_t index = 0; index < mir_module_.globals.size(); ++index) {
    const auto &global = mir_module_.globals[index];
    std::visit(Overloaded{[&](const mir::StringLiteralGlobal &literal) {
               std::string type_name =
                   module_.get_type_name(type::get_typeID(type::Type{
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
  block_builders_.clear();

  std::vector<llvmbuilder::FunctionParameter> params;
  params.reserve(function.params.size());
  for (const auto &param : function.params) {
    params.push_back(
        llvmbuilder::FunctionParameter{module_.get_type_name(param.type),
                                       param.name});
  }

  // Use "void" for unit return types
  std::string return_type;
  if (std::get_if<type::UnitType>(&type::get_type_from_id(function.return_type).value)) {
    return_type = "void";
  } else {
    return_type = module_.get_type_name(function.return_type);
  }

  current_function_builder_ =
      &module_.add_function(get_function_name(function.id),
                  return_type,
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

void Emitter::emit_external_declaration(const mir::ExternalFunction &function) {
  // Build parameter type list
  std::vector<std::string> param_types;
  param_types.reserve(function.param_types.size());
  for (mir::TypeId param_type : function.param_types) {
    param_types.push_back(module_.get_type_name(param_type));
  }
  
  // Build the return type string - use "void" for unit types
  std::string ret_type;
  if (std::get_if<type::UnitType>(&type::get_type_from_id(function.return_type).value)) {
    ret_type = "void";
  } else {
    ret_type = module_.get_type_name(function.return_type);
  }
  
  std::string params;
  for (std::size_t i = 0; i < param_types.size(); ++i) {
    if (i > 0) {
      params += ", ";
    }
    params += param_types[i];
  }
  
  // Emit as LLVM declare statement
  std::string declaration = "declare dso_local " + ret_type + " @" + 
                            function.name + "(" + params + ")";
  module_.add_global(declaration);
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
    std::string llvm_type = module_.get_type_name(local.type);
    entry.emit_alloca_into(local_ptr_name(static_cast<mir::LocalId>(idx)),
                           llvm_type, std::nullopt, std::nullopt);
  }

  const auto &params = current_function_builder_->parameters();
  for (std::size_t idx = 0; idx < current_function_->params.size(); ++idx) {
    const auto &param = current_function_->params[idx];
    std::string type_name = module_.get_type_name(param.type);
    entry.emit_store(type_name, params[idx].name, type_name + "*",
                    local_ptr_name(param.local));
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
  emit_rvalue_into(statement.dest, dest_type, statement.rvalue);
}

void Emitter::emit_load(const mir::LoadStatement &statement) {
  TranslatedPlace place = translate_place(statement.src);
  if (place.pointee_type == mir::invalid_type_id) {
    throw std::logic_error("Load source missing pointee type during codegen");
  }
  std::string value_type = module_.get_type_name(place.pointee_type);
  current_block_builder_->emit_load_into(llvmbuilder::temp_name(statement.dest),
                                         value_type,
                                         pointer_type_name(place.pointee_type),
                                         place.pointer,
                                         std::nullopt);
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

  // Resolve the call target
  std::string ret_type;
  std::string func_name;

  if (statement.target.kind == mir::CallTarget::Kind::Internal) {
    const auto& fn = mir_module_.functions.at(statement.target.id);
    // Use "void" for unit return types
    if (std::get_if<type::UnitType>(&type::get_type_from_id(fn.return_type).value)) {
      ret_type = "void";
    } else {
      ret_type = module_.get_type_name(fn.return_type);
    }
    func_name = fn.name;
  } else {
    const auto& ext_fn = mir_module_.external_functions.at(statement.target.id);
    // Use "void" for unit return types
    if (std::get_if<type::UnitType>(&type::get_type_from_id(ext_fn.return_type).value)) {
      ret_type = "void";
    } else {
      ret_type = module_.get_type_name(ext_fn.return_type);
    }
    func_name = ext_fn.name;
  }

  // Trust the dest field set by lowerer: if dest is present, call returns a value
  if (statement.dest) {
    current_block_builder_->emit_call_into(
        llvmbuilder::temp_name(*statement.dest), ret_type,
        func_name, args);
  } else {
    current_block_builder_->emit_call(
        ret_type, func_name, args, {});
  }
}

void Emitter::emit_phi_nodes(const mir::PhiNode &phi_node) {
  std::vector<std::pair<std::string, std::string>> incomings;
  incomings.reserve(phi_node.incoming.size());
  std::string type_name =
      module_.get_type_name(current_function_->get_temp_type(phi_node.dest));
  for (const auto &incoming : phi_node.incoming) {
    auto value = get_temp(incoming.value);
    incomings.emplace_back(value, block_builders_.at(incoming.block)->label());
  }
  current_block_builder_->emit_phi_into(llvmbuilder::temp_name(phi_node.dest),
                                        type_name, incomings);
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
                 // Check if the return type is a unit type - if so, return void
                 if (const auto* temp_id = std::get_if<mir::TempId>(&ret.value->value)) {
                   mir::TypeId ret_type = current_function_->get_temp_type(*temp_id);
                   if (std::get_if<type::UnitType>(&type::get_type_from_id(ret_type).value)) {
                     current_block_builder_->emit_ret_void();
                   } else {
                     current_block_builder_->emit_ret(operand.type_name,
                                                      operand.value_name);
                   }
                 } else {
                   // Constant value
                   current_block_builder_->emit_ret(operand.type_name,
                                                    operand.value_name);
                 }
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
               const auto &global = mir_module_.globals[global_place.global];
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
      module_.get_type_name(gep_base_type),
      pointer_type_name(gep_base_type), base_pointer, indices, true, "proj");

  return TranslatedPlace{.pointer = std::move(result), .pointee_type = current_type};
}

void Emitter::emit_rvalue_into(mir::TempId dest,
                               mir::TypeId dest_type,
                               const mir::RValue &rvalue) {
  std::visit(Overloaded{[&](const mir::ConstantRValue &value) {
                          emit_constant_rvalue_into(dest, dest_type, value);
                        },
                        [&](const mir::BinaryOpRValue &value) {
                          emit_binary_rvalue_into(dest, value);
                        },
                        [&](const mir::UnaryOpRValue &value) {
                          emit_unary_rvalue_into(dest, dest_type, value);
                        },
                        [&](const mir::RefRValue &value) {
                          emit_ref_rvalue_into(dest, dest_type, value);
                        },
                        [&](const mir::AggregateRValue &value) {
                          emit_aggregate_rvalue_into(dest, dest_type, value);
                        },
                        [&](const mir::ArrayRepeatRValue &value) {
                          emit_array_repeat_rvalue_into(dest, dest_type, value);
                        },
                        [&](const mir::CastRValue &value) {
                          emit_cast_rvalue_into(dest, dest_type, value);
                        },
                        [&](const mir::FieldAccessRValue &value) {
                          emit_field_access_rvalue_into(dest, value);
                        }},
             rvalue.value);
}

void Emitter::emit_constant_rvalue_into(mir::TempId dest,
                                        mir::TypeId dest_type,
                                        const mir::ConstantRValue &value) {
  materialize_constant_operand(dest_type, value.constant, dest);
}

TypedOperand Emitter::materialize_constant_operand(mir::TypeId fallback_type,
                                                   const mir::Constant &constant,
                                                   std::optional<mir::TempId> target_temp) {
  mir::TypeId const_type =
      constant.type == mir::invalid_type_id ? fallback_type : constant.type;
  if (const_type == mir::invalid_type_id) {
    throw std::logic_error("Constant operand missing resolved type during codegen");
  }
  if (const auto *string_const = std::get_if<mir::StringConstant>(&constant.value)) {
    return emit_string_constant_operand(const_type, *string_const, target_temp);
  }

  std::string type_name = module_.get_type_name(const_type);
  std::string value_name;

  std::string literal = format_constant_literal(constant);
  if (target_temp) {
    value_name = current_block_builder_->emit_binary_into(
        llvmbuilder::temp_name(*target_temp), "add", type_name, "0", literal, {});
  } else {
    value_name = current_block_builder_->emit_binary(
        "add", type_name, "0", literal, {});
  }
  return TypedOperand{.type_name = std::move(type_name),
                      .value_name = std::move(value_name),
                      .type = const_type};
}

void Emitter::emit_binary_rvalue_into(mir::TempId dest,
                                      const mir::BinaryOpRValue &value) {
  auto lhs = get_typed_operand(value.lhs);
  auto rhs = get_typed_operand(value.rhs);
  auto spec = detail::classify_binary_op(value.kind);
  if (spec.is_compare) {
    current_block_builder_->emit_icmp_into(llvmbuilder::temp_name(dest), spec.predicate,
                                           lhs.type_name, lhs.value_name, rhs.value_name);
    return;
  }
  current_block_builder_->emit_binary_into(llvmbuilder::temp_name(dest), spec.opcode,
                                           lhs.type_name, lhs.value_name, rhs.value_name, {});
}

void Emitter::emit_unary_rvalue_into(mir::TempId dest,
                                     mir::TypeId dest_type,
                                     const mir::UnaryOpRValue &value) {
  auto operand = get_typed_operand(value.operand);
  auto category = detail::classify_type(operand.type);
  switch (value.kind) {
  case mir::UnaryOpRValue::Kind::Not: {
    std::string rhs = category == detail::ValueCategory::Bool ? "1" : "-1";
    current_block_builder_->emit_binary_into(llvmbuilder::temp_name(dest), "xor",
                                             operand.type_name, operand.value_name, rhs, {});
    return;
  }
  case mir::UnaryOpRValue::Kind::Neg: {
    current_block_builder_->emit_binary_into(llvmbuilder::temp_name(dest), "sub",
                                             operand.type_name, "0", operand.value_name, {});
    return;
  }
  case mir::UnaryOpRValue::Kind::Deref: {
    std::string pointee_type = module_.get_type_name(dest_type);
    current_block_builder_->emit_load_into(llvmbuilder::temp_name(dest), pointee_type,
                                           pointer_type_name(dest_type), operand.value_name,
                                           std::nullopt);
    return;
  }
  }
  throw std::logic_error("Unhandled unary operation during codegen");
}

void Emitter::emit_ref_rvalue_into(mir::TempId dest,
                                   mir::TypeId dest_type,
                                   const mir::RefRValue &value) {
  TranslatedPlace place = translate_place(value.place);
  if (place.pointee_type == mir::invalid_type_id) {
    throw std::logic_error("Reference place missing pointee type during codegen");
  }
  std::string dest_type_name = module_.get_type_name(dest_type);
  current_block_builder_->emit_cast_into(llvmbuilder::temp_name(dest), "bitcast",
                                         pointer_type_name(place.pointee_type),
                                         place.pointer, dest_type_name);
}

void Emitter::emit_aggregate_rvalue_into(mir::TempId dest,
                                         mir::TypeId dest_type,
                                         const mir::AggregateRValue &value) {
  std::string aggregate_type = module_.get_type_name(dest_type);
  if (value.elements.empty()) {
    materialize_constant_into_temp(dest, aggregate_type, "zeroinitializer");
    return;
  }

  std::string current_value = "undef";
  for (std::size_t index = 0; index < value.elements.size(); ++index) {
    auto element = get_typed_operand(value.elements[index]);
    bool is_last = index + 1 == value.elements.size();
    if (is_last) {
      current_value = current_block_builder_->emit_insertvalue_into(
          llvmbuilder::temp_name(dest), aggregate_type, current_value,
          element.type_name, element.value_name, {static_cast<unsigned>(index)});
    } else {
      current_value = current_block_builder_->emit_insertvalue(
          aggregate_type, current_value, element.type_name, element.value_name,
          {static_cast<unsigned>(index)}, {});
    }
  }
}

void Emitter::emit_array_repeat_rvalue_into(mir::TempId dest,
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
  std::string aggregate_type = module_.get_type_name(dest_type);
  
  // Optimization: use zeroinitializer if:
  // 1. count is 0, OR
  // 2. value is zero and element type is zero-initializable
  if (value.count == 0 || 
      (is_const_zero(value.value) && type::helper::type_helper::is_zero_initializable(array_type->element_type))) {
    materialize_constant_into_temp(dest, aggregate_type, "zeroinitializer");
    return;
  }

  auto element_operand = get_typed_operand(value.value);
  std::string current_value = "undef";
  for (std::size_t index = 0; index < value.count; ++index) {
    bool is_last = index + 1 == value.count;
    if (is_last) {
      current_value = current_block_builder_->emit_insertvalue_into(
          llvmbuilder::temp_name(dest), aggregate_type, current_value,
          element_operand.type_name, element_operand.value_name,
          {static_cast<unsigned>(index)});
    } else {
      current_value = current_block_builder_->emit_insertvalue(
          aggregate_type, current_value, element_operand.type_name,
          element_operand.value_name, {static_cast<unsigned>(index)}, {});
    }
  }
}

void Emitter::emit_cast_rvalue_into(mir::TempId dest,
                                    mir::TypeId dest_type,
                                    const mir::CastRValue &value) {
  mir::TypeId target_type = value.target_type == mir::invalid_type_id
                                ? dest_type
                                : value.target_type;
  if (target_type == mir::invalid_type_id) {
    throw std::logic_error("Cast rvalue missing target type during codegen");
  }
  auto operand = get_typed_operand(value.value);
  std::string target_type_name = module_.get_type_name(target_type);
  if (operand.type == target_type) {
    current_block_builder_->emit_binary_into(llvmbuilder::temp_name(dest), "add",
                                             target_type_name, operand.value_name, "0", {});
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
      current_block_builder_->emit_cast_into(llvmbuilder::temp_name(dest), op,
                                             operand.type_name, operand.value_name,
                                             target_type_name);
      return;
    }
    if (to_bits < from_bits) {
      current_block_builder_->emit_cast_into(llvmbuilder::temp_name(dest), "trunc",
                                             operand.type_name, operand.value_name,
                                             target_type_name);
      return;
    }
    current_block_builder_->emit_binary_into(llvmbuilder::temp_name(dest), "add",
                                             target_type_name, operand.value_name, "0", {});
    return;
  }

  if (from_category == detail::ValueCategory::Pointer &&
      to_category == detail::ValueCategory::Pointer) {
    current_block_builder_->emit_cast_into(llvmbuilder::temp_name(dest), "bitcast",
                                           operand.type_name, operand.value_name,
                                           target_type_name);
    return;
  }

  throw std::logic_error("Unsupported cast combination during codegen");
}

void Emitter::emit_field_access_rvalue_into(
    mir::TempId dest, const mir::FieldAccessRValue &value) {
  auto base_type = current_function_->get_temp_type(value.base);
  std::string base_type_name = module_.get_type_name(base_type);
    current_block_builder_->emit_extractvalue_into(
      llvmbuilder::temp_name(dest), base_type_name, get_temp(value.base),
      {static_cast<unsigned>(value.index)});
}

std::string Emitter::get_temp(mir::TempId temp) {
  return llvmbuilder::temp_name(temp);
}

std::string Emitter::get_local_ptr(mir::LocalId local) {
  if (local >= current_function_->locals.size()) {
    throw std::out_of_range("Invalid LocalId");
  }
  return local_ptr_name(local);
}

std::string Emitter::local_ptr_name(mir::LocalId local) const {
  return "%local_" + std::to_string(local);
}

TypedOperand Emitter::get_typed_operand(const mir::Operand &operand) {
  return std::visit(
      Overloaded{[&](const mir::TempId &temp) -> TypedOperand {
                   auto type = current_function_->get_temp_type(temp);
             std::string type_name = module_.get_type_name(type);
                   return TypedOperand{.type_name = std::move(type_name),
                                       .value_name = get_temp(temp),
                                       .type = type};
                 },
                 [&](const mir::Constant &constant) -> TypedOperand {
                   return materialize_constant_operand(mir::invalid_type_id, constant,
                                                       {});
                 }},
      operand.value);
}

std::string Emitter::get_block_label(mir::BasicBlockId block_id) const {
  return "bb" + std::to_string(block_id);
}

std::string Emitter::get_function_name(mir::FunctionId id) const {
  if (id >= mir_module_.functions.size()) {
    throw std::out_of_range("Invalid FunctionId");
  }
  return mir_module_.functions[id].name;
}

std::string Emitter::get_global(mir::GlobalId id) const {
  return "@g" + std::to_string(id);
}

std::string Emitter::pointer_type_name(mir::TypeId pointee_type) {
  return module_.pointer_type_name(pointee_type);
}

std::string Emitter::format_constant_literal(const mir::Constant &constant) {
  return std::visit(Overloaded{[](mir::BoolConstant val) -> std::string {
                     return val.value ? "1" : "0";
                   },
                   [](mir::IntConstant val) -> std::string {
                     std::string prefix = val.is_negative ? "-" : "";
                     return prefix + std::to_string(val.value);
                   },
                   [](mir::CharConstant val) -> std::string {
                     return std::to_string(static_cast<unsigned int>(
                         static_cast<unsigned char>(val.value)));
                   },
                   [](const mir::StringConstant &) -> std::string {
                     throw std::logic_error(
                         "String constants cannot be inlined as immediates");
                   }},
                   constant.value);
}

void Emitter::materialize_constant_into_temp(mir::TempId dest,
                                             const std::string &type_name,
                                             const std::string &literal) {
  auto &entry = current_function_builder_->entry_block();
  std::string scratch = entry.emit_alloca(type_name, std::nullopt, std::nullopt, "const.tmp");
  entry.emit_store(type_name, literal, type_name + "*", scratch);
  current_block_builder_->emit_load_into(llvmbuilder::temp_name(dest), type_name,
                                         type_name + "*", scratch, std::nullopt);
}

TypedOperand Emitter::emit_string_constant_operand(mir::TypeId type,
                                                   const mir::StringConstant &constant,
                                                   std::optional<mir::TempId> target_temp) {
  std::optional<std::string> forced_name;
  if (target_temp) {
    forced_name = llvmbuilder::temp_name(*target_temp);
  }
  std::string value_name = module_.emit_string_literal(
      *current_block_builder_, constant, type, forced_name, {});
  std::string dest_type_name = module_.get_type_name(type);
  return TypedOperand{.type_name = std::move(dest_type_name),
                      .value_name = forced_name ? *forced_name : std::move(value_name),
                      .type = type};
}

} // namespace codegen

