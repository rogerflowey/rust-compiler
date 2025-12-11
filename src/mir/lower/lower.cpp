#include "mir/lower/lower.hpp"

#include "mir.hpp"
#include "mir/lower/lower_common.hpp"
#include "mir/lower/lower_const.hpp"
#include "mir/lower/lower_internal.hpp"
#include "mir/lower/sig_builder.hpp"

#include "semantic/expr_info_helpers.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include "semantic/symbol/predefined.hpp"
#include "type/type.hpp"

#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace mir {
namespace {

using namespace detail;

struct FunctionDescriptor {
  std::variant<const hir::Function *, const hir::Method *> function_or_method;
  const void *key = nullptr;
  std::string name;
  FunctionId id = 0;
  bool is_external = false; // Track if function is external/builtin
  ProtoSig proto_sig;  // Signature information
};

void add_function_descriptor(const hir::Function &function,
                             const std::string &scope,
                             std::vector<FunctionDescriptor> &out) {
  FunctionDescriptor descriptor;
  descriptor.function_or_method = &function;
  descriptor.key = &function;
  descriptor.name = derive_function_name(function, scope);
  descriptor.is_external = !function.body.has_value();
  out.push_back(std::move(descriptor));
}

void add_method_descriptor(const hir::Method &method, const std::string &scope,
                           std::vector<FunctionDescriptor> &out) {
  FunctionDescriptor descriptor;
  descriptor.function_or_method = &method;
  descriptor.key = &method;
  descriptor.name = derive_method_name(method, scope);
  descriptor.is_external = !method.body.has_value();
  out.push_back(std::move(descriptor));
}

std::vector<FunctionDescriptor>
collect_function_descriptors(const hir::Program &program) {
  std::vector<FunctionDescriptor> descriptors;

  // Phase 1: Collect predefined scope functions first (builtins)
  const semantic::Scope &predefined = semantic::get_predefined_scope();
  for (const auto &[name, symbol] : predefined.get_items_local()) {
    if (auto *fn_ptr = std::get_if<hir::Function *>(&symbol)) {
      FunctionDescriptor descriptor;
      descriptor.function_or_method = *fn_ptr;
      descriptor.key = *fn_ptr;
      descriptor.name = std::string(name);
      descriptor.is_external = true; // Mark as external/builtin
      descriptors.push_back(std::move(descriptor));
    }
  }

  // Phase 2: Walk the HIR to find *all* other functions/methods (including
  // nested)
  struct Collector : hir::HirVisitorBase<Collector> {
    const hir::Program *program;
    std::vector<FunctionDescriptor> &out;
    std::string current_scope;

    using Base = hir::HirVisitorBase<Collector>;
    using Base::visit;
    using Base::visit_block;

    Collector(const hir::Program &p, std::vector<FunctionDescriptor> &out)
        : program(&p), out(out) {}

    void visit_program(hir::Program &p) {
      current_scope.clear();
      Base::visit_program(p);
    }

    void visit(hir::Function &f) {
      // Top-level or local function
      add_function_descriptor(f, current_scope, out);
      Base::visit(f);
    }

    void visit(hir::Impl &impl) {
      // Update scope to type name for methods / assoc fns
      TypeId impl_type = hir::helper::get_resolved_type(impl.for_type);
      std::string saved_scope = current_scope;
      current_scope = type_name(impl_type);

      Base::visit(impl);

      current_scope = std::move(saved_scope);
    }

    void visit(hir::Method &m) {
      add_method_descriptor(m, current_scope, out);
      Base::visit(m);
    }

    void visit_block(hir::Block &block) {
      // Items inside blocks will be visited through Base::visit_block
      Base::visit_block(block);
    }
  };

  Collector collector{program, descriptors};
  // Cast away const to use visitor (visitor modifies nothing that affects
  // semantics)
  collector.visit_program(const_cast<hir::Program &>(program));

  return descriptors;
}

MirFunction lower_descriptor(
    const FunctionDescriptor &descriptor,
    const std::unordered_map<const void *, mir::FunctionRef> &fn_map) {
  return std::visit(
      [&fn_map, &descriptor](const auto *fn_ptr) -> MirFunction {
        FunctionLowerer lowerer(*fn_ptr, fn_map, descriptor.id,
                                descriptor.name);
        return lowerer.lower();
      },
      descriptor.function_or_method);
}

// FunctionLowerer declarations are provided in mir/lower/lower_internal.hpp.

} // namespace

namespace detail {

FunctionLowerer::FunctionLowerer(
    const hir::Function &function,
    const std::unordered_map<const void *, mir::FunctionRef> &fn_map,
    FunctionId id, std::string name)
    : function_kind(FunctionKind::Function), hir_function(&function),
      function_map(fn_map) {
  initialize(id, std::move(name));
}

FunctionLowerer::FunctionLowerer(
    const hir::Method &method,
    const std::unordered_map<const void *, mir::FunctionRef> &fn_map,
    FunctionId id, std::string name)
    : function_kind(FunctionKind::Method), hir_method(&method),
      function_map(fn_map) {
  initialize(id, std::move(name));
}

MirFunction FunctionLowerer::lower() {
  const hir::Block *body = get_body();
  if (!body) {
    throw std::logic_error(
          "function missing body during MIR lowering");
  }

  lower_block(*body);
  return std::move(mir_function);
}

void FunctionLowerer::initialize(FunctionId id, std::string name) {
  mir_function.id = id;
  mir_function.name = std::move(name);

  // Build proto signature from HIR
  SigBuilder builder(function_kind == FunctionKind::Function ? 
                     static_cast<SigBuilder::FnOrMethod>(hir_function) :
                     static_cast<SigBuilder::FnOrMethod>(hir_method));
  ProtoSig proto_sig = builder.build_proto_sig();
  
  // Set up return_desc in the signature
  mir_function.sig.return_desc = proto_sig.return_desc;

  // Determine if we need SRET
  uses_sret_ = is_indirect_sret(mir_function.sig.return_desc);

  if (uses_sret_) {
    // Create a temp whose type is &ReturnType
    TypeId ref_ty = make_ref_type(return_type(mir_function.sig.return_desc));
    TempId t = allocate_temp(ref_ty);

    // Return destination is a PointerPlace based on this temp
    Place p;
    p.base = PointerPlace{t};
    return_place_ = std::move(p);
  }

  // Initialize locals (this must happen before collect_parameters)
  init_locals();
  
  // Collect parameters and build MirParam entries
  nrvo_local_ = pick_nrvo_local();
  collect_parameters();
  
  // If using sret, set the result_local in the return descriptor
  if (uses_sret_) {
    if (nrvo_local_) {
      LocalId result_local = require_local_id(nrvo_local_);
      auto& sret = std::get<ReturnDesc::RetIndirectSRet>(mir_function.sig.return_desc.kind);
      sret.result_local = result_local;
    }
  }
  
  // Populate ABI parameters
  populate_abi_params(mir_function.sig);

  BasicBlockId entry = create_block();
  current_block = entry;
  mir_function.start_block = entry;
}

const hir::Block *FunctionLowerer::get_body() const {
  if (function_kind == FunctionKind::Function) {
    return (hir_function && hir_function->body)
               ? hir_function->body->block.get()
               : nullptr;
  }
  return (hir_method && hir_method->body) ? hir_method->body->block.get()
                                          : nullptr;
}

const std::vector<std::unique_ptr<hir::Local>> &
FunctionLowerer::get_locals_vector() const {
  if (function_kind == FunctionKind::Function) {
    return hir_function->body->locals;
  }
  return hir_method->body->locals;
}

const hir::Local *FunctionLowerer::pick_nrvo_local() const {
  if (!uses_sret_) {
    return nullptr;
  }

  TypeId ret_ty = return_type(mir_function.sig.return_desc);
  if (ret_ty == invalid_type_id) {
    return nullptr;
  }

  const hir::Local *candidate = nullptr;
  bool multiple = false;

  auto consider = [&](const hir::Local *local_ptr) {
    if (!local_ptr || !local_ptr->type_annotation) {
      return;
    }

    TypeId ty = hir::helper::get_resolved_type(*local_ptr->type_annotation);
    TypeId normalized = canonicalize_type_for_mir(ty);

    if (normalized != ret_ty) {
      return;
    }

    if (candidate && candidate != local_ptr) {
      multiple = true;
      return;
    }

    candidate = local_ptr;
  };

  if (function_kind == FunctionKind::Method && hir_method && hir_method->body &&
      hir_method->body->self_local) {
    consider(hir_method->body->self_local.get());
  }

  for (const auto &local_ptr : get_locals_vector()) {
    consider(local_ptr.get());
    if (multiple) {
      break;
    }
  }

  if (multiple) {
    return nullptr;
  }

  return candidate;
}

TypeId FunctionLowerer::resolve_return_type() const {
  const auto &annotation = (function_kind == FunctionKind::Function)
                               ? hir_function->sig.return_type
                               : hir_method->sig.return_type;
  if (annotation) {
    return hir::helper::get_resolved_type(*annotation);
  }
  return get_unit_type();
}

void FunctionLowerer::init_locals() {
  auto register_local = [this](const hir::Local *local_ptr) {
    if (!local_ptr) {
      return;
    }
    if (!local_ptr->type_annotation) {
      throw std::logic_error("Local missing resolved type during MIR lowering");
    }
    TypeId type = hir::helper::get_resolved_type(*local_ptr->type_annotation);
    TypeId normalized = canonicalize_type_for_mir(type);
    LocalId id = static_cast<LocalId>(mir_function.locals.size());
    local_ids.emplace(local_ptr, id);

    LocalInfo info;
    info.type = normalized;
    info.debug_name = local_ptr->name.name;

    mir_function.locals.push_back(std::move(info));
  };

  if (function_kind == FunctionKind::Method && hir_method && hir_method->body &&
      hir_method->body->self_local) {
    register_local(hir_method->body->self_local.get());
  }

  for (const auto &local_ptr : get_locals_vector()) {
    if (local_ptr) {
      register_local(local_ptr.get());
    }
  }
}

void FunctionLowerer::collect_parameters() {
  if (function_kind == FunctionKind::Method && hir_method) {
    append_self_parameter();
    append_explicit_parameters(hir_method->sig.params,
                               hir_method->sig.param_type_annotations);
    return;
  }
  if (function_kind == FunctionKind::Function && hir_function) {
    append_explicit_parameters(hir_function->sig.params,
                               hir_function->sig.param_type_annotations);
  }
}

void FunctionLowerer::append_self_parameter() {
  if (!hir_method) {
    throw std::logic_error("Method context missing during MIR lowering");
  }
  if (!hir_method->body || !hir_method->body->self_local) {
    return;
  }
  if (!hir_method->body->self_local->type_annotation) {
    throw std::logic_error(
        "Method self parameter missing resolved type during MIR lowering");
  }
  TypeId self_type = hir::helper::get_resolved_type(
      *hir_method->body->self_local->type_annotation);
  append_parameter(hir_method->body->self_local.get(), self_type);
}

void FunctionLowerer::append_explicit_parameters(
    const std::vector<std::unique_ptr<hir::Pattern>> &params,
    const std::vector<hir::TypeAnnotation> &annotations) {
  if (params.size() != annotations.size()) {
    throw std::logic_error(
        "Parameter/type annotation mismatch during MIR lowering");
  }
  for (std::size_t i = 0; i < params.size(); ++i) {
    const auto &param = params[i];
    if (!param) {
      continue;
    }
    const auto &annotation = annotations[i];
    TypeId param_type = hir::helper::get_resolved_type(annotation);
    append_parameter(resolve_pattern_local(*param), param_type);
  }
}

void FunctionLowerer::append_parameter(const hir::Local *local, TypeId type) {
  if (!local) {
    throw std::logic_error(
        "Parameter pattern did not resolve to a Local during MIR lowering");
  }
  if (type == invalid_type_id) {
    throw std::logic_error(
        "Parameter missing resolved type during MIR lowering");
  }
  TypeId normalized = canonicalize_type_for_mir(type);
  LocalId local_id = require_local_id(local);
  
  // Add to MirFunctionSig.params
  MirParam param;
  param.local = local_id;
  param.type = normalized;
  param.debug_name = local->name.name;
  mir_function.sig.params.push_back(std::move(param));
}

const hir::Local *
FunctionLowerer::resolve_pattern_local(const hir::Pattern &pattern) const {
  if (const auto *binding = std::get_if<hir::BindingDef>(&pattern.value)) {
    if (auto *local_ptr = std::get_if<hir::Local *>(&binding->local)) {
      return *local_ptr;
    }
    throw std::logic_error(
        "Binding definition missing resolved Local during MIR lowering");
  }
  if (const auto *reference =
          std::get_if<hir::ReferencePattern>(&pattern.value)) {
    if (!reference->subpattern) {
      throw std::logic_error(
          "Reference pattern missing subpattern during MIR lowering");
    }
    return resolve_pattern_local(*reference->subpattern);
  }
  throw std::logic_error("Unsupported pattern variant in parameter lowering");
}

bool FunctionLowerer::is_reachable() const { return current_block.has_value(); }

void FunctionLowerer::require_reachable(const char *context) const {
  if (!is_reachable()) {
    throw std::logic_error(std::string("Unreachable code encountered in ") +
                           context);
  }
}

mir::FunctionRef FunctionLowerer::lookup_function(const void *key) const {
  auto it = function_map.find(key);
  if (it == function_map.end()) {
    throw std::logic_error("Call target not registered during MIR lowering");
  }
  return it->second;
}

std::optional<Operand> FunctionLowerer::emit_call(mir::FunctionRef target,
                                                  TypeId result_type,
                                                  std::vector<Operand> &&args) {
  bool result_needed =
      !is_unit_type(result_type) && !is_never_type(result_type);
  std::optional<TempId> dest;
  std::optional<Operand> result;
  if (result_needed) {
    TempId temp = allocate_temp(result_type);
    dest = temp;
    result = make_temp_operand(temp);
  }

  CallStatement call_stmt;
  // Only set dest if result is needed (not unit/never type)
  // This applies same logic for both internal and external calls
  call_stmt.dest = dest;

  // Phase 4: Set correct CallTarget::Kind and ID based on function type
  if (auto *internal = std::get_if<MirFunction *>(&target)) {
    call_stmt.target.kind = mir::CallTarget::Kind::Internal;
    call_stmt.target.id = (*internal)->id;
  } else if (auto *external = std::get_if<ExternalFunction *>(&target)) {
    call_stmt.target.kind = mir::CallTarget::Kind::External;
    call_stmt.target.id = (*external)->id;
  }

  call_stmt.args = std::move(args);
  Statement stmt;
  stmt.value = std::move(call_stmt);
  append_statement(std::move(stmt));
  return result;
}

Operand FunctionLowerer::emit_aggregate(AggregateRValue aggregate,
                                        TypeId result_type) {
  return emit_rvalue_to_temp(std::move(aggregate), result_type);
}

Operand FunctionLowerer::emit_array_repeat(Operand value, std::size_t count,
                                           TypeId result_type) {
  ArrayRepeatRValue repeat;
  repeat.value = std::move(value);
  repeat.count = count;
  return emit_rvalue_to_temp(std::move(repeat), result_type);
}

bool FunctionLowerer::function_uses_sret(const hir::Function &fn) const {
  if (!fn.body) {
    return false; // external/builtin: leave ABI alone for now
  }
  if (!fn.sig.return_type) {
    return false; // unit
  }
  TypeId ret = canonicalize_type_for_mir(
      hir::helper::get_resolved_type(*fn.sig.return_type));
  return is_aggregate_type(ret);
}

bool FunctionLowerer::method_uses_sret(const hir::Method &m) const {
  if (!m.body) {
    return false;
  }
  if (!m.sig.return_type) {
    return false;
  }
  TypeId ret = canonicalize_type_for_mir(
      hir::helper::get_resolved_type(*m.sig.return_type));
  return is_aggregate_type(ret);
}

void FunctionLowerer::emit_call_into_place(mir::FunctionRef target,
                                          TypeId /* result_type */,
                                          Place dest,
                                          std::vector<Operand> &&args) {
  CallStatement call_stmt;
  call_stmt.dest = std::nullopt; // sret-style: no result temp

  if (auto *internal = std::get_if<MirFunction *>(&target)) {
    call_stmt.target.kind = mir::CallTarget::Kind::Internal;
    call_stmt.target.id = (*internal)->id;
  } else if (auto *external = std::get_if<ExternalFunction *>(&target)) {
    call_stmt.target.kind = mir::CallTarget::Kind::External;
    call_stmt.target.id = (*external)->id;
  }

  call_stmt.args = std::move(args);
  call_stmt.sret_dest = std::move(dest);

  Statement stmt;
  stmt.value = std::move(call_stmt);
  append_statement(std::move(stmt));
}

bool FunctionLowerer::try_lower_init_call(const hir::Call &call_expr,
                                         Place dest,
                                         TypeId dest_type) {
  if (!call_expr.callee) {
    return false;
  }

  const auto *func_use =
      std::get_if<hir::FuncUse>(&call_expr.callee->value);
  if (!func_use || !func_use->def) {
    return false;
  }

  const hir::Function *hir_fn = func_use->def;
  if (!function_uses_sret(*hir_fn)) {
    return false; // not an internal aggregate-returning function
  }

  mir::FunctionRef target = lookup_function(hir_fn);

  std::vector<Operand> args;
  args.reserve(call_expr.args.size());
  for (const auto &arg : call_expr.args) {
    if (!arg) {
      throw std::logic_error("Call argument missing during MIR lowering");
    }
    args.push_back(lower_operand(*arg));
  }

  emit_call_into_place(target, dest_type, std::move(dest), std::move(args));
  return true;
}

bool FunctionLowerer::try_lower_init_method_call(const hir::MethodCall &mcall,
                                                Place dest,
                                                TypeId dest_type) {
  const hir::Method *method_def = hir::helper::get_method_def(mcall);
  if (!method_def || !method_uses_sret(*method_def)) {
    return false;
  }

  if (!mcall.receiver) {
    throw std::logic_error("Method call missing receiver during MIR lowering");
  }

  mir::FunctionRef target = lookup_function(method_def);

  std::vector<Operand> args;
  args.reserve(mcall.args.size() + 1);

  // receiver first
  args.push_back(lower_operand(*mcall.receiver));

  for (const auto &arg : mcall.args) {
    if (!arg) {
      throw std::logic_error("Method call argument missing during MIR lowering");
    }
    args.push_back(lower_operand(*arg));
  }

  emit_call_into_place(target, dest_type, std::move(dest), std::move(args));
  return true;
}

BasicBlockId FunctionLowerer::create_block() {
  BasicBlockId id = static_cast<BasicBlockId>(mir_function.basic_blocks.size());
  mir_function.basic_blocks.emplace_back();
  block_terminated.push_back(false);
  return id;
}

bool FunctionLowerer::block_is_terminated(BasicBlockId id) const {
  return block_terminated.at(id);
}

BasicBlockId FunctionLowerer::current_block_id() const {
  if (!current_block) {
    throw std::logic_error("Current block not available");
  }
  return *current_block;
}

TempId FunctionLowerer::allocate_temp(TypeId type) {
  if (type == invalid_type_id) {
    throw std::logic_error(
        "Temporary missing resolved type during MIR lowering");
  }
  TypeId normalized = canonicalize_type_for_mir(type);
  if (is_unit_type(normalized)) {
    throw std::logic_error("Unit temporaries should not be allocated");
  }
  TempId id = static_cast<TempId>(mir_function.temp_types.size());
  mir_function.temp_types.push_back(normalized);
  return id;
}

void FunctionLowerer::append_statement(Statement statement) {
  if (!current_block) {
    return;
  }
  BasicBlockId block_id = *current_block;
  if (block_is_terminated(block_id)) {
    throw std::logic_error("Cannot append statement to terminated block");
  }
  mir_function.basic_blocks[block_id].statements.push_back(
      std::move(statement));
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

void FunctionLowerer::branch_on_bool(const Operand &condition,
                                     BasicBlockId true_block,
                                     BasicBlockId false_block) {
  if (!current_block) {
    return;
  }
  SwitchIntTerminator term;
  term.discriminant = condition;
  term.targets.push_back(SwitchIntTarget{make_bool_constant(true), true_block});
  term.otherwise = false_block;
  terminate_current_block(Terminator{std::move(term)});
}

TempId FunctionLowerer::materialize_operand(const Operand &operand,
                                            TypeId type) {
  if (const auto *temp = std::get_if<TempId>(&operand.value)) {
    return *temp;
  }
  if (!current_block) {
    throw std::logic_error("Cannot materialize operand without active block");
  }
  if (type == invalid_type_id) {
    throw std::logic_error(
        "Operand missing resolved type during materialization");
  }
  TypeId normalized = canonicalize_type_for_mir(type);
  const auto *constant = std::get_if<Constant>(&operand.value);
  if (!constant) {
    throw std::logic_error("Operand must contain a constant value");
  }
  if (constant->type != normalized) {
    throw std::logic_error("Operand type mismatch during materialization");
  }
  TempId dest = allocate_temp(normalized);
  ConstantRValue const_rvalue{*constant};
  RValue rvalue;
  rvalue.value = const_rvalue;
  DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
  Statement stmt;
  stmt.value = std::move(define);
  append_statement(std::move(stmt));
  return dest;
}

Operand FunctionLowerer::make_temp_operand(TempId temp) {
  Operand operand;
  operand.value = temp;
  return operand;
}

Operand FunctionLowerer::make_const_operand(std::uint64_t value, TypeId type, bool is_signed) {
  IntConstant int_const;
  int_const.value = value;
  int_const.is_negative = false;
  int_const.is_signed = is_signed;
  
  Constant c;
  c.type = type;
  c.value = std::move(int_const);
  
  Operand op;
  op.value = std::move(c);
  
  return op;
}

void FunctionLowerer::emit_return(std::optional<Operand> value) {
    const ReturnDesc& ret_desc = mir_function.sig.return_desc;
    
    if (is_never(ret_desc)) {
        throw std::logic_error(
            "emit_return called for never-returning function during MIR lowering: " +
            mir_function.name);
    }

    if (is_indirect_sret(ret_desc)) {
        if (value) {
            throw std::logic_error("Internal invariant: sret function should not return value operand");
        }
    } else {
        if (!value && !is_void_semantic(ret_desc)) {
            throw std::logic_error(
                "emit_return called without value for non-void function: " +
                mir_function.name);
        }
    }
    
    if (!current_block) {
        return;
    }
    ReturnTerminator ret{std::move(value)};
    terminate_current_block(Terminator{std::move(ret)});
}

FunctionLowerer::LoopContext &
FunctionLowerer::push_loop_context(const void *key, BasicBlockId continue_block,
                                   BasicBlockId break_block,
                                   std::optional<TypeId> break_type) {
  LoopContext ctx;
  ctx.continue_block = continue_block;
  ctx.break_block = break_block;
  if (break_type) {
    TypeId normalized = canonicalize_type_for_mir(*break_type);
    ctx.break_type = normalized;
    if (!is_unit_type(normalized) && !is_never_type(normalized)) {
      ctx.break_result = allocate_temp(normalized);
    }
  } else {
    ctx.break_type = std::nullopt;
  }
  loop_stack.emplace_back(key, std::move(ctx));
  return loop_stack.back().second;
}

FunctionLowerer::LoopContext &
FunctionLowerer::lookup_loop_context(const void *key) {
  for (auto it = loop_stack.rbegin(); it != loop_stack.rend(); ++it) {
    if (it->first == key) {
      return it->second;
    }
  }
  throw std::logic_error("Loop context not found");
}

FunctionLowerer::LoopContext
FunctionLowerer::pop_loop_context(const void *key) {
  if (loop_stack.empty() || loop_stack.back().first != key) {
    throw std::logic_error("Loop context stack corrupted");
  }
  LoopContext ctx = std::move(loop_stack.back().second);
  loop_stack.pop_back();
  return ctx;
}

void FunctionLowerer::finalize_loop_context(const LoopContext &ctx) {
  if (ctx.break_result) {
    if (ctx.break_incomings.empty()) {
      throw std::logic_error(
          "Loop expression expects value but no break produced one");
    }
    PhiNode phi;
    phi.dest = *ctx.break_result;
    phi.incoming = ctx.break_incomings;
    mir_function.basic_blocks[ctx.break_block].phis.push_back(std::move(phi));
  }
}

bool FunctionLowerer::lower_block_statements(const hir::Block &block) {
  for (const auto &stmt : block.stmts) {
    if (!is_reachable()) {
      return false;
    }
    if (stmt) {
      lower_statement(*stmt);
    }
  }
  return is_reachable();
}

void FunctionLowerer::lower_block(const hir::Block &hir_block) {
    if (!lower_block_statements(hir_block)) {
        return;
    }

    // === CASE: Block has an explicit final expression ===
    if (hir_block.final_expr) {
        const auto &expr_ptr = *hir_block.final_expr;
        if (!expr_ptr) {
            throw std::logic_error("Ownership violated: Final expression");
        }

        // Use central return handling for all return paths
        handle_return_value(hir_block.final_expr, "Block final expression");
        return;
    }

    // === CASE: Block has no final expression (implicit unit return) ===
    if (!is_reachable()) {
        return;
    }

    // Implicit unit return at end of reachable block
    handle_return_value(std::nullopt, "Block implicit return");
}

std::optional<Operand>
FunctionLowerer::lower_block_expr(const hir::Block &block,
                                  TypeId expected_type) {
  if (!lower_block_statements(block)) {
    return std::nullopt;
  }

  if (block.final_expr) {
    const auto &expr_ptr = *block.final_expr;
    if (expr_ptr) {
      return lower_expr(*expr_ptr);
    }
    return std::nullopt;
  }

  if (is_unit_type(expected_type) || is_never_type(expected_type)) {
    return std::nullopt;
  }

  throw std::logic_error("Block expression missing value");
}

void FunctionLowerer::lower_statement(const hir::Stmt &stmt) {
  if (!is_reachable()) {
    return;
  }
  std::visit([this](const auto &node) { lower_statement_impl(node); },
             stmt.value);
}

void FunctionLowerer::lower_statement_impl(const hir::LetStmt &let_stmt) {
  if (!is_reachable()) {
    return;
  }
  if (!let_stmt.pattern) {
    throw std::logic_error("Let statement missing pattern during MIR lowering");
  }
  if (!let_stmt.initializer) {
    throw std::logic_error(
        "Let statement without initializer not supported in MIR lowering");
  }

  const hir::Expr &init_expr = *let_stmt.initializer;
  lower_let_pattern(*let_stmt.pattern, init_expr);
}



void FunctionLowerer::emit_init_statement(Place dest, InitPattern pattern) {
  InitStatement init_stmt;
  init_stmt.dest = std::move(dest);
  init_stmt.pattern = std::move(pattern);
  Statement stmt;
  stmt.value = std::move(init_stmt);
  append_statement(std::move(stmt));
}

// === Central Init API ===================================================

void FunctionLowerer::lower_init(
    const hir::Expr &expr,
    Place dest,
    TypeId dest_type
) {
  if (dest_type == invalid_type_id) {
    throw std::logic_error("Destination type missing in lower_init");
  }

  // 1) Try specialized init logic (aggregates, etc.)
  if (try_lower_init_outside(expr, dest, dest_type)) {
    // fully handled dest
    return;
  }

  // 2) Fallback: compute a value and assign to dest
  Operand value = lower_operand(expr);

  AssignStatement assign;
  assign.dest = std::move(dest);
  assign.src  = std::move(value);

  Statement stmt;
  stmt.value = std::move(assign);
  append_statement(std::move(stmt));
}

bool FunctionLowerer::try_lower_init_outside(
    const hir::Expr &expr,
    Place dest,
    TypeId dest_type
) {
  if (dest_type == invalid_type_id) {
    return false;
  }

  TypeId normalized = canonicalize_type_for_mir(dest_type);
  const type::Type &ty = type::get_type_from_id(normalized);

  // Struct literal -> struct destination
  if (auto *struct_lit = std::get_if<hir::StructLiteral>(&expr.value)) {
    if (std::holds_alternative<type::StructType>(ty.value)) {
      lower_struct_init(*struct_lit, std::move(dest), normalized);
      return true;
    }
    return false;
  }

  // Array literal -> array destination
  if (auto *array_lit = std::get_if<hir::ArrayLiteral>(&expr.value)) {
    lower_array_literal_init(*array_lit, std::move(dest), normalized);
    return true;
  }

  // Array repeat -> array destination
  if (auto *array_rep = std::get_if<hir::ArrayRepeat>(&expr.value)) {
    lower_array_repeat_init(*array_rep, std::move(dest), normalized);
    return true;
  }

  // Call -> sret destination
  if (auto *call = std::get_if<hir::Call>(&expr.value)) {
    if (try_lower_init_call(*call, std::move(dest), normalized)) {
      return true;
    }
  }

  // Method call -> sret destination
  if (auto *mcall = std::get_if<hir::MethodCall>(&expr.value)) {
    if (try_lower_init_method_call(*mcall, std::move(dest), normalized)) {
      return true;
    }
  }

  {
    semantic::ExprInfo info = hir::helper::get_expr_info(expr);
    if (info.is_place) {
      if (!info.has_type || info.type == invalid_type_id) {
        throw std::logic_error("Init RHS place missing type");
      }
      TypeId src_ty = canonicalize_type_for_mir(info.type);
      if (src_ty == normalized) {
        Place src_place = lower_expr_place(expr);

        // Only use InitCopy for aggregate types (structs and arrays)
        // where memcpy is beneficial. For scalar types, fall through to default handling.
        if (is_aggregate_type(normalized)) {
          InitCopy copy_pattern{.src = std::move(src_place)};
          InitPattern pattern;
          pattern.value = std::move(copy_pattern);

          emit_init_statement(std::move(dest), std::move(pattern));
          return true;
        }
      }
    }
  }

  // Everything else: not handled here
  return false;
}



void FunctionLowerer::lower_struct_init(
    const hir::StructLiteral &literal,
    Place dest,
    TypeId dest_type
) {
  TypeId normalized = canonicalize_type_for_mir(dest_type);
  auto *struct_ty =
      std::get_if<type::StructType>(&type::get_type_from_id(normalized).value);
  if (!struct_ty) {
    throw std::logic_error(
        "Struct literal init without struct destination type");
  }

  const auto &struct_info =
      type::TypeContext::get_instance().get_struct(struct_ty->id);
  const auto &fields = hir::helper::get_canonical_fields(literal);

  if (fields.initializers.size() != struct_info.fields.size()) {
    throw std::logic_error(
        "Struct literal field count mismatch during struct init");
  }

  InitStruct init_struct;
  init_struct.fields.resize(fields.initializers.size());

  for (std::size_t idx = 0; idx < fields.initializers.size(); ++idx) {
    if (!fields.initializers[idx]) {
      throw std::logic_error(
          "Struct literal field missing initializer during MIR lowering");
    }

    TypeId field_ty =
        canonicalize_type_for_mir(struct_info.fields[idx].type);
    if (field_ty == invalid_type_id) {
      throw std::logic_error(
          "Struct field missing resolved type during MIR lowering");
    }

    const hir::Expr &field_expr = *fields.initializers[idx];
    auto &leaf = init_struct.fields[idx];

    // Build sub-place dest.field[idx]
    Place field_place = dest;
    field_place.projections.push_back(FieldProjection{idx});

    // Try to initialize this field via its own place-directed path:
    if (try_lower_init_outside(field_expr, std::move(field_place), field_ty)) {
      // Field is handled by the MIR just emitted.
      leaf = make_omitted_leaf();
    } else {
      // Fall back: compute an Operand and store via InitPattern
      Operand value = lower_operand(field_expr);
      leaf = make_operand_leaf(std::move(value));
    }
  }

  InitPattern pattern;
  pattern.value = std::move(init_struct);
  emit_init_statement(std::move(dest), std::move(pattern));
}



// === Array Init ========================================================

void FunctionLowerer::lower_array_literal_init(
    const hir::ArrayLiteral &array_literal,
    Place dest,
    TypeId dest_type
) {
  InitArrayLiteral init_array;
  init_array.elements.resize(array_literal.elements.size());

  for (std::size_t idx = 0; idx < array_literal.elements.size(); ++idx) {
    const auto &elem_expr_ptr = array_literal.elements[idx];
    if (!elem_expr_ptr) {
      throw std::logic_error(
          "Array literal element missing during MIR lowering");
    }

    const hir::Expr &elem_expr = *elem_expr_ptr;
    auto &leaf = init_array.elements[idx];

    // Build sub-place dest[idx]
    Place elem_place = dest;
    TypeId usize_ty = type::get_typeID(type::Type{type::PrimitiveKind::USIZE});
    Operand idx_operand = make_const_operand(idx, usize_ty, false);
    elem_place.projections.push_back(IndexProjection{std::move(idx_operand)});

    if (try_lower_init_outside(elem_expr, std::move(elem_place), dest_type)) {
      leaf = make_omitted_leaf();
    } else {
      Operand op = lower_operand(elem_expr);
      leaf = make_operand_leaf(std::move(op));
    }
  }

  InitPattern pattern;
  pattern.value = std::move(init_array);
  emit_init_statement(std::move(dest), std::move(pattern));
}

void FunctionLowerer::lower_array_repeat_init(
    const hir::ArrayRepeat &array_repeat,
    Place dest,
    TypeId dest_type
) {
  InitArrayRepeat init_repeat;
  
  // Extract count from variant
  if (std::holds_alternative<size_t>(array_repeat.count)) {
    init_repeat.count = std::get<size_t>(array_repeat.count);
  } else {
    throw std::logic_error(
        "Array repeat count must be a compile-time constant during MIR lowering");
  }

  // Try to initialize the element via place-directed init
  Place elem_place = dest;
  IntConstant zero_const;
  zero_const.value = 0;
  zero_const.is_negative = false;
  zero_const.is_signed = false;
  Constant c;
  c.type = type::get_typeID(type::Type{type::PrimitiveKind::USIZE});
  c.value = std::move(zero_const);
  Operand zero_operand;
  zero_operand.value = std::move(c);
  elem_place.projections.push_back(IndexProjection{std::move(zero_operand)});

  if (try_lower_init_outside(*array_repeat.value, std::move(elem_place), dest_type)) {
    // Element is handled by separate MIR statements
    init_repeat.element = make_omitted_leaf();
  } else {
    // Fall back: compute an Operand
    Operand op = lower_operand(*array_repeat.value);
    init_repeat.element = make_operand_leaf(std::move(op));
  }

  InitPattern pattern;
  pattern.value = std::move(init_repeat);
  emit_init_statement(std::move(dest), std::move(pattern));
}



// === Pattern-based initialization =====================================

void FunctionLowerer::lower_let_pattern(const hir::Pattern &pattern,
                                        const hir::Expr &init_expr) {
  // Entry point for pattern-based let initialization.
  // For now only BindingDef and ReferencePattern exist; this will be extended
  // to handle struct/tuple/array patterns in an expr-directed way.
  semantic::ExprInfo info = hir::helper::get_expr_info(init_expr);
  if (!info.has_type || info.type == invalid_type_id) {
    throw std::logic_error("Let initializer missing resolved type");
  }
  lower_pattern_from_expr(pattern, init_expr, info.type);
}

void FunctionLowerer::lower_binding_let(const hir::BindingDef &binding,
                                        const hir::Expr &init_expr) {
  // Binding pattern lowering: initialize the local directly from the initializer
  // expression. lower_init will choose the best strategy
  // (struct field-by-field, leaf initialize, or temp+assign).
  hir::Local *local = hir::helper::get_local(binding);
  if (!local) {
    throw std::logic_error("Let binding missing local during MIR lowering");
  }

  if (local->name.name == "_") {
    // For underscore bindings, just lower for side effects
    (void)lower_expr(init_expr);
    return;
  }

  if (!local->type_annotation) {
    throw std::logic_error(
        "Let binding missing resolved type during MIR lowering");
  }

  Place dest = make_local_place(local);
  TypeId dest_type = hir::helper::get_resolved_type(*local->type_annotation);
  lower_init(init_expr, std::move(dest), dest_type);
}

void FunctionLowerer::lower_reference_let(const hir::ReferencePattern &,
                                          const hir::Expr &) {
  throw std::logic_error(
      "Reference patterns not yet supported in MIR lowering");
}

void FunctionLowerer::lower_pattern_from_expr(const hir::Pattern &pattern,
                                              const hir::Expr &expr,
                                              TypeId /* expr_type */) {
  // For now, we only support binding and reference patterns.
  std::visit(
      Overloaded{
          [this, &expr](const hir::BindingDef &binding) {
            lower_binding_let(binding, expr);
          },
          [this, &expr](const hir::ReferencePattern &ref_pattern) {
            lower_reference_let(ref_pattern, expr);
          }},
      pattern.value);
}

void FunctionLowerer::lower_statement_impl(const hir::ExprStmt &expr_stmt) {
  if (!is_reachable()) {
    return;
  }
  if (expr_stmt.expr) {
    semantic::ExprInfo info = hir::helper::get_expr_info(*expr_stmt.expr);
    bool expect_fallthrough = semantic::has_normal_endpoint(info);

    (void)lower_expr(*expr_stmt.expr);

    if (!expect_fallthrough && is_reachable()) {
      throw std::logic_error("ExprStmt divergence mismatch: semantically "
                             "diverging expression leaves block reachable");
    }
  }
}



AggregateRValue FunctionLowerer::build_struct_aggregate(
    const hir::StructLiteral &struct_literal) {
  const auto &fields = hir::helper::get_canonical_fields(struct_literal);
  AggregateRValue aggregate;
  aggregate.kind = AggregateRValue::Kind::Struct;
  aggregate.elements.reserve(fields.initializers.size());
  for (const auto &initializer : fields.initializers) {
    if (!initializer) {
      throw std::logic_error(
          "Struct literal field missing during MIR lowering");
    }
    aggregate.elements.push_back(lower_operand(*initializer));
  }
  return aggregate;
}

AggregateRValue FunctionLowerer::build_array_aggregate(
    const hir::ArrayLiteral &array_literal) {
  AggregateRValue aggregate;
  aggregate.kind = AggregateRValue::Kind::Array;
  aggregate.elements.reserve(array_literal.elements.size());
  for (const auto &element : array_literal.elements) {
    if (!element) {
      throw std::logic_error(
          "Array literal element missing during MIR lowering");
    }
    aggregate.elements.push_back(lower_operand(*element));
  }
  return aggregate;
}

ArrayRepeatRValue FunctionLowerer::build_array_repeat_rvalue(
    const hir::ArrayRepeat &array_repeat) {
  if (!array_repeat.value) {
    throw std::logic_error("Array repeat missing value during MIR lowering");
  }
  size_t count = hir::helper::get_array_count(array_repeat);
  Operand value = lower_operand(*array_repeat.value);
  ArrayRepeatRValue arr_rep;
  arr_rep.value = std::move(value);
  arr_rep.count = count;
  return arr_rep;
}

ConstantRValue FunctionLowerer::build_literal_rvalue(
    const hir::Literal &lit, const semantic::ExprInfo &info) {
  if (std::get_if<hir::Literal::String>(&lit.value)) {
    if (!info.has_type || info.type == invalid_type_id) {
      throw std::logic_error(
          "String literal missing resolved type during MIR lowering");
    }
  }
  Constant constant = lower_literal(lit, info.type);
  ConstantRValue const_rval;
  const_rval.constant = std::move(constant);
  return const_rval;
}

std::optional<Operand> FunctionLowerer::try_lower_to_const(const hir::Expr &expr) {
  // Try to lower the expression as a pure constant operand without creating a temp.
  // This is useful for optimizing array repeat and other contexts where we want
  // to avoid materializing pure constants.
  if (auto *lit = std::get_if<hir::Literal>(&expr.value)) {
    semantic::ExprInfo info = hir::helper::get_expr_info(expr);
    if (std::get_if<hir::Literal::String>(&lit->value)) {
      if (!info.has_type || info.type == invalid_type_id) {
        return std::nullopt;
      }
    }
    Constant constant = lower_literal(*lit, info.type);
    Operand operand;
    operand.value = std::move(constant);
    return operand;
  }
  return std::nullopt;
}

LocalId FunctionLowerer::require_local_id(const hir::Local *local) const {
  if (!local) {
    throw std::logic_error("Local pointer missing during MIR lowering");
  }
  auto it = local_ids.find(local);
  if (it == local_ids.end()) {
    throw std::logic_error("Local not registered during MIR lowering");
  }
  return it->second;
}

Place FunctionLowerer::make_local_place(LocalId local_id) const {
  Place place;
  place.base = LocalPlace{local_id};
  return place;
}

Place FunctionLowerer::make_local_place(const hir::Local *local) const {
  return make_local_place(require_local_id(local));
}

LocalId FunctionLowerer::create_synthetic_local(TypeId type,
                                                bool is_mutable_reference) {
  if (type == invalid_type_id) {
    throw std::logic_error(
        "Synthetic local missing resolved type during MIR lowering");
  }
  TypeId normalized = canonicalize_type_for_mir(type);
  LocalId id = static_cast<LocalId>(mir_function.locals.size());
  LocalInfo info;
  info.type = normalized;
  info.debug_name = is_mutable_reference ? "_ref_mut_tmp" : "_ref_tmp";
  info.debug_name += std::to_string(synthetic_local_counter++);
  mir_function.locals.push_back(std::move(info));
  return id;
}
} // namespace detail

ExternalFunction lower_external_function(const FunctionDescriptor &descriptor) {
  ExternalFunction ext_fn;
  ext_fn.name = descriptor.name;

  // Build proto signature
  SigBuilder builder(descriptor.function_or_method);
  ProtoSig proto_sig = builder.build_proto_sig();

  // Convert proto_sig to full MirFunctionSig for external function
  ext_fn.sig.return_desc = proto_sig.return_desc;
  for (const auto &param : proto_sig.proto_params) {
    MirParam mp;
    mp.type = param.type;
    mp.debug_name = param.debug_name;
    mp.local = 0;  // external functions have no locals
    ext_fn.sig.params.push_back(std::move(mp));
  }
  
  // Populate ABI parameters
  populate_abi_params(ext_fn.sig);

  return ext_fn;
}

MirModule lower_program(const hir::Program &program) {
  std::vector<FunctionDescriptor> descriptors =
      collect_function_descriptors(program);

  // Separate descriptors into internal and external functions
  // Use is_external flag and/or check for body presence
  std::vector<FunctionDescriptor> internal_descriptors;
  std::vector<FunctionDescriptor> external_descriptors;

  for (auto &descriptor : descriptors) {
    // Builtins are explicitly marked as external
    if (descriptor.is_external) {
      external_descriptors.push_back(descriptor);
    } else {
      // User-defined functions without body are also external
      const hir::Block *body = std::visit(
          [](const auto *fn_ptr) -> const hir::Block * {
            if (!fn_ptr) {
              return nullptr;
            }
            return fn_ptr->body ? fn_ptr->body->block.get() : nullptr;
          },
          descriptor.function_or_method);

      if (body == nullptr) {
        external_descriptors.push_back(descriptor);
      } else {
        internal_descriptors.push_back(descriptor);
      }
    }
  }

  // Phase 3: Unified ID mapping
  std::unordered_map<const void *, mir::FunctionRef> function_map;
  MirModule module;

  // Process external functions first
  module.external_functions.reserve(external_descriptors.size());
  for (auto &descriptor : external_descriptors) {
    ExternalFunction::Id ext_id =
        static_cast<ExternalFunction::Id>(module.external_functions.size());

    ExternalFunction ext_fn = lower_external_function(descriptor);
    ext_fn.id = ext_id;
    module.external_functions.push_back(ext_fn);

    // Map HIR pointer to external function reference
    function_map.emplace(descriptor.key, &module.external_functions.back());
  }

  // Create placeholders for internal functions to get stable pointers
  module.functions.reserve(internal_descriptors.size());
  for (auto &descriptor : internal_descriptors) {
    FunctionId fn_id = static_cast<FunctionId>(module.functions.size());
    descriptor.id = fn_id;

    // Create placeholder function
    MirFunction placeholder;
    placeholder.id = fn_id;
    module.functions.push_back(std::move(placeholder));

    // Map HIR pointer to internal function reference
    function_map.emplace(descriptor.key, &module.functions.back());
  }

  // Lower internal function bodies with unified mapping
  for (size_t i = 0; i < internal_descriptors.size(); ++i) {
    const auto &descriptor = internal_descriptors[i];
    module.functions[i] = lower_descriptor(descriptor, function_map);
  }

  return module;
}

} // namespace mir
