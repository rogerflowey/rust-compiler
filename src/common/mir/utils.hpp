#pragma once

#include "common/mir/constant.hpp"
#include "common/mir/function_sig.hpp"

#include "semantic/hir/hir.hpp"
#include "type/type.hpp"

#include <optional>
#include <string>

namespace mir {
// Re-export BinaryOpRValue kind enum which might be needed for classification
// Wait, BinaryOpRValue is in src/mir/mir.hpp but we didn't move it to
// constant.hpp. The Lowering logic needs it. I should probably move RValue op
// enums to constant.hpp or a new separate header like `ops.hpp`. For now, let's
// look at `utils.hpp` (was `lower_common.hpp`). It declares
// `classify_binary_kind`. It returns `BinaryOpRValue::Kind`. `BinaryOpRValue`
// is defined in `mir.hpp` (Step 11). I missed extracting `BinaryOpRValue` and
// `UnaryOpRValue`. I should check `opt/mir/nodes.hpp`. It has its own
// `BinaryOpNode::Kind`. `lower.cpp` (Step 22) uses `BinaryOpNode::Kind`.
// `lower_expr.cpp` (Step 76) calls `classify_binary_op` (static method in
// `OptFunctionLowerer`?). No, `lower_expr.cpp` line 394 calls
// `classify_binary_op`. `lower_internal.hpp` line 164 declares `static
// BinaryOpNode::Kind classify_binary_op`. So `opt/mir` does NOT use
// `mir::detail::classify_binary_kind`? Let's check
// `opt/mir/lower/lower_expr.cpp`. It calls `classify_binary_op` which is a
// static member of `OptFunctionLowerer`. It seems `opt/mir` re-implemented
// binary op classification? `lower_internal.hpp` (Step 28) line 164: `static
// BinaryOpNode::Kind classify_binary_op(...)`. So `opt/mir` might not need
// `classify_binary_kind` from old MIR. I should verify if `classify_binary_op`
// is implemented in `lower.cpp`. Accessing `mir::detail::classify_binary_kind`
// (if used) would require `BinaryOpRValue`. If `opt/mir` has its own, I don't
// need to move `BinaryOpRValue` to common. I'll check `lower.cpp` or
// `lower_expr.cpp` for the implementation of `classify_binary_op`. Wait,
// `lower_expr.cpp` line 393 call `classify_binary_op`. Where is it defined? I
// haven't seen the definition in the snippets of `lower.cpp` or
// `lower_expr.cpp`. Maybe it's at the end of `lower_expr.cpp` which was
// truncated (only showed 800 lines). Or maybe it delegates to `mir::detail` and
// converts? I will check `lower_expr.cpp` around line 1000 or find where
// `classify_binary_op` is defined. If it delegates, I need `BinaryOpRValue`. If
// it implements logic itself, I don't.
//
// Let's assume for now I only need `is_aggregate_type`, `derive_function_name`,
// types etc. `lower_common.hpp` has `classify_binary_kind`. I might skip moving
// it if unused by OPT MIR.
//
// What about `lower_const.hpp`? It returns `Constant`. I successfully moved
// `Constant`.
//
// So for `utils.hpp`:
namespace detail {

TypeId get_unit_type();
TypeId get_bool_type();

bool is_unit_type(TypeId type);
bool is_never_type(TypeId type);
bool is_aggregate_type(TypeId type); // Returns true for StructType or ArrayType

Constant make_bool_constant(bool value);
// Operand make_constant_operand(const Constant& constant); // Operand not moved
// yet.

std::optional<type::PrimitiveKind> get_primitive_kind(TypeId type);
bool is_signed_integer_type(TypeId type);
bool is_unsigned_integer_type(TypeId type);
bool is_bool_type(TypeId type);
TypeId canonicalize_type_for_mir(TypeId type);
TypeId make_ref_type(TypeId pointee); // Return a &T / reference type

void populate_abi_params(MirFunctionSig &sig);

std::string type_name(TypeId type);
std::string derive_function_name(const hir::Function &function,
                                 const std::string &scope);
std::string derive_method_name(const hir::Method &method,
                               const std::string &scope);

} // namespace detail
} // namespace mir
