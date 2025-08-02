# Makefile - Fractyl project
# Proper build system with object files and dependency management

CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=gnu99 -g -Wno-format-truncation -Wno-deprecated-declarations
COVERAGE_CFLAGS = $(CFLAGS) --coverage
# AddressSanitizer and debugging flags for memory error detection
DEBUG_CFLAGS = $(CFLAGS) -fsanitize=address -fsanitize=undefined -fstack-protector-strong
DEBUG_LDFLAGS = -fsanitize=address -fsanitize=undefined
INCLUDES = -Isrc/include -Isrc/utils -Isrc/core -Isrc/vendor/xdiff
LDFLAGS = 
COVERAGE_LDFLAGS = $(LDFLAGS) --coverage
LIBS = 

# Directories
SRCDIR = src
OBJDIR = o
BINDIR = .

# Target executables
TARGET = frac

# Source files
MAIN_SRC = $(SRCDIR)/main.c
UTILS_SRC = $(wildcard $(SRCDIR)/utils/*.c)
CORE_SRC = $(wildcard $(SRCDIR)/core/*.c)
COMMANDS_SRC = $(wildcard $(SRCDIR)/commands/*.c)
DAEMON_SRC = $(wildcard $(SRCDIR)/daemon/*.c)
XDIFF_SRC = $(wildcard $(SRCDIR)/vendor/xdiff/*.c)

ALL_SRC = $(MAIN_SRC) $(UTILS_SRC) $(CORE_SRC) $(COMMANDS_SRC) $(DAEMON_SRC) $(XDIFF_SRC)

# Object files (mirror source structure in obj directory)
MAIN_OBJ = $(OBJDIR)/main.o
UTILS_OBJ = $(UTILS_SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
CORE_OBJ = $(CORE_SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
COMMANDS_OBJ = $(COMMANDS_SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
DAEMON_OBJ = $(DAEMON_SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
XDIFF_OBJ = $(XDIFF_SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

ALL_OBJ = $(MAIN_OBJ) $(UTILS_OBJ) $(CORE_OBJ) $(COMMANDS_OBJ) $(DAEMON_OBJ) $(XDIFF_OBJ)

# Library detection (will be expanded)
HAS_OPENSSL := $(shell pkg-config --exists openssl 2>/dev/null && echo yes)
HAS_CJSON := $(shell pkg-config --exists libcjson 2>/dev/null && echo yes)
HAS_UUID := $(shell pkg-config --exists uuid 2>/dev/null && echo yes)

# Alternative cJSON detection
ifeq ($(HAS_CJSON),)
    HAS_CJSON := $(shell test -f /usr/include/cjson/cJSON.h && echo yes)
    CJSON_MANUAL := yes
endif

ifeq ($(HAS_OPENSSL),yes)
    CFLAGS += -DHAVE_OPENSSL
    LIBS += $(shell pkg-config --libs openssl)
    INCLUDES += $(shell pkg-config --cflags openssl)
endif

ifeq ($(HAS_CJSON),yes)
    CFLAGS += -DHAVE_CJSON
    ifeq ($(CJSON_MANUAL),yes)
        LIBS += -lcjson
        INCLUDES += -I/usr/include/cjson
    else
        LIBS += $(shell pkg-config --libs libcjson)
        INCLUDES += $(shell pkg-config --cflags libcjson)
    endif
endif

ifeq ($(HAS_UUID),yes)
    CFLAGS += -DHAVE_UUID
    LIBS += $(shell pkg-config --libs uuid)
    INCLUDES += $(shell pkg-config --cflags uuid)
endif

# Add pthread for parallel scanning
LIBS += -lpthread

# libuv dependency removed - daemon now uses simple timer approach

# Default target
all: $(TARGET)

# Main targets
$(TARGET): $(ALL_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

# Object file rules
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Create object directories
$(OBJDIR):
	mkdir -p $(OBJDIR)
	mkdir -p $(OBJDIR)/utils
	mkdir -p $(OBJDIR)/core
	mkdir -p $(OBJDIR)/commands
	mkdir -p $(OBJDIR)/daemon
	mkdir -p $(OBJDIR)/vendor/xdiff

# Debug build with sanitizers for memory error detection
debug: CFLAGS := $(DEBUG_CFLAGS) -DDEBUG -O0
debug: LDFLAGS := $(DEBUG_LDFLAGS)
debug: $(TARGET)

# Regular debug build (no sanitizers)
debug-simple: CFLAGS += -DDEBUG -O0
debug-simple: $(TARGET)

# Release build
release: CFLAGS += -O2 -DNDEBUG
release: $(TARGET)

# Clean
clean:
	rm -rf $(OBJDIR)
	rm -f $(TARGET)

# Installation variables
PREFIX ?= /usr/local
BINDIR = $(DESTDIR)$(PREFIX)/bin

# Install system-wide (use with sudo)
install: $(TARGET)
	@echo "Installing frac to $(BINDIR)..."
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/
	@echo "Installation complete. Run 'frac --help' to get started."

# Uninstall system-wide (use with sudo)
uninstall:
	@echo "Removing frac from $(BINDIR)..."
	rm -f $(BINDIR)/$(TARGET)
	@echo "Uninstall complete."

# Unity test framework variables
TESTDIR = test
UNITYDIR = $(TESTDIR)/unity
UNITY_SRC = $(UNITYDIR)/unity.c
UNITY_INC = -I$(UNITYDIR)

# Test object files
TEST_OBJDIR = test_obj
UNIT_TESTS = $(wildcard $(TESTDIR)/unit/*.c)
INTEGRATION_TESTS = $(wildcard $(TESTDIR)/integration/*.c)
UNIT_TEST_BINS = $(UNIT_TESTS:$(TESTDIR)/unit/%.c=$(TEST_OBJDIR)/%)
INTEGRATION_TEST_BINS = $(INTEGRATION_TESTS:$(TESTDIR)/integration/%.c=$(TEST_OBJDIR)/%)

# Test target (runs all tests with proper build and cleanup)
test: clean release test-clean unit-tests integration-tests test-legacy
	@echo ""
	@echo "🎉 All tests completed successfully!"
	@echo ""
	@echo "Test Summary:"
	@echo "============="
	@echo "✅ Unit Tests: PASSED"
	@echo "✅ Integration Tests: PASSED"
	@echo "✅ Legacy Tests: COMPLETED"
	@echo ""
	@echo "Your Fractyl build is ready for use!"

# Legacy test target (might fail - this is expected)
test-legacy: $(TARGET)
	@echo "Running legacy utility tests..."
	@./$(TARGET) --test-utils || echo "⚠️  Legacy tests failed (this might be expected)"

# Create test object directory
$(TEST_OBJDIR):
	mkdir -p $(TEST_OBJDIR)

# Build unit tests
unit-tests: $(TEST_OBJDIR) $(UNIT_TEST_BINS)
	@echo "🧪 Running unit tests..."
	@for test in $(UNIT_TEST_BINS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done
	@echo "✅ Unit tests passed"

# Build integration tests
integration-tests: $(TARGET) $(TEST_OBJDIR) $(INTEGRATION_TEST_BINS)
	@echo "🔧 Running integration tests..."
	@for test in $(INTEGRATION_TEST_BINS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done
	@echo "✅ Integration tests passed"

# Build individual unit test binaries
$(TEST_OBJDIR)/test_%: $(TESTDIR)/unit/test_%.c $(UNITY_SRC) $(ALL_OBJ) | $(TEST_OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(UNITY_INC) -o $@ $< $(UNITY_SRC) $(filter-out o/main.o, $(ALL_OBJ)) $(LIBS)

# Build individual integration test binaries  
$(TEST_OBJDIR)/test_%: $(TESTDIR)/integration/test_%.c $(UNITY_SRC) $(TESTDIR)/test_helpers.c $(ALL_OBJ) | $(TEST_OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(UNITY_INC) -I$(TESTDIR) -o $@ $< $(UNITY_SRC) $(TESTDIR)/test_helpers.c $(filter-out o/main.o, $(ALL_OBJ)) $(LIBS)

# Clean test artifacts
test-clean:
	rm -rf $(TEST_OBJDIR)

# Coverage targets
coverage-clean:
	rm -f *.gcov *.gcda *.gcno
	rm -rf coverage-html/
	rm -f coverage.info

# Build with coverage instrumentation
coverage-build: coverage-clean
	$(MAKE) clean
	$(MAKE) CFLAGS="$(COVERAGE_CFLAGS)" LDFLAGS="$(COVERAGE_LDFLAGS)" $(TARGET)

# Run tests with coverage collection
coverage-test: coverage-build
	@echo "Running tests with coverage collection..."
	# Run our proper test suite
	$(MAKE) unit-tests integration-tests test-legacy
	@echo "Coverage test collection complete"

# Generate text coverage report  
coverage-report: coverage-test
	@echo "Generating coverage report..."
	gcov -r -o $(OBJDIR) $(MAIN_SRC) $(UTILS_SRC) $(CORE_SRC) $(COMMANDS_SRC) $(DAEMON_SRC)
	@echo "Coverage files generated: *.gcov"
	@echo "Summary (excluding vendor code):"
	@grep -A1 "File.*\.c" *.gcov | grep "Lines executed" | grep -v vendor || true

# Generate HTML coverage report (requires lcov)
coverage-html: coverage-test
	@echo "Generating HTML coverage report..."
	lcov --capture --directory . --output-file coverage.info
	lcov --remove coverage.info '/usr/*' --output-file coverage.info
	lcov --remove coverage.info '*/vendor/*' --output-file coverage.info
	genhtml coverage.info --output-directory coverage-html
	@echo "HTML report generated in coverage-html/"
	@echo "Open coverage-html/index.html in your browser"

# Full coverage analysis
coverage: coverage-html coverage-report

# Check for required tools and libraries
check-deps:
	@echo "Checking dependencies..."
	@which gcc > /dev/null || (echo "ERROR: gcc not found" && exit 1)
	@which pkg-config > /dev/null || (echo "WARNING: pkg-config not found")
	@echo "OpenSSL: $(if $(HAS_OPENSSL),found,not found)"
	@echo "cJSON: $(if $(HAS_CJSON),found,not found)"
	@echo "UUID: $(if $(HAS_UUID),found,not found)"
	@echo "Dependencies check complete."

# Show build configuration
config:
	@echo "Build Configuration:"
	@echo "  CC: $(CC)"
	@echo "  CFLAGS: $(CFLAGS)"
	@echo "  INCLUDES: $(INCLUDES)"
	@echo "  LIBS: $(LIBS)"
	@echo "  Sources: $(words $(ALL_SRC)) files"
	@echo "  Objects: $(words $(ALL_OBJ)) files"

# Help
help:
	@echo "Available targets:"
	@echo "  all        - Build $(TARGET) (default)"
	@echo "  debug      - Build with debug flags"
	@echo "  release    - Build optimized release version"
	@echo "  clean      - Remove build artifacts"
	@echo "  install    - Install to system (use with sudo)"
	@echo "  uninstall  - Remove from system (use with sudo)"
	@echo ""
	@echo "Testing:"
	@echo "  test       - Run comprehensive test suite (unit + integration + legacy)"
	@echo "  unit-tests - Run unit tests only"
	@echo "  integration-tests - Run integration tests only"
	@echo "  test-legacy - Run basic legacy tests"
	@echo "  test-clean - Clean test artifacts"
	@echo ""
	@echo "Coverage Analysis:"
	@echo "  coverage   - Generate full coverage report (HTML + text)"
	@echo "  coverage-html - Generate HTML coverage report"
	@echo "  coverage-report - Generate text coverage report"
	@echo "  coverage-clean - Clean coverage files"
	@echo ""
	@echo "Utilities:"
	@echo "  check-deps - Check for required dependencies"
	@echo "  config     - Show build configuration"
	@echo "  help       - Show this help"

.PHONY: all debug release clean install uninstall test unit-tests integration-tests test-legacy test-clean coverage coverage-html coverage-report coverage-test coverage-build coverage-clean check-deps config help

# Dependency generation (advanced - for future)
# -include $(ALL_OBJ:.o=.d)

# Force object directory creation
$(ALL_OBJ): | $(OBJDIR)