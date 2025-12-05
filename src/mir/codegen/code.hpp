#pragma once
#include <sstream>
#include <string>
#include <vector>

namespace codegen {


struct BlockCode {
  std::string label_line;
  std::vector<std::string> stmt_lines;
};

struct FunctionCode {
  std::string header_line;
  std::vector<BlockCode> blocks;
};

struct ProgramCode {
  std::vector<std::string> independent_lines;
  std::vector<FunctionCode> functions;

  std::string to_string(int indent_width = 2) const {
    std::ostringstream os;

    auto indent = [&](int level) {
      return std::string(level * indent_width, ' ');
    };

    for (const auto &line : independent_lines) {
      os << line << '\n';
    }

    // functions
    for (const auto &fn : functions) {
      if (!independent_lines.empty() || &fn != &functions.front()) {
        os << '\n';
      }
      os << fn.header_line << '\n';

      for (const auto &bb : fn.blocks) {
        os << indent(1) << bb.label_line << '\n';
        for (const auto &stmt : bb.stmt_lines) {
          os << indent(2) << stmt << '\n';
        }
      }
      os << "}\n";
    }

    return os.str();
  }
};

} // namespace codegen