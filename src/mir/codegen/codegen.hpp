#pragma once

#include "mir/mir.hpp"

#include <string>

namespace mir::codegen {

struct CodegenOptions {
    std::string module_id = "rc-module";
    std::string data_layout;
    std::string target_triple;
};

std::string emit_llvm_ir(const MirModule& module, const CodegenOptions& options = {});

} // namespace mir::codegen
