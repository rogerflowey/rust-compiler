#pragma once

#include "mir/mir.hpp"
#include "mir/lower/lower_common.hpp"
#include "mir/lower/lower_const.hpp"

#include "semantic/hir/hir.hpp"
#include "semantic/pass/semantic_check/expr_info.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace mir::lower_v2::detail {

/*
 * MIR Lowering Strategy: Unified Destination-Passing Style (DPS)
 *
 * Goal: Eliminate the legacy dichotomy between lower_expr and lower_init.
 * The new pipeline returns a LowerResult that can represent an Operand,
 * a Place, or a Written marker when a destination hint was honored.
 * The caller enforces placement via LowerResult::write_to_dest.
 */

class FunctionLowerer;

class LowerResult {
public:
    enum class Kind { Operand, Place, Written };

    static LowerResult operand(Operand op);
    static LowerResult place(Place p);
    static LowerResult written();

    Operand as_operand(FunctionLowerer& fl, TypeId type) const;
    Place as_place(FunctionLowerer& fl, TypeId type) const;
    void write_to_dest(FunctionLowerer& fl, Place dest, TypeId type) const;

private:
    Kind kind;
    std::variant<std::monostate, Operand, Place> data;

    LowerResult(Kind k, std::variant<std::monostate, Operand, Place> d);
};

class FunctionLowerer {
public:
    friend class LowerResult;
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

    // Unified lowering API
    LowerResult lower_expr(const hir::Expr& expr,
                           std::optional<Place> maybe_dest = std::nullopt);
    Place lower_place(const hir::Expr& expr);

    template <typename T>
    LowerResult lower_expr_impl(const T& node,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);

private:
    struct LoopContext {
        BasicBlockId continue_block = 0;
        BasicBlockId break_block = 0;
        std::optional<TypeId> break_type;
        std::optional<TempId> break_result;
        std::vector<PhiIncoming> break_incomings;
        std::vector<BasicBlockId> break_predecessors;
    };

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

    // IR construction helpers
    BasicBlockId create_block();
    bool block_is_terminated(BasicBlockId id) const;
    BasicBlockId current_block_id() const;
    TempId allocate_temp(TypeId type);
    TempId materialize_operand(const Operand& operand, TypeId type);
    LocalId create_synthetic_local(TypeId type, std::string debug_name = {});
    void append_statement(Statement statement);
    void set_terminator(BasicBlockId id, Terminator terminator);
    void terminate_current_block(Terminator terminator);
    void add_goto_from_current(BasicBlockId target);
    void switch_to_block(BasicBlockId id);
    void branch_on_bool(const Operand& condition,
                        BasicBlockId true_block,
                        BasicBlockId false_block);
    Operand make_temp_operand(TempId temp);

    // Statement helpers
    void emit_return(std::optional<Operand> value);
    Operand load_place_value(Place place, TypeId type);
    void emit_assign(Place dest, ValueSource src);
    Place make_local_place(const hir::Local* local) const;
    Place make_local_place(LocalId local_id) const;
    Place project_field(const Place& base, std::size_t index) const;
    Place project_index(const Place& base, std::size_t index) const;
    LocalId require_local_id(const hir::Local* local) const;
    Operand expect_operand(const LowerResult& result,
                           const semantic::ExprInfo& info);

    // Parameter helpers
    void collect_parameters();
    void append_self_parameter();
    void append_explicit_parameters(
        const std::vector<std::unique_ptr<hir::Pattern>>& params,
        const std::vector<hir::TypeAnnotation>& annotations);
    void append_parameter(const hir::Local* local, TypeId type);
    const hir::Local* resolve_pattern_local(const hir::Pattern& pattern) const;

    // Control flow helpers
    bool is_reachable() const;
    void require_reachable(const char* context) const;
    LoopContext& push_loop_context(const void* key,
                                   BasicBlockId continue_block,
                                   BasicBlockId break_block,
                                   std::optional<TypeId> break_type);
    LoopContext& lookup_loop_context(const void* key);
    LoopContext pop_loop_context(const void* key);
    void finalize_loop_context(const LoopContext& ctx);

    // Block & statement lowering
    bool lower_block_statements(const hir::Block& block);
    void lower_block(const hir::Block& hir_block);
    LowerResult lower_block_expr(const hir::Block& block,
                                 std::optional<Place> dest);
    void lower_statement(const hir::Stmt& stmt);
    void lower_statement_impl(const hir::LetStmt& let_stmt);
    void lower_statement_impl(const hir::ExprStmt& expr_stmt);

    // Expression lowering helpers
    LowerResult lower_if_expr(const hir::If& if_expr,
                              const semantic::ExprInfo& info,
                              std::optional<Place> dest);
    LowerResult lower_block_expr_result(const hir::Expr& expr,
                                        std::optional<Place> dest,
                                        const semantic::ExprInfo& info);

    // Node-specific lowering (implemented in lower_expr.cpp)
    LowerResult lower_expr_impl(const hir::Literal& literal,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
    LowerResult lower_expr_impl(const hir::Variable& variable,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
    LowerResult lower_expr_impl(const hir::FieldAccess& field_access,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
    LowerResult lower_expr_impl(const hir::Index& index_expr,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
    LowerResult lower_expr_impl(const hir::StructLiteral& literal,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
    LowerResult lower_expr_impl(const hir::ArrayLiteral& array_literal,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
    LowerResult lower_expr_impl(const hir::ArrayRepeat& array_repeat,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
    LowerResult lower_expr_impl(const hir::Cast& cast_expr,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
    LowerResult lower_expr_impl(const hir::BinaryOp& binary,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
    LowerResult lower_expr_impl(const hir::Assignment& assignment,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
    LowerResult lower_expr_impl(const hir::Block& block_expr,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
    LowerResult lower_expr_impl(const hir::If& if_expr,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
    LowerResult lower_expr_impl(const hir::Call& call_expr,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
    LowerResult lower_expr_impl(const hir::Return& return_expr,
                                const semantic::ExprInfo& info,
                                std::optional<Place> dest);
};

template <typename T>
LowerResult FunctionLowerer::lower_expr_impl(const T&, const semantic::ExprInfo&, std::optional<Place>) {
    throw std::logic_error("Expression kind not supported yet in MIR lowering v2");
}

} // namespace mir::lower_v2::detail
