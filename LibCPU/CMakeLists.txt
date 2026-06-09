# LibCPU: COM-based, LLVM-decoupled CPU translation library.
# Produces LibCPU.framework (core) and each backend as a real CFBundle (.backend).
cmake_minimum_required(VERSION 3.24)
project(LibCPU LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# Core framework: LibCPU.framework (the substrate + the bundle loader).
# ---------------------------------------------------------------------------
add_library(LibCPU SHARED src/LibCPULoader.cpp)
set_target_properties(LibCPU PROPERTIES
  FRAMEWORK TRUE
  FRAMEWORK_VERSION A
  MACOSX_FRAMEWORK_IDENTIFIER org.libcpu.LibCPU
  VERSION 1.0.0
  SOVERSION 1
  PUBLIC_HEADER "include/LibCPU/Base.h;include/LibCPU/PCom.h;include/LibCPU/ICpu.h;include/LibCPU/CpuState.h;include/LibCPU/LibCPU.h;include/LibCPU/Loader.h;include/LibCPU/Module.h")
if(APPLE)
  target_link_libraries(LibCPU PRIVATE "-framework CoreFoundation")
endif()

# ---------------------------------------------------------------------------
# Backend bundle helper: each backend is a loadable CFBundle named <name>.backend
# exporting LibCPUModuleCreateBackend.
# ---------------------------------------------------------------------------
set(LIBCPU_BACKEND_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/BackendInfo.plist.in)
function(libcpu_add_backend name)
  add_library(${name}_backend MODULE ${ARGN})
  set_target_properties(${name}_backend PROPERTIES
    OUTPUT_NAME ${name}
    BUNDLE TRUE
    BUNDLE_EXTENSION "backend"
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    MACOSX_BUNDLE_INFO_PLIST ${LIBCPU_BACKEND_PLIST}
    MACOSX_BUNDLE_BUNDLE_NAME ${name}
    MACOSX_BUNDLE_GUI_IDENTIFIER org.libcpu.backend.${name}
    MACOSX_BUNDLE_BUNDLE_VERSION 1.0
    MACOSX_BUNDLE_SHORT_VERSION_STRING 1.0)
endfunction()

# Dependency-light backends (no external libraries).
libcpu_add_backend(interp backends/interp/Interp.cpp backends/interp/InterpModule.cpp)
target_include_directories(interp_backend PRIVATE backends/interp)

libcpu_add_backend(sljit backends/sljit/SljitBackend.cpp backends/sljit/SljitModule.cpp third_party/sljit/sljitLir.c)
target_include_directories(sljit_backend PRIVATE backends/sljit third_party/sljit)

libcpu_add_backend(mir backends/mir/MirBackend.cpp backends/mir/MirModule.cpp third_party/mir/mir.c third_party/mir/mir-gen.c)
target_include_directories(mir_backend PRIVATE backends/mir third_party/mir)

libcpu_add_backend(cc backends/cc/CcBackend.cpp backends/cc/CcModule.cpp)
target_include_directories(cc_backend PRIVATE backends/cc)

# ---------------------------------------------------------------------------
# Loader test: load a backend BUNDLE by path and run the 6502 program.
# ---------------------------------------------------------------------------
add_executable(load6502 test/load6502.cpp frontends/m6502/Cpu6502.cpp)
target_include_directories(load6502 PRIVATE frontends/m6502 test)
target_link_libraries(load6502 PRIVATE LibCPU)
if(APPLE)
  target_link_libraries(load6502 PRIVATE "-framework CoreFoundation")
endif()
