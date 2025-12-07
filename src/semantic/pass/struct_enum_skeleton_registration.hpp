#pragma once

#include "src/semantic/hir/hir.hpp"
#include "src/type/type.hpp"

namespace semantic {

/**
 * @class StructEnumSkeletonRegistrationPass
 * @brief Early struct/enum registration to establish identity before name resolution
 *
 * This pass runs BEFORE name resolution and creates struct/enum IDs with skeleton info.
 * Field types may be invalid (invalid_type_id) at this stage - they will be resolved
 * in StructEnumRegistrationPass which runs after name resolution.
 *
 * Purpose: Allow name resolution to use struct/enum IDs for impl table lookups and
 * associated item resolution, without waiting for full type resolution.
 */
class StructEnumSkeletonRegistrationPass {
public:
    void register_program(hir::Program& program);

private:
    void register_struct_skeleton(hir::StructDef& struct_def);
    void register_enum_skeleton(hir::EnumDef& enum_def);
};

} // namespace semantic
