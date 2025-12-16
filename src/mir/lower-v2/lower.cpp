#include "mir/lower-v2/lower.hpp"

#include "mir/lower-v2/lower_internal.hpp"
#include "mir/lower/sig_builder.hpp"
#include "semantic/expr_info_helpers.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include "semantic/symbol/predefined.hpp"

#include <optional>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

struct FunctionDescriptor {
    std::variant<const hir::Function*, const hir::Method*> function_or_method;
    const void* key = nullptr;
    std::string name;
    mir::FunctionId id = 0;
    bool is_external = false;
    mir::detail::ProtoSig proto_sig;
};

void add_function_descriptor(const hir::Function& function,
                             const std::string& scope,
                             std::vector<FunctionDescriptor>& out) {
    FunctionDescriptor descriptor;
    descriptor.function_or_method = &function;
    descriptor.key = &function;
    descriptor.name = mir::detail::derive_function_name(function, scope);
    descriptor.is_external = !function.body.has_value();
    out.push_back(std::move(descriptor));
}

void add_method_descriptor(const hir::Method& method,
                           const std::string& scope,
                           std::vector<FunctionDescriptor>& out) {
    FunctionDescriptor descriptor;
    descriptor.function_or_method = &method;
    descriptor.key = &method;
    descriptor.name = mir::detail::derive_method_name(method, scope);
    descriptor.is_external = !method.body.has_value();
    out.push_back(std::move(descriptor));
}

std::vector<FunctionDescriptor> collect_function_descriptors(const hir::Program& program) {
    std::vector<FunctionDescriptor> descriptors;

    // Predefined scope first (builtins)
    const semantic::Scope& predefined = semantic::get_predefined_scope();
    for (const auto& [name, symbol] : predefined.get_items_local()) {
        if (auto* fn_ptr = std::get_if<hir::Function*>(&symbol)) {
            FunctionDescriptor descriptor;
            descriptor.function_or_method = *fn_ptr;
            descriptor.key = *fn_ptr;
            descriptor.name = std::string(name);
            descriptor.is_external = true;
            descriptors.push_back(std::move(descriptor));
        }
    }

    struct Collector : hir::HirVisitorBase<Collector> {
        const hir::Program* program;
        std::vector<FunctionDescriptor>& out;
        std::string current_scope;

        using Base = hir::HirVisitorBase<Collector>;
        using Base::visit;
        using Base::visit_block;

        Collector(const hir::Program& p, std::vector<FunctionDescriptor>& out)
            : program(&p), out(out) {}

        void visit_program(hir::Program& p) {
            current_scope.clear();
            Base::visit_program(p);
        }

        void visit(hir::Function& f) {
            add_function_descriptor(f, current_scope, out);
            Base::visit(f);
        }

        void visit(hir::Impl& impl) {
            mir::TypeId impl_type = hir::helper::get_resolved_type(impl.for_type);
            std::string saved_scope = current_scope;
            current_scope = mir::detail::type_name(impl_type);
            Base::visit(impl);
            current_scope = std::move(saved_scope);
        }

        void visit(hir::Method& m) {
            add_method_descriptor(m, current_scope, out);
            Base::visit(m);
        }

        void visit_block(hir::Block& block) { Base::visit_block(block); }
    };

    Collector collector{program, descriptors};
    collector.visit_program(const_cast<hir::Program&>(program));

    return descriptors;
}

mir::MirFunction lower_descriptor(const FunctionDescriptor& descriptor,
                                  const std::unordered_map<const void*, mir::FunctionRef>& fn_map) {
    return std::visit(
        [&fn_map, &descriptor](const auto* fn_ptr) -> mir::MirFunction {
            mir::lower_v2::detail::FunctionLowerer lowerer(*fn_ptr, fn_map, descriptor.id, descriptor.name);
            return lowerer.lower();
        },
        descriptor.function_or_method);
}

} // namespace

namespace mir::lower_v2 {

MirModule lower_program(const hir::Program& program) {
    MirModule module;
    auto descriptors = collect_function_descriptors(program);

    // Assign IDs
    FunctionId next_fn_id = 0;
    ExternalFunction::Id next_ext_id = 0;
    std::unordered_map<const void*, ExternalFunction*> external_lookup;
    for (auto& desc : descriptors) {
        desc.id = next_fn_id++;
        if (desc.is_external) {
            ExternalFunction ext;
            ext.id = next_ext_id++;
            ext.name = desc.name;
            mir::detail::SigBuilder builder(desc.function_or_method);
            auto proto = builder.build_proto_sig();
            ext.sig.return_desc = proto.return_desc;
            for (const auto& param : proto.proto_params) {
                MirParam mir_param;
                mir_param.type = param.type;
                mir_param.debug_name = param.debug_name;
                mir_param.local = 0;
                ext.sig.params.push_back(std::move(mir_param));
            }
            mir::detail::populate_abi_params(ext.sig);
            module.external_functions.push_back(ext);
            external_lookup.emplace(desc.key, &module.external_functions.back());
        }
    }

    // Prepare placeholders for internal functions
    for (const auto& desc : descriptors) {
        if (!desc.is_external) {
            module.functions.emplace_back();
        }
    }

    // Build function map
    std::unordered_map<const void*, mir::FunctionRef> fn_map;
    std::size_t internal_index = 0;
    for (const auto& desc : descriptors) {
        if (desc.is_external) {
            fn_map.emplace(desc.key, external_lookup.at(desc.key));
        } else {
            fn_map.emplace(desc.key, &module.functions[internal_index++]);
        }
    }

    // Lower internal functions
    internal_index = 0;
    for (const auto& desc : descriptors) {
        if (desc.is_external) {
            continue;
        }
        MirFunction lowered = lower_descriptor(desc, fn_map);
        module.functions[internal_index++] = std::move(lowered);
    }

    return module;
}

} // namespace mir::lower_v2

namespace mir::lower_v2::detail {

FunctionLowerer::FunctionLowerer(const hir::Function& function,
                                 const std::unordered_map<const void*, mir::FunctionRef>& fn_map,
                                 FunctionId id,
                                 std::string name)
    : function_kind(FunctionKind::Function), hir_function(&function), function_map(fn_map) {
    initialize(id, std::move(name));
}

FunctionLowerer::FunctionLowerer(const hir::Method& method,
                                 const std::unordered_map<const void*, mir::FunctionRef>& fn_map,
                                 FunctionId id,
                                 std::string name)
    : function_kind(FunctionKind::Method), hir_method(&method), function_map(fn_map) {
    initialize(id, std::move(name));
}

MirFunction FunctionLowerer::lower() {
    const hir::Block* body = get_body();
    if (!body) {
        throw std::logic_error("function missing body during MIR lowering v2");
    }
    lower_block(*body);
    return std::move(mir_function);
}

void FunctionLowerer::initialize(FunctionId id, std::string name) {
    mir_function.id = id;
    mir_function.name = std::move(name);

    mir::detail::SigBuilder builder(function_kind == FunctionKind::Function
                                        ? mir::detail::SigBuilder::FnOrMethod{hir_function}
                                        : mir::detail::SigBuilder::FnOrMethod{hir_method});
    mir::detail::ProtoSig proto_sig = builder.build_proto_sig();
    mir_function.sig.return_desc = proto_sig.return_desc;

    init_locals();
    collect_parameters();
    mir::detail::populate_abi_params(mir_function.sig);
    return_plan = build_return_plan();
    apply_abi_aliasing(return_plan);

    BasicBlockId entry = create_block();
    current_block = entry;
    mir_function.start_block = entry;
}

const hir::Block* FunctionLowerer::get_body() const {
    if (function_kind == FunctionKind::Function) {
        return (hir_function && hir_function->body) ? hir_function->body->block.get() : nullptr;
    }
    return (hir_method && hir_method->body) ? hir_method->body->block.get() : nullptr;
}

const std::vector<std::unique_ptr<hir::Local>>& FunctionLowerer::get_locals_vector() const {
    if (function_kind == FunctionKind::Function) {
        return hir_function->body->locals;
    }
    return hir_method->body->locals;
}

TypeId FunctionLowerer::resolve_return_type() const {
    const auto& annotation = (function_kind == FunctionKind::Function) ? hir_function->sig.return_type
                                                                        : hir_method->sig.return_type;
    if (annotation) {
        return hir::helper::get_resolved_type(*annotation);
    }
    return mir::detail::get_unit_type();
}

void FunctionLowerer::init_locals() {
    auto register_local = [this](const hir::Local* local_ptr) {
        if (!local_ptr) {
            return;
        }
        if (!local_ptr->type_annotation) {
            throw std::logic_error("Local missing resolved type during MIR lowering v2");
        }
        TypeId type = hir::helper::get_resolved_type(*local_ptr->type_annotation);
        TypeId normalized = mir::detail::canonicalize_type_for_mir(type);
        LocalId id = static_cast<LocalId>(mir_function.locals.size());
        local_ids.emplace(local_ptr, id);

        LocalInfo info;
        info.type = normalized;
        info.debug_name = local_ptr->name.name;
        mir_function.locals.push_back(std::move(info));
    };

    if (function_kind == FunctionKind::Method && hir_method && hir_method->body && hir_method->body->self_local) {
        register_local(hir_method->body->self_local.get());
    }

    for (const auto& local_ptr : get_locals_vector()) {
        if (local_ptr) {
            register_local(local_ptr.get());
        }
    }
}

const hir::Local* FunctionLowerer::pick_nrvo_local() const {
    if (!mir::is_indirect_sret(mir_function.sig.return_desc)) {
        return nullptr;
    }

    TypeId ret_ty = mir::return_type(mir_function.sig.return_desc);
    if (ret_ty == invalid_type_id) {
        return nullptr;
    }

    auto matches_return_type = [&](const hir::Local* local) -> bool {
        if (!local || !local->type_annotation) {
            return false;
        }
        TypeId ty = hir::helper::get_resolved_type(*local->type_annotation);
        return mir::detail::canonicalize_type_for_mir(ty) == ret_ty;
    };

    const auto& locals = get_locals_vector();
    size_t start_idx = mir_function.sig.params.size();
    for (size_t i = start_idx; i < locals.size(); ++i) {
        const hir::Local* local = locals[i].get();
        if (matches_return_type(local)) {
            return local;
        }
    }

    return nullptr;
}

ReturnStoragePlan FunctionLowerer::build_return_plan() {
    ReturnStoragePlan plan;
    if (!mir::is_indirect_sret(mir_function.sig.return_desc)) {
        plan.is_sret = false;
        plan.ret_type = mir::return_type(mir_function.sig.return_desc);
        return plan;
    }

    plan.is_sret = true;
    plan.ret_type = mir::return_type(mir_function.sig.return_desc);

    AbiParamIndex sret_index = std::numeric_limits<AbiParamIndex>::max();
    for (AbiParamIndex idx = 0; idx < mir_function.sig.abi_params.size(); ++idx) {
        if (std::holds_alternative<AbiParamSRet>(mir_function.sig.abi_params[idx].kind)) {
            sret_index = idx;
            break;
        }
    }
    if (sret_index == std::numeric_limits<AbiParamIndex>::max()) {
        throw std::logic_error("SRET required but no AbiParamSRet found in v2 lowering");
    }
    plan.sret_abi_index = sret_index;

    const hir::Local* nrvo_local = pick_nrvo_local();
    if (nrvo_local) {
        plan.return_slot_local = require_local_id(nrvo_local);
        plan.uses_nrvo_local = true;
        return plan;
    }

    LocalId synthetic_local_id = static_cast<LocalId>(mir_function.locals.size());
    LocalInfo return_info;
    return_info.type = plan.ret_type;
    return_info.debug_name = "<return>";
    mir_function.locals.push_back(std::move(return_info));
    plan.return_slot_local = synthetic_local_id;
    plan.uses_nrvo_local = false;
    return plan;
}

void FunctionLowerer::apply_abi_aliasing(const ReturnStoragePlan& plan) {
    for (AbiParamIndex abi_idx = 0; abi_idx < mir_function.sig.abi_params.size(); ++abi_idx) {
        const AbiParam& abi_param = mir_function.sig.abi_params[abi_idx];
        if (std::holds_alternative<AbiParamSRet>(abi_param.kind)) {
            if (!plan.is_sret) {
                throw std::logic_error("apply_abi_aliasing: sret param without sret plan");
            }
            LocalId sret_alias_local = plan.return_slot_local;
            mir_function.locals[sret_alias_local].is_alias = true;
            mir_function.locals[sret_alias_local].alias_target = abi_idx;
            continue;
        }
        if (std::holds_alternative<AbiParamByValCallerCopy>(abi_param.kind)) {
            if (abi_param.param_index) {
                ParamIndex param_idx = *abi_param.param_index;
                if (param_idx < mir_function.sig.params.size()) {
                    const MirParam& param = mir_function.sig.params[param_idx];
                    LocalId local_id = param.local;
                    mir_function.locals[local_id].is_alias = true;
                    mir_function.locals[local_id].alias_target = abi_idx;
                }
            }
        }
    }
}

mir::FunctionRef FunctionLowerer::lookup_function(const void* key) const {
    auto it = function_map.find(key);
    if (it == function_map.end()) {
        throw std::logic_error("Call target not registered during MIR lowering v2");
    }
    return it->second;
}

const MirFunctionSig& FunctionLowerer::get_callee_sig(mir::FunctionRef target) const {
    if (auto* internal = std::get_if<MirFunction*>(&target)) {
        return (*internal)->sig;
    }
    if (auto* external = std::get_if<ExternalFunction*>(&target)) {
        return (*external)->sig;
    }
    throw std::logic_error("Invalid FunctionRef in get_callee_sig");
}

BasicBlockId FunctionLowerer::create_block() {
    BasicBlockId id = static_cast<BasicBlockId>(mir_function.basic_blocks.size());
    mir_function.basic_blocks.emplace_back();
    block_terminated.push_back(false);
    return id;
}

bool FunctionLowerer::block_is_terminated(BasicBlockId id) const {
    return id < block_terminated.size() && block_terminated[id];
}

BasicBlockId FunctionLowerer::current_block_id() const {
    if (!current_block) {
        throw std::logic_error("No active block during MIR lowering v2");
    }
    return *current_block;
}

TempId FunctionLowerer::allocate_temp(TypeId type) {
    if (type == invalid_type_id) {
        throw std::logic_error("Temporary missing resolved type during MIR lowering v2");
    }
    TypeId normalized = mir::detail::canonicalize_type_for_mir(type);
    if (mir::detail::is_unit_type(normalized)) {
        throw std::logic_error("Unit temporaries should not be allocated");
    }
    TempId id = static_cast<TempId>(mir_function.temp_types.size());
    mir_function.temp_types.push_back(normalized);
    return id;
}

LocalId FunctionLowerer::create_synthetic_local(TypeId type, std::string debug_name) {
    LocalInfo info;
    info.type = mir::detail::canonicalize_type_for_mir(type);
    if (debug_name.empty()) {
        std::ostringstream oss;
        oss << "<tmp" << synthetic_local_counter++ << ">";
        info.debug_name = oss.str();
    } else {
        info.debug_name = std::move(debug_name);
    }
    LocalId id = static_cast<LocalId>(mir_function.locals.size());
    mir_function.locals.push_back(std::move(info));
    return id;
}

void FunctionLowerer::append_statement(Statement statement) {
    if (!current_block) {
        return;
    }
    BasicBlockId block_id = *current_block;
    if (block_is_terminated(block_id)) {
        throw std::logic_error("Cannot append statement to terminated block in v2 lowering");
    }
    mir_function.basic_blocks[block_id].statements.push_back(std::move(statement));
}

void FunctionLowerer::set_terminator(BasicBlockId id, Terminator terminator) {
    if (block_is_terminated(id)) {
        throw std::logic_error("Terminator already set for block");
    }
    mir_function.basic_blocks[id].terminator = std::move(terminator);
    block_terminated[id] = true;
}

void FunctionLowerer::terminate_current_block(Terminator terminator) {
    if (!current_block) {
        return;
    }
    set_terminator(*current_block, std::move(terminator));
    current_block.reset();
}

void FunctionLowerer::add_goto_from_current(BasicBlockId target) {
    if (!current_block) {
        return;
    }
    if (block_is_terminated(*current_block)) {
        return;
    }
    GotoTerminator go{target};
    terminate_current_block(Terminator{std::move(go)});
}

void FunctionLowerer::switch_to_block(BasicBlockId id) { current_block = id; }

void FunctionLowerer::branch_on_bool(const Operand& condition,
                                     BasicBlockId true_block,
                                     BasicBlockId false_block) {
    if (!current_block) {
        return;
    }
    SwitchIntTerminator term;
    term.discriminant = condition;
    term.targets.push_back(SwitchIntTarget{mir::detail::make_bool_constant(true), true_block});
    term.otherwise = false_block;
    terminate_current_block(Terminator{std::move(term)});
}

Operand FunctionLowerer::make_temp_operand(TempId temp) {
    Operand operand;
    operand.value = temp;
    return operand;
}

void FunctionLowerer::emit_return(std::optional<Operand> value) {
    const ReturnDesc& ret_desc = mir_function.sig.return_desc;
    if (mir::is_indirect_sret(ret_desc)) {
        if (value) {
            throw std::logic_error("sret function should not return value operand in v2 lowering");
        }
    } else if (!value && !mir::is_void_semantic(ret_desc)) {
        throw std::logic_error("missing return value for non-void function in v2 lowering");
    }
    if (!current_block) {
        return;
    }
    ReturnTerminator ret{std::move(value)};
    terminate_current_block(Terminator{std::move(ret)});
}

Operand FunctionLowerer::load_place_value(Place place, TypeId type) {
    TempId dest = allocate_temp(type);
    LoadStatement load{.dest = dest, .src = std::move(place)};
    Statement stmt;
    stmt.value = std::move(load);
    append_statement(std::move(stmt));
    return make_temp_operand(dest);
}

void FunctionLowerer::emit_assign(Place dest, ValueSource src) {
    AssignStatement assign{.dest = std::move(dest), .src = std::move(src)};
    Statement stmt;
    stmt.value = std::move(assign);
    append_statement(std::move(stmt));
}

Place FunctionLowerer::make_local_place(const hir::Local* local) const {
    return make_local_place(require_local_id(local));
}

Place FunctionLowerer::make_local_place(LocalId local_id) const {
    Place p;
    p.base = LocalPlace{local_id};
    return p;
}

Place FunctionLowerer::project_field(const Place& base, std::size_t index) const {
    Place projected = base;
    projected.projections.push_back(FieldProjection{index});
    return projected;
}

Place FunctionLowerer::project_index(const Place& base, std::size_t index) const {
    Place projected = base;
    IntConstant ic;
    ic.value = index;
    ic.is_negative = false;
    ic.is_signed = false;
    Constant c;
    c.type = type::get_typeID(type::Type{type::PrimitiveKind::USIZE});
    c.value = ic;
    Operand idx_operand = mir::detail::make_constant_operand(c);
    projected.projections.push_back(IndexProjection{std::move(idx_operand)});
    return projected;
}

LocalId FunctionLowerer::require_local_id(const hir::Local* local) const {
    auto it = local_ids.find(local);
    if (it == local_ids.end()) {
        throw std::logic_error("Local not registered during MIR lowering v2");
    }
    return it->second;
}

Operand FunctionLowerer::expect_operand(const LowerResult& result,
                                        const semantic::ExprInfo& info) {
    return result.as_operand(*this, info.type);
}

void FunctionLowerer::collect_parameters() {
    if (function_kind == FunctionKind::Method) {
        append_self_parameter();
        append_explicit_parameters(hir_method->sig.params, hir_method->sig.param_type_annotations);
    } else {
        append_explicit_parameters(hir_function->sig.params, hir_function->sig.param_type_annotations);
    }
}

void FunctionLowerer::append_self_parameter() {
    if (function_kind != FunctionKind::Method) {
        throw std::logic_error("append_self_parameter called for non-method");
    }
    if (!hir_method || !hir_method->body || !hir_method->body->self_local) {
        return;
    }
    if (!hir_method->body->self_local->type_annotation) {
        throw std::logic_error("Method self parameter missing resolved type during MIR lowering v2");
    }
    TypeId self_type = hir::helper::get_resolved_type(*hir_method->body->self_local->type_annotation);
    append_parameter(hir_method->body->self_local.get(), self_type);
}

void FunctionLowerer::append_explicit_parameters(const std::vector<std::unique_ptr<hir::Pattern>>& params,
                                                 const std::vector<hir::TypeAnnotation>& annotations) {
    if (params.size() != annotations.size()) {
        throw std::logic_error("Parameter/type annotation mismatch during MIR lowering v2");
    }
    for (std::size_t i = 0; i < params.size(); ++i) {
        const auto& param = params[i];
        if (!param) {
            continue;
        }
        const auto& annotation = annotations[i];
        TypeId param_type = hir::helper::get_resolved_type(annotation);
        append_parameter(resolve_pattern_local(*param), param_type);
    }
}

void FunctionLowerer::append_parameter(const hir::Local* local, TypeId type) {
    if (!local) {
        throw std::logic_error("Parameter pattern did not resolve to a Local during MIR lowering v2");
    }
    if (type == invalid_type_id) {
        throw std::logic_error("Parameter missing resolved type during MIR lowering v2");
    }
    TypeId normalized = mir::detail::canonicalize_type_for_mir(type);
    LocalId local_id = require_local_id(local);

    MirParam param;
    param.local = local_id;
    param.type = normalized;
    param.debug_name = local->name.name;
    mir_function.sig.params.push_back(std::move(param));
}

const hir::Local* FunctionLowerer::resolve_pattern_local(const hir::Pattern& pattern) const {
    if (const auto* binding = std::get_if<hir::BindingDef>(&pattern.value)) {
        if (auto* local_ptr = std::get_if<hir::Local*>(&binding->local)) {
            return *local_ptr;
        }
        throw std::logic_error("Binding definition missing resolved Local during MIR lowering v2");
    }
    if (const auto* reference = std::get_if<hir::ReferencePattern>(&pattern.value)) {
        if (!reference->subpattern) {
            throw std::logic_error("Reference pattern missing subpattern during MIR lowering v2");
        }
        return resolve_pattern_local(*reference->subpattern);
    }
    throw std::logic_error("Unsupported pattern variant in parameter lowering v2");
}

bool FunctionLowerer::is_reachable() const { return current_block.has_value(); }

void FunctionLowerer::require_reachable(const char* context) const {
    if (!is_reachable()) {
        throw std::logic_error(std::string("Unreachable code encountered in ") + context);
    }
}

FunctionLowerer::LoopContext& FunctionLowerer::push_loop_context(const void* key,
                                                                 BasicBlockId continue_block,
                                                                 BasicBlockId break_block,
                                                                 std::optional<TypeId> break_type) {
    LoopContext ctx;
    ctx.continue_block = continue_block;
    ctx.break_block = break_block;
    if (break_type) {
        TypeId normalized = mir::detail::canonicalize_type_for_mir(*break_type);
        ctx.break_type = normalized;
        if (!mir::detail::is_unit_type(normalized) && !mir::detail::is_never_type(normalized)) {
            ctx.break_result = allocate_temp(normalized);
        }
    } else {
        ctx.break_type = std::nullopt;
    }
    loop_stack.emplace_back(key, std::move(ctx));
    return loop_stack.back().second;
}

FunctionLowerer::LoopContext& FunctionLowerer::lookup_loop_context(const void* key) {
    for (auto it = loop_stack.rbegin(); it != loop_stack.rend(); ++it) {
        if (it->first == key) {
            return it->second;
        }
    }
    throw std::logic_error("Loop context not found in v2 lowering");
}

FunctionLowerer::LoopContext FunctionLowerer::pop_loop_context(const void* key) {
    if (loop_stack.empty() || loop_stack.back().first != key) {
        throw std::logic_error("Loop context stack mismatch in v2 lowering");
    }
    auto ctx = std::move(loop_stack.back().second);
    loop_stack.pop_back();
    return ctx;
}

void FunctionLowerer::finalize_loop_context(const LoopContext& ctx) {
    BasicBlock& break_block = mir_function.basic_blocks[ctx.break_block];
    if (ctx.break_result) {
        TempId dest = *ctx.break_result;
        PhiNode phi;
        phi.dest = dest;
        for (std::size_t i = 0; i < ctx.break_incomings.size(); ++i) {
            PhiIncoming incoming;
            incoming.block = ctx.break_predecessors[i];
            incoming.value = ctx.break_incomings[i].value;
            phi.incoming.push_back(std::move(incoming));
        }
        break_block.phis.push_back(std::move(phi));
    }
}

bool FunctionLowerer::lower_block_statements(const hir::Block& block) {
    for (const auto& stmt_ptr : block.stmts) {
        if (!stmt_ptr) {
            continue;
        }
        lower_statement(*stmt_ptr);
        if (!is_reachable()) {
            return false;
        }
    }
    return true;
}

void FunctionLowerer::lower_block(const hir::Block& hir_block) {
    if (!lower_block_statements(hir_block)) {
        return;
    }

    if (!hir_block.final_expr) {
        // Unit return
        if (!mir::is_never(mir_function.sig.return_desc)) {
            emit_return(std::nullopt);
        }
        return;
    }

    const hir::Expr& expr = **hir_block.final_expr;
    semantic::ExprInfo info = hir::helper::get_expr_info(expr);
    if (return_plan.is_sret) {
        Place dest = return_plan.return_place();
        LowerResult res = lower_expr(expr, dest);
        res.write_to_dest(*this, dest, return_plan.ret_type);
        emit_return(std::nullopt);
    } else {
        LowerResult res = lower_expr(expr, std::nullopt);
        Operand value = res.as_operand(*this, info.type);
        emit_return(std::move(value));
    }
}

LowerResult FunctionLowerer::lower_block_expr(const hir::Block& block,
                                              std::optional<Place> dest) {
    if (!lower_block_statements(block)) {
        return LowerResult::written();
    }
    if (!block.final_expr) {
        if (!dest) {
            Constant unit_placeholder = mir::detail::make_bool_constant(false);
            return LowerResult::operand(mir::detail::make_constant_operand(unit_placeholder));
        }
        return LowerResult::written();
    }
    const hir::Expr& expr = **block.final_expr;
    semantic::ExprInfo info = hir::helper::get_expr_info(expr);
    return lower_expr(expr, dest);
}

void FunctionLowerer::lower_statement(const hir::Stmt& stmt) {
    std::visit([&](const auto& node) { lower_statement_impl(node); }, stmt.value);
}

void FunctionLowerer::lower_statement_impl(const hir::LetStmt& let_stmt) {
    if (!let_stmt.pattern || !let_stmt.initializer) {
        return;
    }
    const hir::Local* local = resolve_pattern_local(*let_stmt.pattern);
    Place target = make_local_place(local);
    TypeId type = let_stmt.type_annotation ? hir::helper::get_resolved_type(*let_stmt.type_annotation)
                                           : hir::helper::get_expr_info(*let_stmt.initializer).type;
    LowerResult res = lower_expr(*let_stmt.initializer, target);
    res.write_to_dest(*this, target, type);
}

void FunctionLowerer::lower_statement_impl(const hir::ExprStmt& expr_stmt) {
    if (!expr_stmt.expr) {
        return;
    }
    lower_expr(*expr_stmt.expr, std::nullopt);
}

LowerResult FunctionLowerer::lower_if_expr(const hir::If& if_expr,
                                           const semantic::ExprInfo& info,
                                           std::optional<Place> dest) {
    semantic::ExprInfo cond_info = hir::helper::get_expr_info(*if_expr.condition);
    Operand condition = lower_expr(*if_expr.condition, std::nullopt).as_operand(*this, cond_info.type);

    auto make_zero_operand = [&](TypeId ty) -> Operand {
        TypeId canon = mir::detail::canonicalize_type_for_mir(ty);
        if (mir::detail::is_bool_type(canon)) {
            return mir::detail::make_constant_operand(mir::detail::make_bool_constant(false));
        }
        if (mir::detail::is_signed_integer_type(canon) || mir::detail::is_unsigned_integer_type(canon)) {
            IntConstant ic;
            ic.value = 0;
            ic.is_negative = false;
            ic.is_signed = mir::detail::is_signed_integer_type(canon);
            Constant c;
            c.type = canon;
            c.value = ic;
            return mir::detail::make_constant_operand(c);
        }
        Constant c;
        c.type = canon;
        c.value = CharConstant{0};
        return mir::detail::make_constant_operand(c);
    };

    BasicBlockId then_block = create_block();
    BasicBlockId else_block = create_block();
    BasicBlockId join_block = create_block();

    branch_on_bool(condition, then_block, else_block);

    // Then branch
    switch_to_block(then_block);
    LowerResult then_res = lower_block_expr(*if_expr.then_block, dest);
    std::optional<TempId> then_temp;
    if (dest) {
        then_res.write_to_dest(*this, *dest, info.type);
    } else {
        Operand op = then_res.as_operand(*this, info.type);
        then_temp = materialize_operand(op, info.type);
    }
    BasicBlockId then_end = current_block ? current_block_id() : then_block;
    add_goto_from_current(join_block);

    // Else branch
    switch_to_block(else_block);
    LowerResult else_res = LowerResult::written();
    std::optional<TempId> else_temp;
    if (if_expr.else_expr) {
        const hir::Expr& else_expr = **if_expr.else_expr;
        semantic::ExprInfo else_info = hir::helper::get_expr_info(else_expr);
        else_res = lower_block_expr_result(else_expr, dest, else_info);
        if (dest) {
            else_res.write_to_dest(*this, *dest, info.type);
        } else {
            Operand op = else_res.as_operand(*this, info.type);
            else_temp = materialize_operand(op, info.type);
        }
    } else if (!dest) {
        Operand fallback = make_zero_operand(info.type);
        else_temp = materialize_operand(fallback, info.type);
    }
    BasicBlockId else_end = current_block ? current_block_id() : else_block;
    add_goto_from_current(join_block);

    switch_to_block(join_block);
    if (dest) {
        return LowerResult::written();
    }

    TempId result_temp = allocate_temp(info.type);
    PhiNode phi;
    phi.dest = result_temp;
    if (then_temp) {
        phi.incoming.push_back(PhiIncoming{then_end, *then_temp});
    }
    if (else_temp) {
        phi.incoming.push_back(PhiIncoming{else_end, *else_temp});
    }
    mir_function.basic_blocks[join_block].phis.push_back(std::move(phi));
    return LowerResult::operand(make_temp_operand(result_temp));
}

LowerResult FunctionLowerer::lower_block_expr_result(const hir::Expr& expr,
                                                     std::optional<Place> dest,
                                                     const semantic::ExprInfo& info) {
    (void)info;
    return lower_expr(expr, dest);
}

} // namespace mir::lower_v2::detail
