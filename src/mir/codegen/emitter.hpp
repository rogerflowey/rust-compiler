#pragma once
#include "mir/mir.hpp"
#include "mir/codegen/llvmbuilder/builder.hpp"
#include <cstddef>
#include <functional>
#include <optional>
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
  explicit Emitter(mir::MirModule &module,
                   std::string target_triple = {},
                   std::string data_layout = {});

  std::string emit();
  const llvmbuilder::ModuleBuilder &module() const { return module_; }

  Emitter(const Emitter &) = delete;
  Emitter &operator=(const Emitter &) = delete;

private:
  mir::MirModule &mir_module_;
  const mir::MirFunction *current_function_ = nullptr;
  llvmbuilder::ModuleBuilder module_;
  llvmbuilder::FunctionBuilder *current_function_builder_ = nullptr;
  llvmbuilder::BasicBlockBuilder *current_block_builder_ = nullptr;

  std::unordered_map<mir::BasicBlockId, llvmbuilder::BasicBlockBuilder *> block_builders_;

  std::string target_triple_;
  std::string data_layout_;

  // emit helpers
  void emit_function(const mir::MirFunction &function);
  void emit_external_declaration(const mir::ExternalFunction &function);
  void emit_block(mir::BasicBlockId block_id);
  void emit_statement(const mir::Statement &statement);
  void emit_terminator(const mir::Terminator &terminator);
  void emit_phi_nodes(const mir::PhiNode &phi_node);
  void emit_globals();
  void emit_entry_block_prologue();
  void emit_define(const mir::DefineStatement &statement);
  void emit_load(const mir::LoadStatement &statement);
  void emit_assign(const mir::AssignStatement &statement);
  void emit_init_statement(const mir::InitStatement &statement);
  void emit_call(const mir::CallStatement &statement);

  // translation helpers: both emit and get_*
  TranslatedPlace translate_place(const mir::Place &place);
  void emit_rvalue_into(mir::TempId dest, mir::TypeId dest_type, const mir::RValue &rvalue);
  void emit_constant_rvalue_into(mir::TempId dest,
                                 mir::TypeId dest_type,
                                 const mir::ConstantRValue &value);
  TypedOperand materialize_constant_operand(mir::TypeId fallback_type,
                                            const mir::Constant &constant,
                                            std::optional<mir::TempId> target_temp = std::nullopt);
  void emit_binary_rvalue_into(mir::TempId dest, const mir::BinaryOpRValue &value);
  void emit_unary_rvalue_into(mir::TempId dest,
                              mir::TypeId dest_type,
                              const mir::UnaryOpRValue &value);
  void emit_ref_rvalue_into(mir::TempId dest,
                            mir::TypeId dest_type,
                            const mir::RefRValue &value);
  void emit_aggregate_rvalue_into(mir::TempId dest,
                                  mir::TypeId dest_type,
                                  const mir::AggregateRValue &value);
  void emit_array_repeat_rvalue_into(mir::TempId dest,
                                     mir::TypeId dest_type,
                                     const mir::ArrayRepeatRValue &value);
  void emit_cast_rvalue_into(mir::TempId dest,
                             mir::TypeId dest_type,
                             const mir::CastRValue &value);
  void emit_field_access_rvalue_into(mir::TempId dest,
                                     const mir::FieldAccessRValue &value);

  // per-field aggregate initialization helpers
  void emit_aggregate_init_per_field(
      const std::string &base_ptr,
      mir::TypeId aggregate_type,
      const mir::AggregateRValue &agg);
  void emit_array_repeat_init_per_element(
      const std::string &base_ptr,
      mir::TypeId array_type_id,
      const mir::ArrayRepeatRValue &value);

  // lookup helpers
  std::string get_temp(mir::TempId temp);
  std::string get_local_ptr(mir::LocalId local);
  std::string local_ptr_name(mir::LocalId local) const;
  TypedOperand get_typed_operand(const mir::Operand &operand);
  std::string get_block_label(mir::BasicBlockId block_id) const;
  std::string get_function_name(mir::FunctionId id) const;
  std::string get_global(mir::GlobalId id) const;
  std::string pointer_type_name(mir::TypeId pointee_type);

  // helpers
  std::string format_constant_literal(const mir::Constant &constant);
  TypedOperand emit_string_constant_operand(mir::TypeId type,
                                            const mir::StringConstant &constant,
                                            std::optional<mir::TempId> target_temp = std::nullopt);
  void materialize_constant_into_temp(mir::TempId dest,
                                      const std::string &type_name,
                                      const std::string &literal);
  std::string emit_sizeof_bytes(mir::TypeId type);
};

} // namespace codegen