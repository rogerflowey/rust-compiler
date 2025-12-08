#include "mir/lower/lower.hpp"

#include "mir.hpp"
#include "mir/lower/lower_common.hpp"
#include "mir/lower/lower_const.hpp"
#include "mir/lower/lower_internal.hpp"

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
  TypeId return_type = resolve_return_type();
  mir_function.return_type = canonicalize_type_for_mir(return_type);
  init_locals();
  collect_parameters();
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
  FunctionParameter param;
  param.local = local_id;
  param.type = normalized;
  param.name = local->name.name;
  mir_function.params.push_back(std::move(param));
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
  return emit_rvalue(std::move(aggregate), result_type);
}

Operand FunctionLowerer::emit_array_repeat(Operand value, std::size_t count,
                                           TypeId result_type) {
  ArrayRepeatRValue repeat;
  repeat.value = std::move(value);
  repeat.count = count;
  return emit_rvalue(std::move(repeat), result_type);
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

void FunctionLowerer::emit_return(std::optional<Operand> value) {
    
    TypeId ret_ty = mir_function.return_type;
    if (is_never_type(ret_ty)) {
        throw std::logic_error(
            "emit_return called for never-returning function during MIR lowering: " +
            mir_function.name);
    }

    if (!value && !is_unit_type(ret_ty)) {
        throw std::logic_error(
            "emit_return called without value for non-unit function: " +
            mir_function.name);
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
    TypeId ret_ty = mir_function.return_type;
    // === CASE 1: Block has an explicit final expression ===
    if (hir_block.final_expr) {
        const auto &expr_ptr = *hir_block.final_expr;
        if (!expr_ptr) {
            throw std::logic_error("Ownership violated: Final expression");
        }
        std::optional<Operand> value = lower_expr(*expr_ptr);

        if (!is_reachable()) {
            return;
        }

        if (is_never_type(ret_ty)) {
            throw std::logic_error("Function promising diverge does not diverge");
        }
        if (!value && !is_unit_type(ret_ty)) {
            throw std::logic_error(
                "Missing return value for function requiring return value");
        }

        // Unit-returning: value may be empty; emit_return(nullptr) → ret void.
        emit_return(std::move(value));
        return;
    }

    if (!is_reachable()) {
        return;
    }

    if (is_never_type(ret_ty)) {
        throw std::logic_error("Function promising diverge dos not diverge");
    }

    if (is_unit_type(ret_ty)) {
        emit_return(std::nullopt);
        return;
    }
    throw std::logic_error("Non-unit,Non-diverged function does not have proper final return");
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
  PatternValue pval;

  // Check if the pattern is an underscore binding that won't store the value
  // In that case, always fully lower the expression for side-effects
  bool is_underscore_binding = false;
  if (auto *binding = std::get_if<hir::BindingDef>(&let_stmt.pattern->value)) {
    if (hir::Local *local = hir::helper::get_local(*binding)) {
      if (local->name.name == "_") {
        is_underscore_binding = true;
      }
    }
  }

  if (is_underscore_binding) {
    // For underscore bindings, always lower the expression to ensure
    // side-effects are captured, even though the value won't be stored
    (void)lower_expr(init_expr);
    return;
  }

  // First try: can we treat this as a pure RValue init?
  std::optional<RValue> rvalue_opt = lower_expr_as_rvalue(init_expr);
  if (rvalue_opt) {
    pval.mode = PatternStoreMode::Initialize;
    pval.value = std::move(*rvalue_opt);
  } else {
    // Fallback: lower to Operand and treat as normal assignment
    Operand value = expect_operand(lower_expr(init_expr),
                                   "Let initializer must produce value");
    pval.mode = PatternStoreMode::Assign;
    pval.value = std::move(value);
  }

  // Let the pattern decide how to store this value
  lower_pattern_store(*let_stmt.pattern, pval);
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

void FunctionLowerer::lower_pattern_store(const hir::Pattern &pattern,
                                          const PatternValue &pval) {
  std::visit(
      [this, &pval](const auto &pat) { lower_pattern_store_impl(pat, pval); },
      pattern.value);
}

std::optional<RValue> FunctionLowerer::try_lower_pure_rvalue(
  const hir::Expr &expr, const semantic::ExprInfo &info) {
  return std::visit(
    Overloaded{
      [this](const hir::StructLiteral &struct_lit) -> std::optional<RValue> {
        RValue rv;
        rv.value = build_struct_aggregate(struct_lit);
        return rv;
      },
      [this](const hir::ArrayLiteral &array_lit) -> std::optional<RValue> {
        RValue rv;
        rv.value = build_array_aggregate(array_lit);
        return rv;
      },
      [this](const hir::ArrayRepeat &array_rep) -> std::optional<RValue> {
        RValue rv;
        rv.value = build_array_repeat_rvalue(array_rep);
        return rv;
      },
      [this, &info](const hir::Literal &lit) -> std::optional<RValue> {
        RValue rv;
        rv.value = build_literal_rvalue(lit, info);
        return rv;
      },
      [](const auto &) -> std::optional<RValue> {
        return std::nullopt;
      },
    },
    expr.value);
}

std::optional<RValue> FunctionLowerer::lower_expr_as_rvalue(
    const hir::Expr &expr) {
  semantic::ExprInfo info = hir::helper::get_expr_info(expr);
  return try_lower_pure_rvalue(expr, info);
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
    aggregate.elements.push_back(expect_operand(
        lower_expr(*initializer), "Struct literal field must produce value"));
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
    aggregate.elements.push_back(expect_operand(
        lower_expr(*element), "Array element must produce value"));
  }
  return aggregate;
}

ArrayRepeatRValue FunctionLowerer::build_array_repeat_rvalue(
    const hir::ArrayRepeat &array_repeat) {
  if (!array_repeat.value) {
    throw std::logic_error("Array repeat missing value during MIR lowering");
  }
  size_t count = hir::helper::get_array_count(array_repeat);
  Operand value = expect_operand(lower_expr(*array_repeat.value),
                                 "Array repeat value must produce operand");
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

void FunctionLowerer::lower_pattern_store_impl(const hir::BindingDef &binding,
                                               const PatternValue &pval) {
  hir::Local *local = hir::helper::get_local(binding);
  if (!local || local->name.name == "_") {
    // `_` binding: initializer already lowered for side-effects,
    // nothing to store.
    return;
  }

  Place dest = make_local_place(local);

  // Initialize mode with an RValue → InitializeStatement
  if (pval.mode == PatternStoreMode::Initialize) {
    if (const auto *rv = std::get_if<RValue>(&pval.value)) {
      InitializeStatement init_stmt;
      init_stmt.dest = std::move(dest);
      init_stmt.rvalue = *rv; // copy; you can std::move if you want
      Statement stmt;
      stmt.value = std::move(init_stmt);
      append_statement(std::move(stmt));
      return;
    }
    // If mode is Initialize but we got an Operand, just fall through and treat
    // it as a normal assignment. That shouldn't happen with current callers,
    // but it's a safe, reasonable fallback.
  }

  // Normal assignment path (Assign mode, or Initialize-without-RValue)
  const auto *operand = std::get_if<Operand>(&pval.value);
  if (!operand) {
    throw std::logic_error(
        "Pattern assignment expects Operand value but got RValue");
  }

  AssignStatement assign;
  assign.dest = std::move(dest);
  assign.src  = *operand;
  Statement stmt;
  stmt.value = std::move(assign);
  append_statement(std::move(stmt));
}

void FunctionLowerer::lower_pattern_store_impl(const hir::ReferencePattern &,
                                               const PatternValue &) {
  throw std::logic_error(
      "Reference patterns not yet supported in MIR lowering");
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

  std::vector<TypeId> param_types;
  TypeId return_type = type::invalid_type_id;

  std::visit(
      [&return_type, &param_types](const auto *fn_ptr) {
        if (!fn_ptr) {
          return;
        }
        // Extract return type
        if (fn_ptr->sig.return_type) {
          return_type =
              hir::helper::get_resolved_type(*fn_ptr->sig.return_type);
        } else {
          return_type = type::invalid_type_id;
        }

        // Extract parameter types from param_type_annotations
        for (const auto &param_annotation :
             fn_ptr->sig.param_type_annotations) {
          TypeId param_type =
              hir::helper::get_resolved_type(param_annotation);
          param_types.push_back(param_type);
        }
      },
      descriptor.function_or_method);

  ext_fn.return_type = return_type;
  ext_fn.param_types = std::move(param_types);

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
