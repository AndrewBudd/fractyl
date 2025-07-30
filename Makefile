# Makefile - Fractyl project
# Proper build system with object files and dependency management

CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=gnu99 -g -Wno-format-truncation
COVERAGE_CFLAGS = $(CFLAGS) --coverage
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

# Debug build
debug: CFLAGS += -DDEBUG -O0
debug: $(TARGET)

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

# Test target (runs all tests)
test: unit-tests integration-tests
	@echo "All tests completed!"

# Legacy test target
test-legacy: $(TARGET)
	./$(TARGET) --test-utils

# Create test object directory
$(TEST_OBJDIR):
	mkdir -p $(TEST_OBJDIR)

# Build unit tests
unit-tests: $(TEST_OBJDIR) $(UNIT_TEST_BINS)
	@echo "Running unit tests..."
	@for test in $(UNIT_TEST_BINS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done

# Build integration tests
integration-tests: $(TEST_OBJDIR) $(INTEGRATION_TEST_BINS)
	@echo "Running integration tests..."
	@for test in $(INTEGRATION_TEST_BINS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done

# Build individual unit test binaries
$(TEST_OBJDIR)/test_%: $(TESTDIR)/unit/test_%.c $(UNITY_SRC) $(ALL_OBJ) | $(TEST_OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(UNITY_INC) -o $@ $< $(UNITY_SRC) $(filter-out o/main.o, $(ALL_OBJ)) $(LIBS)

# Build individual integration test binaries  
$(TEST_OBJDIR)/test_%: $(TESTDIR)/integration/test_%.c $(UNITY_SRC) $(ALL_OBJ) | $(TEST_OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(UNITY_INC) -o $@ $< $(UNITY_SRC) $(filter-out o/main.o, $(ALL_OBJ)) $(LIBS)

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
	@echo "Running comprehensive tests with coverage collection..."
	@echo "=== Built-in unit tests ==="
	./$(TARGET) --test-utils
	@echo "=== Basic command tests ==="
	./$(TARGET) --help > /dev/null || true
	./$(TARGET) --version > /dev/null || true
	@echo "=== Error handling tests ==="
	./$(TARGET) nonexistent-command || true
	./$(TARGET) snapshot || true
	./$(TARGET) restore || true
	./$(TARGET) delete || true
	./$(TARGET) diff || true
	./$(TARGET) daemon || true
	./$(TARGET) daemon nonexistent || true
	@echo "=== CLI edge cases ==="
	./$(TARGET) --debug --help || true
	./$(TARGET) --debug --version || true
	./$(TARGET) --debug nonexistent || true
	cd /tmp && rm -rf cli-test && mkdir cli-test && cd cli-test && \
		echo "test" > file.txt && \
		/home/budda/Code/fractyl/$(TARGET) --debug init && \
		/home/budda/Code/fractyl/$(TARGET) --debug snapshot -m "Debug test" && \
		/home/budda/Code/fractyl/$(TARGET) --debug list
	@echo "=== Comprehensive functional tests ==="
	cd /tmp && rm -rf coverage-test && mkdir coverage-test && cd coverage-test && \
		echo "=== Test 1: Repository lifecycle ===" && \
		/home/budda/Code/fractyl/$(TARGET) init && \
		echo "initial content" > file1.txt && \
		echo "second file" > file2.txt && \
		mkdir subdir && echo "nested content" > subdir/nested.txt && \
		/home/budda/Code/fractyl/$(TARGET) snapshot -m "Initial snapshot" && \
		SNAP1=$$(/home/budda/Code/fractyl/$(TARGET) list | head -1 | cut -d' ' -f1) && \
		echo "=== Test 2: File modifications ===" && \
		echo "modified content" > file1.txt && \
		echo "new file" > file3.txt && \
		rm file2.txt && \
		/home/budda/Code/fractyl/$(TARGET) snapshot -m "Modified files" && \
		SNAP2=$$(/home/budda/Code/fractyl/$(TARGET) list | head -1 | cut -d' ' -f1) && \
		echo "=== Test 3: List and restore ===" && \
		/home/budda/Code/fractyl/$(TARGET) list && \
		/home/budda/Code/fractyl/$(TARGET) restore $$SNAP1 && \
		echo "=== Test 4: Diff operations ===" && \
		/home/budda/Code/fractyl/$(TARGET) diff $$SNAP1 $$SNAP2 || true && \
		echo "=== Test 5: Daemon operations ===" && \
		/home/budda/Code/fractyl/$(TARGET) daemon start && \
		sleep 3 && \
		echo "daemon change" > daemon_test.txt && \
		sleep 3 && \
		/home/budda/Code/fractyl/$(TARGET) daemon status && \
		/home/budda/Code/fractyl/$(TARGET) daemon restart && \
		sleep 2 && \
		/home/budda/Code/fractyl/$(TARGET) daemon stop && \
		echo "=== Test 6: Delete operations ===" && \
		/home/budda/Code/fractyl/$(TARGET) list && \
		LAST_SNAP=$$(/home/budda/Code/fractyl/$(TARGET) list | head -1 | cut -d' ' -f1) && \
		/home/budda/Code/fractyl/$(TARGET) delete $$LAST_SNAP || true && \
		echo "=== Test 7: Edge cases ===" && \
		touch empty_file.txt && \
		echo "large content" > large_file.txt && \
		for i in $$(seq 1 100); do echo "Line $$$$i with some content to make it longer" >> large_file.txt; done && \
		/home/budda/Code/fractyl/$(TARGET) snapshot -m "Edge case files" && \
		echo "=== Test 8: Error conditions ===" && \
		/home/budda/Code/fractyl/$(TARGET) restore nonexistent-snap || true && \
		/home/budda/Code/fractyl/$(TARGET) delete nonexistent-snap || true && \
		/home/budda/Code/fractyl/$(TARGET) diff nonexistent1 nonexistent2 || true && \
		echo "=== Test 9: Extended functionality ===" && \
		/home/budda/Code/fractyl/$(TARGET) restore -1 && \
		/home/budda/Code/fractyl/$(TARGET) restore -2 || true && \
		FIRST_SNAP=$$(/home/budda/Code/fractyl/$(TARGET) list | tail -1 | cut -d' ' -f1) && \
		LAST_SNAP=$$(/home/budda/Code/fractyl/$(TARGET) list | head -1 | cut -d' ' -f1) && \
		/home/budda/Code/fractyl/$(TARGET) diff $$FIRST_SNAP $$LAST_SNAP && \
		echo "=== Test 10: Advanced daemon tests ===" && \
		/home/budda/Code/fractyl/$(TARGET) daemon start -i 5 && \
		sleep 8 && \
		echo "change during daemon" > daemon_change.txt && \
		sleep 8 && \
		/home/budda/Code/fractyl/$(TARGET) daemon stop && \
		echo "=== Test 11: File permission tests ===" && \
		chmod 644 large_file.txt && \
		/home/budda/Code/fractyl/$(TARGET) snapshot -m "Permission test" && \
		echo "=== Test 12: Empty and special files ===" && \
		touch .hidden_file && \
		echo "" > empty_content.txt && \
		printf "binary\\x00content" > binary_test.bin && \
		/home/budda/Code/fractyl/$(TARGET) snapshot -m "Special files" && \
		echo "=== Test 13: Repository without git ===" && \
		cd /tmp && rm -rf coverage-nogit && mkdir coverage-nogit && cd coverage-nogit && \
		/home/budda/Code/fractyl/$(TARGET) init && \
		echo "no git here" > file.txt && \
		/home/budda/Code/fractyl/$(TARGET) snapshot -m "No git snapshot" && \
		/home/budda/Code/fractyl/$(TARGET) list
	@echo "=== Git integration tests ==="
	cd /tmp && rm -rf coverage-git-test && mkdir coverage-git-test && cd coverage-git-test && \
		git init && \
		git config user.email "test@example.com" && \
		git config user.name "Test User" && \
		/home/budda/Code/fractyl/$(TARGET) init && \
		echo "git tracked file" > git_file.txt && \
		git add git_file.txt && \
		git commit -m "Initial commit" && \
		/home/budda/Code/fractyl/$(TARGET) snapshot -m "With git context" && \
		git checkout -b feature-branch && \
		echo "branch content" > branch_file.txt && \
		/home/budda/Code/fractyl/$(TARGET) snapshot -m "Branch snapshot" && \
		git checkout master && \
		/home/budda/Code/fractyl/$(TARGET) list

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
	@echo "  test       - Run all Unity tests (unit + integration)"
	@echo "  unit-tests - Run unit tests only"
	@echo "  integration-tests - Run integration tests only"
	@echo "  test-legacy - Run basic legacy tests"
	@echo "  test-clean - Clean test artifacts"
	@echo "  coverage   - Generate full coverage report (HTML + text)"
	@echo "  coverage-html - Generate HTML coverage report"
	@echo "  coverage-report - Generate text coverage report"
	@echo "  coverage-clean - Clean coverage files"
	@echo "  check-deps - Check for required dependencies"
	@echo "  config     - Show build configuration"
	@echo "  help       - Show this help"

.PHONY: all debug release clean install uninstall test unit-tests integration-tests test-legacy test-clean coverage coverage-html coverage-report coverage-test coverage-build coverage-clean check-deps config help

# Dependency generation (advanced - for future)
# -include $(ALL_OBJ:.o=.d)

# Force object directory creation
$(ALL_OBJ): | $(OBJDIR)