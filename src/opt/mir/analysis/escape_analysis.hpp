#pragma once

#include "opt/mir/ir/module.hpp"

#include <vector>

namespace opt::mir {

/// EscapeAnalysis — flow-insensitive slot escape summary.
///
/// A slot is marked as escaping iff there exists any AddressOfNode whose
/// place base is that SlotId.
class EscapeAnalysis {
public:
  static EscapeAnalysis run(const OptFunction &func);

  [[nodiscard]] bool escapes(SlotId slot) const;

private:
  explicit EscapeAnalysis(std::vector<bool> escaped_slots)
      : escaped_slots_(std::move(escaped_slots)) {}

  std::vector<bool> escaped_slots_;
};

} // namespace opt::mir
