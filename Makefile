.PHONY: build run clean help

build:
	@echo "Building compiler..."
	cmake --preset default
	cmake --build build/default
	@echo "Build complete."

run:
	@./build/default/cmd/ir_pipeline - -

clean:
	@echo "Cleaning build directory..."
	@rm -rf build/default
	@echo "Clean complete"

help:
	@echo "Available targets:"
	@echo "  make build - Build the compiler (requires CMake and a C++ compiler)"
	@echo "  make run   - Run compiler from STDIN (reads source code, outputs IR to STDOUT and builtin.c to STDERR)"
	@echo "  make clean - Remove build artifacts"
	@echo "  make help  - Show this help message"
