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

# Execute sc2int
IF(XCODE)
ADD_CUSTOM_COMMAND(OUTPUT ${PROJECT_BINARY_DIR}/netbsd-callbacks.c
                   COMMAND ${PROJECT_BINARY_DIR}/../tools/sc2int/${CMAKE_BUILD_TYPE}/sc2int ${PROJECT_SOURCE_DIR}/netbsd.sc
                   DEPENDS ${PROJECT_SOURCE_DIR}/netbsd.sc sc2int
                   WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
                   COMMENT "Generating System Calls Callbacks")
ELSE()
ADD_CUSTOM_COMMAND(OUTPUT ${PROJECT_BINARY_DIR}/netbsd-callbacks.c
                   COMMAND ${PROJECT_BINARY_DIR}/../tools/sc2int/sc2int ${PROJECT_SOURCE_DIR}/netbsd.sc
                   DEPENDS ${PROJECT_SOURCE_DIR}/netbsd.sc sc2int
                   WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
                   COMMENT "Generating System Calls Callbacks")
ENDIF()

ADD_LIBRARY(netbsd
			SHARED
      netbsd-init.c
      netbsd-personality.c
      netbsd-ioctl.c
      netbsd-structs.c
      netbsd-syscalls.c
      arc4random.c
      ${PROJECT_BINARY_DIR}/netbsd-callbacks.c
      # For the time being
      arch/${GUEST}/netbsd-guest.c)
ADD_DEPENDENCIES(netbsd sc2int)

TARGET_LINK_LIBRARIES(netbsd nix)   # the engine (monitor + us-syscall) now lives in the nix dylib

