#pragma once

#include "semantic/common.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/query/semantic_context.hpp"

namespace semantic {

/**
 * @brief Struct and Enum Field Type Resolution Pass
 *
 * Resolves all struct field type annotations that were registered in skeleton form.
 *
 * **Two-Phase Registration System**:
 * 1. StructEnumSkeletonRegistrationPass (runs before NameResolver)
 *    - Allocates struct/enum IDs
 *    - Creates skeleton StructInfo/EnumInfo with field names only
 *    - Allows name resolution to use struct/enum IDs for impl table
 *
 * 2. StructEnumRegistrationPass (runs after NameResolver, before SemanticChecker)
 *    - Resolves all struct field type annotations using SemanticContext::type_query
 *    - Updates the skeleton StructInfo with fully-resolved field types
 *    - Ensures all field types are valid (never invalid_type_id) before expression checking
 *
 * **Invariants established**:
 * - Every StructInfo in TypeContext has all field types resolved (no invalid_type_id)
 * - All struct/enum field types are available before expression checking
 *
 * **Requirements**:
 * - StructEnumSkeletonRegistrationPass must run before NameResolver
 * - This pass must run after NameResolver but before SemanticCheckVisitor
 */
class StructEnumRegistrationPass {
public:
    explicit StructEnumRegistrationPass(SemanticContext& context)
        : context(context) {}

    /**
     * @brief Visit the entire program and register all structs and enums
     * @param program The HIR program to process
     */
    void register_program(hir::Program& program);

private:
    SemanticContext& context;

    /**
     * @brief Resolve field types for a struct that was already skeleton-registered
     * @param struct_def The struct definition to process
     */
    void resolve_struct_field_types(hir::StructDef& struct_def);
};

} // namespace semantic
