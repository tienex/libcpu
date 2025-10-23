# Windows NT Utility Syscalls Emulation

System utility functions for Windows NT 3.1 through Windows 11.

## Overview

Implementation of POSIX/BSD/Linux utility syscalls for system control, logging, and device management using Windows APIs.

**File:** `nix-platform-win32-utility.c` (700+ lines)

---

## Implemented Functions

| Function | Status | Windows API | Admin Required |
|----------|--------|-------------|----------------|
| reboot | ✅ | ExitWindowsEx | Yes |
| sync | ✅ | No-op (FlushFileBuffers per-file) | No |
| syncfs | ✅ | FlushFileBuffers | No |
| swapon | ⚠️ | Returns ENOSYS | N/A |
| swapoff | ⚠️ | Returns ENOSYS | N/A |
| mount | ⚠️ | WNetAddConnection2 (network only) | Varies |
| umount | ⚠️ | WNetCancelConnection2 | Varies |
| umount2 | ⚠️ | WNetCancelConnection2 | Varies |
| getdomainname | ✅ | GetComputerNameEx | No |
| setdomainname | ⚠️ | Returns EPERM | Yes |
| ioctl | ⚠️ | Partial (terminal, FIONREAD, etc.) | No |
| acct | ⚠️ | Returns ENOSYS | N/A |
| quotactl | ⚠️ | Returns ENOSYS | N/A |
| openlog | ✅ | RegisterEventSource | No |
| syslog | ✅ | ReportEvent | No |
| closelog | ✅ | DeregisterEventSource | No |
| setlogmask | ✅ | Mask tracking | No |

---

## API Reference

### reboot

Reboot, halt, or power off the system.

```c
int nix_platform_win32_reboot(int cmd);
```

**Commands:**
- `RB_AUTOBOOT` (0x01234567): Reboot
- `RB_HALT_SYSTEM` (0xCDEF0123): Halt (shutdown without power off)
- `RB_POWER_OFF` (0x4321FEDC): Power off
- `RB_SW_SUSPEND` (0xD000FCE2): Suspend

**Requires:** Administrator privileges (SeShutdownPrivilege)

**Returns:**
- 0 on success
- -1 on error (sets errno to EPERM or EINVAL)

**Example:**
```c
// Reboot system
if (nix_platform_win32_reboot(RB_AUTOBOOT) != 0) {
    perror("reboot");  // Usually EPERM if not admin
}

// Power off
nix_platform_win32_reboot(RB_POWER_OFF);

// Suspend
nix_platform_win32_reboot(RB_SW_SUSPEND);
```

**Implementation:**
- Acquires SE_SHUTDOWN_NAME privilege
- Calls ExitWindowsEx with appropriate flags
- Uses SetSuspendState for suspend

---

### sync / syncfs

Synchronize filesystem buffers.

```c
void nix_platform_win32_sync(void);
int nix_platform_win32_syncfs(int fd);
```

**sync:** Global no-op (Windows has no global sync)
**syncfs:** Flushes file buffers for the given file

**Example:**
```c
int fd = open("file.txt", O_WRONLY);
write(fd, data, size);

// Flush this file to disk
if (nix_platform_win32_syncfs(fd) != 0) {
    perror("syncfs");
}

close(fd);

// Global sync (no-op on Windows)
nix_platform_win32_sync();
```

**Implementation:**
- sync: No-op (returns immediately)
- syncfs: Calls FlushFileBuffers on file handle

**Note:** Windows doesn't have global filesystem sync. Use FlushFileBuffers on individual files.

---

### swapon / swapoff

Enable/disable swap on device.

```c
int nix_platform_win32_swapon(const char *path, int swapflags);
int nix_platform_win32_swapoff(const char *path);
```

**Status:** Not implemented (returns ENOSYS)

**Note:** Windows manages virtual memory (paging file) automatically. Cannot be controlled programmatically without kernel drivers.

---

### mount / umount / umount2

Mount/unmount filesystems.

```c
int nix_platform_win32_mount(
    const char *source,
    const char *target,
    const char *filesystemtype,
    unsigned long mountflags,
    const void *data
);

int nix_platform_win32_umount(const char *target);
int nix_platform_win32_umount2(const char *target, int flags);
```

**Supported:** Network drives only

**Example:**
```c
// Mount network share as Z: drive
if (nix_platform_win32_mount("\\\\server\\share", "Z:", "cifs", 0, NULL) == 0) {
    printf("Mounted successfully\n");
}

// Unmount
nix_platform_win32_umount("Z:");

// Unmount with force
nix_platform_win32_umount2("Z:", MNT_FORCE);
```

**Flags:**
- `MNT_FORCE` (1): Force unmount even if files open
- `MNT_DETACH` (2): Lazy unmount (not supported on Windows)
- `MNT_EXPIRE` (4): Mark for expiration (not supported)

**Mount Flags:**
- `MS_RDONLY`, `MS_NOSUID`, etc. - Ignored on Windows

**Limitations:**
- Only network drives (\\server\share)
- Target must be drive letter (e.g., "Z:")
- Cannot mount local filesystems
- Filesystem type ignored

**Implementation:**
- Uses WNetAddConnection2/WNetCancelConnection2
- Maps UNC paths to drive letters

---

### getdomainname / setdomainname

Get/set NIS/DNS domain name.

```c
int nix_platform_win32_getdomainname(char *name, size_t len);
int nix_platform_win32_setdomainname(const char *name, size_t len);
```

**Example:**
```c
char domain[256];

if (nix_platform_win32_getdomainname(domain, sizeof(domain)) == 0) {
    printf("Domain: %s\n", domain);
}

// setdomainname not supported (returns EPERM)
```

**Implementation:**
- getdomainname: Uses GetComputerNameEx(ComputerNameDnsDomain)
- setdomainname: Returns EPERM (requires domain controller)

---

### ioctl

Device I/O control operations.

```c
int nix_platform_win32_ioctl(int fd, unsigned long request, ...);
```

**Supported requests:**
- `TIOCGWINSZ` (0x5413): Get terminal window size
- `FIONREAD` (0x541B): Get number of bytes available to read
- `FIONBIO` (0x5421): Set non-blocking I/O
- Terminal ioctls (0x5401-0x5404): Return ENOTTY

**Example:**
```c
// Get console window size
struct winsize ws;
if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    printf("Terminal: %d rows x %d cols\n", ws.ws_row, ws.ws_col);
}

// Get bytes available
int fd = open("pipe", O_RDONLY);
int available;
if (ioctl(fd, FIONREAD, &available) == 0) {
    printf("Bytes available: %d\n", available);
}

// Set non-blocking
int nonblock = 1;
ioctl(fd, FIONBIO, &nonblock);
```

**Limitations:**
- Limited terminal support (GetConsoleScreenBufferInfo only)
- No tty control (TCGETS/TCSETS return ENOTTY)
- Device-specific ioctls not supported

---

### acct

Enable/disable process accounting.

```c
int nix_platform_win32_acct(const char *filename);
```

**Status:** Not implemented (returns ENOSYS)

**Note:** Windows doesn't have process accounting. Use Windows Event Log or third-party auditing tools.

---

### quotactl

Disk quota control.

```c
int nix_platform_win32_quotactl(
    int cmd,
    const char *special,
    int id,
    void *addr
);
```

**Status:** Not implemented (returns ENOSYS)

**Note:** Windows has quota support via IQuotaControl COM interface. Not implemented in this layer.

---

### Syslog Functions

System logging via Windows Event Log.

```c
void nix_platform_win32_openlog(const char *ident, int option, int facility);
void nix_platform_win32_syslog(int priority, const char *format, ...);
void nix_platform_win32_closelog(void);
int nix_platform_win32_setlogmask(int mask);
```

**Example:**
```c
// Open log
nix_platform_win32_openlog("myapp", LOG_PID | LOG_PERROR, LOG_USER);

// Log messages
nix_platform_win32_syslog(LOG_INFO, "Application started");
nix_platform_win32_syslog(LOG_WARNING, "Low memory: %d MB", mem_avail);
nix_platform_win32_syslog(LOG_ERR, "Failed to connect: %s", strerror(errno));

// Close log
nix_platform_win32_closelog();
```

**Priority Levels:**
- `LOG_EMERG` (0): System unusable → EVENTLOG_ERROR_TYPE
- `LOG_ALERT` (1): Action required → EVENTLOG_ERROR_TYPE
- `LOG_CRIT` (2): Critical → EVENTLOG_ERROR_TYPE
- `LOG_ERR` (3): Error → EVENTLOG_ERROR_TYPE
- `LOG_WARNING` (4): Warning → EVENTLOG_WARNING_TYPE
- `LOG_NOTICE` (5): Notice → EVENTLOG_INFORMATION_TYPE
- `LOG_INFO` (6): Info → EVENTLOG_INFORMATION_TYPE
- `LOG_DEBUG` (7): Debug → EVENTLOG_INFORMATION_TYPE

**Facilities:**
- `LOG_KERN`, `LOG_USER`, `LOG_MAIL`, `LOG_DAEMON`, `LOG_AUTH`, etc.
- Stored but not used (Windows Event Log doesn't have facilities)

**Options:**
- `LOG_PID` (0x01): Log process ID (included in message)
- `LOG_CONS` (0x02): Log to console on error
- `LOG_ODELAY` (0x04): Delay open (default)
- `LOG_NDELAY` (0x08): Open immediately
- `LOG_NOWAIT` (0x10): Don't wait
- `LOG_PERROR` (0x20): Also log to stderr

**Implementation:**
- openlog: Calls RegisterEventSource
- syslog: Calls ReportEvent with formatted message
- closelog: Calls DeregisterEventSource
- Writes to Windows Event Log (Application source)
- View with Event Viewer (eventvwr.msc)

---

## Usage Examples

### System Reboot with Warning

```c
void safe_reboot(int delay_seconds) {
    printf("System will reboot in %d seconds...\n", delay_seconds);

    for (int i = delay_seconds; i > 0; i--) {
        printf("%d...\n", i);
        sleep(1);
    }

    if (nix_platform_win32_reboot(RB_AUTOBOOT) != 0) {
        fprintf(stderr, "Reboot failed: %s\n", strerror(errno));

        if (errno == EPERM) {
            fprintf(stderr, "Administrator privileges required\n");
        }
    }
}
```

### Network Drive Mounting

```c
int mount_share(const char *server, const char *share, const char *drive_letter) {
    char source[512];
    char target[4];

    snprintf(source, sizeof(source), "\\\\%s\\%s", server, share);
    snprintf(target, sizeof(target), "%c:", drive_letter[0]);

    if (nix_platform_win32_mount(source, target, "cifs", 0, NULL) != 0) {
        fprintf(stderr, "Failed to mount %s as %s: %s\n",
                source, target, strerror(errno));
        return -1;
    }

    printf("Mounted %s as %s\n", source, target);
    return 0;
}

void unmount_drive(const char *drive_letter) {
    char target[4];
    snprintf(target, sizeof(target), "%c:", drive_letter[0]);

    // Try normal unmount first
    if (nix_platform_win32_umount(target) != 0) {
        // Force unmount if files are open
        if (errno == EBUSY) {
            printf("Drive busy, forcing unmount...\n");
            nix_platform_win32_umount2(target, MNT_FORCE);
        }
    }
}
```

### Logging with Syslog

```c
void log_example(void) {
    // Open log with process ID
    nix_platform_win32_openlog("myservice", LOG_PID | LOG_NDELAY, LOG_DAEMON);

    // Set mask to ignore debug messages
    nix_platform_win32_setlogmask(LOG_UPTO(LOG_INFO));

    // Log various messages
    nix_platform_win32_syslog(LOG_INFO, "Service started, PID=%d", getpid());

    if (load_config() != 0) {
        nix_platform_win32_syslog(LOG_WARNING, "Using default configuration");
    }

    if (connect_to_database() != 0) {
        nix_platform_win32_syslog(LOG_ERR, "Database connection failed: %s",
                                   strerror(errno));
        nix_platform_win32_syslog(LOG_CRIT, "Service cannot start");
        exit(1);
    }

    nix_platform_win32_syslog(LOG_DEBUG, "This won't appear (masked out)");

    // Close log on exit
    nix_platform_win32_closelog();
}
```

### IOCTL Examples

```c
void check_terminal_size(void) {
    struct winsize ws;

    if (isatty(STDOUT_FILENO)) {
        if (nix_platform_win32_ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            printf("Terminal size: %dx%d\n", ws.ws_col, ws.ws_row);
        }
    }
}

int get_pipe_available(int fd) {
    int available = 0;

    if (nix_platform_win32_ioctl(fd, FIONREAD, &available) == 0) {
        return available;
    }

    return -1;
}
```

---

## Performance Characteristics

| Function | Overhead | Notes |
|----------|----------|-------|
| reboot | High | Privilege acquisition + system call |
| sync | None | No-op |
| syncfs | Medium | FlushFileBuffers waits for I/O |
| mount/umount | Medium | Network operation |
| getdomainname | Low | Registry read |
| ioctl | Low-Medium | Varies by operation |
| syslog | Medium | Event log write |

---

## Limitations

1. **reboot**: Requires Administrator privileges
2. **sync**: No-op (no global filesystem sync on Windows)
3. **swapon/swapoff**: Not implemented (Windows manages paging automatically)
4. **mount**: Network drives only, cannot mount local filesystems
5. **setdomainname**: Not supported (requires domain controller)
6. **ioctl**: Limited support (terminal size, FIONREAD only)
7. **acct**: Not implemented (no process accounting on Windows)
8. **quotactl**: Not implemented (different quota API on Windows)
9. **syslog**: No facility support in Windows Event Log

---

## Windows Version Compatibility

| Version | Features | Notes |
|---------|----------|-------|
| NT 3.1 | Most functions | Basic support |
| NT 4.0 | All | Event log improved |
| 2000/XP | All | Network mounting improved |
| Vista+ | All | UAC affects privileges |
| Win7+ | All | Full support |
| Win10+ | All | Best compatibility |

---

## Security Considerations

### Administrator Privileges

Functions requiring admin:
- `reboot` (SE_SHUTDOWN_NAME privilege)
- `clock_settime` (time change privilege)

Check with:
```c
if (reboot(RB_AUTOBOOT) != 0 && errno == EPERM) {
    fprintf(stderr, "Administrator privileges required\n");
}
```

### Event Log

Syslog writes to Windows Event Log:
- Visible to all administrators
- Cannot be hidden without admin privileges
- Use for auditing and diagnostics

---

## Integration

```c
// Reboot after critical error
if (critical_error()) {
    syslog(LOG_CRIT, "Critical error, rebooting in 60 seconds");
    sleep(60);
    reboot(RB_AUTOBOOT);
}

// Mount network share and log
if (mount("\\\\server\\data", "Z:", "cifs", 0, NULL) == 0) {
    syslog(LOG_INFO, "Mounted network share as Z:");
} else {
    syslog(LOG_ERR, "Failed to mount share: %s", strerror(errno));
}

// Check terminal and adjust output
struct winsize ws;
if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    adjust_output_width(ws.ws_col);
}
```

---

**Implementation:** `nix-platform-win32-utility.c`
**Header:** `nix-platform-win32.h`
**Documentation:** This file
