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
CHECK_INCLUDE_FILES(sys/reboot.h HAVE_SYS_REBOOT_H)
CHECK_INCLUDE_FILES(sys/shm.h HAVE_SYS_SHM_H)
CHECK_INCLUDE_FILES("sys/types.h;netinet/in_systm.h" HAVE_NETINET_IN_SYSTM_H)
CHECK_INCLUDE_FILES(poll.h HAVE_POLL_H)
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

INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../../LibCPU/include)

# LibCPU's shared logging API (LCLog) lives in core/LcLog.c (libcpu-owned source). The nix dylib
# always carries it, so a consumer that links nix -- nixtest, sc2int, an ABI personality, or lcx --
# gets LCLog from the one shared library.
SET(NIX_LOG_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../LibCPU/core/LcLog.c)

CONFIGURE_FILE(nix-config.h.cmake ${PROJECT_BINARY_DIR}/nix-config.h)

# The portable file-op core -- what the host-op conformance test exercises and the
# first set ported to every host.
SET(NIX_CORE_SRC
			nix-host.c
			nix-monitor.c
			nix-us-syscall.c
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
			nix-rt-process.c
			nix-process.c
			nix-signal.c
			nix-socket.c
			nix-s5-msg.c
			nix-s5-sem.c
			nix-s5-shm.c
			nix-xcpt.c
			nix-structs.c)

# The rest of the generic layer -- all brought onto the win32 core surface; nothing POSIX-only
# remains here. (Kept as a distinct list for clarity / future host families.)
SET(NIX_GENERIC_SRC)

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

# Host selection. POSIX hosts (Linux/BSD/macOS/Solaris) build the full generic + BSD/Linux/HP-UX
# families. The non-POSIX / partially-POSIX hosts pick the surface their runtime actually provides;
# each has a nix-<host>-compat.h shim (included from nix-base.h) for the gaps:
#   * win32 (MinGW): the portable file-op CORE only -- VERIFIED under Wine (run-libnix-wine.sh).
#     win32 is the one CORE-only host (no fork/wait/pipe/uio families), which is why the CORE's
#     `#ifndef _WIN32` guards omit those family calls there.
#   * OS/2 (kLIBC): CORE + the BSD family. The modern OS/2 GCC runtime (kLIBC) is POSIX-family
#     capable (fork/wait/pipe/fcntl/select/sockets), so it builds like a POSIX host -- which also
#     means the CORE's family calls (e.g. nix_bsd_wait4) resolve, unlike a CORE-only host.
#   * Haiku (POSIX + BeOS): CORE + the BSD family (Haiku's net stack is FreeBSD-derived); its libroot
#     is POSIX, so almost nothing is shimmed.
#   * OpenVMS (DEC/VSI C RTL): CORE + the BSD family; the CRTL is largely POSIX.
#
# NIX_HOST_PROFILE lets a build force another host's source SELECTION on the current build host, so
# Haiku's/OS-2's exact source set can be compiled + run (nixtest) on an available BSD-family host
# (e.g. macOS) -- functional cross-verification of the selection that "host: working" depends on,
# even when that host's own toolchain is unavailable. (The compat headers stay host-macro-guarded.)
SET(NIX_HOST_PROFILE "auto" CACHE STRING "host source profile: auto|posix|win32|os2|haiku|vms")
IF(NOT NIX_HOST_PROFILE STREQUAL "auto")
	SET(_nix_profile ${NIX_HOST_PROFILE})
ELSEIF(WIN32)
	SET(_nix_profile win32)
ELSEIF(CMAKE_SYSTEM_NAME STREQUAL "OS2" OR OS2)
	SET(_nix_profile os2)
ELSEIF(HAIKU OR CMAKE_SYSTEM_NAME STREQUAL "Haiku")
	SET(_nix_profile haiku)
ELSEIF(CMAKE_SYSTEM_NAME STREQUAL "OpenVMS")
	SET(_nix_profile vms)
ELSE()
	SET(_nix_profile posix)
ENDIF()

IF(_nix_profile STREQUAL "win32")
	SET(NIX_SRC ${NIX_CORE_SRC})
ELSEIF(_nix_profile STREQUAL "os2" OR _nix_profile STREQUAL "haiku" OR _nix_profile STREQUAL "vms")
	SET(NIX_SRC ${NIX_CORE_SRC} ${NIX_GENERIC_SRC} ${NIX_BSD_SRC})
ELSE()
	SET(NIX_SRC ${NIX_CORE_SRC} ${NIX_GENERIC_SRC} ${NIX_BSD_SRC} ${NIX_LINUX_SRC} ${NIX_HPUX_SRC})
ENDIF()
MESSAGE(STATUS "libnix host profile: ${_nix_profile}  (sources: ${NIX_SRC})")

# Static on win32 so the test executable is self-contained under Wine (no DLL deps); shared on the
# POSIX-family hosts. Also static when cross-compiling to Haiku here, so the port can be compiled +
# archived with the Haiku cross toolchain without the target's shared libroot (a native Haiku build
# links libnix shared normally).
IF(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Haiku")
	ADD_LIBRARY(nix STATIC ${NIX_SRC} ${NIX_LOG_SRC})
ELSE()
	ADD_LIBRARY(nix SHARED ${NIX_SRC} ${NIX_LOG_SRC})   # nix is a dylib; ABI personalities link it
ENDIF()
# (the former xec-compat dependency is gone -- nix-host.c provides the host runtime natively)
IF(WIN32)
  # winsock: nix-socket's socket calls + nix-io's socket-fd recv/send/closesocket routing.
  TARGET_LINK_LIBRARIES(nix ws2_32)
ENDIF()
IF(HAIKU OR CMAKE_SYSTEM_NAME STREQUAL "Haiku")
  # Haiku's BSD sockets live in libnetwork (not libc/libroot).
  TARGET_LINK_LIBRARIES(nix network)
ENDIF()
IF(SOCKET_LIBRARY)
  TARGET_LINK_LIBRARIES(nix socket)
ENDIF()

IF(NSL_LIBRARY)
  TARGET_LINK_LIBRARIES(nix nsl)
ENDIF()

IF(RT_LIBRARY)
  TARGET_LINK_LIBRARIES(nix rt)
ENDIF()

