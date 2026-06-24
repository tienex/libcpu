CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(nix)

INCLUDE(FindThreads)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckLibraryExists)
INCLUDE(CheckFunctionExists)

CHECK_INCLUDE_FILES(sys/param.h HAVE_SYS_PARAM_H)
CHECK_INCLUDE_FILES(sys/event.h HAVE_SYS_EVENT_H)
CHECK_INCLUDE_FILES(sys/filio.h HAVE_SYS_FILIO_H)
CHECK_INCLUDE_FILES(sys/mkdev.h HAVE_SYS_MKDEV_H)
CHECK_INCLUDE_FILES("stdio.h;sys/mnttab.h" HAVE_SYS_MNTTAB_H)
CHECK_INCLUDE_FILES(sys/mount.h HAVE_SYS_MOUNT_H)
CHECK_INCLUDE_FILES(sys/vfs.h HAVE_SYS_VFS_H)
CHECK_INCLUDE_FILES(sys/statvfs.h HAVE_SYS_STATVFS_H)
CHECK_INCLUDE_FILES(sys/statfs.h HAVE_SYS_STATFS_H)
CHECK_INCLUDE_FILES(bitstring.h HAVE_BITSTRING_H)
CHECK_INCLUDE_FILES(pthread.h HAVE_PTHREAD_H)
CHECK_INCLUDE_FILES(sched.h HAVE_SCHED_H)
CHECK_INCLUDE_FILES(ucred.h HAVE_UCRED_H)
CHECK_INCLUDE_FILES(features.h HAVE_FEATURES_H)

# XXX check librt for clock_XXX.

CHECK_FUNCTION_EXISTS(adjtime HAVE_ADJTIME)
CHECK_FUNCTION_EXISTS(adjfreq HAVE_ADJFREQ)
CHECK_FUNCTION_EXISTS(chflags HAVE_CHFLAGS)
CHECK_FUNCTION_EXISTS(fchflags HAVE_FCHFLAGS)
CHECK_FUNCTION_EXISTS(fdatasync HAVE_FDATASYNC)
CHECK_FUNCTION_EXISTS(clock_gettime HAVE_CLOCK_GETTIME)
CHECK_FUNCTION_EXISTS(clock_getres HAVE_CLOCK_GETRES)
CHECK_FUNCTION_EXISTS(clock_settime HAVE_CLOCK_SETTIME)
CHECK_FUNCTION_EXISTS(revoke HAVE_REVOKE)
CHECK_FUNCTION_EXISTS(acct HAVE_ACCT)
CHECK_FUNCTION_EXISTS(gethostid HAVE_GETHOSTID)
CHECK_FUNCTION_EXISTS(sethostid HAVE_SETHOSTID)
CHECK_FUNCTION_EXISTS(issetugid HAVE_ISSETUGID)
CHECK_FUNCTION_EXISTS(getreuid HAVE_GETREUID)
CHECK_FUNCTION_EXISTS(getregid HAVE_GETREGID)
CHECK_FUNCTION_EXISTS(getresuid HAVE_GETRESUID)
CHECK_FUNCTION_EXISTS(getresgid HAVE_GETRESGID)
CHECK_FUNCTION_EXISTS(getfsuid HAVE_GETFSUID)
CHECK_FUNCTION_EXISTS(getfsgid HAVE_GETFSGID)
CHECK_FUNCTION_EXISTS(getfsstat HAVE_GETFSSTAT)
CHECK_FUNCTION_EXISTS(nice HAVE_NICE)
CHECK_FUNCTION_EXISTS(statfs HAVE_STATFS)
CHECK_FUNCTION_EXISTS(fstatfs HAVE_FSTATFS)
CHECK_FUNCTION_EXISTS(statvfs HAVE_STATVFS)
CHECK_FUNCTION_EXISTS(fstatvfs HAVE_FSTATVFS)
CHECK_FUNCTION_EXISTS(resetmnttab HAVE_RESETMNTTAB)
CHECK_FUNCTION_EXISTS(getmntent HAVE_GETMNTENT)
CHECK_FUNCTION_EXISTS(getextmntent HAVE_GETEXTMNTENT)
CHECK_FUNCTION_EXISTS(setlogin HAVE_SETLOGIN)
CHECK_FUNCTION_EXISTS(futimes HAVE_FUTIMES)
CHECK_FUNCTION_EXISTS(futimesat HAVE_FUTIMESAT)
CHECK_FUNCTION_EXISTS(flock HAVE_FLOCK)
CHECK_FUNCTION_EXISTS(lockf HAVE_LOCKF)
CHECK_FUNCTION_EXISTS(getpeereid HAVE_GETPEEREID)
IF(HAVE_UCRED_H)
CHECK_FUNCTION_EXISTS(getpeerucred HAVE_GETPEERUCRED)
ENDIF()
CHECK_FUNCTION_EXISTS(pthread_yield HAVE_PTHREAD_YIELD)
IF(HAVE_SYS_EVENT_H)
	CHECK_FUNCTION_EXISTS(kqueue HAVE_KQUEUE)
ENDIF()

CHECK_LIBRARY_EXISTS(socket connect "" SOCKET_LIBRARY)
CHECK_LIBRARY_EXISTS(nsl gethostbyaddr "" NSL_LIBRARY)
CHECK_LIBRARY_EXISTS(rt clock_gettime "" RT_LIBRARY)

INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR})

CONFIGURE_FILE(nix-config.h.cmake ${PROJECT_BINARY_DIR}/nix-config.h)

# The portable file-op core -- what the host-op conformance test exercises and the
# first set ported to every host.
SET(NIX_CORE_SRC
			nix-common.c
			nix-env.c
			nix-init.c
			nix-fd.c
			nix-io.c
			nix-file.c
			nix-dir.c
			nix-time.c
			nix-hostinfo.c
			nix-cred.c
			nix-mem.c
			nix-rt-time.c
			nix-xcpt.c
			nix-structs.c)

# The rest of the generic layer (directories, memory, processes, signals, sockets, SysV
# IPC, time) -- POSIX today; ported to non-POSIX hosts incrementally.
SET(NIX_GENERIC_SRC
			nix-process.c
			nix-rt-process.c
			nix-s5-msg.c
			nix-s5-sem.c
			nix-s5-shm.c
			nix-signal.c
			nix-socket.c)

# Host-OS families: the per-host implementations of the host-specific operations.
SET(NIX_BSD_SRC
			nix-bsd-cred.c nix-bsd-dir.c nix-bsd-file.c nix-bsd-fs.c nix-bsd-io.c
			nix-bsd-mem.c nix-bsd-nfs.c nix-bsd-obsolete.c nix-bsd-process.c
			nix-bsd-signal.c nix-bsd-system.c nix-bsd-time.c)
SET(NIX_LINUX_SRC
			nix-linux-cred.c nix-linux-file.c nix-linux-fs.c nix-linux-i386.c
			nix-linux-mem.c nix-linux-nfs.c nix-linux-process.c nix-linux-signal.c
			nix-linux-system.c nix-linux-time.c)
SET(NIX_HPUX_SRC nix-hpux-cred.c)

# Host selection. POSIX hosts build the full generic + BSD/Linux/HP-UX families exactly as
# before; win32 builds the portable file-op core only -- the rest of the syscall surface is
# brought up on win32 incrementally, keeping `ctest nix.hostops` green at every step.
IF(WIN32)
	SET(NIX_SRC ${NIX_CORE_SRC})
ELSE()
	SET(NIX_SRC ${NIX_CORE_SRC} ${NIX_GENERIC_SRC} ${NIX_BSD_SRC} ${NIX_LINUX_SRC} ${NIX_HPUX_SRC})
ENDIF()

# Static on win32 so the test executable is self-contained under Wine (no DLL deps); shared on POSIX.
IF(WIN32)
	ADD_LIBRARY(nix STATIC ${NIX_SRC})
ELSE()
	ADD_LIBRARY(nix SHARED ${NIX_SRC})
ENDIF()
TARGET_LINK_LIBRARIES(nix xec-compat)
IF(SOCKET_LIBRARY)
  TARGET_LINK_LIBRARIES(nix socket)
ENDIF()

IF(NSL_LIBRARY)
  TARGET_LINK_LIBRARIES(nix nsl)
ENDIF()

IF(RT_LIBRARY)
  TARGET_LINK_LIBRARIES(nix rt)
ENDIF()

