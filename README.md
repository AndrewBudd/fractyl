# Fractyl

A content-addressable file snapshot tool inspired by Git, written in GNU99 C for maximum portability and performance. Fractyl creates efficient, local-only snapshots of your working directory while respecting `.gitignore` patterns.

## Overview

Fractyl stores file snapshots using content-addressable storage in a `.fractyl/` directory. Each file is hashed (SHA-256) and stored by its hash, enabling efficient deduplication and integrity verification. The tool maintains a DAG (Directed Acyclic Graph) of snapshots, allowing you to track the evolution of your project over time.

### Key Features

- **Content-addressable storage**: Files are stored by their SHA-256 hash, eliminating duplicates
- **Git integration**: Respects `.gitignore` files and captures Git status in snapshots
- **Efficient snapshots**: Only stores changed files, with hard links for unchanged content
- **Tree-based listing**: View snapshot history as a branching tree
- **Local-only**: No network dependencies, all data stays on your machine
- **C implementation**: Fast, portable, minimal dependencies

## Installation

### Prerequisites

- GCC with C99 support
- OpenSSL development libraries
- cJSON library (optional, enables JSON features)
- pkg-config (recommended)

### Building from Source

```bash
# Clone or download the source
cd fractyl/

# Check dependencies
make check-deps

# Build the project
make

# Run tests
make test

# Install (optional)
sudo make install
```

### Build Configuration

The build system automatically detects available libraries:

- **OpenSSL**: Required for SHA-256 hashing
- **cJSON**: Optional, enables JSON output formats
- **UUID**: Optional, for generating snapshot IDs

Run `make config` to see your current build configuration.

## Usage

### Initialize a Repository

Fractyl works in any directory. No explicit initialization is required - the `.fractyl/` directory is created on first use.

### Creating Snapshots

```bash
# Create a snapshot with a message
fractyl snapshot -m "Initial project state"

# Create a snapshot with debug output
fractyl snapshot -m "Added new feature" --debug
```

### Listing Snapshots

```bash
# Show all snapshots in tree format
fractyl list

# Example output:
# * a1b2c3d4  "Initial project state" (2024-01-15 10:30:00)
# |\
# | * b2c3d4e5  "Added new feature" (2024-01-15 11:45:00)
# |/
# * c3d4e5f6  "Bug fixes" (2024-01-15 14:20:00)
```

### Restoring Snapshots

```bash
# Restore to a specific snapshot
fractyl restore a1b2c3d4

# Force restore (removes untracked files)
fractyl restore a1b2c3d4 --force
```

### Deleting Snapshots

```bash
# Delete a snapshot (keeps objects if referenced elsewhere)
fractyl delete a1b2c3d4
```

### Additional Commands

```bash
# Show help
fractyl --help

# Show version
fractyl --version

# Run utility tests
fractyl --test-utils
```

## Architecture

### Directory Structure

```
.fractyl/
├── objects/                     # Content-addressable storage
│   └── <first 2 chars>/
│       └── <remaining hash>     # File blobs stored by SHA-256
├── index                        # Current working directory state
├── snapshots/
│   └── <snapshot-id>.json       # Snapshot metadata
└── snapshot-graph.json          # DAG of snapshot relationships
```

### Core Data Structures

#### Index Entry
```c
typedef struct {
    char *path;                 // Relative path from repo root
    unsigned char hash[32];     // SHA-256 hash
    mode_t mode;                // File permissions
    off_t size;                 // File size
    time_t mtime;               // Modification time
} index_entry_t;
```

#### Snapshot Metadata
```c
typedef struct {
    char id[64];                // UUID or hash
    char *parent;               // Parent snapshot ID (nullable)
    char *description;          // User description
    time_t timestamp;           // Creation time
    unsigned char index_hash[32]; // Hash of index at snapshot time
    char **git_status;          // Array of git status lines
    size_t git_status_count;
} snapshot_t;
```

### Ignore Rules

Fractyl respects the following ignore patterns:
- `.gitignore` files (standard Git ignore syntax)
- `.fractylignore` files (same syntax as .gitignore)
- Always ignores: `.fractyl/`, `.git/`

### File Size Limits

- Files larger than 1GB are excluded from snapshots
- Large files are noted in the index but not stored
- This prevents the object store from becoming unwieldy

## Implementation Details

### Hash Algorithm

Uses SHA-256 for content addressing:
- Cryptographically secure
- Extremely low collision probability
- Standard library support via OpenSSL

### Object Storage

Files are stored in `.fractyl/objects/` using a two-level directory structure:
- First 2 characters of hash → subdirectory
- Remaining characters → filename
- Example: `deadbeef123...` → `.fractyl/objects/de/adbeef123...`

### Snapshot Creation Process

1. Load current index from disk
2. Walk working directory, respecting ignore rules
3. Compare files with index (using stat + hash for changed files)
4. Store new/changed objects in content-addressable storage
5. Update index with new file states
6. Create snapshot metadata including Git status (if available)
7. Update snapshot graph with parent relationships

### Error Handling

Comprehensive error handling with specific error codes:
- `FRACTYL_OK`: Success
- `FRACTYL_ERROR_IO`: File I/O errors
- `FRACTYL_ERROR_OUT_OF_MEMORY`: Memory allocation failures
- `FRACTYL_ERROR_SNAPSHOT_NOT_FOUND`: Invalid snapshot ID
- `FRACTYL_ERROR_HASH_MISMATCH`: Corruption detected

## Build System

### Makefile Targets

- `make` or `make all`: Build the executable
- `make debug`: Build with debug symbols and verbose output
- `make release`: Build optimized release version
- `make clean`: Remove build artifacts
- `make test`: Run basic functionality tests
- `make install`: Install to system PATH
- `make check-deps`: Verify required dependencies
- `make config`: Display build configuration
- `make help`: Show all available targets

### Build Configuration

The build system features:
- Automatic dependency detection
- Modular compilation with object files in `o/` directory
- Separate debug/release configurations
- Cross-platform compatibility
- Library detection via pkg-config

## Development

### Project Structure

```
fractyl/
├── Makefile                    # Build system
├── src/                        # Source code
│   ├── main.c                  # Entry point and command dispatch
│   ├── commands/               # Command implementations
│   │   ├── snapshot.c
│   │   ├── restore.c
│   │   ├── delete.c
│   │   └── list.c
│   ├── core/                   # Core functionality modules
│   │   ├── index.c             # Index management
│   │   ├── objects.c           # Object storage operations
│   │   ├── hash.c              # SHA hashing utilities
│   │   └── *.h                 # Module headers
│   ├── utils/                  # Utility modules
│   │   ├── fs.c                # File system operations
│   │   ├── cli.c               # Command line parsing
│   │   ├── json.c              # JSON serialization
│   │   └── *.h                 # Utility headers
│   └── include/                # Public headers
│       ├── fractyl.h           # Common definitions
│       ├── core.h              # Core data structures
│       ├── commands.h          # Command declarations
│       └── utils.h             # Utility declarations
├── o/                          # Object files (generated)
├── tests/                      # Test suite
└── README.md                   # This file
```

### Code Quality Standards

- **C99 compliance**: Uses GNU99 standard for portability
- **Memory safety**: Explicit allocation/deallocation with cleanup functions
- **Error handling**: Consistent error codes and cleanup on failure paths
- **Compiler warnings**: Builds with `-Wall -Wextra -Werror`
- **Testing**: Unit tests for all core modules

### Contributing

1. Ensure your changes build cleanly: `make clean && make`
2. Run tests: `make test`
3. Follow existing code style and conventions
4. Add tests for new functionality
5. Update documentation as needed

## Security Considerations

- **Path validation**: All file paths are validated to prevent directory traversal
- **Permission respect**: File permissions are preserved and respected
- **Hash integrity**: SHA-256 provides cryptographic integrity verification
- **Symlink handling**: Symbolic links are handled carefully to prevent attacks

## Limitations

- **Local only**: No remote synchronization capabilities
- **Single-threaded**: No concurrent operations (may be added in future)
- **File size limits**: 1GB maximum file size
- **No compression**: Objects stored uncompressed (may be added in future)

## Future Enhancements

- `fractyl diff <snapshot-a> <snapshot-b>`: Compare snapshots
- `fractyl tag <snapshot-id> <name>`: Tag snapshots for easy reference
- `fractyl export <snapshot-id> --to tar.gz`: Export snapshots
- `fractyl watch`: Automatic periodic snapshots
- Object compression and garbage collection
- Multi-threaded operations for large repositories

## License

This project is implemented according to the GNU99 C specification for maximum portability and compatibility.

## Troubleshooting

### Common Issues

**Build fails with "OpenSSL not found"**
```bash
# Ubuntu/Debian
sudo apt-get install libssl-dev

# RHEL/CentOS/Fedora
sudo yum install openssl-devel  # or dnf install
```

**Build fails with "cJSON not found"**
```bash
# Ubuntu/Debian
sudo apt-get install libcjson-dev

# The build will work without cJSON but some features may be limited
```

**"No changes to snapshot" error**
- Fractyl only creates snapshots when files have actually changed
- Use `fractyl list` to see existing snapshots
- Check that files aren't ignored by `.gitignore` or `.fractylignore`

**Permission denied errors**
- Ensure you have write access to the working directory
- The `.fractyl/` directory needs read/write permissions
- Some files may have restrictive permissions that prevent snapshotting

### Debug Output

Use the `--debug` flag with any command to see detailed operation information:

```bash
fractyl snapshot -m "Debug test" --debug
```

This shows:
- Files being processed
- Hash calculations
- Object storage operations
- Index updates
- Error details

### Getting Help

- Run `fractyl --help` for command usage
- Run `make help` for build system help
- Check the implementation plan and progress logs for development details
- Use `fractyl --test-utils` to verify basic functionality