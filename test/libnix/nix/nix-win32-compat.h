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
NIX_WIN32_ENOSYS_STUB (utimes, (char const *path, struct timeval const *times))

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

#endif  /* _WIN32 */

#endif  /* !__nix_win32_compat_h */
