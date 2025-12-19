.PHONY: build run clean

# Build directory
BUILD_DIR ?= build/ninja-debug
BINARY := $(BUILD_DIR)/cmd/ir_pipeline

build:
	@echo "Building compiler..."
	cmake --preset ninja-debug
	cmake --build $(BUILD_DIR)
	@echo "Build complete. Binary: $(BINARY)"

run: build
	@echo "Running compiler from STDIN..."
	@echo "Enter source code (use Ctrl+D to end):"
	@$(BINARY) - /dev/stdout

clean:
	@echo "Cleaning build directory..."
	rm -rf $(BUILD_DIR)
	@echo "Clean complete"

help:
	@echo "Available targets:"
	@echo "  make build - Build the compiler (requires CMake)"
	@echo "  make run   - Run compiler from STDIN (reads source code, outputs IR to STDOUT and builtin.c to STDERR)"
	@echo "  make clean - Remove build artifacts"
	@echo "  make help  - Show this help message"
