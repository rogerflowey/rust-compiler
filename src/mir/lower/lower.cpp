#include "mir/lower/lower.hpp"

#include "mir/lower/lower_internal.hpp"
#include "mir/lower/lower_common.hpp"
#include "mir/lower/lower_const.hpp"

#include "semantic/hir/helper.hpp"
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
	enum class Kind { Function, Method };
	Kind kind = Kind::Function;
	const void* key = nullptr;
	const hir::Function* function = nullptr;
	const hir::Method* method = nullptr;
	std::string name;
	FunctionId id = 0;
	bool is_external = false; // NEW: Track if function is external/builtin
};

void add_function_descriptor(const hir::Function& function,
					 const std::string& scope,
					 std::vector<FunctionDescriptor>& out) {
	FunctionDescriptor descriptor;
	descriptor.kind = FunctionDescriptor::Kind::Function;
	descriptor.function = &function;
	descriptor.key = &function;
	descriptor.name = derive_function_name(function, scope);
	out.push_back(std::move(descriptor));
}

void add_method_descriptor(const hir::Method& method,
			       const std::string& scope,
			       std::vector<FunctionDescriptor>& out) {
	FunctionDescriptor descriptor;
	descriptor.kind = FunctionDescriptor::Kind::Method;
	descriptor.method = &method;
	descriptor.key = &method;
	descriptor.name = derive_method_name(method, scope);
	out.push_back(std::move(descriptor));
}

std::vector<FunctionDescriptor> collect_function_descriptors(const hir::Program& program) {
	std::vector<FunctionDescriptor> descriptors;
	
	// Phase 2: Collect predefined scope functions first (builtins)
	const semantic::Scope& predefined = semantic::get_predefined_scope();
	for (const auto& [name, symbol] : predefined.get_items_local()) {
		if (auto* fn_ptr = std::get_if<hir::Function*>(&symbol)) {
			FunctionDescriptor descriptor;
			descriptor.kind = FunctionDescriptor::Kind::Function;
			descriptor.function = *fn_ptr;
			descriptor.key = *fn_ptr;
			descriptor.name = std::string(name);
			descriptor.is_external = true; // Mark as external/builtin
			descriptors.push_back(std::move(descriptor));
		}
	}
	
	// Collect from program (user-defined)
	for (const auto& item_ptr : program.items) {
		if (!item_ptr) {
			continue;
		}
		if (auto* function = std::get_if<hir::Function>(&item_ptr->value)) {
			add_function_descriptor(*function, std::string{}, descriptors);
			continue;
		}
		if (auto* impl = std::get_if<hir::Impl>(&item_ptr->value)) {
			TypeId impl_type = hir::helper::get_resolved_type(impl->for_type);
			std::string scope = type_name(impl_type);
			for (const auto& assoc_item : impl->items) {
				if (!assoc_item) {
					continue;
				}
				if (auto* method = std::get_if<hir::Method>(&assoc_item->value)) {
					add_method_descriptor(*method, scope, descriptors);
				} else if (auto* assoc_fn = std::get_if<hir::Function>(&assoc_item->value)) {
					add_function_descriptor(*assoc_fn, scope, descriptors);
				}
			}
		}
	}
	return descriptors;
}

MirFunction lower_descriptor(const FunctionDescriptor& descriptor,
	       const std::unordered_map<const void*, mir::FunctionRef>& fn_map) {
	if (descriptor.kind == FunctionDescriptor::Kind::Function) {
		FunctionLowerer lowerer(*descriptor.function, fn_map, descriptor.id, descriptor.name);
		return lowerer.lower();
	}
	FunctionLowerer lowerer(*descriptor.method, fn_map, descriptor.id, descriptor.name);
	return lowerer.lower();
}

// FunctionLowerer declarations are provided in mir/lower/lower_internal.hpp.

} // namespace

namespace detail {

FunctionLowerer::FunctionLowerer(const hir::Function& function,
			       const std::unordered_map<const void*, mir::FunctionRef>& fn_map,
		       FunctionId id,
	       std::string name)
	: function_kind(FunctionKind::Function),
	  hir_function(&function),
	  function_map(fn_map) {
	initialize(id, std::move(name));
}

FunctionLowerer::FunctionLowerer(const hir::Method& method,
			       const std::unordered_map<const void*, mir::FunctionRef>& fn_map,
	       FunctionId id,
	       std::string name)
	: function_kind(FunctionKind::Method),
	  hir_method(&method),
	  function_map(fn_map) {
	initialize(id, std::move(name));
}

MirFunction FunctionLowerer::lower() {
	const hir::Block* body = get_body();
	if (!body) {
		if (mir_function.return_type != get_unit_type()) {
			throw std::logic_error("Non-unit function missing body during MIR lowering");
		}
	emit_return(std::nullopt);
	return std::move(mir_function);
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

const hir::Block* FunctionLowerer::get_body() const {
	if (function_kind == FunctionKind::Function) {
		return hir_function && hir_function->body ? hir_function->body.get() : nullptr;
	}
	return hir_method && hir_method->body ? hir_method->body.get() : nullptr;
}

const std::vector<std::unique_ptr<hir::Local>>& FunctionLowerer::get_locals_vector() const {
	if (function_kind == FunctionKind::Function) {
		return hir_function->locals;
	}
	return hir_method->locals;
}

TypeId FunctionLowerer::resolve_return_type() const {
	const auto& annotation = (function_kind == FunctionKind::Function)
		? hir_function->return_type
		: hir_method->return_type;
	if (annotation) {
		return hir::helper::get_resolved_type(*annotation);
	}
	return get_unit_type();
}

void FunctionLowerer::init_locals() {
	auto register_local = [this](const hir::Local* local_ptr) {
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

	if (function_kind == FunctionKind::Method && hir_method && hir_method->self_local) {
		register_local(hir_method->self_local.get());
	}

	for (const auto& local_ptr : get_locals_vector()) {
		if (local_ptr) {
			register_local(local_ptr.get());
		}
	}
}

void FunctionLowerer::collect_parameters() {
	if (function_kind == FunctionKind::Method && hir_method) {
		append_self_parameter();
		append_explicit_parameters(hir_method->params, hir_method->param_type_annotations);
		return;
	}
	if (function_kind == FunctionKind::Function && hir_function) {
		append_explicit_parameters(hir_function->params, hir_function->param_type_annotations);
	}
}

void FunctionLowerer::append_self_parameter() {
	if (!hir_method) {
		throw std::logic_error("Method context missing during MIR lowering");
	}
	if (!hir_method->self_local) {
		return;
	}
	if (!hir_method->self_local->type_annotation) {
		throw std::logic_error("Method self parameter missing resolved type during MIR lowering");
	}
	TypeId self_type = hir::helper::get_resolved_type(*hir_method->self_local->type_annotation);
	append_parameter(hir_method->self_local.get(), self_type);
}

void FunctionLowerer::append_explicit_parameters(const std::vector<std::unique_ptr<hir::Pattern>>& params,
			   const std::vector<std::optional<hir::TypeAnnotation>>& annotations) {
	if (params.size() != annotations.size()) {
		throw std::logic_error("Parameter/type annotation mismatch during MIR lowering");
	}
	for (std::size_t i = 0; i < params.size(); ++i) {
		const auto& param = params[i];
		if (!param) {
			continue;
		}
		const auto& annotation = annotations[i];
		if (!annotation) {
			throw std::logic_error("Parameter missing resolved type during MIR lowering");
		}
		TypeId param_type = hir::helper::get_resolved_type(*annotation);
		append_parameter(resolve_pattern_local(*param), param_type);
	}
}

void FunctionLowerer::append_parameter(const hir::Local* local, TypeId type) {
	if (!local) {
		throw std::logic_error("Parameter pattern did not resolve to a Local during MIR lowering");
	}
	if (type == invalid_type_id) {
		throw std::logic_error("Parameter missing resolved type during MIR lowering");
	}
	TypeId normalized = canonicalize_type_for_mir(type);
	LocalId local_id = require_local_id(local);
	FunctionParameter param;
	param.local = local_id;
	param.type = normalized;
	param.name = local->name.name;
	mir_function.params.push_back(std::move(param));
}

const hir::Local* FunctionLowerer::resolve_pattern_local(const hir::Pattern& pattern) const {
	if (const auto* binding = std::get_if<hir::BindingDef>(&pattern.value)) {
		if (auto* local_ptr = std::get_if<hir::Local*>(&binding->local)) {
			return *local_ptr;
		}
		throw std::logic_error("Binding definition missing resolved Local during MIR lowering");
	}
	if (const auto* reference = std::get_if<hir::ReferencePattern>(&pattern.value)) {
		if (!reference->subpattern) {
			throw std::logic_error("Reference pattern missing subpattern during MIR lowering");
		}
		return resolve_pattern_local(*reference->subpattern);
	}
	throw std::logic_error("Unsupported pattern variant in parameter lowering");
}

bool FunctionLowerer::is_reachable() const {
	return current_block.has_value();
}

void FunctionLowerer::require_reachable(const char* context) const {
	if (!is_reachable()) {
		throw std::logic_error(std::string("Unreachable code encountered in ") + context);
	}
}

mir::FunctionRef FunctionLowerer::lookup_function(const void* key) const {
	auto it = function_map.find(key);
	if (it == function_map.end()) {
		throw std::logic_error("Call target not registered during MIR lowering");
	}
	return it->second;
}

	std::optional<Operand> FunctionLowerer::emit_call(mir::FunctionRef target,
		           TypeId result_type,
		           std::vector<Operand>&& args) {
	bool result_needed = !is_unit_type(result_type) && !is_never_type(result_type);
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
	if (auto* internal = std::get_if<MirFunction*>(&target)) {
		call_stmt.target.kind = mir::CallTarget::Kind::Internal;
		call_stmt.target.id = (*internal)->id;
	} else if (auto* external = std::get_if<ExternalFunction*>(&target)) {
		call_stmt.target.kind = mir::CallTarget::Kind::External;
		call_stmt.target.id = (*external)->id;
	}
	
	call_stmt.args = std::move(args);
	Statement stmt;
	stmt.value = std::move(call_stmt);
	append_statement(std::move(stmt));
	return result;
}

Operand FunctionLowerer::emit_aggregate(AggregateRValue aggregate, TypeId result_type) {
	return emit_rvalue(std::move(aggregate), result_type);
}

Operand FunctionLowerer::emit_array_repeat(Operand value,
	                                   std::size_t count,
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
		throw std::logic_error("Temporary missing resolved type during MIR lowering");
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

void FunctionLowerer::switch_to_block(BasicBlockId id) {
	current_block = id;
}

void FunctionLowerer::branch_on_bool(const Operand& condition,
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

TempId FunctionLowerer::materialize_operand(const Operand& operand, TypeId type) {
	if (const auto* temp = std::get_if<TempId>(&operand.value)) {
		return *temp;
	}
	if (!current_block) {
		throw std::logic_error("Cannot materialize operand without active block");
	}
	if (type == invalid_type_id) {
		throw std::logic_error("Operand missing resolved type during materialization");
	}
	TypeId normalized = canonicalize_type_for_mir(type);
	const auto* constant = std::get_if<Constant>(&operand.value);
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
	if (!current_block) {
		return;
	}
	ReturnTerminator ret{std::move(value)};
	terminate_current_block(Terminator{std::move(ret)});
}

FunctionLowerer::LoopContext& FunctionLowerer::push_loop_context(const void* key,
					     BasicBlockId continue_block,
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

FunctionLowerer::LoopContext& FunctionLowerer::lookup_loop_context(const void* key) {
	for (auto it = loop_stack.rbegin(); it != loop_stack.rend(); ++it) {
		if (it->first == key) {
			return it->second;
		}
	}
	throw std::logic_error("Loop context not found");
}

FunctionLowerer::LoopContext FunctionLowerer::pop_loop_context(const void* key) {
	if (loop_stack.empty() || loop_stack.back().first != key) {
		throw std::logic_error("Loop context stack corrupted");
	}
	LoopContext ctx = std::move(loop_stack.back().second);
	loop_stack.pop_back();
	return ctx;
}

void FunctionLowerer::finalize_loop_context(const LoopContext& ctx) {
	if (ctx.break_result) {
		if (ctx.break_incomings.empty()) {
			throw std::logic_error("Loop expression expects value but no break produced one");
		}
		PhiNode phi;
		phi.dest = *ctx.break_result;
		phi.incoming = ctx.break_incomings;
		mir_function.basic_blocks[ctx.break_block].phis.push_back(std::move(phi));
	}
}

bool FunctionLowerer::lower_block_statements(const hir::Block& block) {
	for (const auto& stmt : block.stmts) {
		if (!is_reachable()) {
			return false;
		}
		if (stmt) {
			lower_statement(*stmt);
		}
	}
	return is_reachable();
}

void FunctionLowerer::lower_block(const hir::Block& hir_block) {
	if (!lower_block_statements(hir_block)) {
		return;
	}

	if (hir_block.final_expr) {
		const auto& expr_ptr = *hir_block.final_expr;
		if (!expr_ptr) {
			emit_return(std::nullopt);
			return;
		}
		std::optional<Operand> value = lower_expr(*expr_ptr);
		if (!is_reachable()) {
            return;
        }
		if (!value && !is_unit_type(mir_function.return_type) && !is_never_type(mir_function.return_type)) {
			throw std::logic_error("Missing return value for function requiring return value");
		}
		emit_return(std::move(value));
		return;
	}

	if (mir_function.return_type == get_unit_type() || is_never_type(mir_function.return_type)) {
		emit_return(std::nullopt);
	} else {
		throw std::logic_error("Missing final expression for non-unit function");
	}
}

	std::optional<Operand> FunctionLowerer::lower_block_expr(const hir::Block& block, TypeId expected_type) {
	if (!lower_block_statements(block)) {
		return std::nullopt;
	}

	if (block.final_expr) {
		const auto& expr_ptr = *block.final_expr;
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

void FunctionLowerer::lower_statement(const hir::Stmt& stmt) {
	if (!is_reachable()) {
		return;
	}
	std::visit([this](const auto& node) { lower_statement_impl(node); }, stmt.value);
}

void FunctionLowerer::lower_statement_impl(const hir::LetStmt& let_stmt) {
	if (!is_reachable()) {
		return;
	}
	if (!let_stmt.pattern) {
		throw std::logic_error("Let statement missing pattern during MIR lowering");
	}
	if (!let_stmt.initializer) {
		throw std::logic_error("Let statement without initializer not supported in MIR lowering");
	}
	Operand value = expect_operand(lower_expr(*let_stmt.initializer), "Let initializer must produce value");
	lower_pattern_store(*let_stmt.pattern, value);
}

void FunctionLowerer::lower_statement_impl(const hir::ExprStmt& expr_stmt) {
	if (!is_reachable()) {
		return;
	}
	if (expr_stmt.expr) {
		(void)lower_expr(*expr_stmt.expr);
	}
}

void FunctionLowerer::lower_pattern_store(const hir::Pattern& pattern, Operand value) {
	std::visit([this, &value](const auto& pat) { lower_pattern_store_impl(pat, value); }, pattern.value);
}

void FunctionLowerer::lower_pattern_store_impl(const hir::BindingDef& binding, const Operand& value) {
	hir::Local* local = hir::helper::get_local(binding);
	if (local && local->name.name == "_") {
		return;
	}
	Place dest = make_local_place(local);
	AssignStatement assign{.dest = std::move(dest), .src = value};
	Statement stmt;
	stmt.value = std::move(assign);
	append_statement(std::move(stmt));
}

void FunctionLowerer::lower_pattern_store_impl(const hir::ReferencePattern&, const Operand&) {
	throw std::logic_error("Reference patterns not yet supported in MIR lowering");
}

LocalId FunctionLowerer::require_local_id(const hir::Local* local) const {
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

Place FunctionLowerer::make_local_place(const hir::Local* local) const {
	return make_local_place(require_local_id(local));
}

LocalId FunctionLowerer::create_synthetic_local(TypeId type,
					      bool is_mutable_reference) {
	if (type == invalid_type_id) {
		throw std::logic_error("Synthetic local missing resolved type during MIR lowering");
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

MirFunction lower_function(const hir::Function& function,
	       const std::unordered_map<const void*, FunctionId>& id_map,
	       FunctionId id) {
	// Convert old FunctionId map to new FunctionRef map (this is only for backwards compatibility)
	(void)id_map;  // Unused in new design
	std::unordered_map<const void*, mir::FunctionRef> fn_map;
	// Not used in this path since we don't call external functions from standalone lowering
	FunctionLowerer lowerer(function, fn_map, id, derive_function_name(function, std::string{}));
	return lowerer.lower();
}

MirFunction lower_function(const hir::Function& function) {
	std::unordered_map<const void*, mir::FunctionRef> fn_map;
	return FunctionLowerer(function, fn_map, 0, derive_function_name(function, std::string{})).lower();
}

ExternalFunction lower_external_function(const FunctionDescriptor& descriptor) {
	ExternalFunction ext_fn;
	ext_fn.name = descriptor.name;
	
	std::vector<TypeId> param_types;
	TypeId return_type = type::invalid_type_id;
	
	if (descriptor.kind == FunctionDescriptor::Kind::Function && descriptor.function) {
		// Extract return type
		if (descriptor.function->return_type) {
			return_type = hir::helper::get_resolved_type(*descriptor.function->return_type);
		} else {
			return_type = type::invalid_type_id;
		}
		
		// Extract parameter types from param_type_annotations
		for (const auto& param_annotation : descriptor.function->param_type_annotations) {
			if (param_annotation) {
				TypeId param_type = hir::helper::get_resolved_type(*param_annotation);
				param_types.push_back(param_type);
			}
		}
	} else if (descriptor.kind == FunctionDescriptor::Kind::Method && descriptor.method) {
		// Extract return type
		if (descriptor.method->return_type) {
			return_type = hir::helper::get_resolved_type(*descriptor.method->return_type);
		} else {
			return_type = type::invalid_type_id;
		}
		
		// Extract parameter types from param_type_annotations
		for (const auto& param_annotation : descriptor.method->param_type_annotations) {
			if (param_annotation) {
				TypeId param_type = hir::helper::get_resolved_type(*param_annotation);
				param_types.push_back(param_type);
			}
		}
	}
	
	ext_fn.return_type = return_type;
	ext_fn.param_types = std::move(param_types);
	
	return ext_fn;
}

MirModule lower_program(const hir::Program& program) {
	std::vector<FunctionDescriptor> descriptors = collect_function_descriptors(program);
	
	// Separate descriptors into internal and external functions
	// Use is_external flag and/or check for body presence
	std::vector<FunctionDescriptor> internal_descriptors;
	std::vector<FunctionDescriptor> external_descriptors;
	
	for (auto& descriptor : descriptors) {
		// Builtins are explicitly marked as external
		if (descriptor.is_external) {
			external_descriptors.push_back(descriptor);
		} else {
			// User-defined functions without body are also external
			const hir::Block* body = nullptr;
			if (descriptor.kind == FunctionDescriptor::Kind::Function && descriptor.function) {
				body = descriptor.function->body.get();
			} else if (descriptor.kind == FunctionDescriptor::Kind::Method && descriptor.method) {
				body = descriptor.method->body.get();
			}
			
			if (body == nullptr) {
				external_descriptors.push_back(descriptor);
			} else {
				internal_descriptors.push_back(descriptor);
			}
		}
	}
	
	// Phase 3: Unified ID mapping
	std::unordered_map<const void*, mir::FunctionRef> function_map;
	MirModule module;
	
	// Process external functions first
	module.external_functions.reserve(external_descriptors.size());
	for (auto& descriptor : external_descriptors) {
		ExternalFunction::Id ext_id = static_cast<ExternalFunction::Id>(module.external_functions.size());
		
		ExternalFunction ext_fn = lower_external_function(descriptor);
		ext_fn.id = ext_id;
		module.external_functions.push_back(ext_fn);
		
		// Map HIR pointer to external function reference
		function_map.emplace(descriptor.key, &module.external_functions.back());
	}
	
	// Create placeholders for internal functions to get stable pointers
	module.functions.reserve(internal_descriptors.size());
	for (auto& descriptor : internal_descriptors) {
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
		const auto& descriptor = internal_descriptors[i];
		module.functions[i] = lower_descriptor(descriptor, function_map);
	}
	
	return module;
}

} // namespace mir
