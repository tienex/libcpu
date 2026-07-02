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

# Execute sc2int
IF(XCODE)
ADD_CUSTOM_COMMAND(OUTPUT ${PROJECT_BINARY_DIR}/openbsd-callbacks.c
                   COMMAND ${PROJECT_BINARY_DIR}/../tools/sc2int/${CMAKE_BUILD_TYPE}/sc2int ${PROJECT_SOURCE_DIR}/openbsd.sc
                   DEPENDS ${PROJECT_SOURCE_DIR}/openbsd.sc sc2int
                   WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
                   COMMENT "Generating System Calls Callbacks")
ELSE()
ADD_CUSTOM_COMMAND(OUTPUT ${PROJECT_BINARY_DIR}/openbsd-callbacks.c
                   COMMAND ${PROJECT_BINARY_DIR}/../tools/sc2int/sc2int ${PROJECT_SOURCE_DIR}/openbsd.sc
                   DEPENDS ${PROJECT_SOURCE_DIR}/openbsd.sc sc2int
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

