/** @file
  Win32 host-compatibility shim for libnix's portable core.

  The generic nix-*.c layer is written against POSIX host primitives. Win32 (MinGW) provides
  the basic file ops (open/read/write/close/lseek/fstat/stat/chmod/...) but lacks a number of
  others. This header -- included only under _WIN32, from nix-base.h, so every nix source sees
  it -- supplies the gap: real implementations where the semantics map cleanly (fsync, pread,
  pwrite, ftruncate, truncate, lstat) and ENOSYS stubs for the ones with no win32 analog in the
  file-op core (ownership/permission/link/symlink/special-file ops). It keeps the full nix_ API
  present on win32 -- those syscalls return ENOSYS rather than failing to link -- and is the
  place to grow real win32 implementations (e.g. ACL-based chmod) incrementally.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/
#ifndef __nix_win32_compat_h
#define __nix_win32_compat_h

#ifdef _WIN32

#include <io.h>
#include <direct.h>       /* _mkdir / _chdir / _rmdir / _getcwd */
#include <process.h>      /* _getpid */
#include <errno.h>
#include <stdio.h>        /* SEEK_SET / SEEK_CUR */
#include <fcntl.h>        /* _O_WRONLY */
#include <sys/types.h>
#include <sys/stat.h>

/* win32 mkdir takes no mode argument; the file-op core's mode is advisory there. */
#define mkdir(path, mode) _mkdir (path)

#ifndef ENOSYS
#define ENOSYS 40
#endif

/* POSIX id types win32 lacks (libnix only moves these between guest and host as integers). */
typedef int uid_t;
typedef int gid_t;

/* Mode bits absent from win32's <sys/stat.h> (no symlinks/sockets/set-id in its stat mode).
   The standard POSIX values -- win32 stat never sets them, so the conversions just don't match. */
#ifndef S_IFLNK
#define S_IFLNK  0120000
#endif
#ifndef S_IFSOCK
#define S_IFSOCK 0140000
#endif
#ifndef S_ISUID
#define S_ISUID  04000
#endif
#ifndef S_ISGID
#define S_ISGID  02000
#endif
#ifndef S_ISVTX
#define S_ISVTX  01000
#endif

/* Cleanly mappable primitives. */
static __inline int
fsync (int fd)
{
  return _commit (fd);
}

/* ftruncate / truncate are provided by MinGW-w64. */

static __inline long
pread (int fd, void *buf, unsigned int n, off_t off)
{
  long cur = _lseek (fd, 0, SEEK_CUR);
  long r;
  if (cur < 0)
    return -1;
  if (_lseek (fd, (long) off, SEEK_SET) < 0)
    return -1;
  r = (long) _read (fd, buf, n);
  _lseek (fd, cur, SEEK_SET);              /* leave the offset undisturbed, as pread requires */
  return r;
}

static __inline long
pwrite (int fd, void const *buf, unsigned int n, off_t off)
{
  long cur = _lseek (fd, 0, SEEK_CUR);
  long r;
  if (cur < 0)
    return -1;
  if (_lseek (fd, (long) off, SEEK_SET) < 0)
    return -1;
  r = (long) _write (fd, buf, n);
  _lseek (fd, cur, SEEK_SET);
  return r;
}

/* No symbolic links on win32 in the POSIX sense: lstat is just stat. */
static __inline int
lstat (char const *path, struct stat *sb)
{
  return stat (path, sb);
}

/* No win32 file-op-core analog: report unsupported. Real implementations (ACL-based perms,
   junctions for symlinks, ...) can replace these as the win32 surface grows. */
#define NIX_WIN32_ENOSYS_STUB(name, params) \
  static __inline int name params { errno = ENOSYS; return -1; }

NIX_WIN32_ENOSYS_STUB (fchmod, (int fd, int mode))
NIX_WIN32_ENOSYS_STUB (fchown, (int fd, uid_t uid, gid_t gid))
NIX_WIN32_ENOSYS_STUB (chown,  (char const *path, uid_t uid, gid_t gid))
NIX_WIN32_ENOSYS_STUB (lchown, (char const *path, uid_t uid, gid_t gid))
NIX_WIN32_ENOSYS_STUB (symlink,(char const *a, char const *b))
NIX_WIN32_ENOSYS_STUB (link,   (char const *a, char const *b))
NIX_WIN32_ENOSYS_STUB (flock,  (int fd, int op))
NIX_WIN32_ENOSYS_STUB (mknod,  (char const *path, int mode, dev_t dev))
NIX_WIN32_ENOSYS_STUB (mkfifo, (char const *path, int mode))
NIX_WIN32_ENOSYS_STUB (fchdir, (int fd))
NIX_WIN32_ENOSYS_STUB (chroot, (char const *path))

struct timeval;   /* declared by <sys/time.h>; a pointer param needs only the tag */
struct timezone;
NIX_WIN32_ENOSYS_STUB (utimes, (char const *path, struct timeval const *times))
/* gettimeofday + time + nanosleep are provided by MinGW; setting the wall clock is not in the
   file-op-core surface yet. */
NIX_WIN32_ENOSYS_STUB (settimeofday, (struct timeval const *tv, struct timezone const *tz))

static __inline long
fpathconf (int fd, int name)
{
  (void) fd; (void) name;
  errno = ENOSYS;
  return -1;
}

static __inline long
pathconf (char const *path, int name)
{
  (void) path; (void) name;
  errno = ENOSYS;
  return -1;
}

static __inline int
readlink (char const *path, char *buf, int bufsiz)
{
  (void) path; (void) buf; (void) bufsiz;
  errno = ENOSYS;
  return -1;
}

/* Host identity. Win32 has no POSIX gethostname in <unistd.h> -- the BSD one lives in winsock
   (needs WSAStartup). The machine name is reachable through kernel32's GetComputerNameA with no
   init and no winsock link, so we use that. Declared by hand (matching the real WINBASEAPI
   prototype) to avoid pulling all of <windows.h> into every nix source. */
__declspec (dllimport) int __stdcall GetComputerNameA (char *lpBuffer, unsigned long *nSize);

#ifndef ENAMETOOLONG
#define ENAMETOOLONG 38
#endif

static __inline int
gethostname (char *buf, size_t len)
{
  unsigned long n = (unsigned long) len;
  if (GetComputerNameA (buf, &n))
    return 0;
  errno = ENAMETOOLONG;          /* the only documented GetComputerNameA failure: buffer too small */
  return -1;
}

/* No NIS/YP domain concept on win32: the faithful POSIX representation of "not in a domain" is an
   empty domain name reported successfully (what a non-domain-joined POSIX host returns too). */
static __inline int
getdomainname (char *buf, size_t len)
{
  if (len == 0)
    return 0;
  buf[0] = '\0';
  return 0;
}

/* Setting host/domain identity has no unprivileged win32 analog: report unsupported. */
NIX_WIN32_ENOSYS_STUB (sethostname,   (char const *name, size_t len))
NIX_WIN32_ENOSYS_STUB (setdomainname, (char const *name, size_t len))

/* Process credentials. Win32 has no POSIX numeric uid/gid (its model is SID/token-based), so the
   emulated process runs as one fixed, non-root identity: queries report it, and "setting" to that
   same id succeeds (you are always who you already are) while any other id is EPERM -- the same
   thing an unprivileged POSIX process sees. */
#ifndef NIX_WIN32_UID
#define NIX_WIN32_UID 1000
#endif
#ifndef NIX_WIN32_GID
#define NIX_WIN32_GID 1000
#endif
#ifndef EPERM
#define EPERM 1
#endif

static __inline uid_t getuid  (void) { return NIX_WIN32_UID; }
static __inline uid_t geteuid (void) { return NIX_WIN32_UID; }
static __inline gid_t getgid  (void) { return NIX_WIN32_GID; }
static __inline gid_t getegid (void) { return NIX_WIN32_GID; }

#define NIX_WIN32_SETID_STUB(name, type, fixed)                 \
  static __inline int name (type id)                            \
  { if (id == (fixed)) return 0; errno = EPERM; return -1; }
NIX_WIN32_SETID_STUB (setuid,  uid_t, NIX_WIN32_UID)
NIX_WIN32_SETID_STUB (seteuid, uid_t, NIX_WIN32_UID)
NIX_WIN32_SETID_STUB (setgid,  gid_t, NIX_WIN32_GID)
NIX_WIN32_SETID_STUB (setegid, gid_t, NIX_WIN32_GID)

/* No parent-process concept on win32: report init (1) as the parent. nix-process is otherwise a
   single-process model (fork/exec/wait are ENOSYS). PRIO_PROCESS is referenced by nix_nice's
   no-HAVE_NICE path. */
static __inline pid_t getppid (void) { return 1; }
#ifndef PRIO_PROCESS
#define PRIO_PROCESS 0
#endif

/* No inter-process signal delivery on win32. The guest's own signal dispositions are still fully
   managed in nix-signal's tables (sigaction/sigprocmask/...); only raising a real host signal at
   another process is unsupported. */
NIX_WIN32_ENOSYS_STUB (kill, (pid_t pid, int sig))

/* Process groups / sessions: win32 has no analog. Model the emulated process as a single
   self-led group and session -- it is its own group/session leader (its own pid). */
static __inline pid_t getpgrp (void)             { return (pid_t) _getpid (); }
static __inline pid_t getpgid (pid_t pid)        { (void) pid; return (pid_t) _getpid (); }
static __inline pid_t getsid  (pid_t pid)        { (void) pid; return (pid_t) _getpid (); }
static __inline pid_t setsid  (void)             { return (pid_t) _getpid (); }
static __inline int   setpgid (pid_t p, pid_t g) { (void) p; (void) g; return 0; }

/* The guest must never reboot the host. */
NIX_WIN32_ENOSYS_STUB (reboot, (int howto))

/* Memory protection. nix_mprotect forwards POSIX-style prot bits (PROT_READ=1/WRITE=2/EXEC=4,
   matching NIX_PROT_*) straight to the host, so the win32 mprotect must translate that bit
   combination into win32's combinatorial PAGE_* protection enum and call VirtualProtect.
   Declared by hand (matching the real WINBASEAPI prototype) to keep <windows.h> out. */
__declspec (dllimport) int __stdcall VirtualProtect (void *lpAddress, size_t dwSize,
                                                     unsigned long flNewProtect,
                                                     unsigned long *lpflOldProtect);

#ifndef PAGE_NOACCESS
#define PAGE_NOACCESS          0x01
#define PAGE_READONLY          0x02
#define PAGE_READWRITE         0x04
#define PAGE_EXECUTE           0x10
#define PAGE_EXECUTE_READ      0x20
#define PAGE_EXECUTE_READWRITE 0x40
#endif

#ifndef EACCES
#define EACCES 13
#endif

static __inline int
mprotect (void *addr, size_t len, int prot)
{
  unsigned long newp, oldp;
  switch (prot & 7)                                  /* READ|WRITE|EXEC, as forwarded by nix_mprotect */
    {
    case 0:                          newp = PAGE_NOACCESS;          break;
    case 1:                          newp = PAGE_READONLY;          break;
    case 2: case 3:                  newp = PAGE_READWRITE;         break;  /* write implies read on win32 */
    case 4:                          newp = PAGE_EXECUTE;           break;
    case 5:                          newp = PAGE_EXECUTE_READ;      break;
    default:                         newp = PAGE_EXECUTE_READWRITE; break;  /* 6, 7 */
    }
  if (VirtualProtect (addr, len, newp, &oldp))
    return 0;
  errno = EACCES;                                    /* the documented VirtualProtect failure mode */
  return -1;
}

#endif  /* _WIN32 */

#endif  /* !__nix_win32_compat_h */
