#pragma once

#include "mir/mir.hpp"
#include "mir/lower-v2/lower_result.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/pass/semantic_check/expr_info.hpp"
#include "type/helper.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mir::lower_v2 {

/// FunctionLowerer V2: Main controller for MIR lowering
///
/// Design principles:
/// 1. Single unified API: lower_node() instead of separate lower_expr, lower_init, lower_place
/// 2. Destination-passing style: dest_hint is propagated down to nodes that can use it
/// 3. LowerResult handles adaptation: Caller doesn't need to know result kind
/// 4. No separate "expr" vs "init" paths: Everything is lower_node with optional dest
///
/// Public API:
/// - lower_node(expr, dest_hint?) -> LowerResult
/// - lower_node_place(expr) -> Place (helper for strict place contexts)
/// - lower_node_operand(expr) -> Operand (helper for value contexts)
class FunctionLowerer {
public:
    friend class LowerResult;  // Allow LowerResult to access private helper methods

    enum class FunctionKind { Function, Method };

    FunctionLowerer(const hir::Function& function,
                    const std::unordered_map<const void*, mir::FunctionRef>& fn_map,
                    FunctionId id,
                    std::string name);

    FunctionLowerer(const hir::Method& method,
                    const std::unordered_map<const void*, mir::FunctionRef>& fn_map,
                    FunctionId id,
                    std::string name);

    MirFunction lower();

    // === Public API (Destination-Passing Style) ===

    /// The primary API: Lower an expression.
    /// @param expr: The HIR expression to lower.
    /// @param dest_hint: Optional destination where aggregate types should write their result.
    ///                   Aggregates MUST try to use this for RVO.
    ///                   Scalars WILL ignore this.
    /// @return LowerResult that describes what happened.
    LowerResult lower_node(const hir::Expr& expr, std::optional<Place> dest_hint = std::nullopt);

    /// Strict place context: The expression MUST evaluate to a place.
    /// Syntactic sugar for: lower_node(expr).as_place(*this, info)
    Place lower_node_place(const hir::Expr& expr);

    /// Strict value context: The expression MUST evaluate to an operand.
    /// Syntactic sugar for: lower_node(expr).as_operand(*this, info)
    Operand lower_node_operand(const hir::Expr& expr);

private:
    struct LoopContext {
        BasicBlockId continue_block = 0;
        BasicBlockId break_block = 0;
        std::optional<TypeId> break_type;
        std::optional<TempId> break_result;
        std::vector<PhiIncoming> break_incomings;
        std::vector<BasicBlockId> break_predecessors;
    };

    // === State ===
    FunctionKind function_kind = FunctionKind::Function;
    const hir::Function* hir_function = nullptr;
    const hir::Method* hir_method = nullptr;
    const std::unordered_map<const void*, mir::FunctionRef>& function_map;

    MirFunction mir_function;
    std::optional<BasicBlockId> current_block;
    std::vector<bool> block_terminated;
    std::unordered_map<const hir::Local*, LocalId> local_ids;
    std::vector<std::pair<const void*, LoopContext>> loop_stack;
    size_t synthetic_local_counter = 0;

    ReturnStoragePlan return_plan;

    // === Initialization & Setup ===
    void initialize(FunctionId id, std::string name);
    const hir::Block* get_body() const;
    const std::vector<std::unique_ptr<hir::Local>>& get_locals_vector() const;
    TypeId resolve_return_type() const;
    void init_locals();
    const hir::Local* pick_nrvo_local() const;
    ReturnStoragePlan build_return_plan();
    void apply_abi_aliasing(const ReturnStoragePlan& plan);

    mir::FunctionRef lookup_function(const void* key) const;
    const MirFunctionSig& get_callee_sig(mir::FunctionRef target) const;

    // === Statement Lowering ===
    bool lower_block_statements(const hir::Block& block);
    void lower_block(const hir::Block& hir_block);
    void lower_statement(const hir::Stmt& stmt);
    void lower_statement_impl(const hir::LetStmt& let_stmt);
    void lower_statement_impl(const hir::ExprStmt& expr_stmt);

    // === The Central Dispatcher ===
    /// Internal implementation of lower_node.
    /// Dispatches to visit_* methods for specific node types.
    LowerResult lower_node_impl(const hir::Expr& expr, const semantic::ExprInfo& info,
                                std::optional<Place> dest_hint);

    // === Visitor Methods (One per HIR expression type) ===
    /// Scalars: Return Operand, ignore dest_hint
    LowerResult visit_literal(const hir::Literal& node, const semantic::ExprInfo& info,
                              std::optional<Place> dest_hint);
    LowerResult visit_unresolved_identifier(const hir::UnresolvedIdentifier& node, const semantic::ExprInfo& info,
                                            std::optional<Place> dest_hint);
    LowerResult visit_type_static(const hir::TypeStatic& node, const semantic::ExprInfo& info,
                                  std::optional<Place> dest_hint);
    LowerResult visit_underscore(const hir::Underscore& node, const semantic::ExprInfo& info,
                                 std::optional<Place> dest_hint);
    LowerResult visit_binary(const hir::BinaryOp& node, const semantic::ExprInfo& info,
                             std::optional<Place> dest_hint);
    LowerResult visit_unary(const hir::UnaryOp& node, const semantic::ExprInfo& info,
                            std::optional<Place> dest_hint);
    LowerResult visit_cast(const hir::Cast& node, const semantic::ExprInfo& info,
                           std::optional<Place> dest_hint);

    /// Places: Return Place
    LowerResult visit_variable(const hir::Variable& node, const semantic::ExprInfo& info,
                               std::optional<Place> dest_hint);
    LowerResult visit_field_access(const hir::FieldAccess& node, const semantic::ExprInfo& info,
                                   std::optional<Place> dest_hint);
    LowerResult visit_index(const hir::Index& node, const semantic::ExprInfo& info,
                            std::optional<Place> dest_hint);

    /// Aggregates: Use dest_hint for RVO
    LowerResult visit_struct_literal(const hir::StructLiteral& node, const semantic::ExprInfo& info,
                                     std::optional<Place> dest_hint);
    LowerResult visit_array_literal(const hir::ArrayLiteral& node, const semantic::ExprInfo& info,
                                    std::optional<Place> dest_hint);
    LowerResult visit_array_repeat(const hir::ArrayRepeat& node, const semantic::ExprInfo& info,
                                   std::optional<Place> dest_hint);

    /// Control Flow: Propagate dest_hint
    LowerResult visit_block(const hir::Block& node, const semantic::ExprInfo& info,
                            std::optional<Place> dest_hint);
    LowerResult visit_if(const hir::If& node, const semantic::ExprInfo& info,
                         std::optional<Place> dest_hint);
    LowerResult visit_loop(const hir::Loop& node, const semantic::ExprInfo& info,
                           std::optional<Place> dest_hint);
    LowerResult visit_while(const hir::While& node, const semantic::ExprInfo& info,
                            std::optional<Place> dest_hint);

    /// Exits: Never return (return Operand::Written conceptually)
    LowerResult visit_break(const hir::Break& node, const semantic::ExprInfo& info,
                            std::optional<Place> dest_hint);
    LowerResult visit_continue(const hir::Continue& node, const semantic::ExprInfo& info,
                               std::optional<Place> dest_hint);
    LowerResult visit_return(const hir::Return& node, const semantic::ExprInfo& info,
                             std::optional<Place> dest_hint);

    /// Calls: Hybrid (may return Operand or Written depending on ABI)
    LowerResult visit_call(const hir::Call& node, const semantic::ExprInfo& info,
                           std::optional<Place> dest_hint);
    LowerResult visit_method_call(const hir::MethodCall& node, const semantic::ExprInfo& info,
                                  std::optional<Place> dest_hint);

    /// Other nodes
    LowerResult visit_const_use(const hir::ConstUse& node, const semantic::ExprInfo& info,
                                std::optional<Place> dest_hint);
    LowerResult visit_func_use(const hir::FuncUse& node, const semantic::ExprInfo& info,
                               std::optional<Place> dest_hint);
    LowerResult visit_struct_const(const hir::StructConst& node, const semantic::ExprInfo& info,
                                   std::optional<Place> dest_hint);
    LowerResult visit_enum_variant(const hir::EnumVariant& node, const semantic::ExprInfo& info,
                                   std::optional<Place> dest_hint);
    LowerResult visit_assignment(const hir::Assignment& node, const semantic::ExprInfo& info,
                                 std::optional<Place> dest_hint);

    // === Helpers for control flow ===
    std::optional<Operand> lower_block_expr(const hir::Block& block, TypeId expected_type);
    void handle_return_value(const std::optional<std::unique_ptr<hir::Expr>>& value_ptr,
                             const char* context);
    std::optional<Operand> lower_short_circuit(const hir::BinaryOp& binary,
                                               const semantic::ExprInfo& info, bool is_and);

    // === Helpers for patterns and locals ===
    void lower_let_pattern(const hir::Pattern& pattern, const hir::Expr& init_expr);
    void lower_binding_let(const hir::BindingDef& binding, const hir::Expr& init_expr);
    void lower_reference_let(const hir::ReferencePattern& ref_pattern,
                             const hir::Expr& init_expr);
    void lower_pattern_from_expr(const hir::Pattern& pattern, const hir::Expr& expr,
                                 TypeId expr_type);

    // === Const lowering ===
    std::optional<Operand> try_lower_to_const(const hir::Expr& expr);

    // === Helper functions for building RValues ===
    AggregateRValue build_struct_aggregate(const hir::StructLiteral& struct_literal);
    AggregateRValue build_array_aggregate(const hir::ArrayLiteral& array_literal);
    ArrayRepeatRValue build_array_repeat_rvalue(const hir::ArrayRepeat& array_repeat);
    ConstantRValue build_literal_rvalue(const hir::Literal& lit, const semantic::ExprInfo& info);

    // === Place & Local helpers ===
    LocalId require_local_id(const hir::Local* local) const;
    Place make_local_place(const hir::Local* local) const;
    Place make_local_place(LocalId local_id) const;
    LocalId create_synthetic_local(TypeId type, bool is_mutable_reference);

    // === Block and instruction emission ===
    BasicBlockId create_block();
    bool block_is_terminated(BasicBlockId id) const;
    BasicBlockId current_block_id() const;
    TempId allocate_temp(TypeId type);
    void append_statement(Statement statement);
    void set_terminator(BasicBlockId id, Terminator terminator);
    void terminate_current_block(Terminator terminator);
    void add_goto_from_current(BasicBlockId target);
    void switch_to_block(BasicBlockId id);
    void branch_on_bool(const Operand& condition, BasicBlockId true_block,
                        BasicBlockId false_block);

    // === Operand helpers ===
    Operand load_place_value(Place place, TypeId type);
    Operand make_const_operand(std::uint64_t value, TypeId type, bool is_signed = false);
    Operand make_temp_operand(TempId temp);
    TempId materialize_operand(const Operand& operand, TypeId type);
    TempId materialize_place_base(const hir::Expr& base_expr,
                                  const semantic::ExprInfo& base_info);

    // === RValue emission ===
    template <typename RValueT>
    Operand emit_rvalue_to_temp(RValueT rvalue_kind, TypeId result_type);
    Operand emit_aggregate(AggregateRValue aggregate, TypeId result_type);
    Operand emit_array_repeat(Operand value, std::size_t count, TypeId result_type);

    // === Return handling ===
    void emit_return(std::optional<Operand> value);

    // === Function parameters ===
    void collect_parameters();
    void append_self_parameter();
    void append_explicit_parameters(const std::vector<std::unique_ptr<hir::Pattern>>& params,
                                    const std::vector<hir::TypeAnnotation>& annotations);
    void append_parameter(const hir::Local* local, TypeId type);
    const hir::Local* resolve_pattern_local(const hir::Pattern& pattern) const;

    // === Reachability ===
    bool is_reachable() const;
    void require_reachable(const char* context) const;

    // === Loop context ===
    LoopContext& push_loop_context(const void* key, BasicBlockId continue_block,
                                   BasicBlockId break_block, std::optional<TypeId> break_type);
    LoopContext& lookup_loop_context(const void* key);
    LoopContext pop_loop_context(const void* key);
    void finalize_loop_context(const LoopContext& ctx);
};

// === Template Implementations ===

template <typename RValueT>
Operand FunctionLowerer::emit_rvalue_to_temp(RValueT rvalue_kind, TypeId result_type) {
    TempId dest = allocate_temp(result_type);
    RValue rvalue;
    rvalue.value = std::move(rvalue_kind);
    DefineStatement define{.dest = dest, .rvalue = std::move(rvalue)};
    Statement stmt;
    stmt.value = std::move(define);
    append_statement(std::move(stmt));
    return make_temp_operand(dest);
}

}  // namespace mir::lower_v2

