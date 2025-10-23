PROJECT(libcpu)

# Option to build with LLVM C API compatibility layer
OPTION(LIBCPU_USE_LLVM_COMPAT "Build with LLVM C API compatibility layer (experimental)" OFF)

# Core library sources
SET(LIBCPU_SOURCES
			frontend.cpp
			disasm.cpp
			basicblock.cpp
			function.cpp
			translate.cpp
			translate_all.cpp
			translate_singlestep.cpp
			translate_singlestep_bb.cpp
			tag.cpp
			optimize.cpp
			fp.cpp
			idbg.cpp
			stat.cpp
			sha1.cpp
			interface.cpp
			timings.cpp)

# Add LLVM compatibility layer if enabled
IF(LIBCPU_USE_LLVM_COMPAT)
	MESSAGE(STATUS "Building with LLVM C API compatibility layer")
	LIST(APPEND LIBCPU_SOURCES llvm_compat.cpp)
	ADD_DEFINITIONS(-DLIBCPU_HAVE_LLVM_COMPAT=1)
ENDIF()

ADD_LIBRARY(cpu SHARED ${LIBCPU_SOURCES})

ADD_DEFINITIONS(-DLIBCPU_BUILD_CORE)

IF(CMAKE_COMPILER_IS_GNUCXX)
    ADD_DEFINITIONS(-fno-strict-aliasing)
ENDIF()

TARGET_LINK_LIBRARIES(cpu ${GUEST_ARCHITECTURES_ENABLED})
IF(HAVE_LIBREADLINE)
	ADD_DEFINITIONS(-DUSE_READLINE)
	TARGET_LINK_LIBRARIES(cpu readline)
ENDIF()
IF(HAVE_LIBRT)
	TARGET_LINK_LIBRARIES(cpu rt)
ENDIF()
TARGET_LINK_LLVM(cpu)
