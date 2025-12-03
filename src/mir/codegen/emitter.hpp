#pragma once
#include "mir/codegen/type.hpp"
#include "mir/mir.hpp"
#include <sstream>
#include <string>
#include <unordered_map>

namespace codegen {
class Emitter {
public:
  explicit Emitter(mir::MirModule &module) : module(module) {}

  void emit();

  // New:
  std::string str() const { return output.str(); }
  void write_to(std::ostream &os) const { os << output.str(); }

  Emitter(const Emitter &) = delete;
  Emitter &operator=(const Emitter &) = delete;

private:
  mir::MirModule &module;
  TypeEmitter type_emitter;

  std::stringstream output;

  // name caches
  std::unordered_map<mir::TempId, std::string> temp_names_;
  std::unordered_map<mir::LocalId, std::string> local_ptr_names_;
  std::unordered_map<mir::BasicBlockId, std::string> block_labels_;

  // emit helpers
  void emit_function(const mir::MirFunction &function);
  void emit_block(const mir::BasicBlock &block);
  void emit_statement(const mir::Statement &statement);
  void emit_terminator(const mir::Terminator &terminator);
  void emit_phi_nodes(const mir::BasicBlock &block);

  std::string emit_place_and_get_ptr(const mir::Place &place);

  std::string translate_rvalue(const mir::RValue &rvalue);

  // lookup helpers
  std::string get_temp(mir::TempId temp);
  std::string get_local_ptr(mir::LocalId local);
  std::string get_place(const mir::Place &place);
  std::string get_operand(const mir::Operand &operand);
  std::string get_constant(const mir::Constant &constant);
  std::string get_rvalue(const mir::RValue &rvalue);
  std::string get_constant_ptr(const mir::Constant &constant);
  std::string get_block_label(mir::BasicBlockId block_id);
  std::string get_function_name(mir::FunctionId id) const;
};

} // namespace codegen