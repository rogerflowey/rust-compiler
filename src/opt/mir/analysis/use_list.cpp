#include "opt/mir/analysis/use_list.hpp"
#include "opt/mir/ir/module.hpp"

namespace opt::mir {

namespace {
template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;
} // namespace

// ============================================================================
// Introspection - Defs
// ============================================================================

std::vector<Def> defs_of(NodeId id, const Node &) {
  // A floating node always defines itself (a value).
  return {NodeDef{id}};
}

std::vector<Def> defs_of(InstId, const PinnedInst &, TokenId t_out) {
  // A pinned instruction always defines its output token.
  if (t_out != invalid_token) {
    return {TokenDef{t_out}};
  }
  return {};
}

// ============================================================================
// Introspection - Uses
// ============================================================================

std::vector<Use> uses_of(const Node &node) {
  std::vector<Use> uses;
  std::visit(
      Overloaded{
          [&](const ConstantNode &) {},
          [&](const BinaryOpNode &n) {
            uses.push_back(NodeUse{n.lhs});
            uses.push_back(NodeUse{n.rhs});
          },
          [&](const UnaryOpNode &n) { uses.push_back(NodeUse{n.operand}); },
          [&](const LoadNode &n) {
            uses.push_back(TokenUse{n.token});
            // Place uses
            std::visit(
                Overloaded{[&](SlotId s) { uses.push_back(SlotUse{s}); },
                           [&](NodeId n) { uses.push_back(NodeUse{n}); }},
                n.place.base);
            for (const auto &proj : n.place.projections) {
              if (auto *idx = std::get_if<IndexProjection>(&proj)) {
                uses.push_back(NodeUse{idx->index});
              }
            }
          },
          [&](const CastNode &n) { uses.push_back(NodeUse{n.operand}); },
          [&](const AddressOfNode &n) {
            std::visit(
                Overloaded{[&](SlotId s) { uses.push_back(SlotUse{s}); },
                           [&](NodeId n) { uses.push_back(NodeUse{n}); }},
                n.place.base);
            for (const auto &proj : n.place.projections) {
              if (auto *idx = std::get_if<IndexProjection>(&proj)) {
                uses.push_back(NodeUse{idx->index});
              }
            }
          },
          [&](const CallResultNode &n) {
            uses.push_back(TokenUse{n.call_token});
          }},
      node.kind);
  return uses;
}

std::vector<Use> uses_of(const PinnedInst &inst) {
  std::vector<Use> uses;
  auto scan_place = [&](const Place &p) {
    std::visit(Overloaded{[&](SlotId s) { uses.push_back(SlotUse{s}); },
                          [&](NodeId n) { uses.push_back(NodeUse{n}); }},
               p.base);
    for (const auto &proj : p.projections) {
      if (auto *idx = std::get_if<IndexProjection>(&proj)) {
        uses.push_back(NodeUse{idx->index});
      }
    }
  };

  std::visit(Overloaded{[&](const StoreInst &s) {
                          uses.push_back(TokenUse{s.t_in});
                          scan_place(s.place);
                          uses.push_back(NodeUse{s.value});
                        },
                        [&](const BranchInst &b) {
                          uses.push_back(TokenUse{b.t_in});
                          uses.push_back(NodeUse{b.cond});
                          uses.push_back(BlockUse{b.bb_true});
                          uses.push_back(BlockUse{b.bb_false});
                        },
                        [&](const JumpInst &j) {
                          uses.push_back(TokenUse{j.t_in});
                          uses.push_back(BlockUse{j.target});
                        },
                        [&](const TokenPhiInst &p) {
                          for (const auto &inc : p.incoming) {
                            uses.push_back(TokenUse{inc.token});
                            uses.push_back(BlockUse{inc.block});
                          }
                        },
                        [&](const MemcopyInst &m) {
                          uses.push_back(TokenUse{m.t_in});
                          scan_place(m.dest);
                          scan_place(m.src);
                        },
                        [&](const ReturnInst &r) {
                          uses.push_back(TokenUse{r.t_in});
                          if (r.value) {
                            uses.push_back(NodeUse{*r.value});
                          }
                        },
                        [&](const CallInst &c) {
                          uses.push_back(TokenUse{c.t_in});
                          for (const auto &arg : c.args) {
                            std::visit(Overloaded{[&](NodeId n) {
                                                    uses.push_back(NodeUse{n});
                                                  },
                                                  [&](SlotId s) {
                                                    uses.push_back(SlotUse{s});
                                                  }},
                                       arg);
                          }
                          if (c.sret_slot) {
                            uses.push_back(SlotUse{*c.sret_slot});
                          }
                        }},
             inst.kind);
  return uses;
}

// ============================================================================
// UseLists Implementation
// ============================================================================

UseLists UseLists::build(const OptFunction &func) {
  UseLists ul;
  ul.resize_for(func);

  // Scan floating nodes
  for (std::uint32_t i = 0; i < func.nodes.size(); ++i) {
    auto nid = NodeId{i};
    ul.scan_node(nid, func.nodes[i]);
  }

  // Scan insts in every block
  for (const auto &bb : func.blocks) {
    for (auto iid : bb.inst_ids) {
      ul.scan_inst(iid, func.get_inst(iid));
    }
  }

  return ul;
}

void UseLists::resize_for(const OptFunction &func) {
  node_users_.resize(func.nodes.size());
  token_users_.resize(func.next_token_);

  node_uses_.resize(func.nodes.size());
  inst_uses_.resize(func.insts.size());
}

void UseLists::scan_node(NodeId id, const Node &node) {
  set_uses(id, uses_of(node));
}

void UseLists::scan_inst(InstId id, const PinnedInst &inst) {
  set_uses(id, uses_of(inst));
}

// ----------------------------------------------------------------------------
// Core Update Logic (Forward + Reverse syncing)
// ----------------------------------------------------------------------------

void UseLists::set_uses(NodeId user_id, std::vector<Use> new_uses) {
  if (raw(user_id) >= node_uses_.size()) {
    node_uses_.resize(raw(user_id) + 1);
  }

  // 1. Remove old uses
  User user = NodeUser{user_id};
  for (const auto &u : node_uses_[raw(user_id)]) {
    std::visit(Overloaded{[&](NodeUse use) { remove_user(use.id, user); },
                          [&](TokenUse use) { remove_user(use.id, user); },
                          [&](auto) {}},
               u);
  }

  // 2. Add new uses
  for (const auto &u : new_uses) {
    std::visit(Overloaded{[&](NodeUse use) { add_user(use.id, user); },
                          [&](TokenUse use) { add_user(use.id, user); },
                          [&](auto) {}},
               u);
  }

  // 3. Update forward map
  node_uses_[raw(user_id)] = std::move(new_uses);
}

void UseLists::set_uses(InstId user_id, std::vector<Use> new_uses) {
  if (raw(user_id) >= inst_uses_.size()) {
    inst_uses_.resize(raw(user_id) + 1);
  }

  User user = InstUser{user_id};
  for (const auto &u : inst_uses_[raw(user_id)]) {
    std::visit(Overloaded{[&](NodeUse use) { remove_user(use.id, user); },
                          [&](TokenUse use) { remove_user(use.id, user); },
                          [&](auto) {}},
               u);
  }

  for (const auto &u : new_uses) {
    std::visit(Overloaded{[&](NodeUse use) { add_user(use.id, user); },
                          [&](TokenUse use) { add_user(use.id, user); },
                          [&](auto) {}},
               u);
  }

  inst_uses_[raw(user_id)] = std::move(new_uses);
}

// ----------------------------------------------------------------------------
// Reverse Map Primitive Helpers
// ----------------------------------------------------------------------------

void UseLists::add_user(NodeId def, User user) {
  if (raw(def) >= node_users_.size())
    node_users_.resize(raw(def) + 1);
  node_users_[raw(def)].push_back(user);
}

void UseLists::add_user(TokenId def, User user) {
  if (raw(def) >= token_users_.size())
    token_users_.resize(raw(def) + 1);
  token_users_[raw(def)].push_back(user);
}

void UseLists::remove_user(NodeId def, User user) {
  if (raw(def) >= node_users_.size())
    return;
  auto &users = node_users_[raw(def)];
  auto it = std::find(users.begin(), users.end(), user);
  if (it != users.end()) {
    *it = users.back();
    users.pop_back();
  }
}

void UseLists::remove_user(TokenId def, User user) {
  if (raw(def) >= token_users_.size())
    return;
  auto &users = token_users_[raw(def)];
  auto it = std::find(users.begin(), users.end(), user);
  if (it != users.end()) {
    *it = users.back();
    users.pop_back();
  }
}

// ----------------------------------------------------------------------------
// Queries
// ----------------------------------------------------------------------------

std::span<const User> UseLists::users_of(NodeId def) const {
  if (raw(def) >= node_users_.size())
    return {};
  return node_users_[raw(def)];
}

std::span<const User> UseLists::users_of(TokenId def) const {
  if (raw(def) >= token_users_.size())
    return {};
  return token_users_[raw(def)];
}

// ----------------------------------------------------------------------------
// Notifications
// ----------------------------------------------------------------------------

void UseLists::notify_node_updated(NodeId id, const OptFunction &func) {
  set_uses(id, uses_of(func.nodes[raw(id)]));
}

void UseLists::notify_inst_updated(InstId id, const OptFunction &func) {
  set_uses(id, uses_of(func.get_inst(id)));
}

void UseLists::notify_node_removed(NodeId id) { set_uses(id, {}); }

void UseLists::notify_inst_removed(InstId id) { set_uses(id, {}); }

} // namespace opt::mir
