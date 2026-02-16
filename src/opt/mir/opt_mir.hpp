#pragma once

#include "opt/mir/basic_block.hpp"
#include "opt/mir/node_id.hpp"
#include "opt/mir/nodes.hpp"
#include "opt/mir/slot.hpp"
#include "type/type.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opt::mir {

/// OptFunction — a single function in the optimization MIR.
///
/// Layout:
///   - Arena: all floating nodes (indexed by NodeId)
///   - Skeleton: basic blocks with pinned instructions (indexed by BlockId)
///   - Slots: storage locations (indexed by SlotId)
///   - Tokens: monotonic counter for temporal/control identity
struct OptFunction {
  std::string name;

  // ── The Arena (floating nodes) ──────────────────────────────────────
  std::vector<Node> nodes; // indexed by NodeId

  // ── The Inst Arena (pinned instructions) ────────────────────────────
  std::vector<PinnedInst> insts;      // indexed by InstId
  std::vector<BlockId> inst_to_block; // parallel: InstId → owning BlockId

  // ── The Skeleton (basic blocks) ─────────────────────────────────────
  std::vector<BasicBlock> blocks; // indexed by BlockId
  BlockId entry_block = invalid_block;

  // ── Slots (storage locations) ───────────────────────────────────────
  std::vector<Slot> slots; // indexed by SlotId

  // ── Token counter ───────────────────────────────────────────────────
  std::uint32_t next_token_ = 0;

  // ── Allocation helpers ──────────────────────────────────────────────

  NodeId alloc_node(Node node) {
    auto id = NodeId{static_cast<std::uint32_t>(nodes.size())};
    nodes.push_back(std::move(node));
    return id;
  }

  InstId alloc_inst(PinnedInst inst, BlockId block) {
    auto id = InstId{static_cast<std::uint32_t>(insts.size())};
    insts.push_back(std::move(inst));
    inst_to_block.push_back(block);
    return id;
  }

  TokenId alloc_token() { return TokenId{next_token_++}; }

  SlotId alloc_slot(Slot slot) {
    auto id = SlotId{static_cast<std::uint32_t>(slots.size())};
    slots.push_back(std::move(slot));
    return id;
  }

  BlockId alloc_block() {
    auto id = BlockId{static_cast<std::uint32_t>(blocks.size())};
    blocks.emplace_back();
    blocks.back().id = id;
    return id;
  }

  // ── Accessors ───────────────────────────────────────────────────────

  [[nodiscard]] const Node &get_node(NodeId id) const {
    return nodes.at(raw(id));
  }

  [[nodiscard]] Node &get_node_mut(NodeId id) { return nodes.at(raw(id)); }

  [[nodiscard]] const PinnedInst &get_inst(InstId id) const {
    return insts.at(raw(id));
  }

  [[nodiscard]] PinnedInst &get_inst_mut(InstId id) {
    return insts.at(raw(id));
  }

  [[nodiscard]] BlockId inst_block(InstId id) const {
    return inst_to_block.at(raw(id));
  }

  [[nodiscard]] const BasicBlock &get_block(BlockId id) const {
    return blocks.at(raw(id));
  }

  [[nodiscard]] BasicBlock &get_block_mut(BlockId id) {
    return blocks.at(raw(id));
  }

  [[nodiscard]] const Slot &get_slot(SlotId id) const {
    return slots.at(raw(id));
  }

  [[nodiscard]] type::TypeId node_type(NodeId id) const {
    return nodes.at(raw(id)).type;
  }
};

/// OptModule — top-level container for all functions in the optimization MIR.
struct OptModule {
  std::vector<OptFunction> functions;
};

} // namespace opt::mir
