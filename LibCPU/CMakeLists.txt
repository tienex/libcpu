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

libcpu_add_backend(js_backend js
  SOURCES backends/js/JsBackend.cpp backends/js/JsModule.cpp
  INCLUDES backends/js)

libcpu_add_backend(java_backend java
  SOURCES backends/java/JavaBackend.cpp backends/java/JavaModule.cpp
  INCLUDES backends/java)

libcpu_add_backend(dotnet_backend dotnet
  SOURCES backends/dotnet/DotnetBackend.cpp backends/dotnet/DotnetModule.cpp
  INCLUDES backends/dotnet)

libcpu_add_backend(python_backend python
  SOURCES backends/python/PythonBackend.cpp backends/python/PythonModule.cpp
  INCLUDES backends/python)

libcpu_add_backend(ruby_backend ruby
  SOURCES backends/ruby/RubyBackend.cpp backends/ruby/RubyModule.cpp
  INCLUDES backends/ruby)
libcpu_add_backend(perl_backend perl
  SOURCES backends/perl/PerlBackend.cpp backends/perl/PerlModule.cpp
  INCLUDES backends/perl)
libcpu_add_backend(lua_backend lua
  SOURCES backends/lua/LuaBackend.cpp backends/lua/LuaModule.cpp
  INCLUDES backends/lua)
libcpu_add_backend(swift_backend swift
  SOURCES backends/swift/SwiftBackend.cpp backends/swift/SwiftModule.cpp
  INCLUDES backends/swift)
libcpu_add_backend(tcl_backend tcl
  SOURCES backends/tcl/TclBackend.cpp backends/tcl/TclModule.cpp
  INCLUDES backends/tcl)
libcpu_add_backend(bash_backend bash
  SOURCES backends/bash/BashBackend.cpp backends/bash/BashModule.cpp
  INCLUDES backends/bash)
libcpu_add_backend(zsh_backend zsh
  SOURCES backends/zsh/ZshBackend.cpp backends/zsh/ZshModule.cpp
  INCLUDES backends/zsh)
libcpu_add_backend(ksh_backend ksh
  SOURCES backends/ksh/KshBackend.cpp backends/ksh/KshModule.cpp
  INCLUDES backends/ksh)
libcpu_add_backend(pwsh_backend pwsh
  SOURCES backends/pwsh/PwshBackend.cpp backends/pwsh/PwshModule.cpp
  INCLUDES backends/pwsh)
libcpu_add_backend(tcsh_backend tcsh
  SOURCES backends/tcsh/TcshBackend.cpp backends/tcsh/TcshModule.cpp
  INCLUDES backends/tcsh)
libcpu_add_backend(fish_backend fish
  SOURCES backends/fish/FishBackend.cpp backends/fish/FishModule.cpp
  INCLUDES backends/fish)
libcpu_add_backend(cmd_backend cmd
  SOURCES backends/cmd/CmdBackend.cpp backends/cmd/CmdModule.cpp
  INCLUDES backends/cmd)

# WebAssembly backend: emit a WASM module, run in-process on the vendored wasm3.
libcpu_add_backend(wasm_backend wasm
  SOURCES backends/wasm/WasmBackend.cpp backends/wasm/WasmModule.cpp
    third_party/wasm3/m3_core.c third_party/wasm3/m3_env.c third_party/wasm3/m3_compile.c
    third_party/wasm3/m3_exec.c third_party/wasm3/m3_function.c third_party/wasm3/m3_bind.c
    third_party/wasm3/m3_code.c third_party/wasm3/m3_module.c third_party/wasm3/m3_parse.c
    third_party/wasm3/m3_info.c
  INCLUDES backends/wasm third_party/wasm3)

# In-process JVM backend (JNI DefineClass of hand-emitted bytecode; no javac).
find_package(JNI QUIET)
if(JNI_FOUND)
  get_filename_component(LIBCPU_JVM_LIBDIR "${JAVA_JVM_LIBRARY}" DIRECTORY)
  # libjvm.dylib is single-arch (the host JDK's arch), so this bundle is too.
  libcpu_add_backend(jvm_backend jvm
    ARCHS ${CMAKE_HOST_SYSTEM_PROCESSOR}
    SOURCES backends/jvm/JvmBackend.cpp backends/jvm/JvmModule.cpp
    INCLUDES backends/jvm ${JNI_INCLUDE_DIRS}
    LIBS ${JNI_LIBRARIES}
    RPATH ${LIBCPU_JVM_LIBDIR})
endif()

# In-process JavaScriptCore backend (Apple framework; no-copy typed arrays).
if(APPLE)
  libcpu_add_backend(jscore_backend jscore
    SOURCES backends/jscore/JscoreBackend.cpp backends/jscore/JscoreModule.cpp
    INCLUDES backends/jscore
    LIBS "-framework JavaScriptCore")
endif()

# ---- GNU Lightning backend (in-process register JIT) ----
find_path(LIGHTNING_INC lightning.h
  PATHS /opt/homebrew/opt/lightning/include /opt/homebrew/include /usr/local/include /opt/local/include)
find_library(LIGHTNING_LIB lightning
  PATHS /opt/homebrew/opt/lightning/lib /opt/homebrew/lib /usr/local/lib /opt/local/lib)
if(LIGHTNING_INC AND LIGHTNING_LIB)
  libcpu_lib_arches(_ln_archs "${LIGHTNING_LIB}")
  if(NOT _ln_archs STREQUAL "")
    get_filename_component(_ln_dir "${LIGHTNING_LIB}" DIRECTORY)
    message(STATUS "LibCPU: GNU Lightning [${_ln_archs}] -> lightning.backend")
    libcpu_add_backend(lightning_backend lightning
      ARCHS "${_ln_archs}"
      SOURCES backends/lightning/LightningBackend.cpp backends/lightning/LightningModule.cpp
      INCLUDES backends/lightning ${LIGHTNING_INC}
      LIBS "${LIGHTNING_LIB}"
      RPATH "${_ln_dir}")
  endif()
endif()

# ---- AsmJit backend (in-process a64 JIT; vendored, arm64 only) ----
file(GLOB _aj_src CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asmjit/asmjit/core/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asmjit/asmjit/arm/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asmjit/asmjit/support/*.cpp)
if(_aj_src AND "arm64" IN_LIST LIBCPU_UNIVERSAL_ARCHS)
  message(STATUS "LibCPU: AsmJit [arm64] -> asmjit.backend")
  libcpu_add_backend(asmjit_backend asmjit
    ARCHS "arm64"
    SOURCES backends/asmjit/AsmjitBackend.cpp backends/asmjit/AsmjitModule.cpp ${_aj_src}
    INCLUDES backends/asmjit third_party/asmjit
    DEFINES ASMJIT_STATIC ASMJIT_NO_X86)
endif()

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

# ---- second frontend (CHIP-8) through any backend bundle ----
add_executable(loadchip8 test/loadchip8.cpp frontends/chip8/Chip8.cpp)
target_include_directories(loadchip8 PRIVATE frontends/chip8 test)
target_link_libraries(loadchip8 PRIVATE LibCPU)
if(APPLE)
  target_link_libraries(loadchip8 PRIVATE "-framework CoreFoundation")
endif()

# ---- compiler-discovery test (lists what the cc backend found) ----
add_executable(cclist test/cclist.cpp backends/cc/CcBackend.cpp)
target_include_directories(cclist PRIVATE backends/cc)

add_executable(jslist test/jslist.cpp backends/js/JsBackend.cpp)
target_include_directories(jslist PRIVATE backends/js)
