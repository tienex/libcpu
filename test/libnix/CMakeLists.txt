CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(libnix)

SET(CMAKE_BUILD_TYPE "Debug")

# Remove -Werror from libnix
REMOVE_DEFINITIONS(-Werror)

INCLUDE_DIRECTORIES(nix ${PROJECT_BINARY_DIR}/nix ${CMAKE_CURRENT_SOURCE_DIR}/../../LibCPU/include)

ADD_SUBDIRECTORY(nix)
ADD_SUBDIRECTORY(tools/sc2int)   # the .sc -> dispatch-table compiler (now built against nix-syscall.h)

# The guest-OS ABI personalities (openbsd/netbsd/pdp11unix) are now COM .abi bundles that link the
# LibCPU framework, so they are built by the main LibCPU CMake (libcpu_add_abi), NOT by this
# standalone libnix build -- which exists purely to cross-verify the nix_ host layer (nixtest)
# under the win32/haiku/os2/vms source profiles.

# Host-op conformance test: drives the portable nix_ primitives against a real host
# file with no CPU/monitor. Every host backend (POSIX today; win32/haiku/os2/vms next)
# must make it pass -- the validation vehicle for the portability work.
ENABLE_TESTING()
ADD_EXECUTABLE(nixtest nixtest.c)
TARGET_INCLUDE_DIRECTORIES(nixtest PRIVATE nix ${PROJECT_BINARY_DIR}/nix)
TARGET_LINK_LIBRARIES(nixtest nix)
ADD_TEST(NAME nix.hostops COMMAND nixtest)
