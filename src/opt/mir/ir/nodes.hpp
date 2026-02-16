#pragma once

#include "opt/mir/ir/node_id.hpp"
#include "opt/mir/ir/slot.hpp"
#include "type/type.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace opt::mir {

// ============================================================================
// Constant value (shared with existing MIR where applicable)
// ============================================================================

struct ConstantValue {
  enum class Kind { Bool, Int, Char };

  Kind kind = Kind::Int;
  std::uint64_t bits =
      0; // raw bit pattern (bool: 0/1, char: codepoint, int: value)
  bool is_signed = false; // meaningful for Kind::Int
};

// ============================================================================
// Projections & Place — addressing into slots
// ============================================================================

/// Field access by index (struct/tuple field).
struct FieldProjection {
  std::size_t index = 0;
  bool operator==(const FieldProjection &) const = default;
};

/// Array/pointer index access. The index is a NodeId (computed at runtime).
struct IndexProjection {
  NodeId index = invalid_node;
  bool operator==(const IndexProjection &) const = default;
};

using Projection = std::variant<FieldProjection, IndexProjection>;

/// Place — a memory location: base (slot or pointer) + zero or more
/// projections.
using PlaceBase = std::variant<SlotId, NodeId>;

struct Place {
  PlaceBase base;
  std::vector<Projection> projections;

  bool operator==(const Place &) const = default;

  /// Convenience: construct a simple slot place with no projections.
  static Place simple(SlotId s) { return Place{PlaceBase{s}, {}}; }

  /// Convenience: construct a pointer-based place.
  static Place from_ptr(NodeId ptr) { return Place{PlaceBase{ptr}, {}}; }
};

// ============================================================================
// Floating Nodes — live in the Arena, referenced by NodeId
//   These are pure computations with no control-flow dependency.
// ============================================================================

struct ConstantNode {
  ConstantValue value;
};

struct BinaryOpNode {
  enum class Kind {
    IAdd,
    UAdd,
    ISub,
    USub,
    IMul,
    UMul,
    IDiv,
    UDiv,
    IRem,
    URem,
    BoolAnd,
    BoolOr,
    BitAnd,
    BitXor,
    BitOr,
    Shl,
    ShrLogical,
    ShrArithmetic,
    ICmpEq,
    ICmpNe,
    ICmpLt,
    ICmpLe,
    ICmpGt,
    ICmpGe,
    UCmpEq,
    UCmpNe,
    UCmpLt,
    UCmpLe,
    UCmpGt,
    UCmpGe,
    BoolEq,
    BoolNe
  };

  Kind kind;
  NodeId lhs = invalid_node;
  NodeId rhs = invalid_node;
};

struct UnaryOpNode {
  enum class Kind { Not, Neg };

  Kind kind;
  NodeId operand = invalid_node;
};

/// Load — observation of memory. Floating node tethered to a Token.
/// If analysis proves the token can be retargeted to an earlier point,
/// the Load automatically floats up.
struct LoadNode {
  TokenId token = invalid_token;
  Place place;
};

struct CastNode {
  NodeId operand = invalid_node;
  type::TypeId target_type = type::invalid_type_id;
};

struct AddressOfNode {
  Place place;
  Mutability mutability = Mutability::Immutable;
};

struct CallResultNode {
  TokenId call_token = invalid_token;
};

// The variant of all floating node kinds
using NodeKind = std::variant<ConstantNode, BinaryOpNode, UnaryOpNode, LoadNode,
                              CastNode, AddressOfNode, CallResultNode>;

/// A floating node: its kind + result type.
struct Node {
  NodeKind kind;
  type::TypeId type = type::invalid_type_id;
};

// ============================================================================
// Pinned Instructions — live physically inside BasicBlock lists
//   These anchor side effects and control flow to the skeleton.
// ============================================================================

/// Store — mutation anchored to a token.
///   %t_out = Store(%t_in, place, %value)
struct StoreInst {
  TokenId t_in = invalid_token;
  Place place;
  NodeId value = invalid_node;
  TokenId t_out = invalid_token;
};

/// Branch — produces two control tokens.
///   (%t_true, %t_false) = Branch(%t_in, %cond)
struct BranchInst {
  TokenId t_in = invalid_token;
  NodeId cond = invalid_node;
  TokenId t_true = invalid_token;
  TokenId t_false = invalid_token;
  BlockId bb_true = invalid_block;
  BlockId bb_false = invalid_block;
};

/// Jump — unconditional transfer.
struct JumpInst {
  TokenId t_in = invalid_token;
  BlockId target = invalid_block;
};

/// TokenPhi — merges control tokens at a join point.
///   Must be the first instruction(s) of a Join Block.
struct TokenPhiIncoming {
  BlockId block = invalid_block;
  TokenId token = invalid_token;
};

struct TokenPhiInst {
  std::vector<TokenPhiIncoming> incoming;
  TokenId t_out = invalid_token;
};

/// Memcopy — copies bytes from source place to dest place.
/// Used for aggregate initialization and assignment.
struct MemcopyInst {
  TokenId t_in = invalid_token;
  Place dest;
  Place src;
  type::TypeId type = type::invalid_type_id; // Defines the size/alignment
  TokenId t_out = invalid_token;
};

/// Return — function exit.
struct ReturnInst {
  TokenId t_in = invalid_token;
  std::optional<NodeId> value;
};

/// Call target: internal function id or external name.
struct CallTarget {
  enum class Kind { Internal, External };
  Kind kind = Kind::Internal;
  std::uint32_t id = 0;
  std::string name; // populated for External
};

// Argument: either a scalar value (NodeId) or a byval slot (SlotId)
using CallArg = std::variant<NodeId, SlotId>;

/// Call — side-effecting function invocation.
struct CallInst {
  TokenId t_in = invalid_token;
  CallTarget target;
  std::vector<CallArg> args;
  TokenId t_out = invalid_token;

  // Aggregate return: caller-provided slot
  std::optional<SlotId> sret_slot;

  // Result type (for CallResult node to inherit)
  type::TypeId result_type = type::invalid_type_id;
};

// The variant of all pinned instruction kinds
using PinnedInstKind =
    std::variant<StoreInst, BranchInst, JumpInst, TokenPhiInst, MemcopyInst,
                 ReturnInst, CallInst>;

struct PinnedInst {
  PinnedInstKind kind;
};

} // namespace opt::mir
