#pragma once
#include "codegen/code.hpp"
#include "mir/codegen/type.hpp"
#include "mir/mir.hpp"
#include "mir/codegen/code.hpp"
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace codegen {

struct TranslatedPlace {
  std::string pointer;
  mir::TypeId pointee_type = mir::invalid_type_id;
};

struct TypedOperand {
  std::string type_name;
  std::string value_name;
  mir::TypeId type = mir::invalid_type_id;
};

class Emitter {
public:
  explicit Emitter(mir::MirModule &module) : module(module) {}

  ProgramCode emit();
  const ProgramCode &program_code() const { return program_code_; }

  Emitter(const Emitter &) = delete;
  Emitter &operator=(const Emitter &) = delete;

private:
  mir::MirModule &module;
  TypeEmitter type_emitter_;
  ProgramCode program_code_;

  FunctionCode current_function_code_;
  BlockCode current_block_code_;
  const mir::MirFunction *current_function_ = nullptr;


  // emit helpers
  void emit_function(const mir::MirFunction &function);
  void emit_block(mir::BasicBlockId block_id);
  void emit_statement(const mir::Statement &statement);
  void emit_terminator(const mir::Terminator &terminator);
  void emit_phi_nodes(const mir::PhiNode &phi_node);
  void emit_globals();
  void emit_entry_block_prologue();
  void emit_define(const mir::DefineStatement &statement);
  void emit_load(const mir::LoadStatement &statement);
  void emit_assign(const mir::AssignStatement &statement);
  void emit_call(const mir::CallStatement &statement);
  void emit_struct_definitions();

  // translation helpers: both emit and get_*
  TranslatedPlace translate_place(const mir::Place &place);
  void translate_rvalue(const std::string &dest_name, mir::TypeId dest_type,
                        const mir::RValue &rvalue);
  void emit_constant_rvalue(const std::string &dest_name, mir::TypeId dest_type,
                            const mir::ConstantRValue &value);
  void emit_binary_rvalue(const std::string &dest_name,
                          const mir::BinaryOpRValue &value);
  void emit_unary_rvalue(const std::string &dest_name, mir::TypeId dest_type,
                         const mir::UnaryOpRValue &value);
  void emit_ref_rvalue(const std::string &dest_name, mir::TypeId dest_type,
                       const mir::RefRValue &value);
  void emit_aggregate_rvalue(const std::string &dest_name, mir::TypeId dest_type,
                             const mir::AggregateRValue &value);
  void emit_array_repeat_rvalue(const std::string &dest_name,
                                mir::TypeId dest_type,
                                const mir::ArrayRepeatRValue &value);
  void emit_cast_rvalue(const std::string &dest_name, mir::TypeId dest_type,
                        const mir::CastRValue &value);
  void emit_field_access_rvalue(const std::string &dest_name,
                                const mir::FieldAccessRValue &value);

  // lookup helpers
  std::string get_temp(mir::TempId temp);
  std::string get_local_ptr(mir::LocalId local);
  std::string get_operand(const mir::Operand &operand);
  std::string get_constant(const mir::Constant &constant);
  std::string get_constant_ptr(const mir::Constant &constant);
  TypedOperand get_typed_operand(const mir::Operand &operand);
  std::string get_block_label(mir::BasicBlockId block_id) const;
  std::string get_function_name(mir::FunctionId id) const;
  std::string get_global(mir::GlobalId id) const;

  // helpers
  void finish_block(){
      current_function_code_.blocks.emplace_back(std::move(current_block_code_));
      current_block_code_ = BlockCode{};
  }
  void finish_function(){
      program_code_.functions.emplace_back(std::move(current_function_code_));
      current_function_code_ = FunctionCode{};
  }
  std::string format_constant_literal(const mir::Constant &constant);
  std::string make_internal_value_name(const std::string &base,
                                       std::string_view suffix);
  void append_instruction(const std::string &line);
  std::string format_typed_operand(const TypedOperand &operand) const;

  std::size_t internal_name_counter_ = 0;
};

} // namespace codegen