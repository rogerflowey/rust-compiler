// opt/mir/lower/lower.cpp — Module-level lowering + OptFunctionLowerer core
//
// Adapted from mir/lower/lower.cpp. Emits opt MIR with explicit token
// threading (sea-of-nodes style) instead of old MIR's implicit ordering.

#include "opt/mir/lower/lower.hpp"
#include "opt/mir/lower/lower_internal.hpp"
#include "opt/mir/nodes.hpp"

#include "mir/lower/lower_common.hpp" // shared type helpers

#include "semantic/hir/helper.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include "semantic/symbol/predefined.hpp"

#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace opt::mir {

using type::invalid_type_id;
using type::TypeId;

// ═══════════════════════════════════════════════════════════════════
// Module-level helpers (collect descriptors, build function map)
// ═══════════════════════════════════════════════════════════════════

namespace {

struct FunctionDescriptor {
  std::variant<const hir::Function *, const hir::Method *> fn;
  const void *key = nullptr;
  std::string name;
  bool is_external = false;
};

void add_function_descriptor(const hir::Function &f, const std::string &scope,
                             std::vector<FunctionDescriptor> &out) {
  FunctionDescriptor d;
  d.fn = &f;
  d.key = &f;
  d.name = ::mir::detail::derive_function_name(f, scope);
  d.is_external = !f.body.has_value();
  out.push_back(std::move(d));
}

void add_method_descriptor(const hir::Method &m, const std::string &scope,
                           std::vector<FunctionDescriptor> &out) {
  FunctionDescriptor d;
  d.fn = &m;
  d.key = &m;
  d.name = ::mir::detail::derive_method_name(m, scope);
  d.is_external = !m.body.has_value();
  out.push_back(std::move(d));
}

std::vector<FunctionDescriptor>
collect_function_descriptors(const hir::Program &program) {
  std::vector<FunctionDescriptor> descriptors;

  // Phase 1: predefined builtins
  const semantic::Scope &predefined = semantic::get_predefined_scope();
  for (const auto &[name, symbol] : predefined.get_items_local()) {
    if (auto *fn_ptr = std::get_if<hir::Function *>(&symbol)) {
      FunctionDescriptor d;
      d.fn = *fn_ptr;
      d.key = *fn_ptr;
      d.name = std::string(name);
      d.is_external = true;
      descriptors.push_back(std::move(d));
    }
  }

  // Phase 2: walk HIR for all functions/methods (mirrors old collector)
  struct Collector : hir::HirVisitorBase<Collector> {
    std::vector<FunctionDescriptor> &out;
    std::string current_scope;

    using Base = hir::HirVisitorBase<Collector>;
    using Base::visit;
    using Base::visit_block;

    Collector(std::vector<FunctionDescriptor> &out) : out(out) {}

    void visit(hir::Function &f) {
      add_function_descriptor(f, current_scope, out);
      Base::visit(f);
    }
    void visit(hir::Impl &impl) {
      TypeId impl_type = hir::helper::get_resolved_type(impl.for_type);
      std::string saved = current_scope;
      current_scope = ::mir::detail::type_name(impl_type);
      Base::visit(impl);
      current_scope = std::move(saved);
    }
    void visit(hir::Method &m) {
      add_method_descriptor(m, current_scope, out);
      Base::visit(m);
    }
    void visit_block(hir::Block &b) { Base::visit_block(b); }
  };

  Collector collector{descriptors};
  collector.visit_program(const_cast<hir::Program &>(program));
  return descriptors;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════
// lower_program — public entry point
// ═══════════════════════════════════════════════════════════════════

OptModule lower_program(const hir::Program &program) {
  auto descriptors = collect_function_descriptors(program);

  // Build function map: HIR pointer → CallTarget
  std::unordered_map<const void *, CallTarget> func_map;
  for (std::uint32_t i = 0; i < descriptors.size(); ++i) {
    CallTarget target;
    target.kind = descriptors[i].is_external ? CallTarget::Kind::External
                                             : CallTarget::Kind::Internal;
    target.id = i;
    target.name = descriptors[i].name;
    func_map[descriptors[i].key] = target;
  }

  OptModule module;
  for (const auto &desc : descriptors) {
    bool has_body = std::visit(
        [](const auto *ptr) -> bool { return ptr && ptr->body.has_value(); },
        desc.fn);
    if (!has_body) {
      continue; // skip external/builtin declarations
    }
    OptFunction func = std::visit(
        [&](const auto *ptr) -> OptFunction {
          OptFunctionLowerer lowerer(*ptr, func_map, desc.name);
          return lowerer.lower();
        },
        desc.fn);
    module.functions.push_back(std::move(func));
  }
  return module;
}

// ═══════════════════════════════════════════════════════════════════
// OptFunctionLowerer — construction
// ═══════════════════════════════════════════════════════════════════

OptFunctionLowerer::OptFunctionLowerer(
    const hir::Function &function,
    const std::unordered_map<const void *, CallTarget> &func_map,
    std::string name)
    : function_kind_(FunctionKind::Function), hir_function_(&function),
      func_map_(func_map) {
  initialize(std::move(name));
}

OptFunctionLowerer::OptFunctionLowerer(
    const hir::Method &method,
    const std::unordered_map<const void *, CallTarget> &func_map,
    std::string name)
    : function_kind_(FunctionKind::Method), hir_method_(&method),
      func_map_(func_map) {
  initialize(std::move(name));
}

void OptFunctionLowerer::initialize(std::string name) {
  func_.name = std::move(name);
  register_locals();

  // Create entry block and initial token
  BlockId entry = builder_.new_block();
  current_block_ = entry;
  func_.entry_block = entry;

  // The entry token seeds the token chain for the entire function
  current_token_ = builder_.entry_token();
}

OptFunction OptFunctionLowerer::lower() {
  const hir::Block *body = get_body();
  if (!body) {
    throw std::logic_error("Function missing body during opt MIR lowering");
  }
  lower_block(*body);

  // If still reachable, emit an implicit void return
  if (is_reachable()) {
    builder_.emit_return(current_block_id(), current_token_, std::nullopt);
    current_block_.reset();
  }
  return std::move(func_);
}

const hir::Block *OptFunctionLowerer::get_body() const {
  if (function_kind_ == FunctionKind::Function) {
    return (hir_function_ && hir_function_->body)
               ? hir_function_->body->block.get()
               : nullptr;
  }
  return (hir_method_ && hir_method_->body) ? hir_method_->body->block.get()
                                            : nullptr;
}

const std::vector<std::unique_ptr<hir::Local>> &
OptFunctionLowerer::get_locals() const {
  if (function_kind_ == FunctionKind::Function) {
    return hir_function_->body->locals;
  }
  return hir_method_->body->locals;
}

// ═══════════════════════════════════════════════════════════════════
// Local registration — create a Slot for each HIR Local
// ═══════════════════════════════════════════════════════════════════

void OptFunctionLowerer::register_locals() {
  // Self parameter for methods
  if (function_kind_ == FunctionKind::Method && hir_method_ &&
      hir_method_->body && hir_method_->body->self_local) {
    register_local(hir_method_->body->self_local.get());
  }
  for (const auto &local_ptr : get_locals()) {
    if (local_ptr) {
      register_local(local_ptr.get());
    }
  }
}

SlotId OptFunctionLowerer::register_local(const hir::Local *local) {
  if (!local) {
    throw std::logic_error("Null local during opt MIR lowering");
  }
  if (!local->type_annotation) {
    throw std::logic_error(
        "Local missing resolved type during opt MIR lowering");
  }
  TypeId type = hir::helper::get_resolved_type(*local->type_annotation);
  TypeId normalized = ::mir::detail::canonicalize_type_for_mir(type);

  SlotId id =
      builder_.new_slot(Slot::Kind::StackLocal, normalized, local->name.name);
  local_slots_.emplace(local, id);
  return id;
}

// ═══════════════════════════════════════════════════════════════════
// Block / Statement lowering
// ═══════════════════════════════════════════════════════════════════

void OptFunctionLowerer::lower_block(const hir::Block &block) {
  lower_block_statements(block);
  if (block.final_expr && *block.final_expr && is_reachable()) {
    // Function body: final expression is the return value
    auto result = lower_expr(**block.final_expr);
    if (result && is_reachable()) {
      builder_.emit_return(current_block_id(), current_token_, *result);
      current_block_.reset();
    }
  }
}

std::optional<NodeId>
OptFunctionLowerer::lower_block_expr(const hir::Block &block,
                                     type::TypeId expected_type) {
  lower_block_statements(block);
  if (block.final_expr && *block.final_expr && is_reachable()) {
    return lower_expr(**block.final_expr);
  }
  return std::nullopt;
}

bool OptFunctionLowerer::lower_block_statements(const hir::Block &block) {
  for (const auto &stmt : block.stmts) {
    if (!is_reachable()) {
      return false;
    }
    if (!stmt) {
      continue;
    }
    lower_statement(*stmt);
  }
  return is_reachable();
}

void OptFunctionLowerer::lower_statement(const hir::Stmt &stmt) {
  require_reachable("lower_statement");
  if (const auto *let_stmt = std::get_if<hir::LetStmt>(&stmt.value)) {
    lower_let_stmt(*let_stmt);
  } else if (const auto *expr_stmt = std::get_if<hir::ExprStmt>(&stmt.value)) {
    if (expr_stmt->expr) {
      (void)lower_expr(*expr_stmt->expr);
    }
  }
}

void OptFunctionLowerer::lower_let_stmt(const hir::LetStmt &let_stmt) {
  // Resolve the local for the let binding
  const hir::Local *local = nullptr;
  if (let_stmt.pattern) {
    if (const auto *binding =
            std::get_if<hir::BindingDef>(&let_stmt.pattern->value)) {
      if (auto *local_ptr = std::get_if<hir::Local *>(&binding->local)) {
        local = *local_ptr;
      }
    }
  }
  if (!local) {
    // Underscore or unsupported pattern — lower init for side effects only
    if (let_stmt.initializer) {
      (void)lower_expr(*let_stmt.initializer);
    }
    return;
  }

  SlotId slot = require_slot(local);

  if (let_stmt.initializer) {
    auto init_value = lower_expr(*let_stmt.initializer);
    if (init_value && is_reachable()) {
      current_token_ = builder_.emit_store(current_block_id(), current_token_,
                                           slot, *init_value);
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
// Loop context management
// ═══════════════════════════════════════════════════════════════════

OptFunctionLowerer::LoopContext &
OptFunctionLowerer::push_loop(const void *key, BlockId header, BlockId exit,
                              TokenId header_phi_token,
                              std::optional<type::TypeId> break_type) {
  LoopContext ctx;
  ctx.header_block = header;
  ctx.exit_block = exit;
  ctx.header_phi_token = header_phi_token;
  ctx.break_type = break_type;
  if (break_type && !::mir::detail::is_unit_type(*break_type) &&
      !::mir::detail::is_never_type(*break_type)) {
    ctx.break_result_slot = builder_.new_slot(Slot::Kind::StackLocal,
                                              *break_type, "<break_result>");
  }
  loop_stack_.push_back({key, std::move(ctx)});
  return loop_stack_.back().second;
}

OptFunctionLowerer::LoopContext &
OptFunctionLowerer::find_loop(const void *key) {
  for (auto it = loop_stack_.rbegin(); it != loop_stack_.rend(); ++it) {
    if (it->first == key) {
      return it->second;
    }
  }
  throw std::logic_error("Loop context not found during opt MIR lowering");
}

OptFunctionLowerer::LoopContext OptFunctionLowerer::pop_loop(const void *key) {
  if (loop_stack_.empty() || loop_stack_.back().first != key) {
    throw std::logic_error(
        "Loop context stack mismatch during opt MIR lowering");
  }
  LoopContext ctx = std::move(loop_stack_.back().second);
  loop_stack_.pop_back();
  return ctx;
}

void OptFunctionLowerer::finalize_loop(const LoopContext &ctx) {
  // Insert header TokenPhi (entry + back-edges + continues)
  if (!ctx.header_incoming.empty()) {
    // Build the TokenPhi instruction manually and insert it at the
    // start of the header block so that its output token (pre-allocated)
    // is available to all instructions in the header.
    TokenPhiInst phi;
    phi.incoming.reserve(ctx.header_incoming.size());
    for (auto &[bid, tid] : ctx.header_incoming) {
      phi.incoming.push_back(TokenPhiIncoming{bid, tid});
    }
    phi.t_out = ctx.header_phi_token;

    PinnedInst pinned;
    pinned.kind = phi;
    auto &header = func_.blocks[static_cast<std::size_t>(ctx.header_block)];
    header.instructions.insert(header.instructions.begin(), std::move(pinned));
  }
}

// ═══════════════════════════════════════════════════════════════════
// Utilities
// ═══════════════════════════════════════════════════════════════════

bool OptFunctionLowerer::is_reachable() const {
  return current_block_.has_value();
}

void OptFunctionLowerer::require_reachable(const char *ctx) const {
  if (!is_reachable()) {
    throw std::logic_error(std::string("Unreachable code in ") + ctx);
  }
}

BlockId OptFunctionLowerer::current_block_id() const {
  if (!current_block_) {
    throw std::logic_error("No current block in opt MIR lowering");
  }
  return *current_block_;
}

void OptFunctionLowerer::switch_to_block(BlockId block, TokenId token) {
  current_block_ = block;
  current_token_ = token;
}

void OptFunctionLowerer::jump_to(BlockId target) {
  require_reachable("jump_to");
  builder_.emit_jump(current_block_id(), current_token_, target);
  current_block_.reset();
}

SlotId OptFunctionLowerer::require_slot(const hir::Local *local) const {
  auto it = local_slots_.find(local);
  if (it == local_slots_.end()) {
    throw std::logic_error("Local not mapped to slot in opt MIR lowering");
  }
  return it->second;
}

NodeId OptFunctionLowerer::make_const_int(std::uint64_t value,
                                          type::TypeId type, bool is_signed) {
  ConstantValue cv;
  cv.kind = ConstantValue::Kind::Int;
  cv.bits = value;
  cv.is_signed = is_signed;
  return builder_.make_constant(cv, type);
}

NodeId OptFunctionLowerer::make_const_bool(bool value) {
  ConstantValue cv;
  cv.kind = ConstantValue::Kind::Bool;
  cv.bits = value ? 1ULL : 0ULL;
  cv.is_signed = false;
  return builder_.make_constant(cv, ::mir::detail::get_bool_type());
}

SlotId OptFunctionLowerer::allocate_temp_slot(type::TypeId type,
                                              std::string name) {
  return builder_.new_slot(Slot::Kind::Temp, type, std::move(name),
                           Mutability::Mutable);
}

void OptFunctionLowerer::emit_aggregate_copy(SlotId dest, SlotId src,
                                             type::TypeId type) {
  current_token_ =
      builder_.emit_memcopy(current_block_id(), current_token_,
                            Place::simple(dest), Place::simple(src), type);
}

} // namespace opt::mir
