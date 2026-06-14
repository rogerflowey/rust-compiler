BUILD_DIR ?= build

.PHONY: all build run clean

all: build

build:
	@cmake -S . -B $(BUILD_DIR) -DBUILD_TESTING=OFF >/tmp/rcomp-cmake.log 2>&1 || { cat /tmp/rcomp-cmake.log >&2; exit 1; }
	@cmake --build $(BUILD_DIR) --target submission_pipeline >/tmp/rcomp-build.log 2>&1 || { cat /tmp/rcomp-build.log >&2; exit 1; }

run: build
	@ulimit -s 65536 2>/dev/null || true; $(BUILD_DIR)/cmd/submission_pipeline

clean:
	@cmake --build $(BUILD_DIR) --target clean >/dev/null
