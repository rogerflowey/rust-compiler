#pragma once

#include "opt/mir/ir/node_id.hpp"
#include "opt/mir/ir/nodes.hpp"
#include "opt/mir/ir/module.hpp"

#include <span>
#include <variant>
#include <vector>

namespace opt::mir {

// Overloaded visitor helper (may already be declared in printer.hpp, but
// we keep a local copy to avoid a header dependency on the printer).
namespace detail {
template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;
} // namespace detail

// ============================================================================
// UseLists — reverse-dependency maps for the OptFunction graph.
//
// Three maps:
//   node_users_[n]           → list of NodeIds whose definition references n
//   token_node_consumers_[t] → list of NodeIds whose definition references t
//   token_token_consumers_[t]→ list of TokenIds derived from t (via pinned
//                               instructions that consume t_in and produce
//                               t_out)
//
// Build once with UseLists::build(), then keep in sync via notify_* methods
// when the graph is mutated.
// ============================================================================

// ============================================================================
// Def / Use / User Types
// ============================================================================

struct NodeDef {
  NodeId id;
};
struct TokenDef {
  TokenId id;
};
using Def = std::variant<NodeDef, TokenDef>;

struct NodeUse {
  NodeId id;
};
struct TokenUse {
  TokenId id;
};
struct SlotUse {
  SlotId id;
};
struct BlockUse {
  BlockId id;
};
using Use = std::variant<NodeUse, TokenUse, SlotUse, BlockUse>;

struct NodeUser {
  NodeId id;
  bool operator==(const NodeUser &other) const { return id == other.id; }
};
struct InstUser {
  InstId id;
  bool operator==(const InstUser &other) const { return id == other.id; }
};
using User = std::variant<NodeUser, InstUser>;

// ============================================================================
// Introspection API
// ============================================================================

std::vector<Def> defs_of(NodeId id, const Node &node);
std::vector<Def> defs_of(InstId id, const PinnedInst &inst, TokenId t_out);

std::vector<Use> uses_of(const Node &node);
std::vector<Use> uses_of(const PinnedInst &inst);

// ============================================================================
// UseLists — reverse-dependency maps
// ============================================================================

class UseLists {
public:
  static UseLists build(const OptFunction &func);

  // Unified Query API
  [[nodiscard]] std::span<const User> users_of(NodeId def) const;
  [[nodiscard]] std::span<const User> users_of(TokenId def) const;

  // Incremental updates

  /// A node's content has changed (in-place modification).
  /// We compare old uses (from forward map) with new uses (from introspection)
  /// and update the reverse maps accordingly.
  void notify_node_updated(NodeId id, const OptFunction &func);
  void notify_inst_updated(InstId id, const OptFunction &func);

  /// A node is removed. We use the forward map to remove it from users' lists.
  void notify_node_removed(NodeId id);
  void notify_inst_removed(InstId id);

private:
  // REVERSE MAPS: Map Def -> List of Users

  // Users of a Node value (can be other Nodes or Instructions)
  std::vector<std::vector<User>> node_users_;

  // Users of a Token value (can be other Nodes or Instructions)
  std::vector<std::vector<User>> token_users_;

  // FORWARD MAPS: Map User -> List of Defs used
  // This allows efficiently finding what to disconnect when a node is
  // updated/removed.

  // What does this Node define/use?
  // Wait, UseLists tracks *uses*. So forward map is "What does this Node use?"
  // We store `Use` (variant of NodeUse, TokenUse...).
  // Actually, we need to store what `defs` it uses.
  // The `Use` variant (NodeUse etc) *contains* the Def ID.
  std::vector<std::vector<Use>> node_uses_; // NodeId -> vector<Use>
  std::vector<std::vector<Use>> inst_uses_; // InstId -> vector<Use>

  void resize_for(const OptFunction &func);

  void add_user(NodeId def, User user);
  void add_user(TokenId def, User user);
  void remove_user(NodeId def, User user);
  void remove_user(TokenId def, User user);

  // Helper to sync forward/reverse maps for a user
  void set_uses(NodeId user_id, std::vector<Use> new_uses);
  void set_uses(InstId user_id, std::vector<Use> new_uses);

  void scan_node(NodeId id, const Node &node);
  void scan_inst(InstId id, const PinnedInst &inst);
};

} // namespace opt::mir
