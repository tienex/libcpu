# Convenience wrapper around the CMake build for the LibCPU COM core.
#
#   make            configure (if needed) and build everything
#   make check      build the drivers + CFG backends and run the backend matrix
#   make clean      remove build products (keep the configured tree)
#   make distclean  remove the build tree entirely

BUILD := build
NCPU  := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

all:
	@cmake --version >/dev/null 2>&1 || (echo "Please install cmake before running make." ; exit 1)
	@if [ ! -d $(BUILD) ] || [ ! -f $(BUILD)/CMakeCache.txt ]; then \
	    cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release ; \
	fi
	@cmake --build $(BUILD) -j$(NCPU)

check: all
	@cmake --build $(BUILD) --target check -j$(NCPU)

clean:
	@[ -d $(BUILD) ] && cmake --build $(BUILD) --target clean || true

distclean:
	@rm -rf $(BUILD)

.PHONY: all check clean distclean
