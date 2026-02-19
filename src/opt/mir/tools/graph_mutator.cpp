#include "opt/mir/tools/graph_mutator.hpp"

namespace opt::mir {

namespace {
template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

// ============================================================================
// Per-field rewrite helpers
// ============================================================================

/// Replace old_id → new_id within a Place (base + projections).
bool replace_node_in_place(Place &place, NodeId old_id, NodeId new_id) {
  bool changed = false;
  if (auto *base = std::get_if<NodeId>(&place.base)) {
    if (*base == old_id) {
      place.base = new_id;
      changed = true;
    }
  }
  for (auto &proj : place.projections) {
    if (auto *idx = std::get_if<IndexProjection>(&proj)) {
      if (idx->index == old_id) {
        idx->index = new_id;
        changed = true;
      }
    }
  }
  return changed;
}

bool replace_slot_in_place(Place &place, SlotId old_id, SlotId new_id) {
  bool changed = false;
  if (auto *base = std::get_if<SlotId>(&place.base)) {
    if (*base == old_id) {
      place.base = new_id;
      changed = true;
    }
  }
  return changed;
}

// ── NodeId replacement in Node kinds ──────────────────────────────────

bool replace_node_in(Node &node, NodeId old_id, NodeId new_id) {
  bool changed = false;
  std::visit(Overloaded{[&](ConstantNode &) {},
                        [&](BinaryOpNode &n) {
                          if (n.lhs == old_id) {
                            n.lhs = new_id;
                            changed = true;
                          }
                          if (n.rhs == old_id) {
                            n.rhs = new_id;
                            changed = true;
                          }
                        },
                        [&](UnaryOpNode &n) {
                          if (n.operand == old_id) {
                            n.operand = new_id;
                            changed = true;
                          }
                        },
                        [&](LoadNode &n) {
                          changed |=
                              replace_node_in_place(n.place, old_id, new_id);
                        },
                        [&](CastNode &n) {
                          if (n.operand == old_id) {
                            n.operand = new_id;
                            changed = true;
                          }
                        },
                        [&](AddressOfNode &n) {
                          changed |=
                              replace_node_in_place(n.place, old_id, new_id);
                        },
                        [&](CallResultNode &) {}},
             node.kind);
  return changed;
}

bool replace_node_in(PinnedInst &inst, NodeId old_id, NodeId new_id) {
  bool changed = false;
  std::visit(
      Overloaded{[&](StoreInst &s) {
                   changed |= replace_node_in_place(s.place, old_id, new_id);
                   if (s.value == old_id) {
                     s.value = new_id;
                     changed = true;
                   }
                 },
                 [&](BranchInst &b) {
                   if (b.cond == old_id) {
                     b.cond = new_id;
                     changed = true;
                   }
                 },
                 [&](JumpInst &) {}, [&](TokenPhiInst &) {},
                 [&](MemcopyInst &m) {
                   changed |= replace_node_in_place(m.dest, old_id, new_id);
                   changed |= replace_node_in_place(m.src, old_id, new_id);
                 },
                 [&](ReturnInst &r) {
                   if (r.value && *r.value == old_id) {
                     r.value = new_id;
                     changed = true;
                   }
                 },
                 [&](CallInst &c) {
                   for (auto &arg : c.args) {
                     if (auto *nid = std::get_if<NodeId>(&arg)) {
                       if (*nid == old_id) {
                         arg = new_id;
                         changed = true;
                       }
                     }
                   }
                 }},
      inst.kind);
  return changed;
}

// ── TokenId replacement in Node kinds ─────────────────────────────────

bool replace_token_in(Node &node, TokenId old_id, TokenId new_id) {
  bool changed = false;
  std::visit(Overloaded{[&](ConstantNode &) {}, [&](BinaryOpNode &) {},
                        [&](UnaryOpNode &) {},
                        [&](LoadNode &n) {
                          if (n.token == old_id) {
                            n.token = new_id;
                            changed = true;
                          }
                        },
                        [&](CastNode &) {}, [&](AddressOfNode &) {},
                        [&](CallResultNode &n) {
                          if (n.call_token == old_id) {
                            n.call_token = new_id;
                            changed = true;
                          }
                        }},
             node.kind);
  return changed;
}

bool replace_token_in(PinnedInst &inst, TokenId old_id, TokenId new_id) {
  bool changed = false;
  std::visit(Overloaded{[&](StoreInst &s) {
                          if (s.t_in == old_id) {
                            s.t_in = new_id;
                            changed = true;
                          }
                          if (s.t_out == old_id) {
                            s.t_out = new_id;
                            changed = true;
                          }
                        },
                        [&](BranchInst &b) {
                          if (b.t_in == old_id) {
                            b.t_in = new_id;
                            changed = true;
                          }
                          if (b.t_true == old_id) {
                            b.t_true = new_id;
                            changed = true;
                          }
                          if (b.t_false == old_id) {
                            b.t_false = new_id;
                            changed = true;
                          }
                        },
                        [&](JumpInst &j) {
                          if (j.t_in == old_id) {
                            j.t_in = new_id;
                            changed = true;
                          }
                        },
                        [&](TokenPhiInst &p) {
                          for (auto &inc : p.incoming) {
                            if (inc.token == old_id) {
                              inc.token = new_id;
                              changed = true;
                            }
                          }
                          if (p.t_out == old_id) {
                            p.t_out = new_id;
                            changed = true;
                          }
                        },
                        [&](MemcopyInst &m) {
                          if (m.t_in == old_id) {
                            m.t_in = new_id;
                            changed = true;
                          }
                          if (m.t_out == old_id) {
                            m.t_out = new_id;
                            changed = true;
                          }
                        },
                        [&](ReturnInst &r) {
                          if (r.t_in == old_id) {
                            r.t_in = new_id;
                            changed = true;
                          }
                        },
                        [&](CallInst &c) {
                          if (c.t_in == old_id) {
                            c.t_in = new_id;
                            changed = true;
                          }
                          if (c.t_out == old_id) {
                            c.t_out = new_id;
                            changed = true;
                          }
                        }},
             inst.kind);
  return changed;
}

// ── SlotId replacement in Node kinds ──────────────────────────────────

bool replace_slot_in(Node &node, SlotId old_id, SlotId new_id) {
  bool changed = false;
  std::visit(Overloaded{[&](ConstantNode &) {}, [&](BinaryOpNode &) {},
                        [&](UnaryOpNode &) {},
                        [&](LoadNode &n) {
                          changed |=
                              replace_slot_in_place(n.place, old_id, new_id);
                        },
                        [&](CastNode &) {},
                        [&](AddressOfNode &n) {
                          changed |=
                              replace_slot_in_place(n.place, old_id, new_id);
                        },
                        [&](CallResultNode &) {}},
             node.kind);
  return changed;
}

bool replace_slot_in(PinnedInst &inst, SlotId old_id, SlotId new_id) {
  bool changed = false;
  std::visit(
      Overloaded{[&](StoreInst &s) {
                   changed |= replace_slot_in_place(s.place, old_id, new_id);
                 },
                 [&](BranchInst &) {}, [&](JumpInst &) {},
                 [&](TokenPhiInst &) {},
                 [&](MemcopyInst &m) {
                   changed |= replace_slot_in_place(m.dest, old_id, new_id);
                   changed |= replace_slot_in_place(m.src, old_id, new_id);
                 },
                 [&](ReturnInst &) {},
                 [&](CallInst &c) {
                   for (auto &arg : c.args) {
                     if (auto *sid = std::get_if<SlotId>(&arg)) {
                       if (*sid == old_id) {
                         arg = new_id;
                         changed = true;
                       }
                     }
                   }
                   if (c.sret_slot && *c.sret_slot == old_id) {
                     c.sret_slot = new_id;
                     changed = true;
                   }
                 }},
      inst.kind);
  return changed;
}

} // anonymous namespace

// ============================================================================
// GraphMutator
// ============================================================================

GraphMutator::GraphMutator(OptFunction &func, UseLists &use_lists)
    : func_(func), use_lists_(use_lists) {}

void GraphMutator::replace_node_kind(NodeId id, NodeKind new_kind) {
  func_.get_node_mut(id).kind = std::move(new_kind);
  use_lists_.notify_node_updated(id, func_);
  touched_nodes_.push_back(id);
}

void GraphMutator::replace_inst(InstId id, PinnedInstKind new_kind) {
  func_.get_inst_mut(id).kind = std::move(new_kind);
  use_lists_.notify_inst_updated(id, func_);
  touched_insts_.push_back(id);
}

void GraphMutator::replace_all_uses_of(NodeId old_id, NodeId new_id) {
  // Snapshot user list (iteration invalidation safety).
  auto users = std::vector<User>(use_lists_.users_of(old_id).begin(),
                                 use_lists_.users_of(old_id).end());

  for (const auto &user : users) {
    std::visit(Overloaded{[&](const NodeUser &u) {
                            auto &node = func_.get_node_mut(u.id);
                            if (replace_node_in(node, old_id, new_id)) {
                              use_lists_.notify_node_updated(u.id, func_);
                              touched_nodes_.push_back(u.id);
                            }
                          },
                          [&](const InstUser &u) {
                            auto &inst = func_.get_inst_mut(u.id);
                            if (replace_node_in(inst, old_id, new_id)) {
                              use_lists_.notify_inst_updated(u.id, func_);
                              touched_insts_.push_back(u.id);
                            }
                          }},
               user);
  }
}

void GraphMutator::replace_all_uses_of(TokenId old_id, TokenId new_id) {
  auto users = std::vector<User>(use_lists_.users_of(old_id).begin(),
                                 use_lists_.users_of(old_id).end());

  for (const auto &user : users) {
    std::visit(Overloaded{[&](const NodeUser &u) {
                            auto &node = func_.get_node_mut(u.id);
                            if (replace_token_in(node, old_id, new_id)) {
                              use_lists_.notify_node_updated(u.id, func_);
                              touched_nodes_.push_back(u.id);
                            }
                          },
                          [&](const InstUser &u) {
                            auto &inst = func_.get_inst_mut(u.id);
                            if (replace_token_in(inst, old_id, new_id)) {
                              use_lists_.notify_inst_updated(u.id, func_);
                              touched_insts_.push_back(u.id);
                            }
                          }},
               user);
  }
}

void GraphMutator::replace_all_uses_of(SlotId old_id, SlotId new_id) {
  // Slots don't have a dedicated user list yet. We scan all nodes and
  // instructions. (A slot-user reverse map can be added later for perf.)
  for (std::uint32_t i = 0; i < func_.nodes.size(); ++i) {
    auto nid = NodeId{i};
    auto &node = func_.get_node_mut(nid);
    if (replace_slot_in(node, old_id, new_id)) {
      use_lists_.notify_node_updated(nid, func_);
      touched_nodes_.push_back(nid);
    }
  }

  for (std::uint32_t i = 0; i < func_.insts.size(); ++i) {
    auto iid = InstId{i};
    auto &inst = func_.get_inst_mut(iid);
    if (replace_slot_in(inst, old_id, new_id)) {
      use_lists_.notify_inst_updated(iid, func_);
      touched_insts_.push_back(iid);
    }
  }
}

std::vector<NodeId> GraphMutator::drain_touched_nodes() {
  return std::move(touched_nodes_);
}

std::vector<InstId> GraphMutator::drain_touched_insts() {
  return std::move(touched_insts_);
}

} // namespace opt::mir
