CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(xec-compat)

INCLUDE(CheckIncludeFiles)

CHECK_INCLUDE_FILES(sys/queue.h __XEC_HAVE_SYS_QUEUE_H)

IF(WIN32)
    SET(MMAP_SOURCE lib/xec-mmap-win32.c)
    SET(XCPT_SOURCE lib/xec-xcpt-win32.c)
ELSE()
    SET(MMAP_SOURCE lib/xec-mmap-unix.c)
    SET(XCPT_SOURCE lib/xec-xcpt-unix.c)
ENDIF()

INCLUDE_DIRECTORIES(lib engine arch ${PROJECT_BINARY_DIR})
CONFIGURE_FILE(xec-base.h.cmake ${PROJECT_BINARY_DIR}/xec-base.h)

# Static on win32 (self-contained test exe under Wine); shared on POSIX.
IF(WIN32)
    SET(XEC_LIBTYPE STATIC)
ELSE()
    SET(XEC_LIBTYPE SHARED)
ENDIF()
ADD_LIBRARY(xec-compat ${XEC_LIBTYPE} lib/xec-init.c lib/xec-log.c lib/xec-mem.c ${MMAP_SOURCE} ${XCPT_SOURCE}
    engine/xec-us-syscall.c engine/xec-monitor.c)
