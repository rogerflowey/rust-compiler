#pragma once

#include "semantic/hir/hir.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include <stdexcept>

// Control flow context for tracking targets during linking
class ControlFlowContext {
public:
    using LoopTarget = std::variant<hir::Loop*, hir::While*>;
    using FunctionTarget = std::variant<hir::Function*, hir::Method*>;
    
    void enter_loop(LoopTarget target) { loop_stack.push_back(target); }
    void exit_loop() { if (!loop_stack.empty()) loop_stack.pop_back(); }
    
    void enter_function(FunctionTarget target) { function_stack.push_back(target); }
    void exit_function() { if (!function_stack.empty()) function_stack.pop_back(); }
    
    LoopTarget* find_nearest_loop() {
        return loop_stack.empty() ? nullptr : &loop_stack.back();
    }
    
    FunctionTarget* find_current_function() {
        return function_stack.empty() ? nullptr : &function_stack.back();
    }
    
private:
    std::vector<LoopTarget> loop_stack;
    std::vector<FunctionTarget> function_stack;
};

// Control flow linking visitor
class ControlFlowLinker : public hir::HirVisitorBase<ControlFlowLinker> {
public:
    ControlFlowLinker() = default;
    
    // Main entry point for linking control flow
    void link_control_flow(hir::Program& program);
    
    // Public methods for individual items (for testing)
    void link_control_flow(hir::Item& item);
    void link_control_flow(hir::Function& function);
    void link_control_flow(hir::Method& method);

    // Visitor methods for control flow nodes
    void visit(hir::Function& function);
    void visit(hir::Method& method);
    void visit(hir::Loop& loop);
    void visit(hir::While& while_loop);
    void visit(hir::Return& return_stmt);
    void visit(hir::Break& break_stmt);
    void visit(hir::Continue& continue_stmt);
    
    // For nested functions in blocks, we need to handle them separately
    void visit(hir::Impl& impl);
    // Template catch-all for nodes we don't specifically handle
    template<typename T>
    void visit(T& node) {
        // Use the base class implementation for default traversal
        base().visit(node);
    }

private:
    ControlFlowContext context_;
};