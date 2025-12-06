#include "mir/codegen/llvmbuilder/builder.hpp"

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

std::string build_binary_body(const std::string& opcode,
                              const std::string& type,
                              const std::string& lhs,
                              const std::string& rhs,
                              std::string_view flags) {
    std::ostringstream oss;
    oss << opcode;
    if (!flags.empty()) {
        oss << ' ' << flags;
    }
    oss << ' ' << type << ' ' << lhs << ", " << rhs;
    return oss.str();
}

std::string build_icmp_body(const std::string& predicate,
                            const std::string& type,
                            const std::string& lhs,
                            const std::string& rhs) {
    std::ostringstream oss;
    oss << "icmp " << predicate << ' ' << type << ' ' << lhs << ", " << rhs;
    return oss.str();
}

std::string build_phi_body(
    const std::string& type,
    const std::vector<std::pair<std::string, std::string>>& incomings) {
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
    return oss.str();
}

std::string build_call_body(
    const std::string& return_type,
    const std::string& callee,
    const std::vector<std::pair<std::string, std::string>>& args) {
    std::ostringstream oss;
    oss << "call " << return_type << ' ' << callee << "(";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << args[i].first << ' ' << args[i].second;
    }
    oss << ")";
    return oss.str();
}

std::string build_load_body(const std::string& value_type,
                            const std::string& pointer_type,
                            const std::string& pointer_value,
                            std::optional<unsigned> align) {
    std::ostringstream oss;
    oss << "load " << value_type << ", " << pointer_type << ' ' << pointer_value;
    if (align) {
        oss << ", align " << *align;
    }
    return oss.str();
}

std::string build_alloca_body(
    const std::string& allocated_type,
    std::optional<std::pair<std::string, std::string>> array_size,
    std::optional<unsigned> align) {
    std::ostringstream oss;
    oss << "alloca " << allocated_type;
    if (array_size) {
        oss << ", " << array_size->first << ' ' << array_size->second;
    }
    if (align) {
        oss << ", align " << *align;
    }
    return oss.str();
}

std::string build_gep_body(
    const std::string& pointee_type,
    const std::string& pointer_type,
    const std::string& pointer_value,
    const std::vector<std::pair<std::string, std::string>>& indices,
    bool inbounds) {
    std::ostringstream oss;
    oss << "getelementptr ";
    if (inbounds) {
        oss << "inbounds ";
    }
    oss << pointee_type << ", " << pointer_type << ' ' << pointer_value;
    for (const auto& idx : indices) {
        oss << ", " << idx.first << ' ' << idx.second;
    }
    return oss.str();
}

std::string build_cast_body(const std::string& opcode,
                            const std::string& value_type,
                            const std::string& value,
                            const std::string& target_type) {
    std::ostringstream oss;
    oss << opcode << ' ' << value_type << ' ' << value << " to " << target_type;
    return oss.str();
}

std::string build_extractvalue_body(const std::string& aggregate_type,
                                    const std::string& aggregate_value,
                                    const std::vector<unsigned>& indices) {
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
    return oss.str();
}

std::string build_insertvalue_body(const std::string& aggregate_type,
                                   const std::string& aggregate_value,
                                   const std::string& element_type,
                                   const std::string& element_value,
                                   const std::vector<unsigned>& indices) {
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
    return oss.str();
}

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

std::string temp_name(mir::TempId temp) {
    return "%t" + std::to_string(temp);
}

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

std::string ModuleBuilder::get_type_name(mir::TypeId type) {
    return type_formatter_.get_type_name(type);
}

std::string ModuleBuilder::pointer_type_name(mir::TypeId type) {
    return get_type_name(type) + "*";
}

std::string ModuleBuilder::intern_string_literal(const mir::StringConstant& literal) {
    StringLiteralKey key{literal.data, literal.is_cstyle};
    auto it = string_literal_globals_.find(key);
    if (it != string_literal_globals_.end()) {
        return it->second;
    }

    std::string name = "@str." + std::to_string(next_string_global_id_++);
    std::ostringstream decl;
    decl << name << " = private unnamed_addr constant [" << literal.data.size()
         << " x i8] c\"" << escape_string_literal(literal.data) << "\"";
    globals_.push_back(decl.str());

    auto [inserted, _] = string_literal_globals_.emplace(std::move(key), name);
    return inserted->second;
}

std::string ModuleBuilder::emit_string_literal(BasicBlockBuilder& block,
                                               const mir::StringConstant& literal,
                                               mir::TypeId result_type,
                                               std::optional<std::string> forced_name,
                                               std::string_view hint) {
    if (result_type == mir::invalid_type_id) {
        throw std::logic_error("String literal emission requires resolved type");
    }

    std::string global_name = intern_string_literal(literal);
    std::ostringstream array_type_builder;
    array_type_builder << "[" << literal.data.size() << " x i8]";
    std::string array_type = array_type_builder.str();
    std::string pointer_type = array_type + "*";

    auto char_type_id = type::get_typeID(type::Type{type::PrimitiveKind::CHAR});
    std::string char_pointer_type = get_type_name(char_type_id) + "*";
    std::string dest_type_name = get_type_name(result_type);
    bool needs_cast = dest_type_name != char_pointer_type;

    std::vector<std::pair<std::string, std::string>> indices;
    indices.emplace_back("i32", "0");
    indices.emplace_back("i32", "0");

    std::string element_pointer;
    if (forced_name && !needs_cast) {
        element_pointer = block.emit_getelementptr_into(*forced_name, array_type,
                                                        pointer_type, global_name,
                                                        indices, true);
    } else {
        element_pointer = block.emit_getelementptr(
            array_type, pointer_type, global_name, indices, true, hint);
    }

    if (!needs_cast) {
        return element_pointer;
    }

    if (forced_name) {
        return block.emit_cast_into(*forced_name, "bitcast", char_pointer_type,
                                    element_pointer, dest_type_name);
    }
    return block.emit_cast("bitcast", char_pointer_type, element_pointer,
                           dest_type_name, hint);
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

    bool has_manual_type_defs = !type_defs_.empty();
    bool has_formatter_defs = !type_formatter_.struct_definitions().empty();
    if (has_manual_type_defs || has_formatter_defs) {
        for (const auto& def : type_defs_) {
            oss << "%" << def.first << " = type " << def.second << "\n";
        }
        for (const auto& def : type_formatter_.struct_definitions()) {
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

std::string BasicBlockBuilder::emit_named_instruction(const std::string& dest,
                                                      const std::string& body) {
    ensure_not_terminated("instruction");
    ensure_value_name(dest);
    lines_.push_back("  " + dest + " = " + body);
    return dest;
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

void BasicBlockBuilder::ensure_value_name(const std::string& name) const {
    if (name.empty() || name.front() != '%') {
        throw std::logic_error("SSA value name must start with '%' and cannot be empty");
    }
}

std::string BasicBlockBuilder::emit_binary(const std::string& opcode,
                                           const std::string& type,
                                           const std::string& lhs,
                                           const std::string& rhs,
                                           std::string_view hint,
                                           std::string_view flags) {
    return emit_instruction(build_binary_body(opcode, type, lhs, rhs, flags), hint);
}

std::string BasicBlockBuilder::emit_binary_into(const std::string& dest,
                                                const std::string& opcode,
                                                const std::string& type,
                                                const std::string& lhs,
                                                const std::string& rhs,
                                                std::string_view flags) {
    return emit_named_instruction(dest, build_binary_body(opcode, type, lhs, rhs, flags));
}

std::string BasicBlockBuilder::emit_icmp(const std::string& predicate,
                                         const std::string& type,
                                         const std::string& lhs,
                                         const std::string& rhs,
                                         std::string_view hint) {
    return emit_instruction(build_icmp_body(predicate, type, lhs, rhs), hint);
}

std::string BasicBlockBuilder::emit_icmp_into(const std::string& dest,
                                              const std::string& predicate,
                                              const std::string& type,
                                              const std::string& lhs,
                                              const std::string& rhs) {
    return emit_named_instruction(dest, build_icmp_body(predicate, type, lhs, rhs));
}

std::string BasicBlockBuilder::emit_phi(
    const std::string& type,
    const std::vector<std::pair<std::string, std::string>>& incomings,
    std::string_view hint) {
    return emit_instruction(build_phi_body(type, incomings), hint);
}

std::string BasicBlockBuilder::emit_phi_into(
    const std::string& dest,
    const std::string& type,
    const std::vector<std::pair<std::string, std::string>>& incomings) {
    return emit_named_instruction(dest, build_phi_body(type, incomings));
}

std::optional<std::string> BasicBlockBuilder::emit_call(
    const std::string& return_type,
    const std::string& callee,
    const std::vector<std::pair<std::string, std::string>>& args,
    std::string_view hint) {
    std::string body = build_call_body(return_type, callee, args);
    if (return_type == "void") {
        emit_void_instruction(body);
        return std::nullopt;
    }
    return emit_instruction(body, hint);
}

std::string BasicBlockBuilder::emit_call_into(
    const std::string& dest,
    const std::string& return_type,
    const std::string& callee,
    const std::vector<std::pair<std::string, std::string>>& args) {
    if (return_type == "void") {
        throw std::logic_error("cannot assign result of void call to a name");
    }
    return emit_named_instruction(dest, build_call_body(return_type, callee, args));
}

std::string BasicBlockBuilder::emit_load(const std::string& value_type,
                                         const std::string& pointer_type,
                                         const std::string& pointer_value,
                                         std::optional<unsigned> align,
                                         std::string_view hint) {
    return emit_instruction(build_load_body(value_type, pointer_type, pointer_value, align), hint);
}

std::string BasicBlockBuilder::emit_load_into(const std::string& dest,
                                              const std::string& value_type,
                                              const std::string& pointer_type,
                                              const std::string& pointer_value,
                                              std::optional<unsigned> align) {
    return emit_named_instruction(dest, build_load_body(value_type, pointer_type, pointer_value, align));
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
    return emit_instruction(build_alloca_body(allocated_type, array_size, align), hint);
}

std::string BasicBlockBuilder::emit_alloca_into(
    const std::string& dest,
    const std::string& allocated_type,
    std::optional<std::pair<std::string, std::string>> array_size,
    std::optional<unsigned> align) {
    return emit_named_instruction(dest, build_alloca_body(allocated_type, array_size, align));
}

std::string BasicBlockBuilder::emit_getelementptr(
    const std::string& pointee_type,
    const std::string& pointer_type,
    const std::string& pointer_value,
    const std::vector<std::pair<std::string, std::string>>& indices,
    bool inbounds,
    std::string_view hint) {
    return emit_instruction(build_gep_body(pointee_type, pointer_type, pointer_value, indices, inbounds), hint);
}

std::string BasicBlockBuilder::emit_getelementptr_into(
    const std::string& dest,
    const std::string& pointee_type,
    const std::string& pointer_type,
    const std::string& pointer_value,
    const std::vector<std::pair<std::string, std::string>>& indices,
    bool inbounds) {
    return emit_named_instruction(dest, build_gep_body(pointee_type, pointer_type, pointer_value, indices, inbounds));
}

std::string BasicBlockBuilder::emit_cast(const std::string& opcode,
                                         const std::string& value_type,
                                         const std::string& value,
                                         const std::string& target_type,
                                         std::string_view hint) {
    return emit_instruction(build_cast_body(opcode, value_type, value, target_type), hint);
}

std::string BasicBlockBuilder::emit_cast_into(const std::string& dest,
                                              const std::string& opcode,
                                              const std::string& value_type,
                                              const std::string& value,
                                              const std::string& target_type) {
    return emit_named_instruction(dest, build_cast_body(opcode, value_type, value, target_type));
}

std::string BasicBlockBuilder::emit_extractvalue(const std::string& aggregate_type,
                                                 const std::string& aggregate_value,
                                                 const std::vector<unsigned>& indices,
                                                 std::string_view hint) {
    return emit_instruction(build_extractvalue_body(aggregate_type, aggregate_value, indices), hint);
}

std::string BasicBlockBuilder::emit_extractvalue_into(const std::string& dest,
                                                      const std::string& aggregate_type,
                                                      const std::string& aggregate_value,
                                                      const std::vector<unsigned>& indices) {
    return emit_named_instruction(dest, build_extractvalue_body(aggregate_type, aggregate_value, indices));
}

std::string BasicBlockBuilder::emit_insertvalue(const std::string& aggregate_type,
                                                const std::string& aggregate_value,
                                                const std::string& element_type,
                                                const std::string& element_value,
                                                const std::vector<unsigned>& indices,
                                                std::string_view hint) {
    return emit_instruction(build_insertvalue_body(aggregate_type, aggregate_value, element_type, element_value, indices), hint);
}

std::string BasicBlockBuilder::emit_insertvalue_into(const std::string& dest,
                                                     const std::string& aggregate_type,
                                                     const std::string& aggregate_value,
                                                     const std::string& element_type,
                                                     const std::string& element_value,
                                                     const std::vector<unsigned>& indices) {
    return emit_named_instruction(dest, build_insertvalue_body(aggregate_type, aggregate_value, element_type, element_value, indices));
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
