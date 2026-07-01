CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(libnix)

SET(CMAKE_BUILD_TYPE "Debug")

# Remove -Werror from libnix
REMOVE_DEFINITIONS(-Werror)

INCLUDE_DIRECTORIES(nix ${PROJECT_BINARY_DIR}/nix ${CMAKE_CURRENT_SOURCE_DIR}/../../LibCPU/include)

ADD_SUBDIRECTORY(nix)
ADD_SUBDIRECTORY(tools/sc2int)   # the .sc -> dispatch-table compiler (now built against nix-syscall.h)

# The OpenBSD/m88k ABI personality -- a dylib linking nix; its syscall engine (monitor + us-syscall)
# now lives in the nix dylib, and it is built against the LibCPU-direct nix_ ABI (no xec-compat).
ADD_SUBDIRECTORY(obsd79)
ADD_SUBDIRECTORY(nbsd101)

# Host-op conformance test: drives the portable nix_ primitives against a real host
# file with no CPU/monitor. Every host backend (POSIX today; win32/haiku/os2/vms next)
# must make it pass -- the validation vehicle for the portability work.
ENABLE_TESTING()
ADD_EXECUTABLE(nixtest nixtest.c)
TARGET_INCLUDE_DIRECTORIES(nixtest PRIVATE nix ${PROJECT_BINARY_DIR}/nix)
TARGET_LINK_LIBRARIES(nixtest nix)
ADD_TEST(NAME nix.hostops COMMAND nixtest)
