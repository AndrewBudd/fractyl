# Fractyl

A powerful content-addressable version control system optimized for large files and non-git workflows. Fractyl creates efficient snapshots of directory states using SHA-256 hashing and deduplication, with intelligent git branch awareness and automated daemon mode.

## Key Features

- 📸 **Snapshot-based versioning** - capture directory state at any point in time
- 🔧 **Git branch-aware storage** - separate snapshots per git branch automatically  
- 🤖 **Background daemon mode** - automatic snapshots every 3 minutes (configurable)
- 🔒 **File locking system** - prevents conflicts between daemon and manual operations
- 📊 **Content deduplication** - only stores changed files using SHA-256 hashing
- 🚀 **Fast operations** - optimized for large repositories and frequent snapshots
- ⚡ **Smart snapshot naming** - automatic incremental naming with `./frac` shortcut
- 🧪 **Comprehensive testing** - Unity-based test suite with high code coverage

## Quick Start

```bash
# Build and install
make && sudo make install

# Initialize repository (optional - happens automatically)
frac init

# Start daemon for automatic snapshots every 3 minutes
frac daemon start

# Create manual snapshot with message
frac snapshot -m "Added new feature"

# Quick snapshot with auto-incrementing name
./frac  # Creates "working +1", "working +2", etc.

# List all snapshots
frac list

# Restore to a specific snapshot
frac restore a1b2c3d4

# Stop daemon when done
frac daemon stop
```

## Installation

### Dependencies

- **GCC** with C99 support
- **libssl-dev** (OpenSSL for SHA-256 hashing)
- **libcjson-dev** (JSON handling)
- **uuid-dev** (UUID generation)

### Build and Install

```bash
# Check dependencies
make check-deps

# Clean build
make clean && make

# Run tests
make test

# Install system-wide
sudo make install

# Or run locally
./frac --help
```

## Core Commands

### Daemon Management

The daemon is Fractyl's key feature for automated development workflows:

```bash
# Start daemon (default 3-minute interval)
frac daemon start

# Start with custom interval (60 seconds)
frac daemon start -i 60

# Check daemon status
frac daemon status

# Restart daemon with new settings
frac daemon restart -i 120

# Stop daemon
frac daemon stop

# View daemon activity
cat .fractyl/daemon.log
tail -f .fractyl/daemon.log
```

**Daemon Features:**
- 🔄 Creates snapshots only when files actually change
- 🔒 Uses file locking to prevent conflicts with manual commands
- 📝 Detailed logging of all activity
- 🛡️ Robust error handling and automatic recovery
- ⏱️ Configurable snapshot intervals (minimum 10 seconds)

### Snapshot Management

```bash
# Manual snapshot with descriptive message
frac snapshot -m "Fixed memory leak in hash.c"

# Quick auto-named snapshot (creates "working +1", "working +2", etc.)
./frac

# List all snapshots (most recent first)
frac list

# List with more details
frac list --verbose

# Restore to specific snapshot
frac restore a1b2c3d4

# Restore to previous snapshot
frac restore -1

# Restore to 2 snapshots ago
frac restore -2

# Delete old snapshot
frac delete a1b2c3d4
```

### Comparison and Analysis

```bash
# Compare two snapshots
frac diff snapshot1 snapshot2

# Show changes since last snapshot
frac diff HEAD~1 HEAD

# View specific snapshot details
frac show a1b2c3d4
```

## Git Integration

Fractyl is **git branch-aware** and automatically organizes snapshots per branch:

### Branch-Specific Storage

```
.fractyl/
├── refs/heads/master/
│   ├── snapshots/           # Master branch snapshots
│   └── CURRENT             # Current snapshot for master
├── refs/heads/feature/
│   ├── snapshots/           # Feature branch snapshots  
│   └── CURRENT             # Current snapshot for feature
└── objects/                # Shared object storage
```

### Git Status Capture

Each snapshot automatically captures:
- Current git branch name
- Latest commit hash
- Uncommitted changes status
- Working directory state

```bash
# Switch git branches - Fractyl automatically separates snapshots
git checkout feature-branch
frac snapshot -m "Feature implementation"

git checkout master  
frac list  # Shows only master branch snapshots
```

## Directory Structure

```
.fractyl/
├── objects/                          # Content-addressable object storage
│   └── <first-2-chars>/
│       └── <remaining-hash>          # File blobs by SHA-256
├── refs/heads/<branch>/              # Branch-specific data
│   ├── snapshots/
│   │   └── <snapshot-id>.json        # Snapshot metadata
│   └── CURRENT                       # Current snapshot ID
├── daemon.pid                        # Daemon process ID
├── daemon.log                        # Daemon activity log
├── fractyl.lock                      # Concurrency control
└── config.json                       # Repository configuration
```

## Advanced Usage

### File Locking and Concurrency

Fractyl uses sophisticated file locking to handle concurrent access:

- 🔒 **Manual commands** wait up to 30 seconds for daemon to finish
- 🤖 **Daemon** uses non-blocking locks and skips cycles if busy
- 🧹 **Automatic cleanup** removes stale locks from dead processes

```bash
# Force remove stale lock (if daemon PID not running)
rm .fractyl/fractyl.lock

# Check for lock conflicts
ls -la .fractyl/fractyl.lock
```

### Ignore Patterns

Fractyl respects multiple ignore mechanisms:

- `.gitignore` files (standard Git syntax)
- `.fractylignore` files (same syntax)
- Always ignores: `.fractyl/`, `.git/`, `node_modules/`

### Performance Optimization

- **Content deduplication**: Identical files shared across snapshots
- **Incremental snapshots**: Only processes changed files
- **Smart indexing**: Uses file stat info to detect changes quickly
- **Efficient storage**: Two-level directory structure prevents filesystem limits

## Testing and Quality

### Test Suite

Fractyl includes comprehensive testing using the Unity framework:

```bash
# Run all tests
make test

# Run only unit tests
make unit-tests

# Run with coverage analysis
make coverage

# View HTML coverage report
make coverage-html
open coverage-html/index.html
```

**Test Coverage:**
- ✅ Core functionality (hash, objects, index)
- ✅ Command-line interface and parsing
- ✅ File system operations and utilities
- ✅ Daemon operations and concurrency
- ✅ Git integration and branch handling

### Code Quality

- **C99 standard** with GNU extensions
- **Memory safety** with comprehensive cleanup
- **Error handling** for all system calls
- **Compiler warnings** enabled (`-Wall -Wextra -Werror`)
- **Static analysis** compatible

## Development Workflow Integration

### Recommended Workflow

```bash
# 1. Start daemon for automatic snapshots
frac daemon start

# 2. Work on your code
# ... edit files ...

# 3. Quick checkpoint
./frac  # Auto-named snapshot

# 4. Major milestone
frac snapshot -m "Complete feature X implementation"

# 5. If something breaks
frac list              # See recent snapshots
frac restore a1b2c3d4  # Go back to working state

# 6. End of session
frac daemon stop
```

### Snapshot Naming Strategies

- **Manual snapshots**: Descriptive messages
  ```bash
  frac snapshot -m "Fix memory leak in parser"
  frac snapshot -m "Add unit tests for hash module"
  ```

- **Auto snapshots**: Incremental naming
  ```bash
  ./frac  # Creates "working +1"
  ./frac  # Creates "working +2"
  ```

- **Daemon snapshots**: Automatic timestamped
  ```bash
  # Daemon creates: "Auto-snapshot 2025-07-29 22:40:13"
  ```

## Build System

### Makefile Targets

```bash
# Development
make                    # Build executable
make debug              # Debug build with symbols
make clean              # Remove build artifacts
make config             # Show build configuration

# Testing
make test               # Run all tests
make unit-tests         # Unit tests only
make integration-tests  # Integration tests only
make coverage           # Generate coverage report
make coverage-html      # HTML coverage report

# Installation
make install            # Install system-wide
make uninstall          # Remove from system
make check-deps         # Verify dependencies

# Help
make help               # Show all targets
```

### Build Configuration

The build system automatically detects libraries:

- **OpenSSL**: Required for SHA-256 hashing
- **cJSON**: JSON serialization and metadata
- **UUID**: Snapshot ID generation
- **Unity**: Test framework (included)

## Architecture

### Core Components

1. **Object Storage** (`src/core/objects.c`)
   - Content-addressable storage using SHA-256
   - Two-level directory structure for scalability
   - Deduplication and integrity verification

2. **Index Management** (`src/core/index.c`)
   - Tracks file states and changes
   - Binary format for fast loading/saving
   - Change detection using stat + hash

3. **Snapshot System** (`src/commands/snapshot.c`)
   - Creates atomic snapshots of directory state
   - Captures git context and metadata
   - Parent-child relationships for history

4. **Daemon System** (`src/daemon/daemon_standalone.c`)
   - Background process for automatic snapshots
   - Configurable intervals and change detection
   - File locking for concurrency safety

5. **Git Integration** (`src/utils/git.c`)
   - Branch detection and context capture
   - Automatic branch-specific storage organization
   - Git status and commit information

### Data Structures

```c
// File index entry
typedef struct {
    char *path;                 // Relative path
    unsigned char hash[32];     // SHA-256 hash  
    mode_t mode;                // File permissions
    off_t size;                 // File size
    time_t mtime;               // Modification time
} index_entry_t;

// Snapshot metadata
typedef struct {
    char id[64];                // Unique snapshot ID
    char *parent;               // Parent snapshot (nullable)
    char *description;          // User description
    time_t timestamp;           // Creation timestamp
    unsigned char index_hash[32]; // Index state hash
    char *git_branch;           // Git branch name
    char *git_commit;           // Git commit hash
    int git_dirty;              // Uncommitted changes flag
} snapshot_t;
```

## Troubleshooting

### Common Issues

**Daemon won't start**
```bash
# Check if already running
frac daemon status

# Check for stale PID files
cat .fractyl/daemon.pid
ps aux | grep $(cat .fractyl/daemon.pid)

# Remove stale PID if process not running
rm .fractyl/daemon.pid
frac daemon start
```

**Lock conflicts**
```bash
# Check for active lock
ls -la .fractyl/fractyl.lock

# If stale (daemon not running), remove it
rm .fractyl/fractyl.lock
```

**No snapshots created**
```bash
# Check if files actually changed
frac snapshot -m "Test" --debug

# Verify not all files are ignored
cat .gitignore .fractylignore

# Check daemon logs
tail .fractyl/daemon.log
```

**Permission errors**
```bash
# Ensure write access to working directory
ls -la .fractyl/

# Fix permissions if needed
chmod -R 755 .fractyl/
```

### Debug Information

Use `--debug` flag for detailed output:

```bash
frac snapshot -m "Debug test" --debug
frac daemon start --debug
```

Shows:
- File processing details
- Hash calculations
- Object storage operations
- Lock acquisition/release
- Git integration status

## Security and Reliability

### Security Features

- **Path validation**: Prevents directory traversal attacks
- **Permission preservation**: Maintains original file permissions
- **Hash integrity**: SHA-256 provides cryptographic verification
- **Symlink safety**: Careful handling of symbolic links

### Reliability Features

- **Atomic operations**: Snapshots are created atomically
- **Corruption detection**: Hash mismatches detected automatically
- **Graceful degradation**: Works without git, cJSON, or UUID
- **Process recovery**: Handles daemon crashes and restarts

### File Size Limits

- Files larger than 1GB are excluded from snapshots
- Large files are noted in index but not stored
- Prevents unbounded object storage growth

## Performance Characteristics

- **Change detection**: O(n) scan with stat() optimization
- **Object storage**: O(1) lookup by hash
- **Deduplication**: Automatic across all snapshots
- **Memory usage**: Scales with number of files, not file sizes
- **Disk usage**: Only stores unique content once

## Contributing

1. **Setup development environment**
   ```bash
   make check-deps
   make clean && make debug
   ```

2. **Run tests before changes**
   ```bash
   make test
   make coverage
   ```

3. **Code standards**
   - Follow existing C style and conventions
   - Add tests for new functionality
   - Update documentation as needed
   - Ensure clean builds with `-Werror`

4. **Snapshot your changes**
   ```bash
   frac daemon start  # Use Fractyl for development!
   # ... make changes ...
   frac snapshot -m "Implement feature X"
   ```

## License

GNU General Public License - see source code for details.

---

**💡 Pro Tip**: Start the daemon (`frac daemon start`) at the beginning of any development session for automatic backup snapshots every 3 minutes. Use `./frac` for quick manual checkpoints. This gives you fearless development with complete version history!