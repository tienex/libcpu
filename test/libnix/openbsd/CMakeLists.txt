CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(openbsd)

INCLUDE(CheckIncludeFiles)

CHECK_INCLUDE_FILES(sys/sysctl.h HAVE_SYS_SYSCTL_H)
CHECK_INCLUDE_FILES(linux/sysctl.h HAVE_LINUX_SYSCTL_H)

# For the time being
SET(GUEST m88k)

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
INCLUDE_DIRECTORIES(arch/${GUEST})

CONFIGURE_FILE(nix-openbsd-config.h.cmake ${PROJECT_BINARY_DIR}/nix-openbsd-config.h)

# Derive the version-annotated superset table from the vendored syscalls.master history: schistory
# --filter keeps exactly the implemented calls (from openbsd.seed.sc), adding @since..until derived
# from the masters. Falls back to the seed table if no masters are vendored, so the build works
# offline. sc2int then compiles the resulting table.
SET(OPENBSD_MASTERS ${PROJECT_SOURCE_DIR}/../masters/openbsd)
FILE(GLOB _obsd_mvers RELATIVE ${OPENBSD_MASTERS} ${OPENBSD_MASTERS}/*)
SET(_obsd_margs "")
SET(_obsd_mdeps "")
FOREACH(v IN LISTS _obsd_mvers)
  IF(EXISTS ${OPENBSD_MASTERS}/${v}/syscalls.master)
    LIST(APPEND _obsd_margs "${v}=${OPENBSD_MASTERS}/${v}/syscalls.master")
    LIST(APPEND _obsd_mdeps ${OPENBSD_MASTERS}/${v}/syscalls.master)
  ENDIF()
ENDFOREACH()
IF(_obsd_margs)
  ADD_CUSTOM_COMMAND(OUTPUT ${PROJECT_BINARY_DIR}/openbsd.sc
                     COMMAND schistory --filter ${PROJECT_SOURCE_DIR}/openbsd.seed.sc
                             --name "OpenBSD" --bae EFAULT
                             --out ${PROJECT_BINARY_DIR}/openbsd.sc ${_obsd_margs}
                     DEPENDS schistory ${PROJECT_SOURCE_DIR}/openbsd.seed.sc ${_obsd_mdeps}
                     COMMENT "Deriving OpenBSD version-gated syscall table from masters")
  SET(OPENBSD_SC ${PROJECT_BINARY_DIR}/openbsd.sc)
ELSE()
  SET(OPENBSD_SC ${PROJECT_SOURCE_DIR}/openbsd.seed.sc)
ENDIF()

# Execute sc2int
IF(XCODE)
ADD_CUSTOM_COMMAND(OUTPUT ${PROJECT_BINARY_DIR}/openbsd-callbacks.c
                   COMMAND ${PROJECT_BINARY_DIR}/../tools/sc2int/${CMAKE_BUILD_TYPE}/sc2int ${OPENBSD_SC}
                   DEPENDS ${OPENBSD_SC} sc2int
                   WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
                   COMMENT "Generating System Calls Callbacks")
ELSE()
ADD_CUSTOM_COMMAND(OUTPUT ${PROJECT_BINARY_DIR}/openbsd-callbacks.c
                   COMMAND ${PROJECT_BINARY_DIR}/../tools/sc2int/sc2int ${OPENBSD_SC}
                   DEPENDS ${OPENBSD_SC} sc2int
                   WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
                   COMMENT "Generating System Calls Callbacks")
ENDIF()

# Built as a real COM .abi bundle (openbsd.abi), matched by lcx on `--abi openbsd` (or the legacy
# alias `obsd79`). openbsd-abi.cpp is the IAbi shim bridging COM discovery to nix_personality_create.
libcpu_add_abi(openbsd openbsd
  MATCH openbsd obsd79
  SOURCES
      openbsd-init.c
      openbsd-personality.c
      openbsd-ioctl.c
      openbsd-structs.c
      openbsd-syscalls.c
      arc4random.c
      ${PROJECT_BINARY_DIR}/openbsd-callbacks.c
      # For the time being
      arch/${GUEST}/openbsd-guest.c
      openbsd-abi.cpp
  LIBS nix LibCPU)   # the engine (monitor + us-syscall) lives in nix; LibCPU provides IAbi/ComObject
ADD_DEPENDENCIES(openbsd sc2int)

