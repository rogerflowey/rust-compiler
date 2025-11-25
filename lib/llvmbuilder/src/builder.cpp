#include "llvmbuilder/builder.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>

namespace llvmbuilder {
namespace {

std::string ensure_prefix(std::string name, char prefix) {
    if (name.empty()) {
        name = std::string(1, prefix);
    }
    if (name.front() == prefix) {
        return name;
    }
    return std::string(1, prefix) + name;
}

std::string sanitize_hint(std::string_view hint, std::string_view fallback) {
    std::string base = hint.empty() ? std::string(fallback) : std::string(hint);
    if (base.empty()) {
        base = fallback;
    }
    for (char& ch : base) {
        unsigned char u = static_cast<unsigned char>(ch);
        if (!std::isalnum(u) && ch != '_' && ch != '.') {
            ch = '_';
        }
    }
    if (base.empty()) {
        base = fallback;
    }
    return base;
}

std::string sanitize_label_name(std::string label) {
    return sanitize_hint(label, "block");
}

} // namespace

std::string format_label_operand(std::string_view label) {
    if (label.empty()) {
        throw std::logic_error("label operand cannot be empty");
    }
    if (label.front() == '%') {
        return std::string(label);
    }
    return "%" + std::string(label);
}

ModuleBuilder::ModuleBuilder(std::string module_id)
    : module_id_(std::move(module_id)) {}

void ModuleBuilder::set_data_layout(std::string layout) {
    data_layout_ = std::move(layout);
}

void ModuleBuilder::set_target_triple(std::string triple) {
    target_triple_ = std::move(triple);
}

void ModuleBuilder::add_type_definition(std::string name, std::string body) {
    if (name.empty()) {
        throw std::logic_error("type name cannot be empty");
    }
    if (name.front() == '%') {
        name.erase(name.begin());
    }
    type_defs_.emplace_back(std::move(name), std::move(body));
}

void ModuleBuilder::add_global(std::string declaration) {
    if (declaration.empty()) {
        throw std::logic_error("global declaration cannot be empty");
    }
    globals_.push_back(std::move(declaration));
}

FunctionBuilder& ModuleBuilder::add_function(std::string name,
                                             std::string return_type,
                                             std::vector<FunctionParameter> params) {
    auto builder = std::unique_ptr<FunctionBuilder>(
        new FunctionBuilder(std::move(name),
                             std::move(return_type),
                             std::move(params)));
    FunctionBuilder& ref = *builder;
    functions_.push_back(std::move(builder));
    return ref;
}

std::string ModuleBuilder::str() const {
    std::ostringstream oss;
    oss << "; ModuleID = '" << module_id_ << "'\n";
    if (!data_layout_.empty()) {
        oss << "target datalayout = \"" << data_layout_ << "\"\n";
    }
    if (!target_triple_.empty()) {
        oss << "target triple = \"" << target_triple_ << "\"\n";
    }
    if ((!data_layout_.empty() || !target_triple_.empty()) &&
        (!type_defs_.empty() || !globals_.empty() || !functions_.empty())) {
        oss << "\n";
    }

    if (!type_defs_.empty()) {
        for (const auto& def : type_defs_) {
            oss << "%" << def.first << " = type " << def.second << "\n";
        }
        if (!globals_.empty() || !functions_.empty()) {
            oss << "\n";
        }
    }

    if (!globals_.empty()) {
        for (const auto& global : globals_) {
            oss << global << "\n";
        }
        if (!functions_.empty()) {
            oss << "\n";
        }
    }

    for (std::size_t i = 0; i < functions_.size(); ++i) {
        oss << functions_[i]->str();
        if (i + 1 < functions_.size()) {
            oss << "\n";
        }
    }

    return oss.str();
}

FunctionBuilder::FunctionBuilder(std::string name,
                                 std::string return_type,
                                 std::vector<Parameter> params)
    : name_(normalize_function_name(std::move(name))),
      return_type_(std::move(return_type)),
      params_(std::move(params)) {
    for (std::size_t i = 0; i < params_.size(); ++i) {
        auto& param = params_[i];
        if (param.name.empty()) {
            param.name = "%arg" + std::to_string(i);
        }
        param.name = normalize_local_name(std::move(param.name));
    }
    add_block_internal("entry", true);
}

BasicBlockBuilder& FunctionBuilder::entry_block() {
    if (blocks_.empty()) {
        throw std::logic_error("function has no entry block");
    }
    return *blocks_.front();
}

BasicBlockBuilder& FunctionBuilder::create_block(std::string label) {
    return add_block_internal(std::move(label), false);
}

BasicBlockBuilder& FunctionBuilder::add_block_internal(std::string label, bool is_entry) {
    std::string unique_label = make_unique_label(std::move(label));
    auto block = std::make_unique<BasicBlockBuilder>(*this, unique_label, is_entry);
    BasicBlockBuilder& ref = *block;
    blocks_.push_back(std::move(block));
    return ref;
}

std::string FunctionBuilder::allocate_value_name(std::string_view hint) {
    std::string base = sanitize_hint(hint, "tmp");
    auto& counter = value_name_counters_[base];
    std::string suffix;
    if (counter > 0) {
        suffix = "." + std::to_string(counter);
    }
    ++counter;
    return "%" + base + suffix;
}

std::string FunctionBuilder::make_unique_label(std::string label) {
    std::string base = sanitize_label_name(std::move(label));
    auto& counter = block_name_counters_[base];
    std::string suffix;
    if (counter > 0) {
        suffix = "." + std::to_string(counter);
    }
    ++counter;
    return base + suffix;
}

std::string FunctionBuilder::normalize_function_name(std::string name) {
    if (name.empty()) {
        throw std::logic_error("function name cannot be empty");
    }
    return ensure_prefix(std::move(name), '@');
}

std::string FunctionBuilder::normalize_local_name(std::string name) {
    if (name.empty()) {
        throw std::logic_error("value name cannot be empty");
    }
    return ensure_prefix(std::move(name), '%');
}

std::string FunctionBuilder::str() const {
    std::ostringstream oss;
    oss << "define " << return_type_ << " " << name_ << "(";
    for (std::size_t i = 0; i < params_.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << params_[i].type << " " << params_[i].name;
    }
    oss << ") {\n";
    for (const auto& block : blocks_) {
        oss << block->label_ << ":\n";
        for (const auto& line : block->lines_) {
            oss << line << "\n";
        }
        if (!block->terminated_) {
            oss << "  unreachable\n";
        }
    }
    oss << "}\n";
    return oss.str();
}

BasicBlockBuilder::BasicBlockBuilder(FunctionBuilder& parent, std::string label, bool is_entry)
    : parent_(parent), label_(std::move(label)), is_entry_(is_entry) {}

std::string BasicBlockBuilder::emit_instruction(const std::string& body, std::string_view hint) {
    ensure_not_terminated("instruction");
    std::string name = parent_.allocate_value_name(hint);
    lines_.push_back("  " + name + " = " + body);
    return name;
}

void BasicBlockBuilder::emit_void_instruction(const std::string& text) {
    ensure_not_terminated("instruction");
    lines_.push_back("  " + text);
}

void BasicBlockBuilder::emit_terminator(const std::string& text) {
    ensure_not_terminated("terminator");
    lines_.push_back("  " + text);
    terminated_ = true;
}

void BasicBlockBuilder::ensure_not_terminated(const char* op_name) const {
    if (terminated_) {
        throw std::logic_error(std::string("cannot append ") + op_name + " to terminated block");
    }
}

std::string BasicBlockBuilder::emit_binary(const std::string& opcode,
                                           const std::string& type,
                                           const std::string& lhs,
                                           const std::string& rhs,
                                           std::string_view hint,
                                           std::string_view flags) {
    std::ostringstream oss;
    oss << opcode;
    if (!flags.empty()) {
        oss << ' ' << flags;
    }
    oss << ' ' << type << ' ' << lhs << ", " << rhs;
    return emit_instruction(oss.str(), hint);
}

std::string BasicBlockBuilder::emit_icmp(const std::string& predicate,
                                         const std::string& type,
                                         const std::string& lhs,
                                         const std::string& rhs,
                                         std::string_view hint) {
    std::ostringstream oss;
    oss << "icmp " << predicate << ' ' << type << ' ' << lhs << ", " << rhs;
    return emit_instruction(oss.str(), hint);
}

std::string BasicBlockBuilder::emit_phi(
    const std::string& type,
    const std::vector<std::pair<std::string, std::string>>& incomings,
    std::string_view hint) {
    if (incomings.empty()) {
        throw std::logic_error("phi must have at least one incoming edge");
    }
    std::ostringstream oss;
    oss << "phi " << type << ' ';
    for (std::size_t i = 0; i < incomings.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << "[ " << incomings[i].first << ", "
            << format_label_operand(incomings[i].second) << " ]";
    }
    return emit_instruction(oss.str(), hint);
}

std::optional<std::string> BasicBlockBuilder::emit_call(
    const std::string& return_type,
    const std::string& callee,
    const std::vector<std::pair<std::string, std::string>>& args,
    std::string_view hint) {
    std::ostringstream oss;
    oss << "call " << return_type << ' ' << callee << "(";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << args[i].first << ' ' << args[i].second;
    }
    oss << ")";
    if (return_type == "void") {
        emit_void_instruction(oss.str());
        return std::nullopt;
    }
    return emit_instruction(oss.str(), hint);
}

std::string BasicBlockBuilder::emit_load(const std::string& value_type,
                                         const std::string& pointer_type,
                                         const std::string& pointer_value,
                                         std::optional<unsigned> align,
                                         std::string_view hint) {
    std::ostringstream oss;
    oss << "load " << value_type << ", " << pointer_type << ' ' << pointer_value;
    if (align) {
        oss << ", align " << *align;
    }
    return emit_instruction(oss.str(), hint);
}

void BasicBlockBuilder::emit_store(const std::string& value_type,
                                   const std::string& value,
                                   const std::string& pointer_type,
                                   const std::string& pointer_value,
                                   std::optional<unsigned> align) {
    std::ostringstream oss;
    oss << "store " << value_type << ' ' << value << ", "
        << pointer_type << ' ' << pointer_value;
    if (align) {
        oss << ", align " << *align;
    }
    emit_void_instruction(oss.str());
}

std::string BasicBlockBuilder::emit_alloca(
    const std::string& allocated_type,
    std::optional<std::pair<std::string, std::string>> array_size,
    std::optional<unsigned> align,
    std::string_view hint) {
    std::ostringstream oss;
    oss << "alloca " << allocated_type;
    if (array_size) {
        oss << ", " << array_size->first << ' ' << array_size->second;
    }
    if (align) {
        oss << ", align " << *align;
    }
    return emit_instruction(oss.str(), hint);
}

std::string BasicBlockBuilder::emit_getelementptr(
    const std::string& pointee_type,
    const std::string& pointer_type,
    const std::string& pointer_value,
    const std::vector<std::pair<std::string, std::string>>& indices,
    bool inbounds,
    std::string_view hint) {
    std::ostringstream oss;
    oss << "getelementptr ";
    if (inbounds) {
        oss << "inbounds ";
    }
    oss << pointee_type << ", " << pointer_type << ' ' << pointer_value;
    for (const auto& idx : indices) {
        oss << ", " << idx.first << ' ' << idx.second;
    }
    return emit_instruction(oss.str(), hint);
}

std::string BasicBlockBuilder::emit_cast(const std::string& opcode,
                                         const std::string& value_type,
                                         const std::string& value,
                                         const std::string& target_type,
                                         std::string_view hint) {
    std::ostringstream oss;
    oss << opcode << ' ' << value_type << ' ' << value << " to " << target_type;
    return emit_instruction(oss.str(), hint);
}

std::string BasicBlockBuilder::emit_extractvalue(const std::string& aggregate_type,
                                                 const std::string& aggregate_value,
                                                 const std::vector<unsigned>& indices,
                                                 std::string_view hint) {
    if (indices.empty()) {
        throw std::logic_error("extractvalue requires at least one index");
    }
    std::ostringstream oss;
    oss << "extractvalue " << aggregate_type << ' ' << aggregate_value << ", ";
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << indices[i];
    }
    return emit_instruction(oss.str(), hint);
}

std::string BasicBlockBuilder::emit_insertvalue(const std::string& aggregate_type,
                                                const std::string& aggregate_value,
                                                const std::string& element_type,
                                                const std::string& element_value,
                                                const std::vector<unsigned>& indices,
                                                std::string_view hint) {
    if (indices.empty()) {
        throw std::logic_error("insertvalue requires at least one index");
    }
    std::ostringstream oss;
    oss << "insertvalue " << aggregate_type << ' ' << aggregate_value << ", "
        << element_type << ' ' << element_value << ", ";
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << indices[i];
    }
    return emit_instruction(oss.str(), hint);
}

void BasicBlockBuilder::emit_ret_void() {
    emit_terminator("ret void");
}

void BasicBlockBuilder::emit_ret(const std::string& type, const std::string& value) {
    std::ostringstream oss;
    oss << "ret " << type << ' ' << value;
    emit_terminator(oss.str());
}

void BasicBlockBuilder::emit_br(const std::string& target_label) {
    emit_terminator("br label " + format_label_operand(target_label));
}

void BasicBlockBuilder::emit_cond_br(const std::string& condition,
                                     const std::string& true_label,
                                     const std::string& false_label) {
    std::ostringstream oss;
    oss << "br i1 " << condition << ", label "
        << format_label_operand(true_label) << ", label "
        << format_label_operand(false_label);
    emit_terminator(oss.str());
}

void BasicBlockBuilder::emit_switch(
    const std::string& condition_type,
    const std::string& condition,
    const std::string& default_label,
    const std::vector<std::pair<std::string, std::string>>& cases) {
    std::ostringstream oss;
    oss << "switch " << condition_type << ' ' << condition
        << ", label " << format_label_operand(default_label);
    if (cases.empty()) {
        emit_terminator(oss.str());
        return;
    }
    oss << " [\n";
    for (std::size_t i = 0; i < cases.size(); ++i) {
        oss << "    " << condition_type << ' ' << cases[i].first
            << ", label " << format_label_operand(cases[i].second);
        if (i + 1 < cases.size()) {
            oss << "\n";
        }
    }
    oss << "\n  ]";
    emit_terminator(oss.str());
}

void BasicBlockBuilder::emit_unreachable() {
    emit_terminator("unreachable");
}

void BasicBlockBuilder::emit_comment(const std::string& text) {
    ensure_not_terminated("comment");
    lines_.push_back("  ; " + text);
}

void BasicBlockBuilder::emit_raw(const std::string& text) {
    ensure_not_terminated("raw text");
    lines_.push_back(text);
}

} // namespace llvmbuilder
