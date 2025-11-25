#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace llvmbuilder {

struct FunctionParameter {
    std::string type;
    std::string name;
};

class FunctionBuilder;
class BasicBlockBuilder;

std::string format_label_operand(std::string_view label);

class ModuleBuilder {
public:
    explicit ModuleBuilder(std::string module_id = "rc-module");

    void set_data_layout(std::string layout);
    void set_target_triple(std::string triple);

    void add_type_definition(std::string name, std::string body);
    void add_global(std::string declaration);

    FunctionBuilder& add_function(std::string name,
                                  std::string return_type,
                                  std::vector<FunctionParameter> params = {});

    std::string str() const;

    const std::string& module_id() const { return module_id_; }

private:
    std::string module_id_;
    std::string data_layout_;
    std::string target_triple_;
    std::vector<std::pair<std::string, std::string>> type_defs_;
    std::vector<std::string> globals_;
    std::vector<std::unique_ptr<FunctionBuilder>> functions_;
};

class FunctionBuilder {
public:
    using Parameter = FunctionParameter;

    BasicBlockBuilder& entry_block();
    BasicBlockBuilder& create_block(std::string label);

    const std::vector<Parameter>& parameters() const { return params_; }
    const std::string& name() const { return name_; }
    const std::string& return_type() const { return return_type_; }

    std::string str() const;

private:
    friend class ModuleBuilder;
    friend class BasicBlockBuilder;

    FunctionBuilder(std::string name,
                    std::string return_type,
                    std::vector<Parameter> params);

    BasicBlockBuilder& add_block_internal(std::string label, bool is_entry);
    std::string allocate_value_name(std::string_view hint);
    std::string make_unique_label(std::string label);
    static std::string normalize_function_name(std::string name);
    static std::string normalize_local_name(std::string name);

    std::string name_;
    std::string return_type_;
    std::vector<Parameter> params_;
    std::vector<std::unique_ptr<BasicBlockBuilder>> blocks_;
    std::unordered_map<std::string, std::size_t> value_name_counters_;
    std::unordered_map<std::string, std::size_t> block_name_counters_;
};

class BasicBlockBuilder {
public:
    BasicBlockBuilder(FunctionBuilder& parent, std::string label, bool is_entry);

    const std::string& label() const { return label_; }
    bool terminated() const { return terminated_; }

    std::string emit_binary(const std::string& opcode,
                            const std::string& type,
                            const std::string& lhs,
                            const std::string& rhs,
                            std::string_view hint = {},
                            std::string_view flags = {});

    std::string emit_icmp(const std::string& predicate,
                          const std::string& type,
                          const std::string& lhs,
                          const std::string& rhs,
                          std::string_view hint = {});

    std::string emit_phi(const std::string& type,
                         const std::vector<std::pair<std::string, std::string>>& incomings,
                         std::string_view hint = {});

    std::optional<std::string> emit_call(
        const std::string& return_type,
        const std::string& callee,
        const std::vector<std::pair<std::string, std::string>>& args,
        std::string_view hint = {});

    std::string emit_load(const std::string& value_type,
                          const std::string& pointer_type,
                          const std::string& pointer_value,
                          std::optional<unsigned> align = std::nullopt,
                          std::string_view hint = {});

    void emit_store(const std::string& value_type,
                    const std::string& value,
                    const std::string& pointer_type,
                    const std::string& pointer_value,
                    std::optional<unsigned> align = std::nullopt);

    std::string emit_alloca(
        const std::string& allocated_type,
        std::optional<std::pair<std::string, std::string>> array_size = std::nullopt,
        std::optional<unsigned> align = std::nullopt,
        std::string_view hint = {});

    std::string emit_getelementptr(
        const std::string& pointee_type,
        const std::string& pointer_type,
        const std::string& pointer_value,
        const std::vector<std::pair<std::string, std::string>>& indices,
        bool inbounds = true,
        std::string_view hint = {});

    std::string emit_cast(const std::string& opcode,
                          const std::string& value_type,
                          const std::string& value,
                          const std::string& target_type,
                          std::string_view hint = {});

    std::string emit_extractvalue(const std::string& aggregate_type,
                                  const std::string& aggregate_value,
                                  const std::vector<unsigned>& indices,
                                  std::string_view hint = {});

    std::string emit_insertvalue(const std::string& aggregate_type,
                                 const std::string& aggregate_value,
                                 const std::string& element_type,
                                 const std::string& element_value,
                                 const std::vector<unsigned>& indices,
                                 std::string_view hint = {});

    void emit_ret_void();
    void emit_ret(const std::string& type, const std::string& value);

    void emit_br(const std::string& target_label);
    void emit_cond_br(const std::string& condition,
                      const std::string& true_label,
                      const std::string& false_label);

    void emit_switch(const std::string& condition_type,
                     const std::string& condition,
                     const std::string& default_label,
                     const std::vector<std::pair<std::string, std::string>>& cases);

    void emit_unreachable();

    void emit_comment(const std::string& text);
    void emit_raw(const std::string& text);

private:
    std::string emit_instruction(const std::string& body, std::string_view hint);
    void emit_void_instruction(const std::string& text);
    void emit_terminator(const std::string& text);
    void ensure_not_terminated(const char* op_name) const;

    FunctionBuilder& parent_;
    std::string label_;
    bool is_entry_ = false;
    bool terminated_ = false;
    std::vector<std::string> lines_;

    friend class FunctionBuilder;
};

} // namespace llvmbuilder
