#include "expr_check.hpp"
#include "other_check.hpp"
#include "semantic/type/helper.hpp"
#include "semantic/utils.hpp"
#include "control_flow_helper.hpp"
#include <sstream>
#include <unordered_set>
#include <variant>

using namespace semantic::helper::type_helper;
using namespace semantic::control_flow_helper;
using namespace hir::helper::transform_helper;
using namespace error_helper;

namespace semantic {

ExprChecker::ExprChecker(
    Scope* current_scope,
    const ImplTable* impl_table,
    hir::Function* current_function,
    std::variant<hir::Loop*, hir::While*, std::monostate> current_loop
) : current_scope(current_scope),
    impl_table(impl_table),
    current_function(current_function),
    current_loop(current_loop) {
    if (!current_scope) {
        throw SemanticError("ExprChecker requires a valid scope");
    }
    if (!impl_table) {
        throw SemanticError("ExprChecker requires a valid implementation table");
    }
}

ExprInfo ExprChecker::check(hir::Expr& expr) {
    // Main visitor dispatch
    return std::visit([this](auto& node) -> ExprInfo {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, hir::Literal>) {
            return check_literal(node);
        } else if constexpr (std::is_same_v<T, hir::Variable>) {
            return check_variable(node);
        } else if constexpr (std::is_same_v<T, hir::ConstUse>) {
            return check_const_use(node);
        } else if constexpr (std::is_same_v<T, hir::FuncUse>) {
            return check_func_use(node);
        } else if constexpr (std::is_same_v<T, hir::TypeStatic>) {
            return check_type_static(node);
        } else if constexpr (std::is_same_v<T, hir::Underscore>) {
            return check_underscore(node);
        } else if constexpr (std::is_same_v<T, hir::FieldAccess>) {
            return check_field_access(node);
        } else if constexpr (std::is_same_v<T, hir::StructLiteral>) {
            return check_struct_literal(node);
        } else if constexpr (std::is_same_v<T, hir::ArrayLiteral>) {
            return check_array_literal(node);
        } else if constexpr (std::is_same_v<T, hir::ArrayRepeat>) {
            return check_array_repeat(node);
        } else if constexpr (std::is_same_v<T, hir::Index>) {
            return check_index(node);
        } else if constexpr (std::is_same_v<T, hir::Assignment>) {
            return check_assignment(node);
        } else if constexpr (std::is_same_v<T, hir::UnaryOp>) {
            return check_unary_op(node);
        } else if constexpr (std::is_same_v<T, hir::BinaryOp>) {
            return check_binary_op(node);
        } else if constexpr (std::is_same_v<T, hir::Cast>) {
            return check_cast(node);
        } else if constexpr (std::is_same_v<T, hir::Call>) {
            return check_call(node);
        } else if constexpr (std::is_same_v<T, hir::MethodCall>) {
            return check_method_call(node);
        } else if constexpr (std::is_same_v<T, hir::If>) {
            return check_if(node);
        } else if constexpr (std::is_same_v<T, hir::Loop>) {
            return check_loop(node);
        } else if constexpr (std::is_same_v<T, hir::While>) {
            return check_while(node);
        } else if constexpr (std::is_same_v<T, hir::Break>) {
            return check_break(node);
        } else if constexpr (std::is_same_v<T, hir::Continue>) {
            return check_continue(node);
        } else if constexpr (std::is_same_v<T, hir::Return>) {
            return check_return(node);
        } else if constexpr (std::is_same_v<T, hir::Block>) {
            return check_block(node);
        } else {
            // Handle UnresolvedIdentifier and other unresolved nodes
            throw SemanticError("Unresolved expression node found during semantic checking");
        }
    }, expr.value);
}

// Literal expressions
ExprInfo ExprChecker::check_literal(hir::Literal& literal) {
    return std::visit([this](auto&& value) -> ExprInfo {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, hir::Literal::Integer>) {
            return visitIntegerLiteral(value);
        } else if constexpr (std::is_same_v<T, bool>) {
            return visitBooleanLiteral(value);
        } else if constexpr (std::is_same_v<T, char>) {
            return visitCharacterLiteral(value);
        } else if constexpr (std::is_same_v<T, hir::Literal::String>) {
            return visitStringLiteral(value);
        } else {
            throw SemanticError("Unknown literal type");
        }
    }, literal.value);
}

ExprInfo ExprChecker::visitIntegerLiteral(const hir::Literal::Integer& integer) {
    try {
        // Validate value fits within type bounds
        overflow_int_literal_check(integer);
        
        // Determine type from suffix or use inference types
        TypeId literal_type;
        switch (integer.suffix_type) {
            case ast::IntegerLiteralExpr::I32:
                literal_type = get_typeID(Type{PrimitiveKind::I32});
                break;
            case ast::IntegerLiteralExpr::U32:
                literal_type = get_typeID(Type{PrimitiveKind::U32});
                break;
            case ast::IntegerLiteralExpr::ISIZE:
                literal_type = get_typeID(Type{PrimitiveKind::ISIZE});
                break;
            case ast::IntegerLiteralExpr::USIZE:
                literal_type = get_typeID(Type{PrimitiveKind::USIZE});
                break;
            case ast::IntegerLiteralExpr::NOT_SPECIFIED:
                // Use inference types for type inference
                if (integer.is_negative) {
                    literal_type = get_typeID(Type{PrimitiveKind::__ANYINT__});
                } else {
                    literal_type = get_typeID(Type{PrimitiveKind::__ANYUINT__});
                }
                break;
            default:
                report_error("Unknown integer suffix type");
        }
        
        // Return: appropriate type, non-mutable, non-place, normal endpoint
        return ExprInfo(literal_type, false, false, normal_endpoint());
    } catch (const std::logic_error& e) {
        throw SemanticError(e.what());
    }
}

ExprInfo ExprChecker::visitBooleanLiteral(bool boolean) {
    // Return: BOOL type, non-mutable, non-place, normal endpoint
    TypeId bool_type = get_typeID(Type{PrimitiveKind::BOOL});
    return ExprInfo(bool_type, false, false, normal_endpoint());
}

ExprInfo ExprChecker::visitCharacterLiteral(char character) {
    // Return: CHAR type, non-mutable, non-place, normal endpoint
    TypeId char_type = get_typeID(Type{PrimitiveKind::CHAR});
    return ExprInfo(char_type, false, false, normal_endpoint());
}

ExprInfo ExprChecker::visitStringLiteral(const hir::Literal::String& string) {
    // Return: STRING type, non-mutable, non-place, normal endpoint
    TypeId string_type = get_typeID(Type{PrimitiveKind::STRING});
    return ExprInfo(string_type, false, false, normal_endpoint());
}

ExprInfo ExprChecker::check_variable(hir::Variable& variable) {
    hir::Local* local = variable.local_id;
    TypeId var_type;
    var_type = hir::helper::get_resolved_type(*local->type_annotation);
    
    // Variables are place expressions (they denote memory locations)
    bool is_place = true;
    
    // Return: variable's type, variable's mutability, place, normal endpoint
    return ExprInfo(var_type, local->is_mutable, is_place, normal_endpoint());
}

ExprInfo ExprChecker::check_const_use(hir::ConstUse& const_use) {
    // Verify constant is resolved
    if (!const_use.def) {
        throw SemanticError("Constant reference is not resolved");
    }
    
    // Check if constant has a type
    if (!const_use.def->type) {
        throw SemanticError("Constant type is not resolved");
    }
    
    // Get the resolved type from the constant definition
    TypeId const_type = hir::helper::get_resolved_type(*const_use.def->type);
    
    // Ensure constant is fully evaluated
    if (hir::helper::is_unresolved_const(*const_use.def)) {
        throw SemanticError("Constant is not fully evaluated");
    }
    
    // Constants are non-mutable, non-place expressions
    bool is_mutable = false;
    bool is_place = false;
    
    // Return: constant's type, non-mutable, non-place, normal endpoint
    return ExprInfo(const_type, is_mutable, is_place, normal_endpoint());
}

ExprInfo ExprChecker::check_func_use(hir::FuncUse& func_use) {
    // Functions are not first-class values in this language
    // FuncUse should only appear as the callee in a Call expression
    // Direct use of a function as a value is not allowed
    
    // Verify function is resolved
    if (!func_use.def) {
        throw SemanticError("Function reference is not resolved");
    }
    
    // Functions cannot be used as values directly
    // They must be called immediately
    throw SemanticError("Functions are not first-class values and cannot be used as expressions");
}

ExprInfo ExprChecker::check_type_static(hir::TypeStatic& type_static) {
    // TODO: Implement type static checking
    // - Should be resolved during name resolution
    throw SemanticError("Type static checking not yet implemented");
}

ExprInfo ExprChecker::check_underscore(hir::Underscore& underscore) {
    // TODO: Implement underscore checking
    // - Only valid in specific contexts
    throw SemanticError("Underscore checking not yet implemented");
}

// Composite expressions
ExprInfo ExprChecker::check_field_access(hir::FieldAccess& field_access) {
    // Check base expression and determine its type
    ExprInfo base_info = check(*field_access.base);
    
    // Check if base type is reference type first
    if (is_reference_type(base_info.type)) {
        TypeId dereferenced_type = get_referenced_type(base_info.type);
        if (dereferenced_type) {
            // Apply auto-dereference transformation
            apply_auto_dereference_field_access(field_access, base_info.type);
            
            // Try again with dereferenced type
            ExprInfo deref_info = resolve_field_access(field_access, dereferenced_type);
            // Field access on dereferenced value is a place if base was a place
            return ExprInfo(deref_info.type, base_info.is_mut, base_info.is_place, deref_info.endpoints);
        }
    }
    
    // Try to resolve field access with current base type
    return resolve_field_access(field_access, base_info.type);
}

ExprInfo ExprChecker::resolve_field_access(hir::FieldAccess& field_access, TypeId base_type) {
    // Get the field index (should be resolved by name resolution)
    size_t field_index = hir::helper::get_field_index(field_access);
    
    // Check if base type is a struct
    if (auto struct_type = std::get_if<StructType>(&base_type->value)) {
        const hir::StructDef* struct_def = struct_type->symbol;
        
        // Validate field index is within bounds
        if (field_index >= struct_def->fields.size()) {
            throw SemanticError("Field index out of bounds for struct");
        }
        
        // Get field type
        const auto& field_type_annotation = struct_def->field_type_annotations[field_index];
        TypeId field_type = hir::helper::get_resolved_type(field_type_annotation);
        
        // Return: field's type, base mutability AND field immutability, place if base is place
        // Fields are immutable, so overall mutability depends on base only
        return ExprInfo(field_type, false, true, normal_endpoint());
    }
    
    throw SemanticError("Field access on non-struct type");
}

ExprInfo ExprChecker::check_struct_literal(hir::StructLiteral& struct_literal) {
    // Get struct definition
    hir::StructDef* struct_def = hir::helper::get_struct_def(struct_literal);
    
    // Get struct type
    TypeId struct_type = get_typeID(Type{StructType{.symbol = struct_def}});
    
    // Check fields based on representation
    if (hir::helper::has_syntactic_fields(struct_literal)) {
        const auto& syntactic_fields = hir::helper::get_syntactic_fields(struct_literal);
        
        // Validate no duplicate fields
        std::unordered_set<ast::Identifier> field_names;
        for (const auto& [field_name, field_expr] : syntactic_fields.initializers) {
            if (field_names.contains(field_name)) {
                throw SemanticError("Duplicate field in struct literal");
            }
            field_names.insert(field_name);
        }
        
        // Check all fields exist and have compatible types
        for (const auto& [field_name, field_expr] : syntactic_fields.initializers) {
            // Find field index in struct definition
            size_t field_index = 0;
            bool field_found = false;
            for (size_t i = 0; i < struct_def->fields.size(); ++i) {
                if (struct_def->fields[i].name == field_name) {
                    field_index = i;
                    field_found = true;
                    break;
                }
            }
            
            if (!field_found) {
                throw SemanticError("Field does not exist in struct");
            }
            
            // Check field expression type
            ExprInfo field_info = check(*field_expr);
            
            // Get expected field type
            const auto& field_type_annotation = struct_def->field_type_annotations[field_index];
            TypeId expected_type = hir::helper::get_resolved_type(field_type_annotation);
            
            // Check type compatibility
            if (!is_assignable(expected_type, field_info.type)) {
                report_type_mismatch(expected_type, field_info.type);
            }
        }
        
        // Check all required fields are initialized
        for (size_t i = 0; i < struct_def->fields.size(); ++i) {
            const auto& field = struct_def->fields[i];
            bool field_initialized = false;
            for (const auto& [field_name, field_expr] : syntactic_fields.initializers) {
                if (field.name == field_name) {
                    field_initialized = true;
                    break;
                }
            }
            
            if (!field_initialized) {
                throw SemanticError("Missing field in struct literal");
            }
        }
    } else {
        // Canonical fields - already validated during name resolution
        const auto& canonical_fields = hir::helper::get_canonical_fields(struct_literal);
        
        // Check each field expression
        for (size_t i = 0; i < canonical_fields.initializers.size(); ++i) {
            if (i >= struct_def->fields.size()) {
                throw SemanticError("Too many fields in struct literal");
            }
            
            // Check field expression type
            ExprInfo field_info = check(*canonical_fields.initializers[i]);
            
            // Get expected field type
            const auto& field_type_annotation = struct_def->field_type_annotations[i];
            TypeId expected_type = hir::helper::get_resolved_type(field_type_annotation);
            
            // Check type compatibility
            if (!is_assignable(expected_type, field_info.type)) {
                report_type_mismatch(expected_type, field_info.type);
            }
        }
        
        // Check all required fields are initialized
        if (canonical_fields.initializers.size() != struct_def->fields.size()) {
            throw SemanticError("Missing field in struct literal");
        }
    }
    
    // Return: struct type, non-mutable, non-place, normal endpoint
    return ExprInfo(struct_type, false, false, normal_endpoint());
}

ExprInfo ExprChecker::check_array_literal(hir::ArrayLiteral& array_literal) {
    if (array_literal.elements.empty()) {
        throw SemanticError("Array literal cannot be empty");
    }
    
    // Check first element to get element type
    ExprInfo first_element_info = check(*array_literal.elements[0]);
    TypeId element_type = first_element_info.type;
    
    // Check all elements have compatible types
    for (size_t i = 1; i < array_literal.elements.size(); ++i) {
        ExprInfo element_info = check(*array_literal.elements[i]);
        
        // Try to find common type for type coercion
        auto common_type = find_common_type(element_type, element_info.type);
        if (common_type) {
            element_type = *common_type;
        } else {
            // Try to coerce to first element type
            if (!is_assignable(element_type, element_info.type)) {
                report_type_mismatch(element_type, element_info.type);
            }
        }
    }
    
    // Create array type
    TypeId array_type = get_typeID(Type{ArrayType{.element_type = element_type, .size = array_literal.elements.size()}});
    
    // Return: array type, non-mutable, non-place, normal endpoint
    return ExprInfo(array_type, false, false, normal_endpoint());
}

ExprInfo ExprChecker::check_array_repeat(hir::ArrayRepeat& array_repeat) {
    // Check value expression
    ExprInfo value_info = check(*array_repeat.value);
    
    // Get count
    size_t count;
    if (auto count_value = std::get_if<size_t>(&array_repeat.count)) {
        count = *count_value;
    } else if (auto count_expr = std::get_if<std::unique_ptr<hir::Expr>>(&array_repeat.count)) {
        // Check count expression
        ExprInfo count_info = check(**count_expr);
        
        // Validate count is integer type
        if (!is_numeric_type(count_info.type)) {
            throw SemanticError("Array repeat count must be integer type");
        }
        
        // For now, we assume count is constant (should be resolved during const evaluation)
        throw SemanticError("Array repeat count must be constant");
    } else {
        throw SemanticError("Invalid array repeat count");
    }
    
    // Create array type
    TypeId array_type = get_typeID(Type{ArrayType{.element_type = value_info.type, .size = count}});
    
    // Return: array type, non-mutable, non-place, normal endpoint
    return ExprInfo(array_type, false, false, normal_endpoint());
}

ExprInfo ExprChecker::check_index(hir::Index& index) {
    // Check base expression and determine its type
    ExprInfo base_info = check(*index.base);
    
    // Check if base type is reference type first
    if (is_reference_type(base_info.type)) {
        TypeId dereferenced_type = get_referenced_type(base_info.type);
        if (dereferenced_type) {
            // Apply auto-dereference transformation
            apply_auto_dereference_index(index, base_info.type);
            
            // Try again with dereferenced type
            ExprInfo deref_info = resolve_index(index, dereferenced_type, base_info);
            // Indexing on dereferenced value is a place if base was a place
            return ExprInfo(deref_info.type, base_info.is_mut, base_info.is_place, deref_info.endpoints);
        }
    }
    
    // Try to resolve indexing with current base type
    return resolve_index(index, base_info.type, base_info);
}

ExprInfo ExprChecker::resolve_index(hir::Index& index, TypeId base_type, const ExprInfo& base_info) {
    // Check index expression
    ExprInfo index_info = check(*index.index);
    
    // Validate index is integer type
    if (!is_numeric_type(index_info.type)) {
        throw SemanticError("Array index must be integer type");
    }
    
    // Check if base type is array
    if (auto array_type = std::get_if<ArrayType>(&base_type->value)) {
        TypeId element_type = array_type->element_type;
        
        // Return: element type, base mutability, place if base is place
        return ExprInfo(element_type, base_info.is_mut, base_info.is_place, normal_endpoint());
    }
    
    throw SemanticError("Indexing on non-array type");
}

// Operations
ExprInfo ExprChecker::check_assignment(hir::Assignment& assignment) {
    // Check left-hand side (must be a mutable place)
    ExprInfo lhs_info = check(*assignment.lhs);
    
    // Verify left-hand side is a place (denotes a memory location)
    if (!lhs_info.is_place) {
        throw SemanticError("Left-hand side of assignment must be a place expression");
    }
    
    // Verify left-hand side is mutable
    if (!lhs_info.is_mut) {
        throw SemanticError("Cannot assign to immutable variable");
    }
    
    // Check right-hand side
    ExprInfo rhs_info = check(*assignment.rhs);
    
    // Ensure right-hand side type is assignable to left-hand side
    if (!is_assignable(lhs_info.type, rhs_info.type)) {
        report_type_mismatch(lhs_info.type, rhs_info.type);
    }
    
    // Assignment expression has unit type and is not a place
    TypeId unit_type = get_typeID(Type{UnitType{}});
    
    // Return: unit type, non-mutable, non-place, endpoints from right-hand side
    return ExprInfo(unit_type, false, false, rhs_info.endpoints);
}

ExprInfo ExprChecker::check_unary_op(hir::UnaryOp& unary_op) {
    // Check operand
    ExprInfo operand_info = check(*unary_op.rhs);
    
    TypeId result_type;
    bool is_place = false;
    
    switch (unary_op.op) {
        case hir::UnaryOp::NOT:
            // Operand must be BOOL, result is BOOL
            if (!is_bool_type(operand_info.type)) {
                report_invalid_operation("Logical NOT requires boolean operand");
            }
            result_type = get_typeID(Type{PrimitiveKind::BOOL});
            break;
            
        case hir::UnaryOp::NEGATE:
            // Operand must be numeric, result is same type
            if (!is_numeric_type(operand_info.type)) {
                report_invalid_operation("Negation requires numeric operand");
            }
            result_type = operand_info.type;
            break;
            
        case hir::UnaryOp::DEREFERENCE:
            // Operand must be reference, result is referenced type, creates place
            if (!is_reference_type(operand_info.type)) {
                report_invalid_operation("Dereference requires reference operand");
            }
            result_type = get_referenced_type(operand_info.type);
            if (!result_type) {
                throw SemanticError("Invalid reference type for dereference");
            }
            is_place = true;
            break;
            
        case hir::UnaryOp::REFERENCE:
        case hir::UnaryOp::MUTABLE_REFERENCE:
            // Create reference to operand
            {
                bool is_mutable = (unary_op.op == hir::UnaryOp::MUTABLE_REFERENCE);
                result_type = get_typeID(Type{ReferenceType{.referenced_type = operand_info.type, .is_mutable = is_mutable}});
            }
            break;
    }
    
    // Return: type based on operator, mutability based on operator, place status for dereference
    return ExprInfo(result_type, false, is_place, operand_info.endpoints);
}

ExprInfo ExprChecker::check_binary_op(hir::BinaryOp& binary_op) {
    // Check left and right operands
    ExprInfo lhs_info = check(*binary_op.lhs);
    ExprInfo rhs_info = check(*binary_op.rhs);
    
    TypeId result_type;
    
    switch (binary_op.op) {
        // Arithmetic operations: Both operands numeric, result is coerced common type
        case hir::BinaryOp::ADD:
        case hir::BinaryOp::SUB:
        case hir::BinaryOp::MUL:
        case hir::BinaryOp::DIV:
        case hir::BinaryOp::REM:
            if (!is_numeric_type(lhs_info.type) || !is_numeric_type(rhs_info.type)) {
                report_invalid_operation("Arithmetic operations require numeric operands");
            }
            
            // Try to find common type for type coercion
            if (auto common_type = find_common_type(lhs_info.type, rhs_info.type)) {
                result_type = *common_type;
            } else {
                // Default to left operand type if no common type found
                result_type = lhs_info.type;
            }
            break;
            
        // Comparison operations: Operands comparable, result is BOOL
        case hir::BinaryOp::EQ:
        case hir::BinaryOp::NE:
        case hir::BinaryOp::LT:
        case hir::BinaryOp::GT:
        case hir::BinaryOp::LE:
        case hir::BinaryOp::GE:
            // For now, allow comparison of same types
            if (lhs_info.type != rhs_info.type) {
                report_invalid_operation("Comparison operations require operands of the same type");
            }
            result_type = get_typeID(Type{PrimitiveKind::BOOL});
            break;
            
        // Logical operations: Both operands BOOL, result is BOOL
        case hir::BinaryOp::AND:
        case hir::BinaryOp::OR:
            // For now, allow comparison of same types
            if (lhs_info.type != rhs_info.type) {
                report_invalid_operation("Logical operations require operands of the same type");
            }
            result_type = get_typeID(Type{PrimitiveKind::BOOL});
            break;
            
        // Bitwise operations: Both operands numeric, result is coerced common type
        case hir::BinaryOp::BIT_AND:
        case hir::BinaryOp::BIT_XOR:
        case hir::BinaryOp::BIT_OR:
        case hir::BinaryOp::SHL:
        case hir::BinaryOp::SHR:
            if (!is_numeric_type(lhs_info.type) || !is_numeric_type(rhs_info.type)) {
                report_invalid_operation("Bitwise operations require numeric operands");
            }
            
            // Try to find common type for type coercion
            if (auto common_type = find_common_type(lhs_info.type, rhs_info.type)) {
                result_type = *common_type;
            } else {
                // Default to left operand type if no common type found
                result_type = lhs_info.type;
            }
            break;
    }
    
    // Merge endpoints from both operands (sequential composition)
    EndpointSet endpoints = merge_sequential(lhs_info.endpoints, rhs_info.endpoints);
    
    // Return: type based on operator, mutability based on operator, place status for dereference
    return ExprInfo(result_type, false, false, endpoints);
}

ExprInfo ExprChecker::check_cast(hir::Cast& cast) {
    // Check the expression being cast
    ExprInfo expr_info = check(*cast.expr);
    
    // Get the target type
    TypeId target_type = hir::helper::get_resolved_type(cast.target_type);
    
    // Validate cast is allowed between source and target types
    // For now, allow numeric casts and reference casts
    bool valid_cast = false;
    
    // Numeric casts
    if (is_numeric_type(expr_info.type) && is_numeric_type(target_type)) {
        valid_cast = true;
        
        // Check for potential data loss in numeric casts
        // This is a simplified check - a more comprehensive implementation would
        // check the actual ranges and precision of the types
        if (auto src_prim = std::get_if<PrimitiveKind>(&expr_info.type->value)) {
            if (auto tgt_prim = std::get_if<PrimitiveKind>(&target_type->value)) {
                // Check for narrowing casts
                if ((*src_prim == PrimitiveKind::ISIZE || *src_prim == PrimitiveKind::I32) &&
                    (*tgt_prim == PrimitiveKind::U32 || *tgt_prim == PrimitiveKind::USIZE)) {
                    // Potential data loss when casting from signed to unsigned
                    // In a full implementation, we'd check the actual value
                }
            }
        }
    }
    // Reference casts (upcasting and downcasting)
    else if (is_reference_type(expr_info.type) && is_reference_type(target_type)) {
        valid_cast = true;
    }
    // Cast to/from bool
    else if (is_bool_type(expr_info.type) || is_bool_type(target_type)) {
        valid_cast = true;
    }
    
    if (!valid_cast) {
        report_invalid_operation("Invalid cast between types");
    }
    
    // Return: target type, non-mutable, non-place, endpoints from operand
    return ExprInfo(target_type, false, false, expr_info.endpoints);
}

// Control flow expressions
ExprInfo ExprChecker::check_call(hir::Call& call) {
    // Check callee
    ExprInfo callee_info = check(*call.callee);
    
    // Verify callee is resolved FuncUse
    if (auto func_use = std::get_if<hir::FuncUse>(&call.callee->value)) {
        if (!func_use->def) {
            throw SemanticError("Function reference is not resolved");
        }
        
        // Get function definition
        const hir::Function* func_def = func_use->def;
        
        // Check argument count
        if (call.args.size() != func_def->params.size()) {
            throw SemanticError("Argument count mismatch in function call");
        }
        
        // Check argument types
        for (size_t i = 0; i < call.args.size(); ++i) {
            ExprInfo arg_info = check(*call.args[i]);
            
            // Get expected parameter type
            if (i >= func_def->params.size()) {
                throw SemanticError("Too many arguments in function call");
            }
            
            // For now, we assume parameters are resolved to Local with type annotation
            // In a full implementation, we'd need to handle pattern matching
            const hir::Pattern* param = func_def->params[i].get();
            if (auto binding_def = std::get_if<hir::BindingDef>(&param->value)) {
                if (auto local = std::get_if<hir::Local*>(&binding_def->local)) {
                    if ((*local)->type_annotation) {
                        TypeId param_type = hir::helper::get_resolved_type(*(*local)->type_annotation);
                        
                        // Check type compatibility
                        if (!is_assignable(param_type, arg_info.type)) {
                            report_type_mismatch(param_type, arg_info.type);
                        }
                    } else {
                        throw SemanticError("Parameter type annotation is missing");
                    }
                } else {
                    throw SemanticError("Parameter binding is not resolved to local");
                }
            } else {
                throw SemanticError("Parameter is not a binding definition");
            }
        }
        
        // Get function return type
        TypeId return_type;
        if (func_def->return_type) {
            return_type = hir::helper::get_resolved_type(*func_def->return_type);
        } else {
            // Default to unit type if no return type specified
            return_type = get_typeID(Type{UnitType{}});
        }
        
        // Create endpoints: normal plus return
        EndpointSet endpoints = normal_endpoint();
        endpoints.merge(return_endpoint(std::variant<hir::Function*, hir::Method*>{const_cast<hir::Function*>(func_def)}, return_type));
        
        // Return: function's return type, non-mutable, non-place, normal plus return endpoints
        return ExprInfo(return_type, false, false, endpoints);
    } else {
        throw SemanticError("Callee is not a function");
    }
}

ExprInfo ExprChecker::check_method_call(hir::MethodCall& method_call) {
    // Determine receiver type first
    ExprInfo receiver_info = check(*method_call.receiver);
    
    // Resolve method in impl table
    const hir::Method* method_def = nullptr;
    
    if (auto method_name = std::get_if<ast::Identifier>(&method_call.method)) {
        // Method is not yet resolved, try to find it in impl table
        // This is a simplified implementation - in a full implementation,
        // we'd search the impl table for methods matching the receiver type
        
        // For now, we'll assume the method is already resolved
        throw SemanticError("Method call not resolved - method resolution not implemented");
    } else if (auto method = std::get_if<const hir::Method*>(&method_call.method)) {
        method_def = *method;
    } else {
        throw SemanticError("Invalid method call method reference");
    }
    
    if (!method_def) {
        throw SemanticError("Method definition is not available");
    }
    
    // Check argument count
    if (method_call.args.size() != method_def->params.size()) {
        throw SemanticError("Argument count mismatch in method call");
    }
    
    // Check argument types
    for (size_t i = 0; i < method_call.args.size(); ++i) {
        ExprInfo arg_info = check(*method_call.args[i]);
        
        // Get expected parameter type
        if (i >= method_def->params.size()) {
            throw SemanticError("Too many arguments in method call");
        }
        
        // For now, we assume parameters are resolved to Local with type annotation
        const hir::Pattern* param = method_def->params[i].get();
        if (auto binding_def = std::get_if<hir::BindingDef>(&param->value)) {
            if (auto local = std::get_if<hir::Local*>(&binding_def->local)) {
                if ((*local)->type_annotation) {
                    TypeId param_type = hir::helper::get_resolved_type(*(*local)->type_annotation);
                    
                    // Check type compatibility
                    if (!is_assignable(param_type, arg_info.type)) {
                        report_type_mismatch(param_type, arg_info.type);
                    }
                } else {
                    throw SemanticError("Parameter type annotation is missing");
                }
            } else {
                throw SemanticError("Parameter binding is not resolved to local");
            }
        } else {
            throw SemanticError("Parameter is not a binding definition");
        }
    }
    
    // Check self parameter compatibility
    if (method_def->self_param.is_reference) {
        // Method expects a reference
        if (!is_reference_type(receiver_info.type)) {
            // Try auto-reference
            TypeId ref_type = get_typeID(Type{ReferenceType{
                .referenced_type = receiver_info.type, 
                .is_mutable = method_def->self_param.is_mutable
            }});
            
            // Apply auto-reference transformation if needed
            apply_auto_reference_method_call(method_call, receiver_info.type);
        }
    }
    
    // Get method return type
    TypeId return_type;
    if (method_def->return_type) {
        return_type = hir::helper::get_resolved_type(*method_def->return_type);
    } else {
        // Default to unit type if no return type specified
        return_type = get_typeID(Type{UnitType{}});
    }
    
    // Create endpoints: normal plus return
    EndpointSet endpoints = normal_endpoint();
    endpoints.merge(return_endpoint(std::variant<hir::Function*, hir::Method*>{const_cast<hir::Method*>(method_def)}, return_type));
    
    // Return: method's return type, non-mutable, non-place, normal plus return endpoints
    return ExprInfo(return_type, false, false, endpoints);
}

ExprInfo ExprChecker::check_if(hir::If& if_expr) {
    // Check condition
    ExprInfo condition_info = check(*if_expr.condition);
    
    // Verify condition is BOOL type
    if (!is_bool_type(condition_info.type)) {
        throw SemanticError("If condition must be boolean type");
    }
    
    // Check then block
    ExprInfo then_info = check_block(*if_expr.then_block);
    
    // Check else expression if present
    std::optional<ExprInfo> else_info;
    bool has_else = false;
    if (if_expr.else_expr) {
        has_else = true;
        
        // For now, just check if it's a Block or Expr
        // This is a simplified approach - in a full implementation,
        // we'd handle the variant properly
        try {
            // Try to treat it as an expression
            else_info = check(*if_expr.else_expr.value().get());
        } catch (...) {
            // If that fails, report an error
            throw SemanticError("Invalid else expression type");
        }
    }
    
    // Check branch type compatibility
    TypeId result_type;
    if (has_else && else_info) {
        // If both branches exist, find common type or check compatibility
        if (then_info.type == else_info->type) {
            result_type = then_info.type;
        } else {
            // Try to find common type for type coercion
            if (auto common_type = find_common_type(then_info.type, else_info->type)) {
                result_type = *common_type;
            } else {
                // For now, require exact type match
                report_type_mismatch(then_info.type, else_info->type);
            }
        }
    } else {
        // If no else branch, result type is unit type
        result_type = get_typeID(Type{UnitType{}});
    }
    
    // Merge endpoints from branches
    EndpointSet endpoints;
    if (has_else && else_info) {
        // Union of endpoints from both branches
        endpoints = merge_branches(then_info.endpoints, else_info->endpoints);
    } else {
        // If no else branch, add normal endpoint
        endpoints = then_info.endpoints;
        endpoints.insert(NormalEndpoint{});
    }
    
    // Return: common branch type or unit type, non-mutable, non-place, union of branch endpoints
    return ExprInfo(result_type, false, false, endpoints);
}

ExprInfo ExprChecker::check_loop(hir::Loop& loop) {
    // Save current loop context
    auto saved_loop = current_loop;
    current_loop = &loop;
    
    // Check body expressions
    ExprInfo body_info = check_block(*loop.body);
    
    // Restore current loop context
    current_loop = saved_loop;
    
    // Loop always returns unit type
    TypeId unit_type = get_typeID(Type{UnitType{}});
    
    // Create endpoints: include break endpoints from body, add normal endpoint if body has normal endpoint
    EndpointSet endpoints;
    
    // Include break endpoints from body
    for (const auto& endpoint : body_info.endpoints) {
        if (std::holds_alternative<BreakEndpoint>(endpoint)) {
            endpoints.insert(endpoint);
        }
    }
    
    // Add normal endpoint if body has normal endpoint
    if (body_info.has_normal_endpoint()) {
        endpoints.insert(NormalEndpoint{});
    }
    
    // Return: unit type, non-mutable, non-place, break plus normal endpoints
    return ExprInfo(unit_type, false, false, endpoints);
}

ExprInfo ExprChecker::check_while(hir::While& while_expr) {
    // TODO: Implement while checking
    // - Verify condition is BOOL type
    // - Check body expressions
    throw SemanticError("While checking not yet implemented");
}

ExprInfo ExprChecker::check_break(hir::Break& break_expr) {
    // TODO: Implement break checking
    // - Check value type matches loop expectation if present
    throw SemanticError("Break checking not yet implemented");
}

ExprInfo ExprChecker::check_continue(hir::Continue& continue_expr) {
    // TODO: Implement continue checking
    throw SemanticError("Continue checking not yet implemented");
}

ExprInfo ExprChecker::check_return(hir::Return& return_expr) {
    // TODO: Implement return checking
    // - Verify value type matches function return type
    throw SemanticError("Return checking not yet implemented");
}

// Block expressions
ExprInfo ExprChecker::check_block(hir::Block& block) {
    // TODO: Implement block checking
    // - Check all statements are valid
    // - Validate final expression if present
    throw SemanticError("Block checking not yet implemented");
}

// Type operations are now in semantic::helper::type_helper namespace

// Control flow operations are now in semantic::control_flow_helper namespace

// HIR transformation operations are now in hir::helper::transform_helper namespace

// Error reporting operations are now in error_helper namespace

} // namespace semantic