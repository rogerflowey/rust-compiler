#include "control_flow_linking.hpp"
#include "semantic/utils.hpp"
#include "src/utils/error.hpp"

void ControlFlowLinker::link_control_flow(hir::Program& program) {
    visit_program(program);
}

void ControlFlowLinker::link_control_flow(hir::Item& item) {
    visit_item(item);
}

void ControlFlowLinker::link_control_flow(hir::Function& function) {
    visit(function);
}

void ControlFlowLinker::link_control_flow(hir::Method& method) {
    visit(method);
}

void ControlFlowLinker::visit(hir::Function& function) {
    // Create a new context for this function
    ControlFlowContext function_context;
    function_context.enter_function(&function);
    
    // Save the old context and use the new one
    auto old_context = context_;
    context_ = function_context;
    
    // Visit the function body using the base class implementation
    // This will traverse parameters, return type, and body
    base().visit(function);
    
    // Restore the old context
    context_ = old_context;
}

void ControlFlowLinker::visit(hir::Method& method) {
    // Create a new context for this method
    ControlFlowContext method_context;
    method_context.enter_function(&method);
    
    // Save the old context and use the new one
    auto old_context = context_;
    context_ = method_context;
    
    // Visit the method body using the base class implementation
    // This will traverse self parameter, parameters, return type, and body
    base().visit(method);
    
    // Restore the old context
    context_ = old_context;
}

void ControlFlowLinker::visit(hir::Loop& loop) {
    // Enter the loop context
    context_.enter_loop(&loop);
    
    // Visit the loop body using the base class implementation
    base().visit(loop);
    
    // Exit the loop context
    context_.exit_loop();
}

void ControlFlowLinker::visit(hir::While& while_loop) {
    // Enter the loop context
    context_.enter_loop(&while_loop);
    
    // Visit the while loop using the base class implementation
    // This will traverse condition and body
    base().visit(while_loop);
    
    // Exit the loop context
    context_.exit_loop();
}

void ControlFlowLinker::visit(hir::Return& return_stmt) {
    // Find the current function target
    auto* target = context_.find_current_function();
    if (!target) {
        throw SemanticError("Return statement outside of function", return_stmt.span);
    }
    
    // Set the target
    return_stmt.target = *target;
    
    // Visit the return value using the base class implementation
    base().visit(return_stmt);
}

void ControlFlowLinker::visit(hir::Break& break_stmt) {
    // Find the nearest loop target
    auto* target = context_.find_nearest_loop();
    if (!target) {
        throw SemanticError("Break statement outside of loop", break_stmt.span);
    }
    
    // Set the target
    break_stmt.target = *target;
    
    // Visit the break value using the base class implementation
    base().visit(break_stmt);
}

void ControlFlowLinker::visit(hir::Continue& continue_stmt) {
    // Find the nearest loop target
    auto* target = context_.find_nearest_loop();
    if (!target) {
        throw SemanticError("Continue statement outside of loop", continue_stmt.span);
    }
    
    // Set the target
    continue_stmt.target = *target;
    
    // Continue doesn't have a value to visit, so we're done
}

void ControlFlowLinker::visit(hir::Impl& impl) {
    // For impl blocks, we need to handle nested functions and methods
    // Visit the type annotation first
    base().visit(impl);
    
    // The base class implementation will visit associated items,
    // which will call our visit(Function) and visit(Method) methods
    // with the appropriate context handling
}