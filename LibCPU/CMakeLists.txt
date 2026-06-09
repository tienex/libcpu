# LibCPU: COM-based, LLVM-decoupled CPU translation library.
#
# Produces LibCPU.framework (core) and each backend as a real CFBundle (.backend).
# Universal across the toolchain's arches; discovers every LLVM and libgccjit from
# Homebrew AND MacPorts (one backend per version). The heavy lifting lives in the
# cmake/ modules included below.
cmake_minimum_required(VERSION 3.24)
project(LibCPU LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

include(LibCPUUniversal)    # LIBCPU_UNIVERSAL_ARCHS + libcpu_lib_arches()
include(LibCPUAddBackend)   # libcpu_add_backend()

# ---- core framework: LibCPU.framework ----
add_library(LibCPU SHARED src/LibCPULoader.cpp)
set_target_properties(LibCPU PROPERTIES
  FRAMEWORK TRUE
  FRAMEWORK_VERSION A
  MACOSX_FRAMEWORK_IDENTIFIER org.libcpu.LibCPU
  VERSION 1.0.0 SOVERSION 1
  PUBLIC_HEADER "include/LibCPU/Base.h;include/LibCPU/PCom.h;include/LibCPU/ICpu.h;include/LibCPU/CpuState.h;include/LibCPU/LibCPU.h;include/LibCPU/Loader.h;include/LibCPU/Module.h")
if(APPLE)
  target_link_libraries(LibCPU PRIVATE "-framework CoreFoundation")
endif()

# ---- dependency-light backends (universal) ----
libcpu_add_backend(interp_backend interp
  SOURCES backends/interp/Interp.cpp backends/interp/InterpModule.cpp
  INCLUDES backends/interp)
libcpu_add_backend(sljit_backend sljit
  SOURCES backends/sljit/SljitBackend.cpp backends/sljit/SljitModule.cpp third_party/sljit/sljitLir.c
  INCLUDES backends/sljit third_party/sljit)
libcpu_add_backend(mir_backend mir
  SOURCES backends/mir/MirBackend.cpp backends/mir/MirModule.cpp third_party/mir/mir.c third_party/mir/mir-gen.c
  INCLUDES backends/mir third_party/mir)
libcpu_add_backend(cc_backend cc
  SOURCES backends/cc/CcBackend.cpp backends/cc/CcModule.cpp
  INCLUDES backends/cc)

libcpu_add_backend(qbe_backend qbe
  SOURCES backends/qbe/QbeBackend.cpp backends/qbe/QbeModule.cpp
  INCLUDES backends/qbe)

# ---- external-toolchain backends (one bundle per discovered version) ----
include(LibCPUBackendsLLVM)
include(LibCPUBackendsGccJit)
libcpu_discover_llvm(${CMAKE_CURRENT_SOURCE_DIR})
libcpu_discover_gccjit(${CMAKE_CURRENT_SOURCE_DIR})

# ---- loader test ----
add_executable(load6502 test/load6502.cpp frontends/m6502/Cpu6502.cpp)
target_include_directories(load6502 PRIVATE frontends/m6502 test)
target_link_libraries(load6502 PRIVATE LibCPU)
if(APPLE)
  target_link_libraries(load6502 PRIVATE "-framework CoreFoundation")
endif()

# ---- compiler-discovery test (lists what the cc backend found) ----
add_executable(cclist test/cclist.cpp backends/cc/CcBackend.cpp)
target_include_directories(cclist PRIVATE backends/cc)
