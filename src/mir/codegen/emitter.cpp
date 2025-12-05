#include "mir/codegen/emitter.hpp"
#include "codegen/type.hpp"
#include "helper.hpp"
#include "type.hpp"
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

std::string Emitter::translate_place(const mir::Place &place) {
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
           base_type = type::get_typeID(
             type::Type{type::PrimitiveKind::STRING});
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
  return base;
  }

  TypeId gep_base_type = base_type;
  std::vector<std::string> gep_indices;
  std::string result_name = base+"_proj";

  for (const auto &projection : place.projections) {
  std::visit(
    Overloaded{
      [&](const mir::FieldProjection &field_proj) {
        gep_indices.push_back("i32 0");
        gep_indices.push_back("i32 " +
                  std::to_string(field_proj.index));
        base_type =
          type::helper::type_helper::field(base_type, field_proj.index)
            .value();
      },
      [&](const mir::IndexProjection &index_proj) {
        gep_indices.push_back("i32 0");
        auto index_type =
          current_function_->get_temp_type(index_proj.index);
        std::string index_operand =
          type_emitter_.get_type_name(index_type) + " " +
          get_temp(index_proj.index);
        gep_indices.push_back(index_operand);
        base_type =
          type::helper::type_helper::array_element(base_type).value();
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

  return result_name;
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
  std::string type_name = type_emitter_.get_type_name(constant.type);

  return std::visit(
      Overloaded{[](mir::BoolConstant val) -> std::string {
                   return val.value ? "i1 1" : "i1 0";
                 },
                 [](mir::IntConstant val) -> std::string {
                   if (val.is_negative) {
                     return "i32 -" + std::to_string(val.value);
                   }
                   return "i32 " + std::to_string(val.value);
                 },
                 [](mir::UnitConstant) -> std::string {
                   return "void zeroinitializer";
                 },
                 [](mir::CharConstant val) -> std::string {
                   return "i8 " +
                          std::to_string(static_cast<unsigned char>(val.value));
                 }},
      constant.value);
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

} // namespace codegen