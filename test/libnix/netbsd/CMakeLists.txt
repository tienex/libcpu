CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(netbsd)

INCLUDE(CheckIncludeFiles)

CHECK_INCLUDE_FILES(sys/sysctl.h HAVE_SYS_SYSCTL_H)
CHECK_INCLUDE_FILES(linux/sysctl.h HAVE_LINUX_SYSCTL_H)

# For the time being
SET(GUEST m88k)

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
INCLUDE_DIRECTORIES(arch/${GUEST})

CONFIGURE_FILE(nix-netbsd-config.h.cmake ${PROJECT_BINARY_DIR}/nix-netbsd-config.h)

# Derive the version-annotated superset table from the vendored syscalls.master history (see the
# openbsd CMakeLists for the rationale); fall back to the seed table when no masters are vendored.
SET(NETBSD_MASTERS ${PROJECT_SOURCE_DIR}/../masters/netbsd)
FILE(GLOB _nbsd_mvers RELATIVE ${NETBSD_MASTERS} ${NETBSD_MASTERS}/*)
SET(_nbsd_margs "")
SET(_nbsd_mdeps "")
FOREACH(v IN LISTS _nbsd_mvers)
  IF(EXISTS ${NETBSD_MASTERS}/${v}/syscalls.master)
    LIST(APPEND _nbsd_margs "${v}=${NETBSD_MASTERS}/${v}/syscalls.master")
    LIST(APPEND _nbsd_mdeps ${NETBSD_MASTERS}/${v}/syscalls.master)
  ENDIF()
ENDFOREACH()
IF(_nbsd_margs)
  ADD_CUSTOM_COMMAND(OUTPUT ${PROJECT_BINARY_DIR}/netbsd.sc
                     COMMAND schistory --filter ${PROJECT_SOURCE_DIR}/netbsd.seed.sc
                             --name "NetBSD" --bae EFAULT
                             --out ${PROJECT_BINARY_DIR}/netbsd.sc ${_nbsd_margs}
                     DEPENDS schistory ${PROJECT_SOURCE_DIR}/netbsd.seed.sc ${_nbsd_mdeps}
                     COMMENT "Deriving NetBSD version-gated syscall table from masters")
  SET(NETBSD_SC ${PROJECT_BINARY_DIR}/netbsd.sc)
ELSE()
  SET(NETBSD_SC ${PROJECT_SOURCE_DIR}/netbsd.seed.sc)
ENDIF()

# Execute sc2int
IF(XCODE)
ADD_CUSTOM_COMMAND(OUTPUT ${PROJECT_BINARY_DIR}/netbsd-callbacks.c
                   COMMAND ${PROJECT_BINARY_DIR}/../tools/sc2int/${CMAKE_BUILD_TYPE}/sc2int ${NETBSD_SC}
                   DEPENDS ${NETBSD_SC} sc2int
                   WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
                   COMMENT "Generating System Calls Callbacks")
ELSE()
ADD_CUSTOM_COMMAND(OUTPUT ${PROJECT_BINARY_DIR}/netbsd-callbacks.c
                   COMMAND ${PROJECT_BINARY_DIR}/../tools/sc2int/sc2int ${NETBSD_SC}
                   DEPENDS ${NETBSD_SC} sc2int
                   WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
                   COMMENT "Generating System Calls Callbacks")
ENDIF()

# Built as a real COM .abi bundle (netbsd.abi), matched by lcx on `--abi netbsd` (or the legacy
# alias `nbsd101`). netbsd-abi.cpp is the IAbi shim bridging COM discovery to nix_personality_create.
libcpu_add_abi(netbsd netbsd
  MATCH netbsd nbsd101
  SOURCES
      netbsd-init.c
      netbsd-personality.c
      netbsd-ioctl.c
      netbsd-structs.c
      netbsd-syscalls.c
      arc4random.c
      ${PROJECT_BINARY_DIR}/netbsd-callbacks.c
      # For the time being
      arch/${GUEST}/netbsd-guest.c
      netbsd-abi.cpp
  LIBS nix LibCPU)   # the engine (monitor + us-syscall) lives in nix; LibCPU provides IAbi/ComObject
ADD_DEPENDENCIES(netbsd sc2int)

