## libnix Multi-Platform Porting Guide

This document describes how libnix achieves portability across multiple operating systems and provides guidelines for porting to new platforms.

### Table of Contents

1. [Overview](#overview)
2. [Supported Platforms](#supported-platforms)
3. [Architecture](#architecture)
4. [Platform Layer](#platform-layer)
5. [Adding New Platform Support](#adding-new-platform-support)
6. [Platform-Specific Notes](#platform-specific-notes)
7. [Testing](#testing)

---

## Overview

libnix is a Unix system call compatibility layer that enables emulated guest operating systems to run on various host platforms. The architecture provides:

- **Guest OS Abstraction**: Translates guest system calls (OpenBSD, Linux, etc.)
- **NIX Layer**: Platform-independent system call implementations
- **Host Platform Abstraction**: Maps NIX calls to host OS APIs

```
┌─────────────────────────────────┐
│  Guest OS (OpenBSD 4.1, etc.)   │
│  System Call (e.g., open, read) │
└────────────┬────────────────────┘
             │
       Guest ABI Handler
             │
             ↓
┌─────────────────────────────────┐
│   Guest-Specific Layer          │
│   (obsd41-syscalls.c)           │
│   • Structure conversion        │
│   • Parameter extraction        │
└────────────┬────────────────────┘
             │
             ↓
┌─────────────────────────────────┐
│   NIX Abstraction Layer         │
│   (nix-file.c, nix-process.c)   │
│   • Platform-independent logic  │
│   • Error handling              │
└────────────┬────────────────────┘
             │
             ↓
┌─────────────────────────────────┐
│   Platform Abstraction Layer    │
│   (nix-platform.c)              │
│   • OS detection                │
│   • API mapping                 │
└────────────┬────────────────────┘
             │
  ┌──────────┴──────────┬──────────┬──────────┬──────────┐
  ↓                     ↓          ↓          ↓          ↓
┌──────┐    ┌──────────┐  ┌──────┐  ┌──────┐  ┌────────┐
│Unix  │    │  Win32   │  │Haiku │  │ OS/2 │  │OpenVMS │
│POSIX │    │  API     │  │ API  │  │ API  │  │  RMS   │
└──────┘    └──────────┘  └──────┘  └──────┘  └────────┘
```

---

## Supported Platforms

### Host Operating Systems (where libnix runs)

| Platform | Status | Architecture | Notes |
|----------|--------|--------------|-------|
| **Linux** | ✅ Fully Supported | x86, x86_64, ARM, ARM64, MIPS, PowerPC, SPARC, RISC-V | Primary development platform |
| **FreeBSD** | ✅ Fully Supported | x86, x86_64, ARM64 | Full BSD support |
| **OpenBSD** | ✅ Fully Supported | x86, x86_64, ARM64, SPARC, m88k | Excellent m88k support |
| **NetBSD** | ✅ Fully Supported | x86, x86_64, ARM, m68k, VAX | Wide architecture support |
| **macOS** | ✅ Fully Supported | x86_64, ARM64 (Apple Silicon) | Darwin/XNU kernel |
| **Solaris/illumos** | ⚠️ Legacy Support | x86, x86_64, SPARC | Older code paths |
| **HP-UX** | ⚠️ Legacy Support | PA-RISC, Itanium | Historical support |
| **AIX** | ⚠️ Legacy Support | PowerPC | Historical support |
| **Windows (Win32)** | 🔧 Newly Added | x86, x86_64, ARM64 | Core APIs implemented |
| **Haiku** | 🔧 Newly Added | x86, x86_64 | BeOS successor |
| **OS/2** | 🔧 Newly Added | x86 | OS/2 Warp 4+ |
| **OpenVMS** | 🔧 Newly Added | Alpha, VAX, Itanium | RMS-based |

### Guest Operating Systems (being emulated)

| Guest OS | Version | Architecture | Status |
|----------|---------|--------------|--------|
| **OpenBSD** | 4.1 | m88k, SPARC | ✅ Fully Implemented |
| **Linux** | Various | Multiple | 🔧 Partial (framework exists) |

---

## Architecture

### Layer Structure

#### 1. Guest OS Layer (`obsd41/`, etc.)

**Purpose**: Implements specific guest OS system call interface

**Key Files**:
- `{guest}.sc` - System call definition file
- `{guest}-syscalls.c` - System call implementations (3000-5000 lines)
- `{guest}-structs.c` - Structure conversion (guest ↔ NIX format)
- `{guest}-types.h` - Guest-specific type definitions
- `arch/{arch}/{guest}-guest.c` - Architecture-specific ABI handlers

**Responsibilities**:
- Parse guest system call numbers
- Extract parameters from guest registers/stack
- Convert guest structures to NIX structures
- Handle endianness differences
- Set return values in guest context

#### 2. NIX Abstraction Layer (`nix/`)

**Purpose**: Platform-independent system call logic

**Key Components**:
- `nix-file.c` - File operations
- `nix-process.c` - Process management
- `nix-mem.c` - Memory management
- `nix-signal.c` - Signal handling
- `nix-socket.c` - Network operations
- `nix-*` - Other system call categories

**Responsibilities**:
- Implement system call semantics
- Manage file descriptor mapping
- Handle error codes via `nix_env_t`
- Provide portable interfaces

#### 3. Platform Abstraction Layer (`nix/nix-platform.{h,c}`)

**Purpose**: Abstract host OS differences

**Features**:
- Compile-time platform detection
- Runtime platform identification
- API mapping (POSIX ↔ Win32 ↔ BeOS ↔ etc.)
- Error code translation
- Type definitions

---

## Platform Layer

### Platform Detection (nix-platform.h)

Automatic detection via compiler macros:

```c
// Detected automatically:
#if defined(_WIN32)
  → NIX_HOST_WIN32
#elif defined(__HAIKU__)
  → NIX_HOST_HAIKU
#elif defined(__OS2__)
  → NIX_HOST_OS2
#elif defined(__VMS)
  → NIX_HOST_OPENVMS
#elif defined(__linux__)
  → NIX_HOST_LINUX + NIX_HOST_UNIX
#elif defined(__FreeBSD__)
  → NIX_HOST_FREEBSD + NIX_HOST_BSD + NIX_HOST_UNIX
// ... etc
```

### Type Mappings

Each platform defines host types:

| NIX Type | Win32 | Haiku | OS/2 | OpenVMS | POSIX |
|----------|-------|-------|------|---------|-------|
| `nix_host_fd_t` | `HANDLE` | `int` | `HFILE` | `int` | `int` |
| `nix_host_pid_t` | `DWORD` | `team_id` | `PID` | `uint32_t` | `pid_t` |
| `nix_host_mode_t` | `DWORD` | `mode_t` | `ULONG` | `uint32_t` | `mode_t` |

### Capability Detection

Platform capabilities defined at compile-time:

```c
// Automatically set per platform:
NIX_HAS_POSIX_SIGNALS  // 1 on Unix, 0 on Win32/OS2
NIX_HAS_FORK           // 1 on Unix/Haiku, 0 on Win32/VMS
NIX_HAS_MMAP           // 1 on most platforms
NIX_HAS_SOCKETS        // 1 on all modern platforms
NIX_HAS_SELECT         // 1 on all supported platforms
NIX_HAS_POLL           // Unix/Haiku only
NIX_HAS_EPOLL          // Linux only
NIX_HAS_KQUEUE         // BSD/macOS only
```

---

## Adding New Platform Support

### Step 1: Update Platform Detection

Edit `nix/nix-platform.h`:

```c
// Add OS detection
#elif defined(__YOUR_OS_MACRO__)
# define NIX_HOST_YOUROS 1

// Add architecture if needed
#elif defined(__your_arch__)
# define NIX_HOST_ARCH_YOURARCH 1
# define NIX_HOST_ARCH "yourarch"

// Add platform-specific includes
#if defined(NIX_HOST_YOUROS)
# include <youros/specific.h>
#endif

// Define host types
#if defined(NIX_HOST_YOUROS)
typedef your_fd_type    nix_host_fd_t;
typedef your_pid_type   nix_host_pid_t;
// ... etc

# define NIX_HOST_INVALID_FD    YOUR_INVALID_FD
# define NIX_HOST_PATH_MAX      YOUR_PATH_MAX
# define NIX_HOST_PATH_SEP      YOUR_PATH_SEPARATOR
#endif

// Define capabilities
#if defined(NIX_HOST_YOUROS)
# define NIX_HAS_POSIX_SIGNALS  1/0
# define NIX_HAS_FORK           1/0
# define NIX_HAS_MMAP           1/0
// ... etc
#endif
```

### Step 2: Implement Platform Functions

Edit `nix/nix-platform.c`:

```c
#if defined(NIX_HOST_YOUROS)
int nix_platform_init(void) {
    // Platform-specific initialization
    return 0;
}

nix_host_fd_t nix_platform_open(const char *path, int flags, int mode) {
    // Convert flags, call native API
    return your_os_open(path, converted_flags);
}

// Implement other required functions
#endif
```

### Step 3: Add Platform-Specific NIX Extensions (Optional)

If your platform has unique APIs, create:

```
nix/nix-youros-*.c
```

Examples:
- `nix-youros-fs.c` - Filesystem extensions
- `nix-youros-process.c` - Process extensions
- `nix-youros-ipc.c` - IPC extensions

### Step 4: Update Build System

Edit `nix/CMakeLists.txt`:

```cmake
IF(YOUR_OS_DETECTED)
    SET(PLATFORM_SOURCES
        nix-youros-fs.c
        nix-youros-process.c
        # ... platform-specific files
    )
ENDIF()

ADD_LIBRARY(nix
    # ... common files
    ${PLATFORM_SOURCES}
)
```

### Step 5: Test

Create test programs:

```c
#include "nix.h"

int main() {
    nix_env_t env;
    nix_env_init(&env);

    // Test basic operations
    int fd = nix_open("/tmp/test", O_CREAT|O_RDWR, 0644, &env);
    // ... test other operations

    return 0;
}
```

---

## Platform-Specific Notes

### Win32 (Windows NT 4.0+)

**Key Differences**:
- No POSIX signals → Use Windows events/threads
- No `fork()` → Use `CreateProcess()`
- Different file descriptors → `HANDLE` vs `int`
- Different error codes → Map `GetLastError()` to `errno`
- Case-insensitive filesystem
- Backslash path separators

**API Mappings**:
| POSIX | Win32 |
|-------|-------|
| `open()` | `CreateFile()` |
| `read()` | `ReadFile()` |
| `write()` | `WriteFile()` |
| `close()` | `CloseHandle()` |
| `fork()` | ❌ Not available, use `CreateProcess()` |
| `mmap()` | `CreateFileMapping()` + `MapViewOfFile()` |
| `socket()` | Winsock2 API |

**Initialization Requirements**:
- Call `WSAStartup()` for networking
- Handle Unicode vs ANSI (`CreateFileA` vs `CreateFileW`)

### Haiku (BeOS successor)

**Key Differences**:
- Different IPC model (ports instead of pipes)
- Teams/threads model (not traditional processes)
- Mostly POSIX-compliant with extensions

**API Extensions**:
- `_kern_*` system calls (kernel interface)
- `find_thread()` - Get thread ID
- `spawn_thread()` - Create threads
- `create_port()`, `write_port()` - IPC
- `create_area()` - Shared memory

**Initialization Requirements**:
- Link with libroot.so (POSIX layer)
- Use Be API for IPC operations

### OS/2 (Warp 4+)

**Key Differences**:
- Different API model (DosXxx functions)
- Different file handles (`HFILE`)
- Different error codes
- Case-insensitive filesystem
- Supports POSIX layer but native API preferred

**API Mappings**:
| POSIX | OS/2 |
|-------|------|
| `open()` | `DosOpen()` |
| `read()` | `DosRead()` |
| `write()` | `DosWrite()` |
| `close()` | `DosClose()` |
| `fork()` | `DosExecPgm()` (not exact equivalent) |
| `mmap()` | `DosAllocMem()` |

**Initialization Requirements**:
- Set up exception handlers
- Initialize TCP/IP stack if using sockets

### OpenVMS (Alpha, VAX, Itanium)

**Key Differences**:
- Completely different OS model
- RMS (Record Management Services) for I/O
- System services (SYS$xxx) instead of syscalls
- Different security/privilege model
- Unique file versioning
- Special path syntax (`device:[directory]file.ext;version`)

**API Mappings**:
| POSIX | OpenVMS |
|-------|---------|
| `open()` | `SYS$OPEN` + RMS |
| `read()` | `SYS$READ` / `SYS$GET` |
| `write()` | `SYS$WRITE` / `SYS$PUT` |
| `fork()` | ❌ Not available, use `LIB$SPAWN()` |
| `mmap()` | `SYS$CRMPSC()` |

**Initialization Requirements**:
- Initialize RMS structures
- Set up privilege masks
- Handle quadword (64-bit) types
- Deal with descriptors for strings

---

## Testing

### Unit Tests

Create per-platform tests:

```
test/libnix/tests/
├── test-file-ops.c      # File I/O tests
├── test-process.c       # Process tests
├── test-memory.c        # Memory tests
└── test-platform.c      # Platform-specific tests
```

### Cross-Platform Testing

Test matrix:

| Test | Linux | BSD | macOS | Win32 | Haiku | OS/2 | OpenVMS |
|------|-------|-----|-------|-------|-------|------|---------|
| File I/O | ✅ | ✅ | ✅ | 🔧 | 🔧 | 🔧 | 🔧 |
| Process | ✅ | ✅ | ✅ | ⚠️ | 🔧 | ⚠️ | ❌ |
| Memory | ✅ | ✅ | ✅ | 🔧 | 🔧 | 🔧 | 🔧 |
| Signals | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | ⚠️ |
| Sockets | ✅ | ✅ | ✅ | 🔧 | 🔧 | 🔧 | 🔧 |
| IPC | ✅ | ✅ | ✅ | ⚠️ | 🔧 | 🔧 | 🔧 |

Legend:
- ✅ Fully tested and working
- 🔧 Implemented, needs testing
- ⚠️ Partial implementation
- ❌ Not supported on this platform

### Building Tests

```bash
# Unix/POSIX
cd test/libnix
mkdir build && cd build
cmake ..
make
make test

# Windows (Visual Studio)
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build . --config Release
ctest -C Release

# Haiku
cd test/libnix
mkdir build && cd build
cmake ..
make
./test-suite

# OS/2 (with GCC/EMX)
cd test\libnix
mkdir build && cd build
cmake .. -G "Unix Makefiles"
make

# OpenVMS
$ SET DEFAULT [.test.libnix]
$ @BUILD.COM
$ RUN TEST-SUITE.EXE
```

---

## Platform Status Summary

| Feature | Linux | BSD | macOS | Win32 | Haiku | OS/2 | VMS |
|---------|-------|-----|-------|-------|-------|------|-----|
| **Detection** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Initialization** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **File I/O** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | 🔧 |
| **Process** | ✅ | ✅ | ✅ | ⚠️ | ✅ | ⚠️ | ⚠️ |
| **Memory** | ✅ | ✅ | ✅ | 🔧 | ✅ | 🔧 | 🔧 |
| **Signals** | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | ⚠️ |
| **Networking** | ✅ | ✅ | ✅ | 🔧 | ✅ | 🔧 | 🔧 |
| **Build System** | ✅ | ✅ | ✅ | 🔧 | 🔧 | 🔧 | 🔧 |

---

## Contributing

When adding new platform support:

1. Update `nix-platform.h` with detection macros
2. Implement required functions in `nix-platform.c`
3. Add platform-specific extensions if needed
4. Update build system (CMake)
5. Write tests
6. Update this documentation
7. Submit pull request with:
   - Platform name and version
   - Architecture(s) tested
   - Any known limitations
   - Test results

---

## References

- **POSIX**: IEEE Std 1003.1-2017
- **Win32 API**: Microsoft Windows SDK Documentation
- **Haiku**: https://www.haiku-os.org/docs/api/
- **OS/2**: IBM OS/2 Warp Toolkit Documentation
- **OpenVMS**: HP OpenVMS Documentation
- **libnix**: See README.md in test/libnix/ directory

---

## License

See main libcpu LICENSE file for licensing information.
