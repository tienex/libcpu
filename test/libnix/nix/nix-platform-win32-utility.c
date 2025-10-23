/*
 * nix-platform-win32-utility.c
 *
 * Utility syscalls emulation for Windows NT
 *
 * Implements various POSIX/BSD/Linux utility functions:
 * - reboot - reboot the system
 * - sync - synchronize filesystems
 * - swapon/swapoff - swap management
 * - mount/umount - filesystem mounting
 * - getdomainname/setdomainname - domain name operations
 * - ioctl - device I/O control
 * - acct - process accounting
 * - quotactl - disk quota control
 * - syslog - system logging
 *
 * Compatible with Windows NT 3.1 through Windows 11
 */

#if defined(NIX_HOST_WIN32)

#include "nix-platform-win32.h"
#include <windows.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

/* Reboot flags */
#ifndef RB_AUTOBOOT
#define RB_AUTOBOOT    0x01234567  /* Reboot */
#define RB_HALT_SYSTEM 0xCDEF0123  /* Halt */
#define RB_POWER_OFF   0x4321FEDC  /* Power off */
#define RB_SW_SUSPEND  0xD000FCE2  /* Suspend */
#define RB_KEXEC       0x45584543  /* Kexec */
#endif

/* Mount flags */
#ifndef MS_RDONLY
#define MS_RDONLY      1
#define MS_NOSUID      2
#define MS_NODEV       4
#define MS_NOEXEC      8
#define MS_SYNCHRONOUS 16
#define MS_REMOUNT     32
#define MS_MANDLOCK    64
#define MS_DIRSYNC     128
#define MS_NOATIME     1024
#define MS_NODIRATIME  2048
#define MS_BIND        4096
#endif

/* Unmount flags */
#ifndef MNT_FORCE
#define MNT_FORCE  1
#define MNT_DETACH 2
#define MNT_EXPIRE 4
#endif

/* Syslog priority */
#ifndef LOG_EMERG
#define LOG_EMERG   0  /* System is unusable */
#define LOG_ALERT   1  /* Action must be taken immediately */
#define LOG_CRIT    2  /* Critical conditions */
#define LOG_ERR     3  /* Error conditions */
#define LOG_WARNING 4  /* Warning conditions */
#define LOG_NOTICE  5  /* Normal but significant condition */
#define LOG_INFO    6  /* Informational */
#define LOG_DEBUG   7  /* Debug-level messages */
#endif

/* Syslog facility */
#ifndef LOG_KERN
#define LOG_KERN     (0<<3)  /* Kernel messages */
#define LOG_USER     (1<<3)  /* User-level messages */
#define LOG_MAIL     (2<<3)  /* Mail system */
#define LOG_DAEMON   (3<<3)  /* System daemons */
#define LOG_AUTH     (4<<3)  /* Security/authorization */
#define LOG_SYSLOG   (5<<3)  /* Syslog internal */
#define LOG_LPR      (6<<3)  /* Line printer subsystem */
#define LOG_NEWS     (7<<3)  /* Network news subsystem */
#define LOG_UUCP     (8<<3)  /* UUCP subsystem */
#define LOG_CRON     (9<<3)  /* Clock daemon */
#define LOG_AUTHPRIV (10<<3) /* Security/authorization (private) */
#define LOG_FTP      (11<<3) /* FTP daemon */
#define LOG_LOCAL0   (16<<3) /* Local use 0 */
#define LOG_LOCAL1   (17<<3) /* Local use 1 */
#define LOG_LOCAL2   (18<<3) /* Local use 2 */
#define LOG_LOCAL3   (19<<3) /* Local use 3 */
#define LOG_LOCAL4   (20<<3) /* Local use 4 */
#define LOG_LOCAL5   (21<<3) /* Local use 5 */
#define LOG_LOCAL6   (22<<3) /* Local use 6 */
#define LOG_LOCAL7   (23<<3) /* Local use 7 */
#endif

/* Syslog options */
#ifndef LOG_PID
#define LOG_PID    0x01  /* Log process ID */
#define LOG_CONS   0x02  /* Log on console if error */
#define LOG_ODELAY 0x04  /* Delay open until first syslog() */
#define LOG_NDELAY 0x08  /* Don't delay open */
#define LOG_NOWAIT 0x10  /* Don't wait for child processes */
#define LOG_PERROR 0x20  /* Log to stderr as well */
#endif

/*
 * reboot - Reboot the system
 *
 * Reboots, halts, or powers off the system.
 * Requires Administrator privileges.
 */
int nix_platform_win32_reboot(int cmd)
{
	HANDLE hToken;
	TOKEN_PRIVILEGES tkp;

	/* Get a token for this process */
	if (!OpenProcessToken(GetCurrentProcess(),
						  TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
		errno = EPERM;
		return -1;
	}

	/* Get the LUID for the shutdown privilege */
	LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);

	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	/* Enable the shutdown privilege */
	AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);

	if (GetLastError() != ERROR_SUCCESS) {
		CloseHandle(hToken);
		errno = EPERM;
		return -1;
	}

	/* Determine shutdown type */
	UINT flags = 0;
	DWORD reason = SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER;

	switch (cmd) {
	case RB_AUTOBOOT:
		/* Reboot */
		flags = EWX_REBOOT | EWX_FORCE;
		break;

	case RB_HALT_SYSTEM:
		/* Halt (shutdown without power off) */
		flags = EWX_SHUTDOWN | EWX_FORCE;
		break;

	case RB_POWER_OFF:
		/* Power off */
		flags = EWX_POWEROFF | EWX_FORCE;
		break;

	case RB_SW_SUSPEND:
		/* Suspend (not a true reboot, but similar concept) */
		CloseHandle(hToken);
		if (!SetSuspendState(FALSE, TRUE, FALSE)) {
			errno = EINVAL;
			return -1;
		}
		return 0;

	default:
		CloseHandle(hToken);
		errno = EINVAL;
		return -1;
	}

	/* Initiate shutdown */
	if (!ExitWindowsEx(flags, reason)) {
		CloseHandle(hToken);
		errno = EPERM;
		return -1;
	}

	CloseHandle(hToken);
	return 0;
}

/*
 * sync - Synchronize filesystems
 *
 * Flushes filesystem buffers to disk.
 * Note: Windows doesn't have a global sync, so this is a no-op.
 */
void nix_platform_win32_sync(void)
{
	/* Windows doesn't have a global filesystem sync */
	/* Individual files can be synced with FlushFileBuffers */
	/* This is a no-op for compatibility */
}

/*
 * syncfs - Synchronize a filesystem
 *
 * Flushes filesystem buffers for the filesystem containing the file.
 */
int nix_platform_win32_syncfs(int fd)
{
	/* Windows doesn't support per-filesystem sync */
	/* Try to flush the specific file */

	HANDLE handle = (HANDLE)_get_osfhandle(fd);
	if (handle == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}

	if (!FlushFileBuffers(handle)) {
		DWORD err = GetLastError();
		if (err == ERROR_INVALID_HANDLE) {
			errno = EBADF;
		} else if (err == ERROR_ACCESS_DENIED) {
			errno = EINVAL;  /* Not a file (maybe a socket or pipe) */
		} else {
			errno = EIO;
		}
		return -1;
	}

	return 0;
}

/*
 * swapon - Enable swapping on device
 *
 * Windows manages paging automatically, so this is a no-op.
 */
int nix_platform_win32_swapon(const char *path, int swapflags)
{
	(void)path;
	(void)swapflags;

	/* Windows manages virtual memory automatically */
	/* Return ENOSYS to indicate not implemented */
	errno = ENOSYS;
	return -1;
}

/*
 * swapoff - Disable swapping on device
 *
 * Windows manages paging automatically, so this is a no-op.
 */
int nix_platform_win32_swapoff(const char *path)
{
	(void)path;

	/* Windows manages virtual memory automatically */
	errno = ENOSYS;
	return -1;
}

/*
 * mount - Mount filesystem
 *
 * Mounts a filesystem at the specified mount point.
 * On Windows, this is very limited - we can only map network drives.
 */
int nix_platform_win32_mount(
	const char *source,
	const char *target,
	const char *filesystemtype,
	unsigned long mountflags,
	const void *data
)
{
	(void)filesystemtype;
	(void)mountflags;
	(void)data;

	if (!source || !target) {
		errno = EINVAL;
		return -1;
	}

	/* Try to map as network drive if target is a drive letter */
	if (strlen(target) >= 2 && target[1] == ':') {
		char drive[3] = {target[0], ':', '\0'};
		NETRESOURCEA nr;

		memset(&nr, 0, sizeof(nr));
		nr.dwType = RESOURCETYPE_DISK;
		nr.lpLocalName = drive;
		nr.lpRemoteName = (char *)source;

		DWORD result = WNetAddConnection2A(&nr, NULL, NULL, 0);

		if (result != NO_ERROR) {
			if (result == ERROR_ALREADY_ASSIGNED) {
				errno = EBUSY;
			} else if (result == ERROR_BAD_NET_NAME) {
				errno = ENOENT;
			} else if (result == ERROR_ACCESS_DENIED) {
				errno = EACCES;
			} else {
				errno = EINVAL;
			}
			return -1;
		}

		return 0;
	}

	/* Cannot mount regular filesystems on Windows */
	errno = ENOSYS;
	return -1;
}

/*
 * umount - Unmount filesystem
 *
 * Unmounts a filesystem at the specified mount point.
 */
int nix_platform_win32_umount(const char *target)
{
	if (!target) {
		errno = EINVAL;
		return -1;
	}

	/* Try to disconnect network drive */
	if (strlen(target) >= 2 && target[1] == ':') {
		char drive[3] = {target[0], ':', '\0'};

		DWORD result = WNetCancelConnection2A(drive, 0, FALSE);

		if (result != NO_ERROR) {
			if (result == ERROR_NOT_CONNECTED) {
				errno = EINVAL;
			} else if (result == ERROR_OPEN_FILES) {
				errno = EBUSY;
			} else {
				errno = EINVAL;
			}
			return -1;
		}

		return 0;
	}

	errno = EINVAL;
	return -1;
}

/*
 * umount2 - Unmount filesystem with flags
 */
int nix_platform_win32_umount2(const char *target, int flags)
{
	BOOL force = (flags & MNT_FORCE) ? TRUE : FALSE;

	if (!target) {
		errno = EINVAL;
		return -1;
	}

	/* Try to disconnect network drive */
	if (strlen(target) >= 2 && target[1] == ':') {
		char drive[3] = {target[0], ':', '\0'};

		DWORD result = WNetCancelConnection2A(drive, 0, force);

		if (result != NO_ERROR) {
			if (result == ERROR_NOT_CONNECTED) {
				errno = EINVAL;
			} else if (result == ERROR_OPEN_FILES) {
				errno = EBUSY;
			} else {
				errno = EINVAL;
			}
			return -1;
		}

		return 0;
	}

	errno = EINVAL;
	return -1;
}

/*
 * getdomainname - Get domain name
 *
 * Gets the NIS/YP domain name.
 * On Windows, returns the DNS domain name.
 */
int nix_platform_win32_getdomainname(char *name, size_t len)
{
	if (!name || len == 0) {
		errno = EINVAL;
		return -1;
	}

	/* Get computer name info */
	DWORD size = (DWORD)len;

	if (!GetComputerNameExA(ComputerNameDnsDomain, name, &size)) {
		DWORD err = GetLastError();

		if (err == ERROR_MORE_DATA) {
			errno = EINVAL;  /* Buffer too small */
		} else {
			/* No domain name set */
			name[0] = '\0';
		}
	}

	return 0;
}

/*
 * setdomainname - Set domain name
 *
 * Sets the NIS/YP domain name.
 * Not supported on Windows - requires domain controller.
 */
int nix_platform_win32_setdomainname(const char *name, size_t len)
{
	(void)name;
	(void)len;

	/* Cannot set domain name on Windows without domain controller */
	errno = EPERM;
	return -1;
}

/*
 * ioctl - Device I/O control
 *
 * Performs device-specific control operations.
 * This is a simplified implementation supporting common operations.
 */
int nix_platform_win32_ioctl(int fd, unsigned long request, ...)
{
	va_list args;
	va_start(args, request);

	HANDLE handle = (HANDLE)_get_osfhandle(fd);
	if (handle == INVALID_HANDLE_VALUE) {
		va_end(args);
		errno = EBADF;
		return -1;
	}

	int result = -1;

	/* Common ioctl requests */
	switch (request) {
	case 0x5401:  /* TCGETS - get terminal attributes */
	case 0x5402:  /* TCSETS - set terminal attributes */
	case 0x5403:  /* TCSETSW - set terminal attributes, wait */
	case 0x5404:  /* TCSETSF - set terminal attributes, flush */
		/* Terminal ioctls not fully supported on Windows */
		errno = ENOTTY;
		result = -1;
		break;

	case 0x5413:  /* TIOCGWINSZ - get window size */
	{
		struct {
			unsigned short ws_row;
			unsigned short ws_col;
			unsigned short ws_xpixel;
			unsigned short ws_ypixel;
		} *winsize = va_arg(args, void *);

		/* Get console screen buffer info */
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		if (GetConsoleScreenBufferInfo(handle, &csbi)) {
			winsize->ws_row = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
			winsize->ws_col = csbi.srWindow.Right - csbi.srWindow.Left + 1;
			winsize->ws_xpixel = 0;
			winsize->ws_ypixel = 0;
			result = 0;
		} else {
			errno = ENOTTY;
			result = -1;
		}
		break;
	}

	case 0x5414:  /* TIOCSWINSZ - set window size */
		/* Setting window size not supported */
		errno = ENOTTY;
		result = -1;
		break;

	case 0x541B:  /* FIONREAD - get number of bytes available */
	{
		int *bytes = va_arg(args, int *);
		DWORD available = 0;

		if (PeekNamedPipe(handle, NULL, 0, NULL, &available, NULL)) {
			*bytes = (int)available;
			result = 0;
		} else {
			errno = EINVAL;
			result = -1;
		}
		break;
	}

	case 0x5421:  /* FIONBIO - set non-blocking I/O */
	{
		int *nonblock = va_arg(args, int *);
		/* Non-blocking I/O flags tracked separately */
		/* This is a simplified stub */
		(void)nonblock;
		result = 0;
		break;
	}

	default:
		/* Unsupported ioctl */
		errno = EINVAL;
		result = -1;
		break;
	}

	va_end(args);
	return result;
}

/*
 * acct - Enable/disable process accounting
 *
 * Enables or disables process accounting.
 * Not supported on Windows.
 */
int nix_platform_win32_acct(const char *filename)
{
	(void)filename;

	/* Process accounting not supported on Windows */
	errno = ENOSYS;
	return -1;
}

/*
 * quotactl - Disk quota control
 *
 * Manipulates disk quotas.
 * Windows has quota support but different API.
 */
int nix_platform_win32_quotactl(
	int cmd,
	const char *special,
	int id,
	void *addr
)
{
	(void)cmd;
	(void)special;
	(void)id;
	(void)addr;

	/* Disk quotas use different API on Windows */
	/* Would require IQuotaControl COM interface */
	errno = ENOSYS;
	return -1;
}

/*
 * Syslog implementation
 */

static int g_syslog_option = 0;
static int g_syslog_facility = LOG_USER;
static char g_syslog_ident[256] = {0};
static HANDLE g_event_source = NULL;

/*
 * openlog - Open connection to syslog
 *
 * Opens a connection to the system logger.
 */
void nix_platform_win32_openlog(const char *ident, int option, int facility)
{
	g_syslog_option = option;
	g_syslog_facility = facility;

	if (ident) {
		strncpy(g_syslog_ident, ident, sizeof(g_syslog_ident) - 1);
		g_syslog_ident[sizeof(g_syslog_ident) - 1] = '\0';
	} else {
		g_syslog_ident[0] = '\0';
	}

	/* Open Windows Event Log */
	if (!(option & LOG_NDELAY)) {
		/* Delay opening until first syslog() call */
		return;
	}

	if (!g_event_source) {
		g_event_source = RegisterEventSourceA(NULL, g_syslog_ident[0] ? g_syslog_ident : "Application");
	}
}

/*
 * syslog - Write message to syslog
 *
 * Writes a message to the system logger.
 */
void nix_platform_win32_syslog(int priority, const char *format, ...)
{
	va_list args;
	char message[1024];

	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);

	/* Determine Windows event type */
	WORD event_type;
	int level = priority & 0x07;

	switch (level) {
	case LOG_EMERG:
	case LOG_ALERT:
	case LOG_CRIT:
	case LOG_ERR:
		event_type = EVENTLOG_ERROR_TYPE;
		break;
	case LOG_WARNING:
		event_type = EVENTLOG_WARNING_TYPE;
		break;
	default:
		event_type = EVENTLOG_INFORMATION_TYPE;
		break;
	}

	/* Open event source if not already open */
	if (!g_event_source) {
		g_event_source = RegisterEventSourceA(NULL, g_syslog_ident[0] ? g_syslog_ident : "Application");
	}

	if (g_event_source) {
		const char *messages[1] = {message};
		ReportEventA(
			g_event_source,
			event_type,
			0,  /* Category */
			0,  /* Event ID */
			NULL,  /* User SID */
			1,  /* Number of strings */
			0,  /* Data size */
			messages,
			NULL  /* Data */
		);
	}

	/* Also log to stderr if LOG_PERROR */
	if (g_syslog_option & LOG_PERROR) {
		if (g_syslog_ident[0]) {
			fprintf(stderr, "%s: %s\n", g_syslog_ident, message);
		} else {
			fprintf(stderr, "%s\n", message);
		}
	}
}

/*
 * closelog - Close connection to syslog
 *
 * Closes the connection to the system logger.
 */
void nix_platform_win32_closelog(void)
{
	if (g_event_source) {
		DeregisterEventSource(g_event_source);
		g_event_source = NULL;
	}

	g_syslog_ident[0] = '\0';
	g_syslog_option = 0;
	g_syslog_facility = LOG_USER;
}

/*
 * setlogmask - Set syslog priority mask
 *
 * Sets the log priority mask.
 */
int nix_platform_win32_setlogmask(int mask)
{
	static int current_mask = 0xFF;  /* All priorities enabled */
	int old_mask = current_mask;

	if (mask != 0) {
		current_mask = mask;
	}

	return old_mask;
}

#endif /* NIX_HOST_WIN32 */
