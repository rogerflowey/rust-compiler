# Makefile for Online Judge integration
#
# Targets:
#   make build  - Configure (via CMake preset) and build the semantic pipeline checker
#   make run    - Read program text from STDIN, run the semantic pipeline, forward exit code

PRESET ?= ninja-debug
BUILD_DIR := build/$(PRESET)
PIPELINE := $(BUILD_DIR)/cmd/semantic_pipeline
CONFIG_STAMP := $(BUILD_DIR)/CMakeCache.txt

.PHONY: all build run clean

all: build

$(CONFIG_STAMP):
	cmake --preset $(PRESET)

$(PIPELINE): $(CONFIG_STAMP)
	cmake --build $(BUILD_DIR) --target semantic_pipeline

build: $(PIPELINE)
	@true

run: $(PIPELINE)
	@tmp=$$(mktemp -t rcompiler.XXXXXX); \
	cat - > $$tmp; \
	"$(PIPELINE)" $$tmp; \
	status=$$?; \
	rm -f $$tmp; \
	exit $$status

clean:
	rm -rf build
