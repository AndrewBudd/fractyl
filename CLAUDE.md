# Fractyl Development Guide for Claude

## 🚨 **CRITICAL: USE FRACTYL FOR ALL DEVELOPMENT WORK**

**YOU MUST use Fractyl snapshots as part of your development process. This is not optional.**

### **Development Workflow Requirements:**

1. **📸 Create snapshots FREQUENTLY** - after every significant change
2. **🔄 Use the daemon** - `frac daemon start` for automatic snapshots every 3 minutes
3. **📝 Document progress** with meaningful snapshot messages
4. **🔙 Roll back if stuck** - use `frac restore <snapshot-id>` if you break something
5. **📊 Track your work** - use `frac list` to see development history

### **Essential Commands for Development:**

```bash
# Start daemon for automatic snapshots (ALWAYS DO THIS FIRST)
frac daemon start

# Check daemon status
frac daemon status

# Manual snapshot with descriptive message
frac snapshot -m "Fixed parsing bug in CLI module"

# List recent work
frac list

# Go back if you break something
frac restore <snapshot-id>

# Compare changes between versions
frac diff <old-snapshot> <new-snapshot>

# Stop daemon when done
frac daemon stop
```

### **When to Create Manual Snapshots:**
- ✅ Before starting a major feature
- ✅ After fixing a bug
- ✅ Before refactoring code
- ✅ When tests pass
- ✅ Before attempting risky changes
- ✅ When you reach a good working state

### **Snapshot Message Guidelines:**
- 🎯 **Be specific**: "Fix memory leak in object_store_file()" not "Fix bug"
- 🎯 **Include context**: "Add file locking to prevent daemon conflicts"
- 🎯 **Note failures**: "WIP: Daemon not creating snapshots - need to debug"
- 🎯 **Mark milestones**: "Complete daemon implementation - all tests pass"

---

## 📋 **Project Overview**

**Fractyl** is a content-addressable version control system optimized for large files and non-git workflows. It creates efficient snapshots of directory states using SHA-256 hashing and deduplication.

### **Key Features:**
- 📸 **Snapshot-based versioning** - capture directory state at any point
- 🔧 **Branch-aware storage** - separate snapshots per git branch  
- 🤖 **Background daemon** - automatic snapshots every 3 minutes
- 🔒 **File locking** - prevents conflicts between daemon and manual operations
- 📊 **Content deduplication** - only stores changed files
- 🚀 **Fast operations** - optimized for large repositories

---

## 🏗️ **Project Structure**

```
fractyl/
├── src/
│   ├── main.c              # Entry point and CLI routing
│   ├── commands/           # All CLI commands
│   │   ├── snapshot.c      # Create snapshots
│   │   ├── restore.c       # Restore to snapshot  
│   │   ├── list.c          # List snapshots
│   │   ├── diff.c          # Compare snapshots
│   │   ├── delete.c        # Delete snapshots
│   │   ├── daemon.c        # Daemon management
│   │   └── init.c          # Initialize repository
│   ├── core/               # Core functionality
│   │   ├── objects.c       # Object storage system
│   │   ├── index.c         # File indexing
│   │   └── hash.c          # SHA-256 hashing
│   ├── daemon/             # Background daemon
│   │   └── daemon_standalone.c # Standalone daemon implementation
│   ├── utils/              # Utility functions
│   │   ├── lock.c          # File locking system
│   │   ├── paths.c         # Path management
│   │   ├── git.c           # Git integration
│   │   ├── json.c          # JSON serialization
│   │   └── fs.c            # File system utilities
│   └── include/            # Header files
├── .fractyl/               # Fractyl repository data
│   ├── objects/            # Content-addressable object storage
│   ├── snapshots/          # Snapshot metadata (per git branch)
│   ├── branches/           # Branch-specific data
│   ├── daemon.pid          # Daemon process ID
│   ├── daemon.log          # Daemon activity log
│   └── fractyl.lock        # Concurrency control lock
├── Makefile               # Build configuration
└── CLAUDE.md              # This file (development guide)
```

---

## 🔧 **Build System**

### **Dependencies:**
- `gcc` with C99 support
- `libssl-dev` (OpenSSL for SHA-256)
- `libcjson-dev` (JSON handling)
- `uuid-dev` (UUID generation)

### **Build Commands:**
```bash
# Check dependencies
make check-deps

# Clean build
make clean && make

# Debug build
make debug

# Release build  
make release

# Install system-wide
sudo make install
```

### **Development Build Workflow:**
```bash
# 1. Start daemon for automatic snapshots
frac daemon start

# 2. Make code changes
# ... edit files ...

# 3. Test build
make clean && make

# 4. Create snapshot if build succeeds
frac snapshot -m "Fixed compilation error in daemon.c"

# 5. Test functionality
./frac daemon status

# 6. Create snapshot for working state
frac snapshot -m "All tests pass - daemon working correctly"
```

---

## 🤖 **Daemon System**

The daemon is the key feature that makes Fractyl powerful for development workflows:

### **Daemon Features:**
- 🔄 **Automatic snapshots** every 3 minutes (configurable)
- 🔒 **File locking** prevents conflicts with manual operations  
- 📝 **Detailed logging** in `.fractyl/daemon.log`
- 🎯 **Change detection** only creates snapshots when files change
- 🛡️ **Robust error handling** graceful shutdown and recovery

### **Daemon Management:**
```bash
# Start with default 3-minute interval
frac daemon start

# Start with custom interval (60 seconds)
frac daemon start -i 60

# Check if running
frac daemon status

# Stop daemon
frac daemon stop

# Restart with new settings
frac daemon restart -i 120
```

### **Reading Daemon Logs:**
```bash
# View daemon activity
cat .fractyl/daemon.log

# Monitor in real-time
tail -f .fractyl/daemon.log
```

---

## 🔐 **Concurrency & File Locking**

Fractyl uses a sophisticated file locking system to handle concurrent access:

### **How It Works:**
- 🔒 Creates `.fractyl/fractyl.lock` during snapshot operations
- ⏱️ Manual commands wait up to 30 seconds for locks
- 🤖 Daemon uses non-blocking locks (skips if busy)
- 🧹 Automatic cleanup of stale locks from dead processes

### **Lock Behavior:**
- **Manual snapshot during daemon**: Manual command waits for daemon to finish
- **Daemon cycle during manual**: Daemon skips cycle, continues on next interval
- **Multiple manual commands**: Second command waits for first to complete

---

## 📸 **Snapshot System**

### **Snapshot Storage:**
- 📁 **Branch-aware**: Snapshots stored per git branch in `.fractyl/snapshots/<branch>/`
- 🔗 **Content-addressed**: Files stored by SHA-256 hash in `.fractyl/objects/`
- 📊 **Deduplication**: Identical files shared across snapshots
- 📝 **Metadata**: JSON files with timestamp, description, git info

### **Snapshot Commands:**
```bash
# Create snapshot with message
frac snapshot -m "Implement new feature"

# List all snapshots
frac list

# Restore to specific snapshot
frac restore a1b2c3d4

# Compare two snapshots
frac diff a1b2c3d4 e5f6g7h8

# Delete old snapshot
frac delete a1b2c3d4
```

### **Snapshot Naming:**
- 🎯 **Auto-generated**: "Auto-snapshot 2025-07-29 22:40:13" (daemon)
- 📝 **Manual descriptions**: "Fix memory leak in hash.c" (manual)
- 🔄 **Incremental**: "working +1", "working +2" (auto-increment)
- 🌿 **Branch-aware**: Separate numbering per git branch

---

## 🔧 **Development Guidelines**

### **Code Style:**
- 📏 **C99 standard** with GNU extensions
- 🚫 **No C++ style comments** (use `/* */`)
- 📝 **Descriptive function names**
- 🔒 **Error handling** for all system calls
- 📊 **Consistent indentation** 

### **Memory Management:**
- ✅ **Free all allocations** - use valgrind to check
- 🔒 **Check all malloc/calloc** returns
- 📝 **Use strdup() consistently** for string duplication
- 🧹 **Cleanup on error paths**

### **File Operations:**
- 🔒 **Always check file operations** (fopen, fread, fwrite)
- 📁 **Use absolute paths** where possible  
- 🧹 **Close file descriptors** promptly
- 🔐 **Handle permissions** correctly

### **Testing Strategy:**
- 🧪 **Test each feature** before committing snapshots
- 🔄 **Test edge cases** (empty files, large files, permissions)
- 🤖 **Test daemon thoroughly** (start/stop/restart/concurrency)
- 🔒 **Test file locking** with concurrent operations

---

## 🐛 **Debugging & Troubleshooting**

### **Common Issues:**

1. **Build Fails:**
   ```bash
   # Check dependencies
   make check-deps
   
   # Clean rebuild
   make clean && make
   
   # Create snapshot before attempting fixes
   frac snapshot -m "WIP: Build failing, investigating"
   ```

2. **Daemon Not Working:**
   ```bash
   # Check if running
   frac daemon status
   
   # Check logs
   cat .fractyl/daemon.log
   
   # Restart daemon
   frac daemon restart
   ```

3. **Lock Conflicts:**
   ```bash
   # Check for stale locks
   ls -la .fractyl/fractyl.lock
   
   # Force remove if stale (daemon PID not running)
   rm .fractyl/fractyl.lock
   ```

4. **Snapshots Not Created:**
   ```bash
   # Check if in fractyl repository
   ls .fractyl/
   
   # Initialize if needed
   frac init
   
   # Check for changes
   frac snapshot -m "Test snapshot"
   ```

### **Debug Build:**
```bash
make debug
gdb ./frac
(gdb) run snapshot -m "Debug test"
```

---

## 🎯 **Key Architecture Decisions**

### **Design Principles:**
1. 🎯 **Content-addressable storage** - SHA-256 based deduplication
2. 🌿 **Branch-aware organization** - separate snapshots per git branch  
3. 🔒 **Concurrency safety** - file locking prevents corruption
4. 🤖 **Automated workflows** - daemon for hands-free operation
5. 📊 **Efficient storage** - only store changed files

### **Why These Choices:**
- **SHA-256 hashing**: Reliable content identification and deduplication
- **Branch separation**: Prevents snapshot pollution across git branches
- **File locking**: Essential for daemon + manual command safety
- **JSON metadata**: Human-readable, extensible snapshot information
- **C implementation**: Performance and system integration

---

## 🧪 **Testing Requirements**

**⚠️ CRITICAL: ALWAYS RUN TESTS AFTER ANY CODE CHANGES**

### **Testing Workflow - MANDATORY:**
1. **🔨 Build the project** - `make clean && make`
2. **🧪 Run all tests** - Use test runner scripts
3. **📸 Create snapshot** - Only after tests pass
4. **🔄 Fix any failures** - Never commit broken tests

### **Test Suite Structure:**
```
test/
├── unity/                 # Unity testing framework
├── test_helpers.h/.c      # Test utility functions
├── unit/                  # Unit tests for individual components
│   ├── test_core.c        # Core functionality (hashing, objects, index)
│   └── test_utils.c       # Utility functions
├── integration/           # Integration tests for complete workflows
│   ├── test_fractyl_integration.c    # Main integration test suite
│   ├── test_restore_behavior.c       # Restore functionality tests
│   └── test_simple_validation.c     # Basic validation tests
└── run_tests.sh          # Test runner script
```

### **Essential Test Commands:**

```bash
# Build all tests
make tests

# Run individual test suites
./test_obj/test_core                    # Core functionality tests
./test_obj/test_utils                   # Utility function tests  
./test_obj/test_fractyl_integration     # Main integration tests
./test_obj/test_restore_behavior        # Restore behavior tests
./test_obj/test_simple_validation       # Basic validation tests

# Run all tests with script
./run_tests.sh

# Run basic tests only
./run_basic_tests.sh
```

### **Test Categories:**

1. **🔧 Unit Tests** - Test individual functions and components
   - Hash functions and consistency
   - Object storage and retrieval
   - Index operations
   - File system utilities
   - CLI argument parsing

2. **🔄 Integration Tests** - Test complete workflows
   - Repository initialization
   - Snapshot creation and listing
   - File modification tracking
   - Snapshot restoration (including cleanup of extra files)
   - Complex file operations with directories
   - Edge cases (empty files, permissions)

3. **🛡️ Behavior Tests** - Test specific behaviors
   - Restore removes files not in snapshot
   - Directory cleanup after restore
   - Concurrent operation handling

### **Test Development Guidelines:**

- **🎯 Test-driven approach**: Write tests for new features before implementation
- **📁 Use temporary directories**: All tests use `/tmp/fractyl_test_*` for isolation
- **🧹 Clean isolation**: Each test gets fresh temporary repository
- **🔍 Comprehensive coverage**: Test both success and failure cases
- **📝 Clear assertions**: Use descriptive test names and clear failure messages

### **Test Helper Functions:**

```c
// Repository management
test_repo_t* test_repo_create(const char* name);
int test_repo_enter(test_repo_t* repo);
void test_repo_destroy(test_repo_t* repo);

// File operations
int test_file_create(const char* path, const char* content);
int test_file_modify(const char* path, const char* content);
int test_file_remove(const char* path);
char* test_file_read(const char* path);

// Fractyl operations
int test_fractyl_init(test_repo_t* repo);
int test_fractyl_snapshot(test_repo_t* repo, const char* message);
int test_fractyl_restore(test_repo_t* repo, const char* snapshot_id);
char* test_fractyl_list(test_repo_t* repo);
char* test_fractyl_get_latest_snapshot_id(test_repo_t* repo);

// Assertions
TEST_ASSERT_FRACTYL_SUCCESS(result);
TEST_ASSERT_FILE_EXISTS(path);
TEST_ASSERT_FILE_NOT_EXISTS(path);
TEST_ASSERT_FILE_CONTENT(path, expected_content);
TEST_ASSERT_DIR_EXISTS(path);
```

### **Pre-commit Testing Checklist:**

Before creating any snapshot or committing code, ensure:

- ✅ **Build succeeds**: `make clean && make`
- ✅ **All unit tests pass**: `./test_obj/test_core && ./test_obj/test_utils`
- ✅ **All integration tests pass**: `./test_obj/test_fractyl_integration`
- ✅ **Restore behavior tests pass**: `./test_obj/test_restore_behavior`
- ✅ **Validation tests pass**: `./test_obj/test_simple_validation`
- ✅ **No memory leaks**: Run key tests with valgrind if needed
- ✅ **Clean build warnings**: Address any compiler warnings

### **Test Failure Protocol:**

1. **🚫 Never ignore test failures** - All tests must pass
2. **🔍 Debug immediately** - Use test output to understand failures
3. **📸 Snapshot before fixing** - Create snapshot documenting the failure
4. **🔧 Fix the root cause** - Don't mask symptoms
5. **✅ Verify fix** - Ensure tests pass after changes
6. **📸 Snapshot success** - Create snapshot when tests pass

### **Adding New Tests:**

When implementing new features:

1. **📝 Write tests first** - Define expected behavior
2. **🔧 Implement feature** - Make tests pass
3. **🧪 Test edge cases** - Add tests for error conditions
4. **📊 Verify coverage** - Ensure comprehensive testing

Example test structure:
```c
void test_new_feature(void) {
    test_repo_t* repo = test_repo_create("new_feature_test");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    // Setup
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Test the feature
    // ... test code ...
    
    // Cleanup
    test_repo_destroy(repo);
}
```

---

## 🚀 **Claude Development Workflow**

**Remember: ALWAYS use Fractyl as part of your development process!**

### **Starting Work:**
```bash
# 1. Start daemon
frac daemon start

# 2. Create baseline snapshot
frac snapshot -m "Starting work on [feature/bug/task]"

# 3. Begin development...
```

### **During Development:**
```bash
# After each meaningful change - BUT ONLY AFTER TESTS PASS
make clean && make && ./run_basic_tests.sh
frac snapshot -m "Specific description of what changed"

# Before attempting risky changes
frac snapshot -m "Safe state before refactoring X"

# MANDATORY: When all tests pass
make clean && make && ./run_tests.sh
frac snapshot -m "All tests pass - [feature] working"

# If tests fail - NEVER snapshot broken code
# Fix first, then snapshot
```

### **If Something Breaks:**
```bash
# See recent snapshots
frac list | head -10

# Go back to working state
frac restore <last-good-snapshot-id>

# Create snapshot documenting issue
frac snapshot -m "Restored from broken state - issue was X"
```

### **Finishing Work:**
```bash
# MANDATORY: Run full test suite before final snapshot
make clean && make && ./run_tests.sh

# Final working snapshot - ONLY after all tests pass
frac snapshot -m "Complete [feature/task] - all tests pass - ready for review"

# Stop daemon if desired
frac daemon stop
```

---

**💡 Remember: Fractyl snapshots are your safety net. Use them liberally and descriptively!**