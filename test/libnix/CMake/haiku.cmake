# Haiku cross toolchain for building libnix (and its host-op conformance test) for Haiku, to
# validate the Haiku host backend with the real Haiku compiler + headers without a Haiku machine.
# Usage:
#
#   cmake -DCMAKE_TOOLCHAIN_FILE=CMake/haiku.cmake \
#         -DHAIKU_CROSS_DIR=<.../haiku/generated/cross-tools-x86_64> \
#         -DNIX_HOST_PROFILE=haiku <libnix-src-dir> && cmake --build .
#
# The cross tools are produced by Haiku's `./configure --build-cross-tools x86_64`. Running the
# result needs Haiku (e.g. under qemu); compiling against the real Haiku sysroot already exercises
# the port's header/symbol assumptions far beyond a POSIX stand-in build.
set(CMAKE_SYSTEM_NAME Haiku)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT DEFINED HAIKU_CROSS_DIR)
  if(DEFINED ENV{HAIKU_CROSS_DIR})
    set(HAIKU_CROSS_DIR "$ENV{HAIKU_CROSS_DIR}")
  endif()
endif()

set(CMAKE_C_COMPILER   "${HAIKU_CROSS_DIR}/bin/x86_64-unknown-haiku-gcc")
set(CMAKE_CXX_COMPILER "${HAIKU_CROSS_DIR}/bin/x86_64-unknown-haiku-g++")

# build-cross-tools produces the compiler but no populated sysroot: Haiku's system headers live in
# the Haiku source tree (headers/posix + every headers/os/* + headers/config + headers). HAIKU_SRC
# points at that tree; add the standard POSIX-code include set as -isystem so <stdio.h>, <Errors.h>,
# <sys/socket.h> etc. resolve (this is what Haiku's own jam build passes).
if(NOT DEFINED HAIKU_SRC AND DEFINED ENV{HAIKU_SRC})
  set(HAIKU_SRC "$ENV{HAIKU_SRC}")
endif()
if(DEFINED HAIKU_SRC)
  set(_hk_inc "-isystem ${HAIKU_SRC}/headers/posix -isystem ${HAIKU_SRC}/headers/os")
  file(GLOB _hk_osdirs "${HAIKU_SRC}/headers/os/*")
  foreach(_d ${_hk_osdirs})
    if(IS_DIRECTORY "${_d}")
      string(APPEND _hk_inc " -isystem ${_d}")
    endif()
  endforeach()
  string(APPEND _hk_inc " -isystem ${HAIKU_SRC}/headers/config -isystem ${HAIKU_SRC}/headers")
  # NB: headers/compatibility/bsd is deliberately NOT added -- its sys/queue.h etc. collide with
  # xec-compat's own bundled bsdqueue.h. The core needs only the posix + os headers.
  set(CMAKE_C_FLAGS_INIT   "${_hk_inc}")
  set(CMAKE_CXX_FLAGS_INIT "${_hk_inc}")
endif()

set(CMAKE_FIND_ROOT_PATH "${HAIKU_CROSS_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
