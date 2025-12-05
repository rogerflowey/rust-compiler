#pragma once
#include "codegen/code.hpp"
#include "mir/codegen/type.hpp"
#include "mir/mir.hpp"
#include "mir/codegen/code.hpp"
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace codegen {
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
  std::string translate_place(const mir::Place &place);
  void translate_rvalue(const std::string &dest_name, mir::TypeId dest_type,
                        const mir::RValue &rvalue);

  // lookup helpers
  std::string get_temp(mir::TempId temp);
  std::string get_local_ptr(mir::LocalId local);
  std::string get_operand(const mir::Operand &operand);
  std::string get_constant(const mir::Constant &constant);
  std::string get_constant_ptr(const mir::Constant &constant);
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
};

} // namespace codegen