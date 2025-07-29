# Makefile - Fractyl project
# Proper build system with object files and dependency management

CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=gnu99 -g -Wno-format-truncation
INCLUDES = -Isrc/include -Isrc/utils -Isrc/core -Isrc/vendor/xdiff
LDFLAGS = 
LIBS = 

# Directories
SRCDIR = src
OBJDIR = o
BINDIR = .

# Target executable
TARGET = fractyl

# Source files
MAIN_SRC = $(SRCDIR)/main.c
UTILS_SRC = $(wildcard $(SRCDIR)/utils/*.c)
CORE_SRC = $(wildcard $(SRCDIR)/core/*.c)
COMMANDS_SRC = $(wildcard $(SRCDIR)/commands/*.c)
XDIFF_SRC = $(wildcard $(SRCDIR)/vendor/xdiff/*.c)

ALL_SRC = $(MAIN_SRC) $(UTILS_SRC) $(CORE_SRC) $(COMMANDS_SRC) $(XDIFF_SRC)

# Object files (mirror source structure in obj directory)
MAIN_OBJ = $(OBJDIR)/main.o
UTILS_OBJ = $(UTILS_SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
CORE_OBJ = $(CORE_SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
COMMANDS_OBJ = $(COMMANDS_SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
XDIFF_OBJ = $(XDIFF_SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

ALL_OBJ = $(MAIN_OBJ) $(UTILS_OBJ) $(CORE_OBJ) $(COMMANDS_OBJ) $(XDIFF_OBJ)

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

# Default target
all: $(TARGET)

# Main target
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
	@echo "Installing fractyl to $(BINDIR)..."
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/
	@echo "Installation complete. Run 'fractyl --help' to get started."

# Uninstall system-wide (use with sudo)
uninstall:
	@echo "Removing fractyl from $(BINDIR)..."
	rm -f $(BINDIR)/$(TARGET)
	@echo "Uninstall complete."

# Test target (placeholder)
test: $(TARGET)
	./$(TARGET) --test-utils

# Check for required tools and libraries
check-deps:
	@echo "Checking dependencies..."
	@which gcc > /dev/null || (echo "ERROR: gcc not found" && exit 1)
	@which pkg-config > /dev/null || (echo "WARNING: pkg-config not found")
	@echo "OpenSSL: $(if $(HAS_OPENSSL),found,not found)"
	@echo "cJSON: $(if $(HAS_CJSON),found,not found)"
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
	@echo "  test       - Run basic tests"
	@echo "  check-deps - Check for required dependencies"
	@echo "  config     - Show build configuration"
	@echo "  help       - Show this help"

.PHONY: all debug release clean install uninstall test check-deps config help

# Dependency generation (advanced - for future)
# -include $(ALL_OBJ:.o=.d)

# Force object directory creation
$(ALL_OBJ): | $(OBJDIR)