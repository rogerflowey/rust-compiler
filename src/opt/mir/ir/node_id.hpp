#pragma once

#include <cstdint>
#include <functional>
#include <limits>

namespace opt::mir {

// Strong typedef IDs — each is a distinct type wrapping uint32_t
enum class NodeId : std::uint32_t {};
enum class TokenId : std::uint32_t {};
enum class SlotId : std::uint32_t {};
enum class BlockId : std::uint32_t {};
enum class InstId : std::uint32_t {};

// Sentinel values
inline constexpr auto invalid_node =
    NodeId{std::numeric_limits<std::uint32_t>::max()};
inline constexpr auto invalid_token =
    TokenId{std::numeric_limits<std::uint32_t>::max()};
inline constexpr auto invalid_slot =
    SlotId{std::numeric_limits<std::uint32_t>::max()};
inline constexpr auto invalid_block =
    BlockId{std::numeric_limits<std::uint32_t>::max()};
inline constexpr auto invalid_inst =
    InstId{std::numeric_limits<std::uint32_t>::max()};

// Convenience: raw index extraction
inline constexpr std::uint32_t raw(NodeId id) {
  return static_cast<std::uint32_t>(id);
}
inline constexpr std::uint32_t raw(TokenId id) {
  return static_cast<std::uint32_t>(id);
}
inline constexpr std::uint32_t raw(SlotId id) {
  return static_cast<std::uint32_t>(id);
}
inline constexpr std::uint32_t raw(BlockId id) {
  return static_cast<std::uint32_t>(id);
}
inline constexpr std::uint32_t raw(InstId id) {
  return static_cast<std::uint32_t>(id);
}

} // namespace opt::mir

// Hash support for use in containers
template <> struct std::hash<opt::mir::NodeId> {
  std::size_t operator()(opt::mir::NodeId id) const noexcept {
    return std::hash<std::uint32_t>{}(opt::mir::raw(id));
  }
};
template <> struct std::hash<opt::mir::TokenId> {
  std::size_t operator()(opt::mir::TokenId id) const noexcept {
    return std::hash<std::uint32_t>{}(opt::mir::raw(id));
  }
};
template <> struct std::hash<opt::mir::SlotId> {
  std::size_t operator()(opt::mir::SlotId id) const noexcept {
    return std::hash<std::uint32_t>{}(opt::mir::raw(id));
  }
};
template <> struct std::hash<opt::mir::BlockId> {
  std::size_t operator()(opt::mir::BlockId id) const noexcept {
    return std::hash<std::uint32_t>{}(opt::mir::raw(id));
  }
};
template <> struct std::hash<opt::mir::InstId> {
  std::size_t operator()(opt::mir::InstId id) const noexcept {
    return std::hash<std::uint32_t>{}(opt::mir::raw(id));
  }
};
