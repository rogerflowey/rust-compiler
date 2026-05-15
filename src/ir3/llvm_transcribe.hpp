#pragma once

#include "ir3/ir3.hpp"

#include <ostream>
#include <stdexcept>
#include <string>

namespace ir3 {

class TranscriptionError : public std::runtime_error {
public:
    explicit TranscriptionError(const std::string& message)
        : std::runtime_error(message) {}
};

void transcribe_llvm(std::ostream& out, const Module& module);
std::string to_llvm_string(const Module& module);

} // namespace ir3
