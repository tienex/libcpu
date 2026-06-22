CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(libnix)

SET(CMAKE_BUILD_TYPE "Debug")

# Remove -Werror from libnix
REMOVE_DEFINITIONS(-Werror)

INCLUDE_DIRECTORIES(xec-compat xec-compat/lib xec-compat/engine xec-compat/arch nix)
INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR}/xec-compat nix)

ADD_SUBDIRECTORY(xec-compat)
ADD_SUBDIRECTORY(tools/sc2int)

ADD_SUBDIRECTORY(nix)
ADD_SUBDIRECTORY(obsd41)

# Host-op conformance test: drives the portable nix_ primitives against a real host
# file with no CPU/monitor. Every host backend (POSIX today; win32/haiku/os2/vms next)
# must make it pass -- the validation vehicle for the portability work.
ENABLE_TESTING()
ADD_EXECUTABLE(nixtest nixtest.c)
TARGET_INCLUDE_DIRECTORIES(nixtest PRIVATE nix ${PROJECT_BINARY_DIR}/nix xec-compat xec-compat/lib xec-compat/engine)
TARGET_LINK_LIBRARIES(nixtest nix xec-compat)
ADD_TEST(NAME nix.hostops COMMAND nixtest)
