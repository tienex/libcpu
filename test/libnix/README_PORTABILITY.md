# libnix Multi-Platform Support

**Version**: 2.0.0
**Last Updated**: 2025-10-23

## Overview

libnix is now portable across a wide range of operating systems, enabling guest OS emulation on diverse host platforms including Unix/POSIX systems, Windows, Haiku, OS/2, and OpenVMS.

## Supported Host Platforms

### ✅ Fully Supported (Production-Ready)

| Platform | Architectures | Notes |
|----------|---------------|-------|
| **Linux** | x86, x86_64, ARM, ARM64, MIPS, PowerPC, SPARC, RISC-V | Primary development platform |
| **FreeBSD** | x86, x86_64, ARM64 | Full BSD support with kqueue |
| **OpenBSD** | x86, x86_64, ARM64, SPARC, m88k | Excellent legacy architecture support |
| **NetBSD** | x86, x86_64, ARM, m68k, VAX | Widest architecture support |
| **macOS** | x86_64, ARM64 (Apple Silicon) | Darwin/XNU kernel |

### 🔧 Newly Added (Experimental)

| Platform | Architectures | Status |
|----------|---------------|--------|
| **Windows (Win32/Win64)** | x86, x86_64, ARM64 | Core APIs implemented, testing needed |
| **Haiku** | x86, x86_64 | BeOS successor, modern POSIX-like |
| **OS/2** | x86 (32-bit) | OS/2 Warp 4+, DosAPI wrapping |
| **OpenVMS** | Alpha, VAX, Itanium | RMS-based I/O, partial support |

### ⚠️ Legacy Support

| Platform | Status |
|----------|--------|
| **Solaris/illumos** | Older code paths maintained |
| **HP-UX** | Historical support (PA-RISC, Itanium) |
| **AIX** | Historical support (PowerPC) |

## Key Features

### 🎯 Platform Detection

Automatic compile-time detection of:
- Operating system (30+ platforms recognized)
- CPU architecture (15+ architectures)
- Available system capabilities (signals, fork, mmap, etc.)
- Filesystem characteristics (case-sensitivity, path separators)

### 🔄 API Abstraction

Unified interface across platforms:
- File I/O (open, read, write, close)
- Process management (fork, exec, wait)
- Memory management (mmap, brk, sbrk)
- Signal handling (where supported)
- Network operations (sockets, select)
- Error handling (errno translation)

### 📊 Capability Matrix

| Feature | Linux | BSD | macOS | Win32 | Haiku | OS/2 | OpenVMS |
|---------|-------|-----|-------|-------|-------|------|---------|
| **POSIX Signals** | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | ⚠️ |
| **fork()** | ✅ | ✅ | ✅ | ❌ | ✅ | ⚠️ | ❌ |
| **mmap()** | ✅ | ✅ | ✅ | ✅¹ | ✅ | ✅² | ⚠️ |
| **pthreads** | ✅ | ✅ | ✅ | ❌ | ✅ | ✅³ | ✅ |
| **Sockets** | ✅ | ✅ | ✅ | ✅⁴ | ✅ | ✅ | ✅ |
| **select()** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| **poll()** | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | ❌ |
| **epoll()** | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **kqueue()** | ❌ | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |

**Notes**:
1. Via CreateFileMapping/MapViewOfFile
2. Via DosAllocMem
3. OS/2 native threads
4. Winsock2 API

## Architecture

### Layer Structure

```
┌─────────────────────────────────────────┐
│  Guest OS (OpenBSD, Linux, etc.)        │
└────────────┬────────────────────────────┘
             │
             ↓
┌─────────────────────────────────────────┐
│  Guest-Specific Layer                   │
│  • obsd41-syscalls.c                    │
│  • Structure conversion                 │
│  • ABI handling                         │
└────────────┬────────────────────────────┘
             │
             ↓
┌─────────────────────────────────────────┐
│  NIX Abstraction Layer                  │
│  • Platform-independent logic           │
│  • 250+ system call implementations     │
│  • File descriptor mapping              │
│  • Error handling                       │
└────────────┬────────────────────────────┘
             │
             ↓
┌─────────────────────────────────────────┐
│  Platform Abstraction Layer (NEW)       │
│  • nix-platform.h (detection)           │
│  • nix-platform.c (implementation)      │
│  • Automatic OS/arch detection          │
│  • API mapping                          │
└────────────┬────────────────────────────┘
             │
    ┌────────┴────────┬──────────┬──────────┬──────────┐
    ↓                 ↓          ↓          ↓          ↓
┌────────┐   ┌─────────┐   ┌────────┐  ┌─────┐   ┌──────┐
│Unix    │   │ Win32   │   │ Haiku  │  │OS/2 │   │OpenVMS│
│POSIX   │   │ API     │   │ API    │  │ API │   │ RMS  │
└────────┘   └─────────┘   └────────┘  └─────┘   └──────┘
```

## Quick Start

### Building on Linux/Unix

```bash
cd test/libnix
mkdir build && cd build
cmake ..
make
make test
```

### Building on Windows

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64
cmake --build . --config Release
ctest -C Release
```

### Building on Haiku

```bash
cd test/libnix
mkdir build && cd build
cmake ..
make
./test-suite
```

### Building on OS/2

```bash
cd test\libnix
mkdir build && cd build
cmake .. -G "Unix Makefiles"
make
```

### Building on OpenVMS

```bash
$ SET DEFAULT [.test.libnix]
$ @BUILD.COM
$ RUN TEST-SUITE.EXE
```

## Platform-Specific Notes

### Windows (Win32/Win64)

**Key Differences from POSIX:**
- Uses HANDLEs instead of integer file descriptors
- No native fork() support (use CreateProcess instead)
- Different error codes (GetLastError() → errno mapping)
- Winsock2 for networking (requires WSAStartup/WSACleanup)
- Backslash path separators
- Case-insensitive filesystem

**API Mappings:**
- `open()` → `CreateFile()`
- `read()`/`write()` → `ReadFile()`/`WriteFile()`
- `close()` → `CloseHandle()`
- `mmap()` → `CreateFileMapping()` + `MapViewOfFile()`
- `socket()` → Winsock2 functions

**Build Requirements:**
- Windows SDK (for headers)
- Winsock2 library (ws2_32.lib)

### Haiku

**Key Differences:**
- Teams/threads architecture (not traditional Unix processes)
- Different IPC model (ports instead of pipes)
- Mostly POSIX-compliant with BeOS extensions

**Special APIs:**
- `_kern_*` system calls (kernel interface)
- `create_port()`/`write_port()` for IPC
- `spawn_thread()` for threading
- `create_area()` for shared memory

**Build Requirements:**
- Haiku R1/beta or later
- GCC toolchain

### OS/2

**Key Differences:**
- DosAPI instead of POSIX (DosOpen, DosRead, etc.)
- Different process creation (DosExecPgm, not fork)
- HFILE type for file handles
- Case-insensitive filesystem

**API Mappings:**
- `open()` → `DosOpen()`
- `read()`/`write()` → `DosRead()`/`DosWrite()`
- `mmap()` → `DosAllocMem()`
- `fork()` → `DosExecPgm()` (not exact equivalent)

**Build Requirements:**
- OS/2 Warp 4+ or ArcaOS
- GCC/EMX or IBM VisualAge C++
- OS/2 Toolkit

### OpenVMS

**Key Differences:**
- RMS (Record Management Services) for I/O
- System services (SYS$xxx) instead of syscalls
- Quadword (64-bit) types
- String descriptors
- Unique file versioning
- Special path syntax (`DEVICE:[DIRECTORY]FILE.EXT;VERSION`)

**API Mappings:**
- `open()` → `SYS$OPEN` + RMS
- `read()`/`write()` → `SYS$READ`/`SYS$WRITE` or RMS GET/PUT
- `fork()` → Not available (use `LIB$SPAWN()`)
- `mmap()` → `SYS$CRMPSC()`

**Build Requirements:**
- OpenVMS 7.3+ (Alpha), 8.4+ (Itanium)
- DEC C compiler or GCC
- OpenVMS development kit

## Testing

### Test Suite

Located in `test/libnix/tests/`:
- `test-file-ops.c` - File I/O operations
- `test-process.c` - Process management
- `test-memory.c` - Memory operations
- `test-signals.c` - Signal handling
- `test-sockets.c` - Network operations
- `test-platform.c` - Platform-specific tests

### Running Tests

```bash
# After building
make test

# Or directly
./test-suite

# Verbose output
./test-suite --verbose

# Specific test
./test-file-ops
```

### Test Coverage

| Test Category | Linux | BSD | macOS | Win32 | Haiku | OS/2 | OpenVMS |
|---------------|-------|-----|-------|-------|-------|------|---------|
| File I/O | ✅ | ✅ | ✅ | 🔧 | 🔧 | 🔧 | 🔧 |
| Process | ✅ | ✅ | ✅ | ⚠️ | 🔧 | ⚠️ | ⚠️ |
| Memory | ✅ | ✅ | ✅ | 🔧 | 🔧 | 🔧 | 🔧 |
| Signals | ✅ | ✅ | ✅ | ❌ | 🔧 | ❌ | ⚠️ |
| Network | ✅ | ✅ | ✅ | 🔧 | 🔧 | 🔧 | 🔧 |
| IPC | ✅ | ✅ | ✅ | ⚠️ | 🔧 | 🔧 | 🔧 |

**Legend:**
- ✅ Fully tested and working
- 🔧 Implemented, needs testing
- ⚠️ Partial implementation/known limitations
- ❌ Not supported on this platform

## API Reference

### Platform Detection Macros

```c
#include "nix-platform.h"

// OS Detection
#if defined(NIX_HOST_WIN32)
  // Windows-specific code
#elif defined(NIX_HOST_HAIKU)
  // Haiku-specific code
#elif defined(NIX_HOST_OS2)
  // OS/2-specific code
#elif defined(NIX_HOST_OPENVMS)
  // OpenVMS-specific code
#elif defined(NIX_HOST_LINUX)
  // Linux-specific code
#elif defined(NIX_HOST_BSD)
  // BSD-specific code (FreeBSD, OpenBSD, NetBSD, macOS)
#endif

// Architecture Detection
#if defined(NIX_HOST_ARCH_X86_64)
  // 64-bit x86 code
#elif defined(NIX_HOST_ARCH_I386)
  // 32-bit x86 code
#elif defined(NIX_HOST_ARCH_ARM64)
  // 64-bit ARM code
#endif

// Capability Detection
#if NIX_HAS_FORK
  // Can use fork()
#endif

#if NIX_HAS_POSIX_SIGNALS
  // Can use signal()
#endif
```

### Platform Functions

```c
// Initialization
int nix_platform_init(void);
void nix_platform_shutdown(void);

// File Operations
nix_host_fd_t nix_platform_open(const char *path, int flags, int mode);
int nix_platform_close(nix_host_fd_t fd);
ssize_t nix_platform_read(nix_host_fd_t fd, void *buf, size_t count);
ssize_t nix_platform_write(nix_host_fd_t fd, const void *buf, size_t count);

// Process Operations
nix_host_pid_t nix_platform_getpid(void);
nix_host_pid_t nix_platform_fork(void);

// Error Handling
int nix_platform_get_errno(void);
void nix_platform_set_errno(int error);

// Utilities
const char *nix_platform_get_name(void);
const char *nix_platform_get_arch(void);
int nix_platform_is_case_sensitive_fs(void);
```

## Adding New Platform Support

See **[PORTING.md](PORTING.md)** for comprehensive guide on:
- Adding new platform detection
- Implementing platform-specific functions
- Creating platform extensions
- Testing and validation

Quick summary:
1. Update `nix-platform.h` with platform detection macros
2. Implement platform functions in `nix-platform.c`
3. Add platform-specific extensions if needed (e.g., `nix-yourplatform-*.c`)
4. Update build system (CMakeLists.txt)
5. Write tests
6. Submit pull request

## Known Limitations

### Windows
- No fork() support (use CreateProcess instead)
- No POSIX signals (use events/threads)
- Different file descriptor model (HANDLE vs int)

### OS/2
- No direct fork() equivalent (use DosExecPgm)
- Limited POSIX signal support

### OpenVMS
- No fork() support (use LIB$SPAWN)
- Different I/O model (RMS vs stream)
- Special path syntax handling required

## Performance Considerations

- **Linux/BSD/macOS**: Native POSIX, minimal overhead
- **Windows**: Translation layer adds slight overhead
- **Haiku**: Native APIs, good performance
- **OS/2**: API translation overhead
- **OpenVMS**: RMS adds overhead for stream I/O

## Contributing

Contributions for:
- Additional platform support
- Platform-specific optimizations
- Bug fixes for existing platforms
- Test coverage improvements
- Documentation enhancements

See [CONTRIBUTING.md](../../CONTRIBUTING.md) for guidelines.

## Changelog

### Version 2.0.0 (2025-10-23)

- ✨ **NEW**: Multi-platform support infrastructure
- ✨ **NEW**: Windows (Win32/Win64) support
- ✨ **NEW**: Haiku OS support
- ✨ **NEW**: OS/2 Warp 4+ support
- ✨ **NEW**: OpenVMS (Alpha/VAX/Itanium) support
- ✨ **NEW**: Comprehensive platform detection (nix-platform.h)
- ✨ **NEW**: Platform abstraction layer (nix-platform.c)
- ✨ **NEW**: Detailed porting guide (PORTING.md)
- 🔧 **IMPROVED**: Build system supports multiple platforms
- 📚 **DOCS**: Comprehensive portability documentation

### Version 1.x

- Initial Unix/POSIX support (Linux, BSD, macOS, Solaris)
- OpenBSD 4.1 guest support (m88k, SPARC)
- 250+ NIX system call implementations
- File descriptor mapping
- Signal handling
- Network operations

## License

See main libcpu LICENSE file for licensing information.

## Support

For issues, questions, or contributions:
- File issues on GitHub
- See PORTING.md for implementation details
- Consult platform-specific documentation

## References

- **libnix Architecture**: See test/libnix/README.md
- **Porting Guide**: See PORTING.md
- **Platform APIs**:
  - POSIX: IEEE Std 1003.1-2017
  - Win32: Microsoft Windows SDK Documentation
  - Haiku: https://www.haiku-os.org/docs/api/
  - OS/2: IBM OS/2 Warp Toolkit
  - OpenVMS: HP OpenVMS Documentation

---

**Made portable across 15+ platforms** 🌐
