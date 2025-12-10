#include "struct_enum_registration.hpp"

#include "semantic/hir/visitor/visitor_base.hpp"
#include "type/type.hpp"
#include "src/utils/error.hpp"

namespace semantic {

namespace {
    // Small visitor to walk the entire HIR and collect all struct definitions
    // This visitor leverages HirVisitorBase to properly traverse the entire HIR tree,
    // ensuring structs defined at any scope (global, inside functions, etc.) are collected.
    class StructCollectorVisitor : public hir::HirVisitorBase<StructCollectorVisitor> {
    public:
        std::vector<hir::StructDef*> structs;

        // Override struct visit to collect it and continue traversal
        void visit(hir::StructDef& struct_def) {
            structs.push_back(&struct_def);
            // Continue visiting via base class (which handles field type annotations)
            base().visit(struct_def);
        }

        // For all other nodes, use the base implementation which properly recurses
        // through the entire HIR tree (blocks, functions, expressions, etc.)
        template <typename T>
        void visit(T& node) {
            // Delegate to base class for proper traversal
            base().visit(node);
        }
    };
}

void StructEnumRegistrationPass::register_program(hir::Program& program) {
    // Resolve all struct and enum field types
    // Struct/enum skeleton registration (ID allocation) happens in StructEnumSkeletonRegistrationPass
    // which runs before name resolution. This pass fills in the field type information.
    
    // Collect all struct definitions from the entire HIR
    StructCollectorVisitor collector;
    for (auto& item_ptr : program.items) {
        std::visit([&collector](auto& item) { collector.visit(item); }, item_ptr->value);
    }

    // Resolve field types for all collected structs
    for (auto* struct_def : collector.structs) {
        resolve_struct_field_types(*struct_def);
    }
}

void StructEnumRegistrationPass::resolve_struct_field_types(hir::StructDef& struct_def) {
    // Resolve field types for a struct that was already registered in skeleton form
    auto struct_id_opt = type::TypeContext::get_instance().try_get_struct_id(&struct_def);
    if (!struct_id_opt) {
        throw SemanticError(
            "Struct '" + struct_def.name.name + "' not registered in skeleton pass",
            struct_def.name.span
        );
    }
    
    auto& struct_info = type::TypeContext::get_instance().get_struct(*struct_id_opt);
    
    // Verify field count matches
    if (struct_def.fields.size() != struct_info.fields.size()) {
        throw SemanticError(
            "Internal error: struct field count mismatch",
            struct_def.name.span
        );
    }

    // Resolve each field type
    for (size_t i = 0; i < struct_def.fields.size(); ++i) {
        // Resolve the field type annotation using SemanticContext
        if (i < struct_def.field_type_annotations.size()) {
            TypeId resolved_type = context.type_query(struct_def.field_type_annotations[i]);
            if (resolved_type == invalid_type_id) {
                throw SemanticError(
                    "Failed to resolve field type for '" + struct_info.fields[i].name + "'",
                    struct_def.fields[i].span
                );
            }
            // Update the cached field type
            const_cast<type::StructFieldInfo&>(struct_info.fields[i]).type = resolved_type;

            // Also update the struct_def field for backwards compatibility
            struct_def.fields[i].type = resolved_type;
        } else {
            throw SemanticError(
                "Struct field '" + struct_info.fields[i].name + "' has no type annotation",
                struct_def.fields[i].span
            );
        }
    }
}

} // namespace semantic
