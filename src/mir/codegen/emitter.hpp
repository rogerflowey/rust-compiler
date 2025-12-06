#pragma once
#include "mir/codegen/type.hpp"
#include "mir/mir.hpp"
#include "llvmbuilder/builder.hpp"
#include <cstddef>
#include <functional>
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
  TypeEmitter type_emitter_;
  const mir::MirFunction *current_function_ = nullptr;
  llvmbuilder::ModuleBuilder module_;
  llvmbuilder::FunctionBuilder *current_function_builder_ = nullptr;
  llvmbuilder::BasicBlockBuilder *current_block_builder_ = nullptr;

  std::unordered_map<mir::TempId, std::string> temp_names_;
  std::unordered_map<mir::LocalId, std::string> local_ptrs_;
  std::unordered_map<mir::BasicBlockId, llvmbuilder::BasicBlockBuilder *> block_builders_;

  std::string target_triple_;
  std::string data_layout_;

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
  TypedOperand translate_rvalue(mir::TypeId dest_type, const mir::RValue &rvalue,
                                std::string_view hint = {});
  TypedOperand emit_constant_rvalue(mir::TypeId dest_type,
                                    const mir::ConstantRValue &value,
                                    std::string_view hint);
  TypedOperand materialize_constant_operand(mir::TypeId fallback_type,
                                            const mir::Constant &constant,
                                            std::string_view hint);
  TypedOperand emit_binary_rvalue(const mir::BinaryOpRValue &value,
                                  std::string_view hint);
  TypedOperand emit_unary_rvalue(mir::TypeId dest_type,
                                 const mir::UnaryOpRValue &value,
                                 std::string_view hint);
  TypedOperand emit_ref_rvalue(mir::TypeId dest_type, const mir::RefRValue &value);
  TypedOperand emit_aggregate_rvalue(mir::TypeId dest_type,
                                     const mir::AggregateRValue &value,
                                     std::string_view hint);
  TypedOperand emit_array_repeat_rvalue(mir::TypeId dest_type,
                                        const mir::ArrayRepeatRValue &value,
                                        std::string_view hint);
  TypedOperand emit_cast_rvalue(mir::TypeId dest_type, const mir::CastRValue &value,
                                std::string_view hint);
  TypedOperand emit_field_access_rvalue(const mir::FieldAccessRValue &value,
                                        std::string_view hint);

  // lookup helpers
  std::string get_temp(mir::TempId temp);
  std::string get_local_ptr(mir::LocalId local);
  TypedOperand get_typed_operand(const mir::Operand &operand);
  std::string get_block_label(mir::BasicBlockId block_id) const;
  std::string get_function_name(mir::FunctionId id) const;
  std::string get_global(mir::GlobalId id) const;
  std::string pointer_type_name(mir::TypeId pointee_type);
  std::string temp_hint(mir::TempId temp) const;

  // helpers
  std::string format_constant_literal(const mir::Constant &constant);
  TypedOperand emit_string_constant_operand(mir::TypeId type,
                                            const mir::StringConstant &constant,
                                            std::string_view hint);
  std::string ensure_string_global(const mir::StringConstant &constant);

  struct StringLiteralKey {
    std::string data;
    bool is_cstyle = false;

    bool operator==(const StringLiteralKey &other) const {
      return is_cstyle == other.is_cstyle && data == other.data;
    }
  };

  struct StringLiteralHasher {
    std::size_t operator()(const StringLiteralKey &key) const noexcept {
      std::size_t hash = std::hash<std::string>{}(key.data);
      std::size_t flag = key.is_cstyle ? 0x9e3779b97f4a7c15ull
                                       : 0x7f4a7c159e3779b9ull;
      hash ^= flag + 0x9e3779b9 + (hash << 6) + (hash >> 2);
      return hash;
    }
  };

  std::unordered_map<StringLiteralKey, std::string, StringLiteralHasher>
      string_literal_globals_;
  std::size_t next_string_global_id_ = 0;
};

} // namespace codegen