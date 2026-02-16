#pragma once

#include "opt/mir/node_id.hpp"
#include "opt/mir/nodes.hpp"
#include "opt/mir/opt_mir.hpp"

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
  void notify_node_replaced(NodeId old_id, NodeId new_id,
                            const OptFunction &func);
  void notify_node_added(NodeId id, const OptFunction &func);
  void notify_node_removed(NodeId id);
  void notify_token_retargeted(TokenId old_tok, TokenId new_tok);

private:
  // Users of a Node value (can be other Nodes or Instructions)
  std::vector<std::vector<User>> node_users_;

  // Users of a Token value (can be other Nodes or Instructions)
  std::vector<std::vector<User>> token_users_;

  void resize_for(const OptFunction &func);

  void add_user(NodeId def, User user);
  void add_user(TokenId def, User user);

  void scan_node(NodeId id, const Node &node);
  void scan_inst(InstId id, const PinnedInst &inst);
};

} // namespace opt::mir
