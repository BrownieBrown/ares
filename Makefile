# Ares - Personal Financial Management Application
# Makefile wrapper for CMake build system

.PHONY: build test run clean sanitize format help

# Default target
all: build

# Build the project
build:
	@cmake -B build -DCMAKE_BUILD_TYPE=Debug
	@cmake --build build -j$$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Build release version
release:
	@cmake -B build -DCMAKE_BUILD_TYPE=Release
	@cmake --build build -j$$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Run all tests
test: build
	@cd build && ctest --output-on-failure

# Run the application (pass ARGS for arguments: make run ARGS="import file.csv")
run: build
	@./build/ares $(ARGS)

# Create symlink for easier access
install-local: build
	@ln -sf $(PWD)/build/ares /usr/local/bin/ares 2>/dev/null || ln -sf $(PWD)/build/ares ~/bin/ares 2>/dev/null || echo "Could not create symlink. Run: sudo ln -s $(PWD)/build/ares /usr/local/bin/ares"

# Clean build artifacts
clean:
	@rm -rf build build-san

# Build with address/undefined behavior sanitizers
sanitize:
	@cmake -B build-san -DARES_USE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
	@cmake --build build-san -j$$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Run sanitized version
run-san: sanitize
	@./build-san/ares

# Format source code (requires clang-format)
format:
	@find src tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i 2>/dev/null || echo "clang-format not found"

# Show help
help:
	@echo "Ares Build System"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  build     - Build debug version (default)"
	@echo "  release   - Build release version"
	@echo "  test      - Run all tests"
	@echo "  run       - Run the application"
	@echo "  clean     - Remove build directories"
	@echo "  sanitize  - Build with sanitizers enabled"
	@echo "  run-san   - Run sanitized version"
	@echo "  format    - Format source code with clang-format"
	@echo "  help      - Show this help message"
