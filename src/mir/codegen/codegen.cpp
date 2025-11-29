#include "mir/codegen/codegen.hpp"

#include "llvmbuilder/builder.hpp"

#include "semantic/type/type.hpp"

#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mir::codegen {
namespace {

constexpr const char* kPointerType = "ptr";

std::string normalize_function_symbol(const MirFunction& function) {
    std::string name = function.name;
    if (name.empty()) {
        name = "mir_fn_" + std::to_string(function.id);
    }
    if (name.empty()) {
        name = "mir_fn";
    }
    if (name.front() != '@') {
        name.insert(name.begin(), '@');
    }
    return name;
}

class TypeFormatter {
public:
    std::string value_type(semantic::TypeId type) const {
        if (!type) {
            throw std::logic_error("Value type is not resolved");
        }
        return std::visit(
            [this](const auto& node) -> std::string {
                using NodeT = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<NodeT, semantic::PrimitiveKind>) {
                    return primitive_type(node);
                } else if constexpr (std::is_same_v<NodeT, semantic::StructType> ||
                                     std::is_same_v<NodeT, semantic::EnumType>) {
                    throw std::logic_error("Struct and enum values are not supported in LLVM codegen yet");
                } else if constexpr (std::is_same_v<NodeT, semantic::ReferenceType>) {
                    (void)node;
                    return kPointerType;
                } else if constexpr (std::is_same_v<NodeT, semantic::ArrayType>) {
                    std::ostringstream oss;
                    oss << "[" << node.size << " x " << value_type(node.element_type) << "]";
                    return oss.str();
                } else if constexpr (std::is_same_v<NodeT, semantic::UnitType> ||
                                     std::is_same_v<NodeT, semantic::NeverType> ||
                                     std::is_same_v<NodeT, semantic::UnderscoreType>) {
                    throw std::logic_error("Zero-sized values are not representable in SSA form");
                } else {
                    static_assert(!sizeof(NodeT*), "Unhandled type variant");
                }
            },
            type->value);
    }

    std::string storage_type(semantic::TypeId type) const {
        if (!type || is_unit(type) || is_never(type)) {
            return "i8";
        }
        return value_type(type);
    }

    std::string return_type(semantic::TypeId type) const {
        return returns_void(type) ? std::string("void") : value_type(type);
    }

    bool returns_void(semantic::TypeId type) const {
        return !type || is_unit(type) || is_never(type);
    }

private:
    std::string primitive_type(semantic::PrimitiveKind kind) const {
        switch (kind) {
            case semantic::PrimitiveKind::I32:
            case semantic::PrimitiveKind::U32:
                return "i32";
            case semantic::PrimitiveKind::ISIZE:
            case semantic::PrimitiveKind::USIZE:
                return "i64";
            case semantic::PrimitiveKind::BOOL:
                return "i1";
            case semantic::PrimitiveKind::CHAR:
                return "i8";
            case semantic::PrimitiveKind::STRING:
                return kPointerType;
        }
        throw std::logic_error("Unknown primitive type");
    }

    bool is_unit(semantic::TypeId type) const {
        return type && std::holds_alternative<semantic::UnitType>(type->value);
    }

    bool is_never(semantic::TypeId type) const {
        return type && std::holds_alternative<semantic::NeverType>(type->value);
    }
};

struct TypedValue {
    std::string type;
    std::string value;
    semantic::TypeId semantic_type = semantic::invalid_type_id;
};

struct PlaceInfo {
    std::string pointer;
    semantic::TypeId type = semantic::invalid_type_id;
};

enum class BinaryCategory {
    Arithmetic,
    Compare
};

struct BinarySpec {
    BinaryCategory category;
    const char* opcode = nullptr;
    const char* predicate = nullptr;
};

BinarySpec describe_binary(BinaryOpRValue::Kind kind) {
    switch (kind) {
        case BinaryOpRValue::Kind::IAdd:
        case BinaryOpRValue::Kind::UAdd:
            return {BinaryCategory::Arithmetic, "add"};
        case BinaryOpRValue::Kind::ISub:
        case BinaryOpRValue::Kind::USub:
            return {BinaryCategory::Arithmetic, "sub"};
        case BinaryOpRValue::Kind::IMul:
        case BinaryOpRValue::Kind::UMul:
            return {BinaryCategory::Arithmetic, "mul"};
        case BinaryOpRValue::Kind::IDiv:
            return {BinaryCategory::Arithmetic, "sdiv"};
        case BinaryOpRValue::Kind::UDiv:
            return {BinaryCategory::Arithmetic, "udiv"};
        case BinaryOpRValue::Kind::IRem:
            return {BinaryCategory::Arithmetic, "srem"};
        case BinaryOpRValue::Kind::URem:
            return {BinaryCategory::Arithmetic, "urem"};
        case BinaryOpRValue::Kind::BoolAnd:
        case BinaryOpRValue::Kind::BitAnd:
            return {BinaryCategory::Arithmetic, "and"};
        case BinaryOpRValue::Kind::BoolOr:
        case BinaryOpRValue::Kind::BitOr:
            return {BinaryCategory::Arithmetic, "or"};
        case BinaryOpRValue::Kind::BitXor:
            return {BinaryCategory::Arithmetic, "xor"};
        case BinaryOpRValue::Kind::Shl:
            return {BinaryCategory::Arithmetic, "shl"};
        case BinaryOpRValue::Kind::ShrLogical:
            return {BinaryCategory::Arithmetic, "lshr"};
        case BinaryOpRValue::Kind::ShrArithmetic:
            return {BinaryCategory::Arithmetic, "ashr"};
        case BinaryOpRValue::Kind::ICmpEq:
        case BinaryOpRValue::Kind::UCmpEq:
        case BinaryOpRValue::Kind::BoolEq:
            return {BinaryCategory::Compare, nullptr, "eq"};
        case BinaryOpRValue::Kind::ICmpNe:
        case BinaryOpRValue::Kind::UCmpNe:
        case BinaryOpRValue::Kind::BoolNe:
            return {BinaryCategory::Compare, nullptr, "ne"};
        case BinaryOpRValue::Kind::ICmpLt:
            return {BinaryCategory::Compare, nullptr, "slt"};
        case BinaryOpRValue::Kind::ICmpLe:
            return {BinaryCategory::Compare, nullptr, "sle"};
        case BinaryOpRValue::Kind::ICmpGt:
            return {BinaryCategory::Compare, nullptr, "sgt"};
        case BinaryOpRValue::Kind::ICmpGe:
            return {BinaryCategory::Compare, nullptr, "sge"};
        case BinaryOpRValue::Kind::UCmpLt:
            return {BinaryCategory::Compare, nullptr, "ult"};
        case BinaryOpRValue::Kind::UCmpLe:
            return {BinaryCategory::Compare, nullptr, "ule"};
        case BinaryOpRValue::Kind::UCmpGt:
            return {BinaryCategory::Compare, nullptr, "ugt"};
        case BinaryOpRValue::Kind::UCmpGe:
            return {BinaryCategory::Compare, nullptr, "uge"};
    }
    throw std::logic_error("Binary operator not supported in LLVM codegen");
}

class FunctionEmitter {
public:
    FunctionEmitter(const MirFunction& function,
                    const std::unordered_map<FunctionId, const MirFunction*>& functions_by_id,
                    const std::unordered_map<FunctionId, std::string>& symbols,
                    llvmbuilder::ModuleBuilder& module,
                    const TypeFormatter& types)
        : function_(function),
          functions_by_id_(functions_by_id),
          symbols_(symbols),
          module_(module),
          types_(types) {}

    void emit() {
        build_prototype();
        allocate_locals();
        store_parameters();
        emit_blocks();
    }

private:
    void build_prototype() {
        symbol_ = symbols_.at(function_.id);
        std::vector<llvmbuilder::FunctionParameter> params;
        params.reserve(function_.params.size());
        for (const auto& param : function_.params) {
            llvmbuilder::FunctionParameter fb_param;
            fb_param.type = types_.value_type(param.type);
            fb_param.name = param.name;
            params.push_back(std::move(fb_param));
        }

        returns_void_ = types_.returns_void(function_.return_type);
        fn_builder_ = &module_.add_function(symbol_, types_.return_type(function_.return_type), std::move(params));

        std::size_t block_count = function_.basic_blocks.size();
        blocks_.resize(block_count, nullptr);
        block_labels_.resize(block_count);
        temp_values_.resize(function_.temp_types.size());
        local_slots_.resize(function_.locals.size());

        if (block_count == 0) {
            throw std::logic_error("MIR function is missing basic blocks");
        }

        entry_block_ = &fn_builder_->entry_block();
        blocks_[function_.start_block] = entry_block_;
        block_labels_[function_.start_block] = entry_block_->label();

        for (std::size_t block_id = 0; block_id < block_count; ++block_id) {
            if (blocks_[block_id]) {
                continue;
            }
            auto label_hint = "bb" + std::to_string(block_id);
            auto& block = fn_builder_->create_block(label_hint);
            blocks_[block_id] = &block;
            block_labels_[block_id] = block.label();
        }
    }

    void allocate_locals() {
        if (!entry_block_) {
            return;
        }
        for (std::size_t local_id = 0; local_id < function_.locals.size(); ++local_id) {
            const auto& local = function_.locals[local_id];
            std::string hint = local.debug_name.empty() ? "local.slot" : local.debug_name + ".slot";
            local_slots_[local_id] = entry_block_->emit_alloca(types_.storage_type(local.type), std::nullopt, std::nullopt, hint);
        }
    }

    void store_parameters() {
        if (function_.params.empty()) {
            return;
        }
        const auto& params = fn_builder_->parameters();
        if (params.size() != function_.params.size()) {
            throw std::logic_error("LLVM function parameter list mismatch");
        }
        for (std::size_t i = 0; i < function_.params.size(); ++i) {
            const auto& param = function_.params[i];
            if (param.local >= local_slots_.size()) {
                throw std::logic_error("Parameter local index out of range");
            }
            entry_block_->emit_store(params[i].type, params[i].name, kPointerType, local_slots_[param.local]);
        }
    }

    void emit_blocks() {
        for (std::size_t block_id = 0; block_id < function_.basic_blocks.size(); ++block_id) {
            emit_block(static_cast<BasicBlockId>(block_id));
        }
    }

    void emit_block(BasicBlockId id) {
        auto* builder = blocks_.at(id);
        if (!builder) {
            throw std::logic_error("Missing LLVM block for MIR block");
        }
        const auto& block = function_.basic_blocks.at(id);
        emit_phi_nodes(*builder, block);
        for (const auto& stmt : block.statements) {
            emit_statement(*builder, stmt);
        }
        emit_terminator(id, *builder, block.terminator);
    }

    void emit_phi_nodes(llvmbuilder::BasicBlockBuilder& builder, const BasicBlock& block) {
        for (const auto& phi : block.phis) {
            if (phi.dest >= temp_values_.size()) {
                throw std::logic_error("Phi destination temp out of range");
            }
            std::vector<std::pair<std::string, std::string>> incomings;
            incomings.reserve(phi.incoming.size());
            auto type = types_.value_type(function_.temp_types.at(phi.dest));
            for (const auto& incoming : phi.incoming) {
                std::string value = ensure_temp_value(incoming.value);
                incomings.emplace_back(value, block_labels_.at(incoming.block));
            }
            temp_values_[phi.dest] = builder.emit_phi(type, incomings, temp_hint(phi.dest));
        }
    }

    void emit_statement(llvmbuilder::BasicBlockBuilder& builder, const Statement& statement) {
        std::visit(
            [&](const auto& stmt) {
                using StmtT = std::decay_t<decltype(stmt)>;
                if constexpr (std::is_same_v<StmtT, DefineStatement>) {
                    emit_define(builder, stmt);
                } else if constexpr (std::is_same_v<StmtT, LoadStatement>) {
                    emit_load(builder, stmt);
                } else if constexpr (std::is_same_v<StmtT, AssignStatement>) {
                    emit_assign(builder, stmt);
                } else if constexpr (std::is_same_v<StmtT, CallStatement>) {
                    emit_call(builder, stmt);
                } else {
                    throw std::logic_error("Unsupported MIR statement in LLVM codegen");
                }
            },
            statement.value);
    }

    void emit_define(llvmbuilder::BasicBlockBuilder& builder, const DefineStatement& stmt) {
        if (stmt.dest >= temp_values_.size()) {
            throw std::logic_error("Define destination temp out of range");
        }
        if (const auto* constant = std::get_if<ConstantRValue>(&stmt.rvalue.value)) {
            temp_values_[stmt.dest] = format_constant_value(constant->constant);
            return;
        }

        if (const auto* binary = std::get_if<BinaryOpRValue>(&stmt.rvalue.value)) {
            auto lhs = materialize_operand(binary->lhs);
            auto rhs = materialize_operand(binary->rhs);
            const BinarySpec spec = describe_binary(binary->kind);
            if (spec.category == BinaryCategory::Arithmetic) {
                temp_values_[stmt.dest] = builder.emit_binary(spec.opcode, lhs.type, lhs.value, rhs.value, temp_hint(stmt.dest));
                return;
            }
            temp_values_[stmt.dest] = builder.emit_icmp(spec.predicate, lhs.type, lhs.value, rhs.value, temp_hint(stmt.dest));
            return;
        }

        throw std::logic_error("Unsupported rvalue in LLVM codegen");
    }

    void emit_load(llvmbuilder::BasicBlockBuilder& builder, const LoadStatement& stmt) {
        if (stmt.dest >= temp_values_.size()) {
            throw std::logic_error("Load destination temp out of range");
        }
        PlaceInfo place = materialize_place(stmt.src);
        auto type_id = function_.temp_types.at(stmt.dest);
        auto type = types_.value_type(type_id);
        temp_values_[stmt.dest] = builder.emit_load(type, kPointerType, place.pointer, std::nullopt, temp_hint(stmt.dest));
    }

    void emit_assign(llvmbuilder::BasicBlockBuilder& builder, const AssignStatement& stmt) {
        PlaceInfo place = materialize_place(stmt.dest);
        auto value = materialize_operand(stmt.src, place.type);
        builder.emit_store(value.type, value.value, kPointerType, place.pointer);
    }

    void emit_call(llvmbuilder::BasicBlockBuilder& builder, const CallStatement& stmt) {
        auto callee_it = symbols_.find(stmt.function);
        if (callee_it == symbols_.end()) {
            throw std::logic_error("Call target not found in MIR module");
        }
        const MirFunction* callee = nullptr;
        auto fn_it = functions_by_id_.find(stmt.function);
        if (fn_it != functions_by_id_.end()) {
            callee = fn_it->second;
        }
        const auto& callee_symbol = callee_it->second;
        std::string return_type = callee ? types_.return_type(callee->return_type) : std::string("void");

        std::vector<std::pair<std::string, std::string>> args;
        args.reserve(stmt.args.size());
        for (const auto& arg : stmt.args) {
            auto lowered = materialize_operand(arg);
            args.emplace_back(lowered.type, lowered.value);
        }

        auto result = builder.emit_call(return_type, callee_symbol, args, "call");
        if (stmt.dest) {
            if (!result) {
                throw std::logic_error("Call produced no value but MIR expected one");
            }
            if (*stmt.dest >= temp_values_.size()) {
                throw std::logic_error("Call destination temp out of range");
            }
            temp_values_[*stmt.dest] = *result;
        }
    }

    void emit_terminator(BasicBlockId id, llvmbuilder::BasicBlockBuilder& builder, const Terminator& terminator) {
        std::visit(
            [&](const auto& term) {
                using TermT = std::decay_t<decltype(term)>;
                if constexpr (std::is_same_v<TermT, GotoTerminator>) {
                    builder.emit_br(block_labels_.at(term.target));
                } else if constexpr (std::is_same_v<TermT, ReturnTerminator>) {
                    emit_return(builder, term);
                } else if constexpr (std::is_same_v<TermT, UnreachableTerminator>) {
                    builder.emit_unreachable();
                } else if constexpr (std::is_same_v<TermT, SwitchIntTerminator>) {
                    emit_switch(builder, term);
                } else {
                    throw std::logic_error("Unsupported terminator in LLVM codegen");
                }
            },
            terminator.value);
        (void)id;
    }

    void emit_return(llvmbuilder::BasicBlockBuilder& builder, const ReturnTerminator& term) {
        if (returns_void_) {
            builder.emit_ret_void();
            return;
        }
        if (!term.value) {
            throw std::logic_error("Non-void function missing return value");
        }
        auto value = materialize_operand(*term.value, function_.return_type);
        builder.emit_ret(value.type, value.value);
    }

    void emit_switch(llvmbuilder::BasicBlockBuilder& builder, const SwitchIntTerminator& term) {
        auto discr = materialize_operand(term.discriminant);
        std::vector<std::pair<std::string, std::string>> cases;
        cases.reserve(term.targets.size());
        for (const auto& target : term.targets) {
            std::string literal = format_constant_literal(target.match_value, discr.semantic_type);
            cases.emplace_back(discr.type + " " + literal, block_labels_.at(target.block));
        }
        builder.emit_switch(discr.type, discr.value, block_labels_.at(term.otherwise), cases);
    }

    TypedValue materialize_operand(const Operand& operand, semantic::TypeId expected_type = semantic::invalid_type_id) {
        TypedValue result;
        if (const auto* temp = std::get_if<TempId>(&operand.value)) {
            if (*temp >= temp_values_.size()) {
                throw std::logic_error("Temp operand out of range");
            }
            result.semantic_type = function_.temp_types.at(*temp);
            result.type = types_.value_type(result.semantic_type);
            result.value = ensure_temp_value(*temp);
            return result;
        }
        const auto& constant = std::get<Constant>(operand.value);
        semantic::TypeId type_id = constant.type != semantic::invalid_type_id ? constant.type : expected_type;
        if (type_id == semantic::invalid_type_id) {
            throw std::logic_error("Constant operand missing type information");
        }
        result.semantic_type = type_id;
        result.type = types_.value_type(type_id);
        result.value = format_constant_literal(constant, type_id);
        return result;
    }

    PlaceInfo materialize_place(const Place& place) {
        if (!place.projections.empty()) {
            throw std::logic_error("Place projections are not supported yet in LLVM codegen");
        }
        PlaceInfo info;
        if (const auto* local = std::get_if<LocalPlace>(&place.base)) {
            if (local->id >= local_slots_.size()) {
                throw std::logic_error("Local place id out of range");
            }
            info.pointer = local_slots_[local->id];
            info.type = function_.locals.at(local->id).type;
            return info;
        }
        throw std::logic_error("Only local places are supported in LLVM codegen");
    }

    std::string ensure_temp_value(TempId id) const {
        const auto& value = temp_values_.at(id);
        if (value.empty()) {
            throw std::logic_error("Temp used before definition during LLVM codegen");
        }
        return value;
    }

    std::string temp_hint(TempId id) const {
        return "t" + std::to_string(id);
    }

    std::string format_constant_literal(const Constant& constant, semantic::TypeId fallback_type) const {
        if (auto bool_const = std::get_if<BoolConstant>(&constant.value)) {
            return bool_const->value ? "1" : "0";
        }
        if (auto int_const = std::get_if<IntConstant>(&constant.value)) {
            std::ostringstream oss;
            if (int_const->is_negative && int_const->value != 0) {
                oss << '-';
            }
            oss << int_const->value;
            return oss.str();
        }
        if (auto char_const = std::get_if<CharConstant>(&constant.value)) {
            return std::to_string(static_cast<unsigned>(static_cast<unsigned char>(char_const->value)));
        }
        if (std::holds_alternative<UnitConstant>(constant.value)) {
            (void)fallback_type;
            return "0";
        }
        throw std::logic_error("Constant kind not supported in LLVM codegen");
    }

    std::string format_constant_value(const Constant& constant) const {
        return format_constant_literal(constant, constant.type);
    }

    const MirFunction& function_;
    const std::unordered_map<FunctionId, const MirFunction*>& functions_by_id_;
    const std::unordered_map<FunctionId, std::string>& symbols_;
    llvmbuilder::ModuleBuilder& module_;
    const TypeFormatter& types_;
    llvmbuilder::FunctionBuilder* fn_builder_ = nullptr;
    llvmbuilder::BasicBlockBuilder* entry_block_ = nullptr;
    bool returns_void_ = false;
    std::string symbol_;
    std::vector<std::string> temp_values_;
    std::vector<std::string> local_slots_;
    std::vector<llvmbuilder::BasicBlockBuilder*> blocks_;
    std::vector<std::string> block_labels_;
};

class ModuleGenerator {
public:
    ModuleGenerator(const MirModule& module, const CodegenOptions& options)
        : module_(module), options_(options) {
        index_functions();
    }

    std::string run() {
        llvmbuilder::ModuleBuilder builder(options_.module_id);
        if (!options_.data_layout.empty()) {
            builder.set_data_layout(options_.data_layout);
        }
        if (!options_.target_triple.empty()) {
            builder.set_target_triple(options_.target_triple);
        }
        for (const auto& function : module_.functions) {
            FunctionEmitter emitter(function, functions_by_id_, symbols_, builder, types_);
            emitter.emit();
        }
        return builder.str();
    }

private:
    void index_functions() {
        for (const auto& function : module_.functions) {
            functions_by_id_.emplace(function.id, &function);
            symbols_.emplace(function.id, normalize_function_symbol(function));
        }
    }

    const MirModule& module_;
    CodegenOptions options_;
    TypeFormatter types_;
    std::unordered_map<FunctionId, const MirFunction*> functions_by_id_;
    std::unordered_map<FunctionId, std::string> symbols_;
};

} // namespace

std::string emit_llvm_ir(const MirModule& module, const CodegenOptions& options) {
    ModuleGenerator generator(module, options);
    return generator.run();
}

} // namespace mir::codegen
