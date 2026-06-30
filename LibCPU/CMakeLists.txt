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
include(LibCPUAddDevice)    # libcpu_add_device()

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
# tclemb: the in-process sibling of `tcl` -- embeds libtcl (Tcl_CreateInterp/Tcl_EvalFile)
# instead of spawning tclsh. Registered only where Tcl is linkable; the spawning `tcl` stays.
find_library(LIBCPU_TCL_LIB NAMES tcl tcl9.0 tcl8.6 tcl8.5)
if(LIBCPU_TCL_LIB)
  find_path(LIBCPU_TCL_INCLUDE tcl.h)
  libcpu_add_backend(tclemb_backend tclemb
    SOURCES backends/tclemb/TclEmbBackend.cpp backends/tclemb/TclEmbModule.cpp
    INCLUDES backends/tclemb ${LIBCPU_TCL_INCLUDE}
    LIBS ${LIBCPU_TCL_LIB})
endif()
libcpu_add_backend(bash_backend bash
  SOURCES backends/bash/BashBackend.cpp backends/bash/BashModule.cpp
  INCLUDES backends/bash)
libcpu_add_backend(cmake_backend cmake
  SOURCES backends/cmake/CmakeBackend.cpp backends/cmake/CmakeModule.cpp
  INCLUDES backends/cmake)
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

# In-process CLR (.NET) backend: CoreCLR is embedded via nethost/hostfxr and the
# hand-emitted CIL is JIT'd through Reflection.Emit (no csc per instruction). A
# one-time managed helper (Emitter.dll) is built with the dotnet SDK.
file(GLOB LIBCPU_NETHOST_DIRS
  "/usr/local/share/dotnet/packs/Microsoft.NETCore.App.Host.osx-${CMAKE_HOST_SYSTEM_PROCESSOR}/*/runtimes/osx-${CMAKE_HOST_SYSTEM_PROCESSOR}/native")
find_program(LIBCPU_DOTNET dotnet)
if(LIBCPU_NETHOST_DIRS AND LIBCPU_DOTNET)
  list(SORT LIBCPU_NETHOST_DIRS)
  list(GET LIBCPU_NETHOST_DIRS -1 LIBCPU_NETHOST_DIR)   # newest host pack
  if(EXISTS "${LIBCPU_NETHOST_DIR}/libnethost.dylib")
    set(LIBCPU_CLR_OUT ${CMAKE_BINARY_DIR}/clr)
    add_custom_command(OUTPUT ${LIBCPU_CLR_OUT}/Emitter.dll
      COMMAND ${LIBCPU_DOTNET} build ${CMAKE_CURRENT_SOURCE_DIR}/backends/clr/managed/Emitter.csproj
              -c Release -o ${LIBCPU_CLR_OUT}
      DEPENDS backends/clr/managed/Emitter.cs backends/clr/managed/Emitter.csproj
      COMMENT "Building managed Emitter.dll (one-time Reflection.Emit helper)"
      VERBATIM)
    add_custom_target(clr_emitter DEPENDS ${LIBCPU_CLR_OUT}/Emitter.dll)
    libcpu_add_backend(clr_backend clr
      ARCHS ${CMAKE_HOST_SYSTEM_PROCESSOR}
      SOURCES backends/clr/ClrBackend.cpp backends/clr/ClrModule.cpp
      INCLUDES backends/clr ${LIBCPU_NETHOST_DIR}
      LIBS ${LIBCPU_NETHOST_DIR}/libnethost.dylib
      DEFINES LIBCPU_CLR_DIR="${LIBCPU_CLR_OUT}"
      RPATH ${LIBCPU_NETHOST_DIR})
    add_dependencies(clr_backend clr_emitter)
  endif()
endif()

# In-process CPython backend (Py_CompileString + PyEval_EvalCode; no subprocess).
find_package(Python3 COMPONENTS Development QUIET)
if(Python3_Development_FOUND)
  # libpython is single-arch, so pin this bundle to the host arch.
  libcpu_add_backend(cpython_backend cpython
    ARCHS ${CMAKE_HOST_SYSTEM_PROCESSOR}
    SOURCES backends/cpython/CPythonBackend.cpp backends/cpython/CPythonModule.cpp
    INCLUDES backends/cpython ${Python3_INCLUDE_DIRS}
    LIBS ${Python3_LIBRARIES}
    RPATH ${Python3_LIBRARY_DIRS})
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

# ---- AsmJit backend (in-process JIT; vendored: AArch64 + x86/x86-64) ----
# AsmJit is target-specific (unlike the host-agnostic SLJIT/Lightning IRs): the backend file selects
# its emitter per slice via ASMJIT_ARCH_*. We compile the core + BOTH the arm and x86 instruction
# backends and let each slice keep only what it needs (asmjit guards them with ASMJIT_ARCH_*), so a
# universal arm64+x86_64 build yields one fat asmjit.backend whose AArch64 slice runs natively and whose
# x86-64 slice runs under Rosetta / on Intel hosts. No ASMJIT_NO_* -- every supported host has a backend.
file(GLOB _aj_src CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asmjit/asmjit/core/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asmjit/asmjit/arm/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asmjit/asmjit/x86/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asmjit/asmjit/support/*.cpp)
# AsmJit emits for AArch64, x86-64 and IA-32 only -- keep the arches the toolchain offers that AsmJit
# can target (e.g. ppc/ppc64 are dropped; arm64+x86_64 on Apple Silicon, i386+x86_64 on an Intel SDK).
set(_aj_archs "")
foreach(_a ${LIBCPU_UNIVERSAL_ARCHS})
  if(_a STREQUAL "arm64" OR _a STREQUAL "x86_64" OR _a STREQUAL "i386")
    list(APPEND _aj_archs ${_a})
  endif()
endforeach()
if(_aj_src AND _aj_archs)
  message(STATUS "LibCPU: AsmJit [${_aj_archs}] -> asmjit.backend")
  libcpu_add_backend(asmjit_backend asmjit
    ARCHS "${_aj_archs}"
    SOURCES backends/asmjit/AsmjitBackend.cpp backends/asmjit/AsmjitModule.cpp ${_aj_src}
    INCLUDES backends/asmjit third_party/asmjit
    DEFINES ASMJIT_STATIC)
endif()

# ---- external-toolchain backends (one bundle per discovered version) ----
include(LibCPUBackendsLLVM)
include(LibCPUBackendsGccJit)
libcpu_discover_llvm(${CMAKE_CURRENT_SOURCE_DIR})
libcpu_discover_gccjit(${CMAKE_CURRENT_SOURCE_DIR})

# ---- loader test ----
add_executable(load6502 test/load6502.cpp frontends/m6502/Cpu6502.generated.cpp)
target_include_directories(load6502 PRIVATE frontends/m6502 test)
target_link_libraries(load6502 PRIVATE LibCPU)

add_executable(aot6502 test/aot6502.cpp aot/AotGenerator.cpp frontends/m6502/Cpu6502.generated.cpp)
target_include_directories(aot6502 PRIVATE frontends/m6502 test aot)
target_link_libraries(aot6502 PRIVATE LibCPU)

add_executable(aotchip8 test/aotchip8.cpp aot/AotGenerator.cpp frontends/chip8/Chip8.generated.cpp)
target_include_directories(aotchip8 PRIVATE frontends/chip8 test aot)
target_link_libraries(aotchip8 PRIVATE LibCPU)

add_executable(aotcfg6502 test/aotcfg6502.cpp aot/AotGenerator.cpp frontends/m6502/Cpu6502.generated.cpp)
target_include_directories(aotcfg6502 PRIVATE frontends/m6502 test aot)
target_link_libraries(aotcfg6502 PRIVATE LibCPU)

add_executable(aotsmc6502 test/aotsmc6502.cpp aot/AotGenerator.cpp frontends/m6502/Cpu6502.generated.cpp)
target_include_directories(aotsmc6502 PRIVATE frontends/m6502 test aot)
target_link_libraries(aotsmc6502 PRIVATE LibCPU)

add_executable(aotv20 test/aotv20.cpp aot/AotGenerator.cpp frontends/v20/CpuV20.cpp frontends/i8080/CpuI8080.cpp)
target_include_directories(aotv20 PRIVATE frontends/v20 frontends/i8080 test aot)
target_link_libraries(aotv20 PRIVATE LibCPU)

find_package(Threads REQUIRED)
add_executable(tiered test/tiered.cpp aot/AotGenerator.cpp core/TieredEngine.cpp core/PerfTrace.cpp core/ProfiledAot.cpp frontends/v20/CpuV20.cpp frontends/i8080/CpuI8080.cpp)
target_include_directories(tiered PRIVATE frontends/v20 frontends/i8080 test aot core)
target_link_libraries(tiered PRIVATE LibCPU Threads::Threads)

# Same tiered / PGO suite, driven through the 6502 frontend (frontend-agnosticism).
add_executable(tiered6502 test/tiered6502.cpp aot/AotGenerator.cpp core/TieredEngine.cpp core/PerfTrace.cpp core/ProfiledAot.cpp frontends/m6502/Cpu6502.generated.cpp)
target_include_directories(tiered6502 PRIVATE frontends/m6502 test aot core)
target_link_libraries(tiered6502 PRIVATE LibCPU Threads::Threads)

# Knowledge-library demo: a DOS program's in-line INT 21h calls become host libc calls.
add_executable(dossys test/dossys.cpp aot/AotGenerator.cpp core/KnowledgeLibrary.cpp frontends/v20/CpuV20.cpp frontends/i8080/CpuI8080.cpp)
target_include_directories(dossys PRIVATE frontends/v20 frontends/i8080 test aot core)
target_link_libraries(dossys PRIVATE LibCPU)

# Single-step debugger (line editor + gdb-like commands + dual disassembly).
add_executable(lcdbg test/lcdbg.cpp core/Debugger.cpp core/LineEditor.cpp frontends/v20/CpuV20.cpp frontends/i8080/CpuI8080.cpp frontends/m6502/Cpu6502.generated.cpp)
target_include_directories(lcdbg PRIVATE frontends/v20 frontends/i8080 frontends/m6502 test core)
target_link_libraries(lcdbg PRIVATE LibCPU)

# Disk-cached translation: translate once, reload the serialised artifact thereafter.
add_executable(cachedemo test/cachedemo.cpp core/TranslationCache.cpp aot/AotGenerator.cpp frontends/v20/CpuV20.cpp frontends/i8080/CpuI8080.cpp)
target_include_directories(cachedemo PRIVATE frontends/v20 frontends/i8080 test aot core)
target_link_libraries(cachedemo PRIVATE LibCPU)

# System-level emulation: an 8086 machine with a device bus and timer interrupts.
add_executable(sysdemo test/sysdemo.cpp core/System.cpp core/X86System.cpp core/CodeCache.cpp core/ZooArchive.cpp third_party/zstd/zstd.c aot/AotGenerator.cpp aot/ShadowCfg.cpp frontends/v20/CpuV20.cpp frontends/i8080/CpuI8080.cpp)
target_include_directories(sysdemo PRIVATE frontends/v20 frontends/i8080 test aot core third_party/zstd)
target_compile_definitions(sysdemo PRIVATE LIBCPU_HAVE_ZSTD)
target_link_libraries(sysdemo PRIVATE LibCPU)

# Native AOT: compile a guest program to a standalone host executable.
add_executable(nativedemo test/nativedemo.cpp core/NativeAot.cpp frontends/v20/CpuV20.cpp frontends/i8080/CpuI8080.cpp)
target_include_directories(nativedemo PRIVATE frontends/v20 frontends/i8080 test core)
target_link_libraries(nativedemo PRIVATE LibCPU)

# lcx -- the unified machine driver tying every subsystem together.
# UPCL -- the CPU-description language. A DYNAMIC sub-framework of LibCPU (clang-style
# diagnostics, a hand-written recursive-descent parser); embeddable, and linked
# dynamically. It is built into LibCPU.framework/Versions/A/Frameworks/UPCL.framework
# and links the LibCPU framework at run time.
add_library(upcl SHARED upcl/Diagnostic.cpp upcl/Lexer.cpp upcl/Parser.cpp upcl/RegisterLayout.cpp upcl/Semantics.cpp upcl/Decoder.cpp upcl/UpclArch.cpp upcl/CppGen.cpp)
set_target_properties(upcl PROPERTIES
  OUTPUT_NAME UPCL
  FRAMEWORK TRUE
  FRAMEWORK_VERSION A
  MACOSX_FRAMEWORK_IDENTIFIER org.libcpu.UPCL
  VERSION 1.0.0 SOVERSION 1
  PUBLIC_HEADER "upcl/Diagnostic.h;upcl/Token.h;upcl/Ast.h;upcl/Lexer.h;upcl/Parser.h;upcl/RegisterLayout.h;upcl/Semantics.h;upcl/Decoder.h;upcl/UpclArch.h;upcl/CppGen.h")
target_include_directories(upcl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/upcl ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(upcl PUBLIC LibCPU)            # dynamic link against the LibCPU framework
if(APPLE)
  # Nest UPCL.framework inside LibCPU.framework as a sub-framework.
  add_dependencies(upcl LibCPU)
  add_custom_command(TARGET upcl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LibCPU>/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:upcl>" "$<TARGET_FILE_DIR:LibCPU>/Frameworks/UPCL.framework"
    COMMENT "Embedding UPCL.framework as a sub-framework of LibCPU.framework")
endif()

# zstd -- the solid compressor behind knowledge-library (ZOO) archives. VENDORED as a
# single-file amalgamation under third_party/zstd, so it builds from source (universal
# across the toolchain's arches, no external dependency) and powers `lcx klib`.
# libnix: the LibCPU-native UNIX host layer (xec-compat-free), built as a shared library (dylib) and
# linked into lcx, which drives it from the guest syscall personalities. The nix dylib carries LCLog
# (LibCPU/core/LcLog.c), so lcx gets the shared logging API from it.
set(NIX_HOST_PROFILE "posix" CACHE STRING "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/nix ${CMAKE_BINARY_DIR}/libnix)
if(TARGET nix)
  target_compile_options(nix PRIVATE -w)   # libnix is older C; do not let its warnings fail the build
endif()

add_executable(lcx test/lcx.cpp
  test/Pdp1Io.cpp
  aot/AotGenerator.cpp aot/ShadowCfg.cpp core/Debugger.cpp core/LineEditor.cpp core/TranslationCache.cpp
  core/KnowledgeLibrary.cpp core/System.cpp core/X86System.cpp core/NativeAot.cpp core/ZooArchive.cpp core/CodeCache.cpp
  core/SymbolReader.cpp core/HeaderParser.cpp core/DerivationEngine.cpp core/DeviceTree.cpp
  core/DeviceNode.cpp core/MachineBuilder.cpp
  console/ConsoleChannel.cpp console/ConsoleBridge.cpp console/TerminalConsole.cpp
  frontends/v20/CpuV20.cpp frontends/i8080/CpuI8080.cpp frontends/m6502/Cpu6502.generated.cpp
  third_party/zstd/zstd.c)
target_include_directories(lcx PRIVATE frontends/v20 frontends/i8080 frontends/m6502 test aot core console third_party/zstd
  ${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/nix ${CMAKE_BINARY_DIR}/libnix)
target_compile_definitions(lcx PRIVATE LIBCPU_HAVE_ZSTD)
target_link_libraries(lcx PRIVATE LibCPU upcl nix ${CMAKE_DL_LIBS})   # nix: libnix host layer; dl for HeaderParser's dlopen(libclang)
if(APPLE)
  target_link_libraries(load6502 PRIVATE "-framework CoreFoundation")
  target_link_libraries(lcx PRIVATE "-framework CoreFoundation")   # MachineBuilder loads .device CFBundles
endif()

# ---- hardware-component bundles (.device): each a COM object the machine builder matches ----
libcpu_add_device(i8259_device i8259 MATCH "intel,i8259;pic" SOURCES devices/i8259/Pic8259.cpp)
libcpu_add_device(ns8250_device ns8250 MATCH "national,ns8250;ns16450;serial" SOURCES devices/ns8250/Uart8250.cpp)
libcpu_add_device(i8253_device i8253 MATCH "intel,i8253;intel,i8254;pit" SOURCES devices/i8253/Pit8253.cpp)
libcpu_add_device(i8255_device i8255 MATCH "intel,i8255;ppi" SOURCES devices/i8255/Ppi8255.cpp)
libcpu_add_device(i8042_device i8042 MATCH "intel,i8042;ibm,kbc;kbc;ps2-kbc" SOURCES devices/i8042/Kbc8042.cpp)
libcpu_add_device(mc146818_device mc146818 MATCH "motorola,mc146818;ibm,rtc;rtc;cmos" SOURCES devices/mc146818/Rtc146818.cpp)
libcpu_add_device(i8237_device i8237 MATCH "intel,i8237;dma" SOURCES devices/i8237/Dma8237.cpp)
libcpu_add_device(mc6845_device mc6845 MATCH "motorola,mc6845;ibm,mda" SOURCES devices/mc6845/Mda6845.cpp)
libcpu_add_device(cga_device cga MATCH "ibm,cga;cga" SOURCES devices/cga/Cga6845.cpp)
libcpu_add_device(ega_device ega MATCH "ibm,ega;ega" SOURCES devices/ega/Ega.cpp)
libcpu_add_device(vga_device vga MATCH "ibm,vga;vga" SOURCES devices/vga/Vga.cpp)
libcpu_add_device(hercules_device hercules MATCH "hercules,hgc;hercules;hgc" SOURCES devices/hercules/Hercules.cpp)
libcpu_add_device(incolor_device incolor MATCH "hercules,incolor;incolor" SOURCES devices/incolor/Incolor.cpp)
libcpu_add_device(i8272_device i8272 MATCH "intel,i8272;nec,upd765;ibm,pc-fdc;fdc" SOURCES devices/i8272/Fdc8272.cpp)
libcpu_add_device(st506_device st506 MATCH "xebec,1210;ibm,pc-xt-hdc;st506;hdc" SOURCES devices/st506/HardDisk506.cpp)
libcpu_add_device(xtide_device xtide MATCH "xtide;xt-ide;ide" SOURCES devices/xtide/Xtide.cpp)
libcpu_add_device(lpt_device lpt MATCH "ibm,parallel;parallel;centronics;lpt" SOURCES devices/lpt/Lpt.cpp)
libcpu_add_device(disk_device disk MATCH "libcpu,disk;ata,disk;scsi,disk;st506,disk;disk" SOURCES devices/disk/HardDisk.cpp)
libcpu_add_device(i8087_device i8087 MATCH "intel,i8087;i8087;fpu;8087" SOURCES devices/i8087/Fpu8087.cpp)
libcpu_add_device(pcspeaker_device pcspeaker MATCH "ibm,pc-speaker;pc-speaker;speaker" SOURCES devices/pcspeaker/Speaker.cpp)
libcpu_add_device(cassette_device cassette MATCH "ibm,pc-cassette;cassette" SOURCES devices/cassette/Cassette.cpp)
libcpu_add_device(ram_device ram MATCH "libcpu,ram" SOURCES devices/ram/Ram.cpp)
libcpu_add_device(rom_device rom MATCH "ibm,pc-xt-bios;libcpu,rom" SOURCES devices/rom/Rom.cpp)

# ---- device-tree public C API exercised from C (proves the LC surface is C-callable) ----
add_executable(dtapi test/dtcapi.c core/DeviceTreeApi.cpp core/DeviceTree.cpp)
target_include_directories(dtapi PRIVATE core)

# ---- second frontend (CHIP-8) through any backend bundle ----
add_executable(loadchip8 test/loadchip8.cpp frontends/chip8/Chip8.generated.cpp)
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

# ---- backend-matrix regression harness (CTest) -----------------------------
# Sweep every CFG-capable backend through the two whole-program CFG drivers
# (aotv20: V20 instruction suite incl. segmentation + far CALL/JMP; tiered: JIT
# promote/hot-swap, PGO inlining, edge counters, recursion). Each driver already
# exits non-zero on any FAIL or crash and zero when every demo PASSes or SKIPs,
# so one driver run == one backend's whole suite. Each backend runs in its own
# process, which keeps the in-process managed runtimes (JVM/CLR/CPython) isolated.
#
# New CFG-capable backend? Add its target to LIBCPU_CFG_BACKENDS. A backend whose
# toolchain was not found is simply skipped (its target does not exist).
enable_testing()
set(LIBCPU_CFG_BACKENDS interp_backend cc_backend cpython_backend jvm_backend clr_backend)
set(LIBCPU_CHECK_DEPS aotv20 tiered tiered6502 load6502 aot6502 loadchip8 aotchip8)
foreach(_be ${LIBCPU_CFG_BACKENDS})
  if(TARGET ${_be})
    set(_bundle "$<TARGET_BUNDLE_DIR:${_be}>")
    add_test(NAME aotv20.${_be}     COMMAND aotv20 "${_bundle}")
    add_test(NAME tiered.${_be}     COMMAND tiered "${_bundle}" "${_bundle}")
    add_test(NAME tiered6502.${_be} COMMAND tiered6502 "${_bundle}" "${_bundle}")
    # The CHIP-8 frontend is generated from chip8.upcl too -- the Vx selector and the Fx55/Fx65
    # block loops index a register array, which the generator turns into a direct register access
    # (the index is known at translate time), so the slice runs on every backend, not just interp.
    add_test(NAME chip8.load.${_be} COMMAND loadchip8 "${_bundle}")
    add_test(NAME chip8.aot.${_be}  COMMAND aotchip8 "${_bundle}")
    # The 6502 generated frontend: the slice program plus the %CC-driven ADC carry/overflow case.
    add_test(NAME m6502.load.${_be} COMMAND load6502 "${_bundle}")
    add_test(NAME m6502.aot.${_be}  COMMAND aot6502 "${_bundle}")
    # Belt-and-suspenders: fail the test if any demo prints a FAIL line, even
    # should the driver's exit code ever regress.
    set_tests_properties(aotv20.${_be} tiered.${_be} tiered6502.${_be} chip8.load.${_be} chip8.aot.${_be}
      m6502.load.${_be} m6502.aot.${_be} PROPERTIES
      FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
    list(APPEND LIBCPU_CHECK_DEPS ${_be})
  endif()
endforeach()

# The knowledge-library demo needs a syscall-capable backend (ICpuSyscallEmitter),
# which today is the interpreter; register it as a single interp test.
if(TARGET interp_backend)
  add_test(NAME knowledge.dos
    COMMAND dossys "$<TARGET_BUNDLE_DIR:interp_backend>" "${CMAKE_CURRENT_SOURCE_DIR}/test/dos21.xml")
  set_tests_properties(knowledge.dos PROPERTIES FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
  list(APPEND LIBCPU_CHECK_DEPS dossys)

  # The single-step debugger, driven by a piped command script (batch mode). The
  # script runs the demo to completion and dumps the result word; the regex asserts
  # the debugger actually stepped the program correctly (loop result at $0200).
  add_test(NAME debugger.v20 COMMAND sh -c
    "printf 'continue\\nexamine 0x200 2\\nquit\\n' | '$<TARGET_FILE:lcdbg>' '$<TARGET_BUNDLE_DIR:interp_backend>' --arch v20")
  set_tests_properties(debugger.v20 PROPERTIES PASS_REGULAR_EXPRESSION "0200: 12 00")
  add_test(NAME debugger.m6502 COMMAND sh -c
    "printf 'continue\\nexamine 0x200 2\\nquit\\n' | '$<TARGET_FILE:lcdbg>' '$<TARGET_BUNDLE_DIR:interp_backend>' --arch 6502")
  set_tests_properties(debugger.m6502 PROPERTIES PASS_REGULAR_EXPRESSION "0200: 0f 00")
  list(APPEND LIBCPU_CHECK_DEPS lcdbg)

  # Disk translation cache: miss-stores / hit-reloads / cap / clean.
  add_test(NAME cache.v20
    COMMAND cachedemo "$<TARGET_BUNDLE_DIR:interp_backend>" "${CMAKE_BINARY_DIR}/cachetest")
  set_tests_properties(cache.v20 PROPERTIES FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
  list(APPEND LIBCPU_CHECK_DEPS cachedemo)

  # System-level emulation: device bus (port IN/OUT) + timer interrupt (IVT/ISR/IRET).
  add_test(NAME system.v20 COMMAND sysdemo "$<TARGET_BUNDLE_DIR:interp_backend>")
  set_tests_properties(system.v20 PROPERTIES FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
  list(APPEND LIBCPU_CHECK_DEPS sysdemo)

  # lcx -- the unified driver. The backend resolves to the bundle beside the exe; a
  # binary fixture exercises the run path, and system/know cover the other subsystems.
  add_test(NAME lcx.run COMMAND lcx run "${CMAKE_CURRENT_SOURCE_DIR}/test/loop.v20.bin" --arch v20 --dump 0x200)
  set_tests_properties(lcx.run PROPERTIES PASS_REGULAR_EXPRESSION "= 0x0012")
  add_test(NAME lcx.system COMMAND lcx system)
  set_tests_properties(lcx.system PROPERTIES FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
  add_test(NAME lcx.know COMMAND lcx know "${CMAKE_CURRENT_SOURCE_DIR}/test/dos21.xml")
  set_tests_properties(lcx.know PROPERTIES FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
  list(APPEND LIBCPU_CHECK_DEPS lcx)
endif()

# Native AOT: compile a guest program to a standalone host executable (no libcpu),
# guest registers mapped to host registers. The demo compiles + runs the binary; the
# lcx test exercises "lcx aot" and runs the produced executable.
add_test(NAME native.v20 COMMAND nativedemo "${CMAKE_BINARY_DIR}/native_demo_exe")
set_tests_properties(native.v20 PROPERTIES FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
list(APPEND LIBCPU_CHECK_DEPS nativedemo)
list(APPEND LIBCPU_CHECK_DEPS dtapi)
list(APPEND LIBCPU_CHECK_DEPS i8259_device)
list(APPEND LIBCPU_CHECK_DEPS ns8250_device)
list(APPEND LIBCPU_CHECK_DEPS i8253_device)
list(APPEND LIBCPU_CHECK_DEPS i8255_device)
list(APPEND LIBCPU_CHECK_DEPS i8237_device)
list(APPEND LIBCPU_CHECK_DEPS mc6845_device)
list(APPEND LIBCPU_CHECK_DEPS ram_device)
list(APPEND LIBCPU_CHECK_DEPS rom_device)
if(TARGET interp_backend)
  add_test(NAME lcx.aot COMMAND sh -c
    "'$<TARGET_FILE:lcx>' aot '${CMAKE_CURRENT_SOURCE_DIR}/test/loop.v20.bin' -o '${CMAKE_BINARY_DIR}/loop.native' --result 0x200 && '${CMAKE_BINARY_DIR}/loop.native'")
  set_tests_properties(lcx.aot PROPERTIES PASS_REGULAR_EXPRESSION "0x0012")
endif()

# UPCL -- the CPU-description language. A valid description checks clean; a broken one
# produces clang-style diagnostics and a non-zero exit (WILL_FAIL).
add_test(NAME upcl.check COMMAND lcx upcl check "${CMAKE_CURRENT_SOURCE_DIR}/test/v20.upcl")
set_tests_properties(upcl.check PROPERTIES PASS_REGULAR_EXPRESSION "3 instruction")
add_test(NAME upcl.diagnostics COMMAND lcx upcl check "${CMAKE_CURRENT_SOURCE_DIR}/test/broken.upcl")
set_tests_properties(upcl.diagnostics PROPERTIES WILL_FAIL TRUE)

# `lcx upcl produce` builds the live frontend and round-trips its encodings.
add_test(NAME upcl.produce COMMAND lcx upcl produce "${CMAKE_CURRENT_SOURCE_DIR}/test/v20.upcl")
set_tests_properties(upcl.produce PROPERTIES PASS_REGULAR_EXPRESSION "mov ax")

# A .upcl description used as a live frontend: `--arch upcl:<file>` runs a real program
# (mov ax,3; mov cx,5; add ax,cx -> ax=8) through the interpreted decode + semantics.
if(TARGET interp_backend)
  add_test(NAME upcl.run COMMAND lcx run "${CMAKE_CURRENT_SOURCE_DIR}/test/add.v20.bin"
    --arch "upcl:${CMAKE_CURRENT_SOURCE_DIR}/test/v20.upcl")
  set_tests_properties(upcl.run PROPERTIES PASS_REGULAR_EXPRESSION "ax =0008")
  set_tests_properties(upcl.run PROPERTIES PASS_REGULAR_EXPRESSION "ax =0008")
  add_test(NAME upcl.translate COMMAND lcx translate "${CMAKE_CURRENT_SOURCE_DIR}/test/add.v20.bin"
    --arch "upcl:${CMAKE_CURRENT_SOURCE_DIR}/test/v20.upcl")
  set_tests_properties(upcl.translate PROPERTIES PASS_REGULAR_EXPRESSION "ax =0008")

  # The standard 8086.upcl -- register_file + `encode` + semantics -- run as a live frontend
  # through the generic decoder + translator: MOV AX,0x1234 (B8 34 12); INC CX (41); HLT (F4)
  # must leave ax=0x1234 and cx=1. Proves a .upcl description executes on a real backend.
  add_test(NAME upcl.run-8086 COMMAND sh -c
    "printf '\\xB8\\x34\\x12\\x41\\xF4' > '${CMAKE_BINARY_DIR}/prog8086.bin' && \
     '$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/prog8086.bin' \
       --arch 'upcl:${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/8086.upcl' | \
     grep -q 'ax =1234' && \
     '$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/prog8086.bin' \
       --arch 'upcl:${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/8086.upcl' | grep -q 'cx =0001'")

  # The 8086 `jump insn jmp : type branch { @i8086_jump(src) }` declaration, given an `encode`
  # with a PC-relative displacement, decodes and executes: mov ax,0x1234 ; jmp +2 ; inc cx ;
  # inc cx ; inc cx ; hlt -- the jump skips two of the three incs, so cx = 1 (not 3), ax stays
  # 0x1234. Proves the segmented jump-insn model runs near relative branches.
  add_test(NAME upcl.run-8086-jmp COMMAND sh -c
    "printf '\\xB8\\x34\\x12\\xEB\\x02\\x41\\x41\\x41\\xF4' > '${CMAKE_BINARY_DIR}/jmp8086.bin' && \
     OUT=$('$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/jmp8086.bin' \
       --arch 'upcl:${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/8086.upcl') && \
     echo \"$OUT\" | grep -q 'ax =1234' && echo \"$OUT\" | grep -q 'cx =0001'")

  # 8086 conditional jump (Jcc) + a real countdown loop: mov cx,3 ; loop: dec cx ; jnz loop ;
  # hlt. JNZ (0x75) decodes via the single jcc encoding (cond=5); @eval_cc(cond) computes !ZF
  # from the flags dec set; the loop runs three times, so cx ends at 0. Proves Jcc condition
  # evaluation + flag flow across the loop edge, all from the .upcl.
  add_test(NAME upcl.run-8086-jcc COMMAND sh -c
    "printf '\\xB9\\x03\\x00\\x49\\x75\\xFD\\xF4' > '${CMAKE_BINARY_DIR}/jcc8086.bin' && \
     '$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/jcc8086.bin' \
       --arch 'upcl:${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/8086.upcl' | grep -q 'cx =0000'")

  # 8086 segmented subroutine: ncall (E8) pushes ip via @i8086_push, nret (C3) pops it via
  # @i8086_jump(@i8086_pop()) -- exercising macro-overload resolution (near vs far i8086_jump),
  # the pc.off->ip composition, and the indirect-branch return. ncall func ; inc cx ; jmp end ;
  # func: inc dx ; ret ; end: hlt -- func runs once (dx=1) and control returns (cx=1).
  add_test(NAME upcl.run-8086-call COMMAND sh -c
    "printf '\\xE8\\x03\\x00\\x41\\xEB\\x02\\x42\\xC3\\xF4' > '${CMAKE_BINARY_DIR}/call8086.bin' && \
     OUT=$('$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/call8086.bin' \
       --arch 'upcl:${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/8086.upcl') && \
     echo \"$OUT\" | grep -q 'cx =0001' && echo \"$OUT\" | grep -q 'dx =0001'")

  # The modular, extensible x86 (x86.upcl includes x86/{disasm,8086,80186,v20}.upcl): the
  # family is split into a core + per-generation/vendor files, each gated on a feature a CPU
  # model selects. PUSH imm16 (0x68) decodes only with the 80186 `ext_186` feature (a v30 has
  # it); an 8086 leaves it an unknown byte. The base 8086 still runs (mov ax,0x1234).
  add_test(NAME upcl.x86-modular COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; E='$<TARGET_FILE:lcx>'; \
     \"$E\" upcl check \"$X\" | grep -q 'cpu .v30. = ext_186 nec' && \
     ! \"$E\" upcl check \"$X\" | grep -q 'CONFLICT' && \
     printf '\\x68\\x78\\x56' > '${CMAKE_BINARY_DIR}/x86push.bin' && \
     LCX_SYNTAX=intel \"$E\" disasm '${CMAKE_BINARY_DIR}/x86push.bin' --count 1 --arch \"upcl:$X@v30\"  | grep -q 'PUSH 5678h' && \
     \"$E\" disasm '${CMAKE_BINARY_DIR}/x86push.bin' --count 1 --arch \"upcl:$X@8086\" | grep -q 'db 0x68' && \
     printf '\\xB8\\x34\\x12\\x41\\xF4' > '${CMAKE_BINARY_DIR}/x86mod.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x86mod.bin' --arch \"upcl:$X@8086\" | grep -q 'ax =1234'")

  # Branches in the standard run path: a relative jmp whose target the decoder folds, so the
  # CFG wires the edge. Program: movi r0,0x1111 ; jmp +3 ; movi r0,0x9999 ; hlt -- the jump
  # skips the second movi, so r0 must stay 0x1111 (a NOT-taken branch would leave 0x9999).
  add_test(NAME upcl.run-branch COMMAND sh -c
    "printf '\\x04\\x11\\x11\\x10\\x03\\x04\\x99\\x99\\x00' > '${CMAKE_BINARY_DIR}/branch.bin' && \
     OUT=$('$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/branch.bin' \
       --arch 'upcl:${CMAKE_CURRENT_SOURCE_DIR}/test/branch.upcl') && \
     echo \"$OUT\" | grep -q 'r0 =1111' && ! echo \"$OUT\" | grep -q 'r0 =9999'")

  # A conditional-branch LOOP: movi r0,3 ; movi r1,0 ; {addi r1,10 ; subi r0,1 ; jnz r0,-8} ;
  # hlt. The jnz takes a signed backward displacement while r0 != 0, so the loop runs three
  # times: r1 = 30 (0x1e) and r0 = 0. Exercises TagConditional + TranslateCond + sign-extended
  # target folding -- the full loop machinery from a .upcl description.
  add_test(NAME upcl.run-loop COMMAND sh -c
    "printf '\\x04\\x03\\x00\\x05\\x00\\x00\\x09\\x0A\\x00\\x0C\\x01\\x00\\x44\\xF8\\x00' > '${CMAKE_BINARY_DIR}/loop.bin' && \
     OUT=$('$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/loop.bin' \
       --arch 'upcl:${CMAKE_CURRENT_SOURCE_DIR}/test/branch.upcl') && \
     echo \"$OUT\" | grep -q 'r1 =001e' && echo \"$OUT\" | grep -q 'r0 =0000'")

  # Subroutines: call pushes the return address + branches; ret pops it + indirect-branches.
  # movi r0,5 ; call func ; movi r1,7 ; jmp done ; func: addi r0,10 ; ret ; done: hlt. func
  # leaves r0=15 (0x0f) and -- proving the return -- control resumes after the call to set
  # r1=7. Exercises TagCall, the return-address push, and the IndirectBranch return.
  add_test(NAME upcl.run-call COMMAND sh -c
    "printf '\\x04\\x05\\x00\\x20\\x05\\x05\\x07\\x00\\x10\\x04\\x08\\x0A\\x00\\x21\\x00' > '${CMAKE_BINARY_DIR}/call.bin' && \
     OUT=$('$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/call.bin' \
       --arch 'upcl:${CMAKE_CURRENT_SOURCE_DIR}/test/branch.upcl') && \
     echo \"$OUT\" | grep -q 'r0 =000f' && echo \"$OUT\" | grep -q 'r1 =0007'")

  # m88k user-space OpenBSD emulation, generated from upcl/examples/m88k.upcl (big-endian, the full
  # syscall ABI). The image is a freestanding "hello" writer: it sets r13=4 (SYS_write), r2=1 (stdout),
  # r3=0x20 (the string), r4=6 (length), traps via `tb0 0,r0,450` (the OpenBSD syscall gate -> vector
  # 0x1c2), then sets r13=1 (SYS_exit), r2=0 and traps again. The "hello\n" bytes sit at offset 0x20.
  # --abi obsd-m88k routes the trap vector through the host syscall shim, so the guest write reaches
  # real stdout. Proves the generated frontend + the trap/syscall seam end to end.
  add_test(NAME upcl.m88k-syscall COMMAND sh -c
    "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/m88k.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\x59\\xa0\\x00\\x04\\x58\\x40\\x00\\x01\\x58\\x60\\x00\\x20\\x58\\x80\\x00\\x06\\xf0\\x00\\xd1\\xc2\\x59\\xa0\\x00\\x01\\x58\\x40\\x00\\x00\\xf0\\x00\\xd1\\xc2\\x68\\x65\\x6c\\x6c\\x6f\\x0a' \
       > '${CMAKE_BINARY_DIR}/hello.m88k' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/hello.m88k' --arch \"upcl:$M\" --abi obsd-m88k | grep -q '^hello$'")

  # The same image wrapped in a real OpenBSD/m88k a.out exec header (OMAGIC): a_midmag packs
  # MID_M88K(153)<<16 | 0407 = 0x00990107, a_text=0x20, a_data=6, a_entry=0; the 38-byte image
  # follows the 32-byte header. --load aout validates the machine id, strips the header, loads
  # text+data at base 0, and takes the entry from a_entry. Exercises the a.out loader, not just
  # a flat image: a non-m88k a.out is rejected, and a non-zero a_entry is honored.
  add_test(NAME upcl.m88k-aout COMMAND sh -c
    "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/m88k.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\x00\\x99\\x01\\x07\\x00\\x00\\x00\\x20\\x00\\x00\\x00\\x06\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x59\\xa0\\x00\\x04\\x58\\x40\\x00\\x01\\x58\\x60\\x00\\x20\\x58\\x80\\x00\\x06\\xf0\\x00\\xd1\\xc2\\x59\\xa0\\x00\\x01\\x58\\x40\\x00\\x00\\xf0\\x00\\xd1\\xc2\\x68\\x65\\x6c\\x6c\\x6f\\x0a' \
       > '${CMAKE_BINARY_DIR}/hello.m88k.aout' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/hello.m88k.aout' --arch \"upcl:$M\" --abi obsd-m88k --load aout | grep -q '^hello$'")

  # A REAL OpenBSD/m88k binary: test/bin/m88k/openbsd/sync (a ZMAGIC a.out). lcx maps the whole file
  # at the m88k text base (0x1000), builds the argc/argv/envp entry stack, and runs crt0 -> main. The
  # binary issues the genuine OpenBSD syscalls sync(36) and exit(1) through the `tb0 0,r0,128` gate
  # (vector 0x80). This exercises the full chain end to end on real compiler output: the ZMAGIC loader,
  # the uframe, the hardwired-zero r0 (RISC register file), the delayed branches (br.n/bsr.n/jmp.n/
  # jsr.n), the register-indexed loads/stores, and the carry-flag syscall ABI. LCX_STRACE logs each
  # syscall; the test confirms the real sync(36) fired and the process exited (no hang/crash).
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../test/bin/m88k/openbsd/sync")
    add_test(NAME upcl.m88k-obsd-sync COMMAND sh -c
      "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/m88k.upcl'; E='$<TARGET_FILE:lcx>'; \
       B='${CMAKE_CURRENT_SOURCE_DIR}/../test/bin/m88k/openbsd/sync'; \
       LCX_STRACE=1 \"$E\" run \"$B\" --arch \"upcl:$M\" --abi obsd-m88k --load aout 2>&1 | grep -q 'r13=36'")
    set_tests_properties(upcl.m88k-obsd-sync PROPERTIES TIMEOUT 60)
  endif()

  # Floating-point foundation: 80-bit (long double) FP registers, BinF* arithmetic, and int<->float
  # casts through the emitter ABI + interpreter. fp.upcl loads small integers as floats, computes,
  # and stores the result back to an integer register. The decisive program is (5 / 2) * 2: real
  # floats give (2.5) * 2 = 5, integer arithmetic would give (2) * 2 = 4 -- so r0 = 5 proves the
  # values are genuine floating point. Also checks 5 + 3 = 8 and 5 * 3 = 15.
  add_test(NAME upcl.fp COMMAND sh -c
    "F='${CMAKE_CURRENT_SOURCE_DIR}/test/fp.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\x10\\x05\\x11\\x02\\x23\\x22\\x30\\xFF' > '${CMAKE_BINARY_DIR}/fpdiv.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/fpdiv.bin' --arch \"upcl:$F\" | grep -q 'r0 =0005' && \
     printf '\\x10\\x05\\x11\\x03\\x20\\x30\\xFF' > '${CMAKE_BINARY_DIR}/fpadd.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/fpadd.bin' --arch \"upcl:$F\" | grep -q 'r0 =0008' && \
     printf '\\x10\\x05\\x11\\x03\\x22\\x30\\xFF' > '${CMAKE_BINARY_DIR}/fpmul.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/fpmul.bin' --arch \"upcl:$F\" | grep -q 'r0 =000f'")

  # Runtime-indexed register access: a register array (4 ** #f80 f?) addressed by a value computed
  # at run time -- the mechanism the 8087 stack ST((TOP+i)&7) needs. The program writes f[2]=7 then
  # f[0]=99 and reads f[2]; r0 = 7 (not 99) proves the index selects element 2 at run time. Reuses
  # the Load/Store primitives via a register-bank address sentinel, so no new emitter method.
  add_test(NAME upcl.fp-indexed COMMAND sh -c
    "F='${CMAKE_CURRENT_SOURCE_DIR}/test/fparray.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\x10\\x02\\x11\\x07\\x10\\x00\\x11\\x63\\x10\\x02\\x20\\xFF' > '${CMAKE_BINARY_DIR}/fparray.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/fparray.bin' --arch \"upcl:$F\" | grep -q 'r0 =0007'")

  # The Intel 8080 (i8080.upcl) -- the set the V20/V30 emulate. Decode must distinguish HLT
  # (0x76, a full opcode) from the general MOV r,r pattern (most-specific match) and render
  # sub-registers by their own name (MVI B, not MVI BC). Runs MVI A,5 ; MVI B,3 ; ADD B ; HLT
  # -> A=8, and HLT halts (wait-on-interrupt).
  add_test(NAME upcl.i8080 COMMAND sh -c
    "I='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/i8080.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\x3E\\x05\\x06\\x03\\x80\\x76' > '${CMAKE_BINARY_DIR}/i8080.bin' && \
     OUT=$(\"$E\" disasm '${CMAKE_BINARY_DIR}/i8080.bin' --count 4 --arch \"upcl:$I\") && \
     echo \"$OUT\" | grep -q 'MVI B, 03H' && echo \"$OUT\" | grep -q 'HLT' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/i8080.bin' --arch \"upcl:$I\" | grep -q 'a  =08'")

  # 80186 PUSHA/POPA roundtrip (a v30 has ext_186): mov ax,9 ; pusha ; mov ax,1 ; popa ; hlt
  # restores ax to 9. Also a NEC BRKEM (0F FF) decodes only with `nec`.
  add_test(NAME upcl.v30-ext COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\xB8\\x09\\x00\\x60\\xB8\\x01\\x00\\x61\\xF4' > '${CMAKE_BINARY_DIR}/pusha.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/pusha.bin' --arch \"upcl:$X@v30\" | grep -q 'ax =0009' && \
     printf '\\x0F\\xFF\\x07' > '${CMAKE_BINARY_DIR}/brkem.bin' && \
     LCX_SYNTAX=intel \"$E\" disasm '${CMAKE_BINARY_DIR}/brkem.bin' --count 1 --arch \"upcl:$X@v30\"  | grep -q 'BRKEM 07h' && \
     \"$E\" disasm '${CMAKE_BINARY_DIR}/brkem.bin' --count 1 --arch \"upcl:$X@8086\" | grep -q 'db 0x0f'")

  # ModR/M variable-length memory addressing via the generic `addrmode modrm16/modrm8` tables.
  # mov ax,0x1111 ; mov [0x50],ax ; mov bx,[0x50] ; add [0x50],bx ; mov cx,[0x50] ; movb dl,[0x50] ; hlt
  # The store/load round-trips through memory (cx = 0x2222 proves add [mem],bx wrote back) and the
  # byte load reads dl = 0x22. Disasm renders the EA per style: Intel [0050h], AT&T 0x0050 (a memory
  # displacement, so NO `$` -- distinct from an immediate $0x1234).
  add_test(NAME upcl.x86-modrm COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\xB8\\x11\\x11\\x89\\x06\\x50\\x00\\x8B\\x1E\\x50\\x00\\x01\\x1E\\x50\\x00\\x8B\\x0E\\x50\\x00\\x8A\\x16\\x50\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x86modrm.bin' && \
     LCX_SYNTAX=intel \"$E\" disasm '${CMAKE_BINARY_DIR}/x86modrm.bin' --count 4 --arch \"upcl:$X@8086\" | grep -q 'MOV \\[0050h\\], AX' && \
     LCX_SYNTAX=att   \"$E\" disasm '${CMAKE_BINARY_DIR}/x86modrm.bin' --count 3 --arch \"upcl:$X@8086\" | grep -q 'movw %ax, 0x0050' && \
     OUT=$(\"$E\" run '${CMAKE_BINARY_DIR}/x86modrm.bin' --arch \"upcl:$X@8086\") && \
     echo \"$OUT\" | grep -q 'cx =2222' && echo \"$OUT\" | grep -q 'dx =0022'")

  # Immediate-to-r/m group: the grouped opcodes 0x81 (iw) / 0x83 (ib, sign-extended) whose ModR/M
  # `reg` field is an opcode extension (/digit) selecting the ALU op, and whose immediate is a TAIL
  # field decoded AFTER the variable-length displacement. mov ax,5 ; add ax,-1 (0x83 /0 sx, ax=4) ;
  # mov bx,0x100 ; or bx,0x000F (0x81 /1, bx=0x010F) ; mov [0x60],ax ; add [0x60],0x10 (0x83 /0 to
  # memory: imm after disp16) ; mov cx,[0x60] (cx=0x14). Disasm shows the sign-extended 0ffffh.
  add_test(NAME upcl.x86-immrm COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\xB8\\x05\\x00\\x83\\xC0\\xFF\\xBB\\x00\\x01\\x81\\xCB\\x0F\\x00\\x89\\x06\\x60\\x00\\x83\\x06\\x60\\x00\\x10\\x8B\\x0E\\x60\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x86immrm.bin' && \
     LCX_SYNTAX=intel \"$E\" disasm '${CMAKE_BINARY_DIR}/x86immrm.bin' --count 6 --arch \"upcl:$X@8086\" | grep -q 'ADD AX, 0ffffh' && \
     LCX_SYNTAX=intel \"$E\" disasm '${CMAKE_BINARY_DIR}/x86immrm.bin' --count 6 --arch \"upcl:$X@8086\" | grep -q 'ADD \\[0060h\\], 0010h' && \
     OUT=$(\"$E\" run '${CMAKE_BINARY_DIR}/x86immrm.bin' --arch \"upcl:$X@8086\") && \
     echo \"$OUT\" | grep -q 'ax =0004' && echo \"$OUT\" | grep -q 'bx =010f' && echo \"$OUT\" | grep -q 'cx =0014'")

  # The 0xF7 /digit group: the unary/multiply/divide opcodes whose ModR/M `reg` field is an opcode
  # extension. NOT/NEG (/2,/3), MUL/IMUL (/4,/5: 16x16->32-bit DX:AX product), DIV/IDIV (/6,/7:
  # 32-bit DX:AX dividend), and TEST imm (/0, a tail immediate). The signed IDIV is a regression
  # guard for the interp backend's BinSDiv (it must divide as INT64, not on the sign-extended
  # UINT64 bit pattern): -20/3 must give ax=0xFFFA (-6), dx=0xFFFE (-2), not the unsigned 0x554E.
  add_test(NAME upcl.x86-f7group COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\xF7\\xD0\\xF7\\xD8\\xF7\\xE3\\xF7\\xEB\\xF7\\xF3\\xF7\\xFB\\xF7\\x06\\x40\\x00\\x34\\x12' > '${CMAKE_BINARY_DIR}/x86f7.bin' && \
     D=$(LCX_SYNTAX=intel \"$E\" disasm '${CMAKE_BINARY_DIR}/x86f7.bin' --count 7 --arch \"upcl:$X@8086\") && \
     echo \"$D\" | grep -q 'NOT AX' && echo \"$D\" | grep -q 'MUL BX' && echo \"$D\" | grep -q 'IDIV BX' && \
     echo \"$D\" | grep -q 'TEST \\[0040h\\], 1234h' && \
     printf '\\xBA\\xFF\\xFF\\xB8\\xEC\\xFF\\xBB\\x03\\x00\\xF7\\xFB\\xF4' > '${CMAKE_BINARY_DIR}/x86idiv.bin' && \
     OUT=$(\"$E\" run '${CMAKE_BINARY_DIR}/x86idiv.bin' --arch \"upcl:$X@8086\") && \
     echo \"$OUT\" | grep -q 'ax =fffa' && echo \"$OUT\" | grep -q 'dx =fffe' && \
     printf '\\xB8\\x00\\x10\\xBB\\x10\\x00\\xF7\\xE3\\xF4' > '${CMAKE_BINARY_DIR}/x86mul.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x86mul.bin' --arch \"upcl:$X@8086\" | grep -q 'dx =0001'")

  # The 0xFE/0xFF /digit group: INC/DEC r/m (/0,/1), PUSH r/m (/6), and the INDIRECT near transfers
  # CALL r/m (/2) and JMP r/m (/4), whose target is the operand value computed at run time. The
  # indirect call/jmp exercise the framework's computed-transfer path (tagger + IndirectBranch),
  # previously used only by `ret`. Program: ax=5 ; bx=0x0E ; call bx -> [func: add ax,3 ; ret] ;
  # add ax,10 -> ax must be 0x12 (5+3+10), proving the indirect call branched AND returned.
  add_test(NAME upcl.x86-feff COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\xFF\\xC0\\xFF\\xC8\\xFE\\xC3\\xFF\\xF3\\xFF\\xD3\\xFF\\xE3\\xFF\\x26\\x40\\x00' > '${CMAKE_BINARY_DIR}/x86feff.bin' && \
     D=$(LCX_SYNTAX=intel \"$E\" disasm '${CMAKE_BINARY_DIR}/x86feff.bin' --count 7 --arch \"upcl:$X@8086\") && \
     echo \"$D\" | grep -q 'INC AX' && echo \"$D\" | grep -q 'INC BL' && echo \"$D\" | grep -q 'PUSH BX' && \
     echo \"$D\" | grep -q 'CALL BX' && echo \"$D\" | grep -q 'JMP \\[0040h\\]' && \
     printf '\\xB8\\x05\\x00\\xBB\\x0E\\x00\\xFF\\xD3\\x83\\xC0\\x0A\\xF4\\x90\\x90\\x83\\xC0\\x03\\xC3' > '${CMAKE_BINARY_DIR}/x86icall.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x86icall.bin' --arch \"upcl:$X@8086\" | grep -q 'ax =0012' && \
     printf '\\xB8\\x01\\x00\\xBB\\x0C\\x00\\xFF\\xE3\\xB8\\xFF\\x00\\x90\\xB8\\x99\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x86ijmp.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x86ijmp.bin' --arch \"upcl:$X@8086\" | grep -q 'ax =0099'")

  # Shifts/rotates: 0xD1 (by 1) and 0xD3 (by CL) /digit. The shift count is an IMPLICIT operand --
  # a fixed 1 (no encoding bits) for 0xD1, the CL register for 0xD3 -- so it disassembles (SHL AX,
  # 01h / SHL AX, CL) without occupying instruction bits. Execution checks SAR (arithmetic, sign
  # preserved: 0x8000 -> 0xF800) is distinct from SHR (logical: 0x8000 -> 0x0800), plus SHL by CL.
  add_test(NAME upcl.x86-shift COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\xD1\\xE0\\xD3\\xE0\\xD1\\xC0\\xD3\\xF8' > '${CMAKE_BINARY_DIR}/x86sh.bin' && \
     D=$(LCX_SYNTAX=intel \"$E\" disasm '${CMAKE_BINARY_DIR}/x86sh.bin' --count 4 --arch \"upcl:$X@8086\") && \
     echo \"$D\" | grep -q 'SHL AX, 01h' && echo \"$D\" | grep -q 'SHL AX, CL' && echo \"$D\" | grep -q 'SAR AX, CL' && \
     printf '\\xB8\\x01\\x00\\xB1\\x04\\xD3\\xE0\\xF4' > '${CMAKE_BINARY_DIR}/x86shl.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x86shl.bin' --arch \"upcl:$X@8086\" | grep -q 'ax =0010' && \
     printf '\\xB8\\x00\\x80\\xB1\\x04\\xD3\\xF8\\xF4' > '${CMAKE_BINARY_DIR}/x86sar.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x86sar.bin' --arch \"upcl:$X@8086\" | grep -q 'ax =f800' && \
     printf '\\xB8\\x00\\x80\\xB1\\x04\\xD3\\xE8\\xF4' > '${CMAKE_BINARY_DIR}/x86shr.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x86shr.bin' --arch \"upcl:$X@8086\" | grep -q 'ax =0800'")

  # `address_display`: how a code address is shown in a disassembly listing. x86 declares it
  # segmented (shift 4, offset 16), so addresses render as the real-mode CS:IP pair (0000:0002);
  # the i8080, with no declaration, keeps the default flat form ($0002:).
  add_test(NAME upcl.addr-display COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; I='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/i8080.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\x89\\xC1\\x89\\xC1\\xEB\\xFE' > '${CMAKE_BINARY_DIR}/x86seg.bin' && \
     S=$(\"$E\" disasm '${CMAKE_BINARY_DIR}/x86seg.bin' --count 3 --arch \"upcl:$X@8086\") && \
     echo \"$S\" | grep -q '0000:0000:' && echo \"$S\" | grep -q '0000:0002:' && echo \"$S\" | grep -q '0000:0004:' && \
     ! echo \"$S\" | grep -q '[$]0000:' && \
     printf '\\x3E\\x05\\x06\\x03' > '${CMAKE_BINARY_DIR}/i80seg.bin' && \
     F=$(\"$E\" disasm '${CMAKE_BINARY_DIR}/i80seg.bin' --count 2 --arch \"upcl:$I\") && \
     echo \"$F\" | grep -q '[$]0000:' && ! echo \"$F\" | grep -q '0000:0000:'")

  # The 8087 numeric coprocessor (feature fpu): the ESC (0xD8-0xDF) group over an 80-bit FP register
  # stack with a TOP-relative rotating pointer (runtime-indexed register access). The decisive
  # program computes (7 / 2) * 2 entirely on the 8087 stack -- fild 2, fild 7, fdiv st(1), fmul
  # st(1), fist -- and stores 7: integer arithmetic would give (3) * 2 = 6, so a memory result of 7
  # proves genuine floating point through the stack. Also FADDP 6+7=13 and FMULP 6*7=42, and the
  # ESC opcodes disassemble (FLD1 / FILD / FIST).
  add_test(NAME upcl.x86-8087 COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\xD9\\xE8\\xDF\\x06\\x40\\x00\\xDF\\x16\\x40\\x00' > '${CMAKE_BINARY_DIR}/x87dis.bin' && \
     D=$(LCX_SYNTAX=intel \"$E\" disasm '${CMAKE_BINARY_DIR}/x87dis.bin' --count 3 --arch \"upcl:$X@8086\") && \
     echo \"$D\" | grep -q 'FLD1' && echo \"$D\" | grep -q 'FILD .0040h.' && echo \"$D\" | grep -q 'FIST .0040h.' && \
     printf '\\xB8\\x02\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xB8\\x07\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xD8\\xF1\\xD8\\xC9\\xDF\\x16\\x40\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87fp.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87fp.bin' --arch \"upcl:$X@8086\" --dump 0x40 | grep -q '0x40. = 0x0007' && \
     printf '\\xB8\\x06\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xB8\\x07\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xDE\\xC1\\xDF\\x16\\x40\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87add.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87add.bin' --arch \"upcl:$X@8086\" --dump 0x40 | grep -q '0x40. = 0x000d'")

  # 8087 memory reals: FLD/FST/FSTP m32real & m64real (the float-width memory casts -- raw IEEE bytes
  # reinterpreted to/from the 80-bit stack). Compute 7/2 = 3.5, FSTP it to memory as a float32,
  # reload with FLD m32, multiply by 2, store the integer: a result of 7 proves the 3.5 survived the
  # float32 round-trip (a truncation to int would give 3*2 = 6). The m64real path is checked too.
  add_test(NAME upcl.x86-8087-mem COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\xB8\\x02\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xB8\\x07\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xD8\\xF1\\xD9\\x1E\\x44\\x00\\xD9\\x06\\x44\\x00\\xB8\\x02\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xDE\\xC9\\xDF\\x16\\x40\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87m32.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87m32.bin' --arch \"upcl:$X@8086\" --dump 0x40 | grep -q '0x40. = 0x0007' && \
     printf '\\xB8\\x02\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xB8\\x07\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xD8\\xF1\\xDD\\x1E\\x44\\x00\\xDD\\x06\\x44\\x00\\xB8\\x02\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xDE\\xC9\\xDF\\x16\\x40\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87m64.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87m64.bin' --arch \"upcl:$X@8086\" --dump 0x40 | grep -q '0x40. = 0x0007'")

  # 8087 unary ops and compares. FSQRT(9)=3, FCHS(5)=-5 (0xfffb), FABS of that = 5. Then FCOM sets the
  # condition codes, observed the authentic 8087 way -- FSTSW to memory (the 8087 has no FSTSW AX),
  # then the CPU tests the bits: 3 < 5 sets C0 (status & 0x0100 != 0), 5 == 5 sets C3 (& 0x4000).
  add_test(NAME upcl.x86-8087-cmp COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\xB8\\x09\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xD9\\xFA\\xDF\\x16\\x40\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87sqrt.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87sqrt.bin' --arch \"upcl:$X@8086\" --dump 0x40 | grep -q '0x40. = 0x0003' && \
     printf '\\xB8\\x05\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xD9\\xE0\\xD9\\xE1\\xDF\\x16\\x40\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87abs.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87abs.bin' --arch \"upcl:$X@8086\" --dump 0x40 | grep -q '0x40. = 0x0005' && \
     printf '\\xB8\\x05\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xB8\\x03\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xD8\\xD1\\xDD\\x3E\\x44\\x00\\x8B\\x06\\x44\\x00\\x81\\xE0\\x00\\x01\\x89\\x06\\x46\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87cl.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87cl.bin' --arch \"upcl:$X@8086\" --dump 0x46 | grep -q '0x46. = 0x0100' && \
     printf '\\xB8\\x05\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xB8\\x05\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xD8\\xD1\\xDD\\x3E\\x44\\x00\\x8B\\x06\\x44\\x00\\x81\\xE0\\x00\\x40\\x89\\x06\\x46\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87ce.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87ce.bin' --arch \"upcl:$X@8086\" --dump 0x46 | grep -q '0x46. = 0x4000'")

  # 8087 control: FNINIT loads the default control word 0x037F (round-to-nearest, exceptions masked),
  # read back with FNSTCW; FLDCW/FNSTCW round-trip an arbitrary control word (0x027F). FWAIT decodes.
  add_test(NAME upcl.x86-8087-ctrl COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\xDB\\xE3\\xD9\\x3E\\x40\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87init.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87init.bin' --arch \"upcl:$X@8086\" --dump 0x40 | grep -q '0x40. = 0x037f' && \
     printf '\\xB8\\x7F\\x02\\x89\\x06\\x40\\x00\\xD9\\x2E\\x40\\x00\\xD9\\x3E\\x42\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87cw.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87cw.bin' --arch \"upcl:$X@8086\" --dump 0x42 | grep -q '0x42. = 0x027f' && \
     printf '\\x9B' > '${CMAKE_BINARY_DIR}/x87wait.bin' && \
     LCX_SYNTAX=intel \"$E\" disasm '${CMAKE_BINARY_DIR}/x87wait.bin' --count 1 --arch \"upcl:$X@8086\" | grep -q 'FWAIT'")

  # 8087 on-chip constants (UPCL float literals -> double-precision FP constants via the bit-reinterpret
  # path, no new emitter method). FLDPI loads pi; * 100, truncated, = 314 (0x013a), proving the value is
  # 3.14159..., not an integer. FLDL2E loads log2(e) = 1.4427; * 1000 = 1442 (0x05a2).
  add_test(NAME upcl.x86-8087-const COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\xD9\\xEB\\xB8\\x64\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xDE\\xC9\\xDF\\x16\\x40\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87pi.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87pi.bin' --arch \"upcl:$X@8086\" --dump 0x40 | grep -q '0x40. = 0x013a' && \
     printf '\\xD9\\xEA\\xB8\\xE8\\x03\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xDE\\xC9\\xDF\\x16\\x40\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87l2e.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87l2e.bin' --arch \"upcl:$X@8086\" --dump 0x40 | grep -q '0x40. = 0x05a2'")

  # 8087 transcendentals (the genuine 8087 set -- F2XM1/FYL2X/FPTAN/FPATAN/FYL2XP1; FSIN/FCOS are 387+).
  # Each result is an exact integer, so any wrong op or pop would miss it.
  #   F2XM1   : 2**3 - 1 = 7                          (FILD 3; F2XM1; FISTP)
  #   FYL2X   : 1 * log2(8) = 3                       (FLD1; FILD 8; FYL2X; FISTP)
  #   FYL2XP1 : 1 * log2(7 + 1) = 3                   (FLD1; FILD 7; FYL2XP1; FISTP)
  #   FPTAN/FPATAN round-trip: atan2(tan(1 rad), 1) = 1 rad = 1, proving tan and the two-arg arctangent
  #     compose to the identity (FLD1; FPTAN; FPATAN; FISTP).
  add_test(NAME upcl.x86-8087-trans COMMAND sh -c
    "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/x86.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\xB8\\x03\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xD9\\xF0\\xDF\\x1E\\x40\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87_2xm1.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87_2xm1.bin' --arch \"upcl:$X@8086\" --dump 0x40 | grep -q '0x40. = 0x0007' && \
     printf '\\xD9\\xE8\\xB8\\x08\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xD9\\xF1\\xDF\\x1E\\x40\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87_yl2x.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87_yl2x.bin' --arch \"upcl:$X@8086\" --dump 0x40 | grep -q '0x40. = 0x0003' && \
     printf '\\xD9\\xE8\\xB8\\x07\\x00\\x89\\x06\\x40\\x00\\xDF\\x06\\x40\\x00\\xD9\\xF9\\xDF\\x1E\\x40\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87_yl2xp1.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87_yl2xp1.bin' --arch \"upcl:$X@8086\" --dump 0x40 | grep -q '0x40. = 0x0003' && \
     printf '\\xD9\\xE8\\xD9\\xF2\\xD9\\xF3\\xDF\\x1E\\x40\\x00\\xF4' > '${CMAKE_BINARY_DIR}/x87_tan.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/x87_tan.bin' --arch \"upcl:$X@8086\" --dump 0x40 | grep -q '0x40. = 0x0001'")
endif()

# Disassembly: one declarative `disasm ( mnemonic:"mov", size:word, operands:dst,src )` plus
# the `disasm features` block + disasm macros render BOTH syntaxes. B8 34 12 (MOV AX,0x1234)
# is "MOV AX, 1234h" (Intel: upper mnemonic/reg, h-suffix) and "movw $0x1234, %ax" (AT&T:
# lower mnemonic+size suffix, reversed, %reg, $0x). B8 CD AB (0xABCD) is "MOV AX, 0abcdh" --
# the macro's `( value & 0x8000 ) ? "0"...` conditional adds a leading 0 before a letter digit.
add_test(NAME upcl.disasm COMMAND sh -c
  "D='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/8086.upcl'; E='$<TARGET_FILE:lcx>'; \
   printf '\\xB8\\x34\\x12' > '${CMAKE_BINARY_DIR}/disasm.bin' && \
   printf '\\xB8\\xCD\\xAB' > '${CMAKE_BINARY_DIR}/disasm3.bin' && \
   LCX_SYNTAX=intel \"$E\" disasm '${CMAKE_BINARY_DIR}/disasm.bin'  --count 1 --arch \"upcl:$D\" | grep -q 'MOV AX, 1234h' && \
   LCX_SYNTAX=att   \"$E\" disasm '${CMAKE_BINARY_DIR}/disasm.bin'  --count 1 --arch \"upcl:$D\" | grep -q 'movw .0x1234, %ax' && \
   LCX_SYNTAX=intel \"$E\" disasm '${CMAKE_BINARY_DIR}/disasm3.bin' --count 1 --arch \"upcl:$D\" | grep -q 'MOV AX, 0abcdh'")

# Clang-style diagnostics: a stray top-level token resyncs at the next declaration (no
# cascade), and the error count is capped. One bad line + a good insn after it => exactly
# one error, and the good declaration still parses.
add_test(NAME upcl.diag-recover COMMAND sh -c
  "printf 'arch \\\"x\\\" { word_size 16; }\\n%% bogus garbage\\ninsn good : ;\\n' > '${CMAKE_BINARY_DIR}/diag.upcl' && \
   N=$('$<TARGET_FILE:lcx>' upcl check '${CMAKE_BINARY_DIR}/diag.upcl' 2>&1 | grep -c 'error:') && \
   test \"$N\" -eq 1")

# Feature / CPU-model selection: ONE description (test/x86.upcl) carries the 8086 base ISA
# plus the 80186 and NEC V20/V30 additions as selectable features; a `cpu` model bundles
# the ones it has. `check` reports the models and verifies no two enabled instructions
# decode the same bytes (no CONFLICT).
add_test(NAME upcl.features COMMAND lcx upcl check "${CMAKE_CURRENT_SOURCE_DIR}/test/x86.upcl")
set_tests_properties(upcl.features PROPERTIES
  PASS_REGULAR_EXPRESSION "cpu \"v30\" = ext_186 nec"
  FAIL_REGULAR_EXPRESSION "CONFLICT")

# Decode gating: the 80186 PUSH imm16 (0x68) and NEC BRKEM (0F FF) are unknown bytes on an
# 8086 model but decode on a V30 -- proving the selected model filters the instruction set.
add_test(NAME upcl.gate-8086 COMMAND sh -c
  "printf '\\xB8\\x00\\x10\\x68\\x34\\x12\\x0F\\xFF' > '${CMAKE_BINARY_DIR}/x86gate.bin' && \
   '$<TARGET_FILE:lcx>' disasm '${CMAKE_BINARY_DIR}/x86gate.bin' --count 4 \
     --arch 'upcl:${CMAKE_CURRENT_SOURCE_DIR}/test/x86.upcl@8086' | grep -q 'db 0x68'")
add_test(NAME upcl.gate-v30 COMMAND sh -c
  "printf '\\xB8\\x00\\x10\\x68\\x34\\x12\\x0F\\xFF' > '${CMAKE_BINARY_DIR}/x86gate.bin' && \
   '$<TARGET_FILE:lcx>' disasm '${CMAKE_BINARY_DIR}/x86gate.bin' --count 3 \
     --arch 'upcl:${CMAKE_CURRENT_SOURCE_DIR}/test/x86.upcl@v30' > '${CMAKE_BINARY_DIR}/x86gate.out' && \
   grep -q 'push 0x1234' '${CMAKE_BINARY_DIR}/x86gate.out' && grep -q 'brkem' '${CMAKE_BINARY_DIR}/x86gate.out'")

# The hand-written parser implements the standard UPCL grammar (register_file with sub-
# register splits and flag bit-maps, macros, decoder_operands, jump insns, the rich semantics
# language). Parse every reference description -- x86/80386/68k/MIPS/SPARC/m88k/PDP-1/6502 --
# with no diagnostics. Drives the normative flex/bison grammar through the new front end.
file(GLOB LIBCPU_UPCL_EXAMPLES "${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/*.upcl")
foreach(_upcl ${LIBCPU_UPCL_EXAMPLES})
  get_filename_component(_upclname ${_upcl} NAME_WE)
  add_test(NAME upcl.parse-${_upclname} COMMAND lcx upcl check "${_upcl}")
  set_tests_properties(upcl.parse-${_upclname} PROPERTIES FAIL_REGULAR_EXPRESSION "error|not valid")
endforeach()
# _test_extends_cycle.upcl is an intentionally invalid file (cycle detection test): override
# the auto-generated parse test so it passes when the cycle diagnostic is emitted -- clear the
# FAIL_REGULAR_EXPRESSION and require the cycle error text to be present instead.
set_tests_properties(upcl.parse-_test_extends_cycle PROPERTIES
  FAIL_REGULAR_EXPRESSION ""
  PASS_REGULAR_EXPRESSION "cycle detected")

# The 6502 frontend is GENERATED from 6502.upcl (Cpu6502.generated.cpp, checked in and compiled
# in place of a hand-written frontend). Guard against drift: regenerate it and confirm the
# committed copy is byte-identical -- so editing 6502.upcl without regenerating fails the build.
add_test(NAME upcl.gen-6502 COMMAND sh -c
  "E='$<TARGET_FILE:lcx>'; U='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/6502.upcl'; \
   G='${CMAKE_CURRENT_SOURCE_DIR}/frontends/m6502/Cpu6502.generated.cpp'; \
   \"$E\" upcl gen \"$U\" Create6502 > '${CMAKE_BINARY_DIR}/Cpu6502.regen.cpp' && \
   diff -u \"$G\" '${CMAKE_BINARY_DIR}/Cpu6502.regen.cpp'")

# The CHIP-8 frontend is likewise generated from chip8.upcl (Chip8.generated.cpp, checked in).
# Same drift guard: regenerate and confirm the committed copy is byte-identical.
add_test(NAME upcl.gen-chip8 COMMAND sh -c
  "E='$<TARGET_FILE:lcx>'; U='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/chip8.upcl'; \
   G='${CMAKE_CURRENT_SOURCE_DIR}/frontends/chip8/Chip8.generated.cpp'; \
   \"$E\" upcl gen \"$U\" CreateChip8 > '${CMAKE_BINARY_DIR}/Chip8.regen.cpp' && \
   diff -u \"$G\" '${CMAKE_BINARY_DIR}/Chip8.regen.cpp'")

# Register layout: flattening 8086.upcl's register_file must reproduce the real hardware --
# ah/al are the high/low bytes of ax, and the carry flag lands on FLAGS bit 0 mapped to the
# meta carry %C. Locks the MSB-first bit-field walk against regressions.
add_test(NAME upcl.layout COMMAND sh -c
  "'$<TARGET_FILE:lcx>' upcl check '${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/8086.upcl' > '${CMAKE_BINARY_DIR}/layout.out' && \
   grep -q 'sub  ah     = ax\\[15:8\\]' '${CMAKE_BINARY_DIR}/layout.out' && \
   grep -q 'sub  al     = ax\\[7:0\\]'  '${CMAKE_BINARY_DIR}/layout.out' && \
   grep -q 'flag C      = flags\\[0\\] -> %C' '${CMAKE_BINARY_DIR}/layout.out' && \
   grep -q 'flag O      = flags\\[11\\] -> %V' '${CMAKE_BINARY_DIR}/layout.out' && \
   grep -q 'r13  pc     #i24  \\[PC\\]' '${CMAKE_BINARY_DIR}/layout.out'")

# Encoding is architecture-NEUTRAL bit-fields: the same `encode #iN ( field:width [=const |
# ->operand] )` describes a 32-bit big-endian RISC R-type/I-type word AND an x86 form (whose
# +r register is just the opcode's low 3 bits as a field) -- no byte / ModR/M notion in the
# language. Field widths must sum to the word.
add_test(NAME upcl.encoding COMMAND sh -c
  "set -e; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl check '${CMAKE_CURRENT_SOURCE_DIR}/test/risc.upcl' > '${CMAKE_BINARY_DIR}/enc.risc' && \
   grep -q 'encode #i32 ( op:6=0x0 rs:5->rs rt:5->rt rd:5->rd shamt:5=0x0 funct:6=0x20 )' '${CMAKE_BINARY_DIR}/enc.risc' && \
   grep -q 'encode #i32 ( op:6=0x8 rs:5->rs rt:5->rt imm:16->imm )' '${CMAKE_BINARY_DIR}/enc.risc' && \
   \"$E\" upcl check '${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/8086.upcl' > '${CMAKE_BINARY_DIR}/enc.8086' && \
   grep -q 'encode #i24 ( op:5=0x17 reg:3->dst.gpr16. imm:16->src )' '${CMAKE_BINARY_DIR}/enc.8086' && \
   grep -q 'encode #i8 ( op:8=0xf4 )' '${CMAKE_BINARY_DIR}/enc.8086'")
set_tests_properties(upcl.encoding PROPERTIES FAIL_REGULAR_EXPRESSION "error|bits but the word is")

# Decode: a real 8086 byte stream through the generic decoder, each insn translated with its
# decoded operands bound. B8 34 12 = MOV AX,0x1234 (the LE immediate must byte-swap to 0x1234
# and the +r register resolve to ax); 41 = INC CX (reg field 1 -> cx, read+written as a
# location operand); F4 = HLT. Proves decode -> operand resolution -> semantics end to end.
add_test(NAME upcl.decode COMMAND sh -c
  "'$<TARGET_FILE:lcx>' upcl decode '${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/8086.upcl' \
     0xB8 0x34 0x12 0x41 0xF4 > '${CMAKE_BINARY_DIR}/decode.out' && \
   grep -q 'mov    dst=ax src=0x1234' '${CMAKE_BINARY_DIR}/decode.out' && \
   grep -q 'put r1 <- v0'             '${CMAKE_BINARY_DIR}/decode.out' && \
   grep -q 'inc    dst=cx'            '${CMAKE_BINARY_DIR}/decode.out' && \
   grep -q 'put r2 <- v2'             '${CMAKE_BINARY_DIR}/decode.out' && \
   grep -q '0x0004: hlt'              '${CMAKE_BINARY_DIR}/decode.out' && \
   '$<TARGET_FILE:lcx>' upcl decode '${CMAKE_CURRENT_SOURCE_DIR}/test/risc.upcl' \
     0x00 0x22 0x18 0x20 0x00 0x22 0x28 0x25 > '${CMAKE_BINARY_DIR}/decode.risc' && \
   grep -q 'add    rd=0x3 rs=0x1 rt=0x2' '${CMAKE_BINARY_DIR}/decode.risc' && \
   grep -q 'or     rd=0x5 rs=0x1 rt=0x2' '${CMAKE_BINARY_DIR}/decode.risc'")

# MIPS (I .. 64R6): decode a few big-endian words from upcl/examples/mips.upcl and check both the
# decode AND the semantics of the %S sign-extension fix -- addiu must SIGN-extend its immediate
# (sext.i64, not zext), slt must select the SIGNED compare (slt, not sltu), and jr ra must decode.
add_test(NAME upcl.decode-mips COMMAND sh -c
  "'$<TARGET_FILE:lcx>' upcl decode '${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/mips.upcl' \
     0x24 0x02 0x12 0x34 0x00 0x22 0x18 0x2a 0x8c 0xa4 0x00 0x08 0x03 0xe0 0x00 0x08 \
       > '${CMAKE_BINARY_DIR}/decode.mips' && \
   grep -q 'addiu  imm=0x1234 rs=0x0 rt=0x2' '${CMAKE_BINARY_DIR}/decode.mips' && \
   grep -q 'sext.i64'                        '${CMAKE_BINARY_DIR}/decode.mips' && \
   grep -q 'slt    rd=0x3 rs=0x1 rt=0x2'     '${CMAKE_BINARY_DIR}/decode.mips' && \
   grep -q ' slt '                           '${CMAKE_BINARY_DIR}/decode.mips' && \
   ! grep -q ' sltu '                        '${CMAKE_BINARY_DIR}/decode.mips' && \
   grep -q 'lw     imm=0x8 rs=0x5 rt=0x4'    '${CMAKE_BINARY_DIR}/decode.mips' && \
   grep -q 'jr     rs=0x1f'                  '${CMAKE_BINARY_DIR}/decode.mips'")
set_tests_properties(upcl.decode-mips PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")

# MIPS ll/sc interlock: %LL must take a reservation (write the synthesised LLbit r69 + address r70
# then load), and %SC must lower to a reservation check (llbit != 0 AND lladdr == addr) gating a
# select-based conditional store, then write the 0/1 success back -- proving the interlock logic.
add_test(NAME upcl.mips-interlock COMMAND sh -c
  "'$<TARGET_FILE:lcx>' upcl decode '${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/mips.upcl' \
     0xc0 0xa4 0x00 0x08 > '${CMAKE_BINARY_DIR}/mips.ll' && \
   grep -q '0x0000: ll'        '${CMAKE_BINARY_DIR}/mips.ll' && \
   grep -q 'put r69 <- '       '${CMAKE_BINARY_DIR}/mips.ll' && \
   grep -q 'put r70 <- '       '${CMAKE_BINARY_DIR}/mips.ll' && \
   '$<TARGET_FILE:lcx>' upcl decode '${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/mips.upcl' \
     0xe0 0xa4 0x00 0x08 > '${CMAKE_BINARY_DIR}/mips.sc' && \
   grep -q '0x0000: sc'        '${CMAKE_BINARY_DIR}/mips.sc' && \
   grep -q 'get r69'           '${CMAKE_BINARY_DIR}/mips.sc' && \
   grep -q 'get r70'           '${CMAKE_BINARY_DIR}/mips.sc' && \
   grep -q 'select '           '${CMAKE_BINARY_DIR}/mips.sc' && \
   grep -q 'store.i32'         '${CMAKE_BINARY_DIR}/mips.sc'")
set_tests_properties(upcl.mips-interlock PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")

# PDP-11 addressing modes: a double-operand insn with two indexed operands consumes BOTH extension
# words (multi-EA-per-word decode), and the autoincrement/autodecrement modes emit the base-register
# side effect -- MOV (R1)+,R2 loads then bumps r1, MOV R0,-(SP) pre-decrements sp then stores.
add_test(NAME upcl.pdp11 COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp11.upcl'; E='$<TARGET_FILE:lcx>'; \
   "$E" upcl decode \"$P\" 0x42 0x14 > '${CMAKE_BINARY_DIR}/pdp11.inc' && \
   grep -q '0x0000: mov'        '${CMAKE_BINARY_DIR}/pdp11.inc' && \
   grep -q 'load.i16'           '${CMAKE_BINARY_DIR}/pdp11.inc' && \
   grep -q 'put r1 <- '         '${CMAKE_BINARY_DIR}/pdp11.inc' && \
   "$E" upcl decode \"$P\" 0x26 0x10 > '${CMAKE_BINARY_DIR}/pdp11.dec' && \
   grep -q 'put r6 <- '         '${CMAKE_BINARY_DIR}/pdp11.dec' && \
   grep -q 'store.i16'          '${CMAKE_BINARY_DIR}/pdp11.dec' && \
   "$E" upcl decode \"$P\" 0x72 0x1c 0x04 0x00 0x06 0x00 > '${CMAKE_BINARY_DIR}/pdp11.two' && \
   grep -q '(6 byte'            '${CMAKE_BINARY_DIR}/pdp11.two' && \
   "$E" upcl decode \"$P\" 0x42 0x90 > '${CMAKE_BINARY_DIR}/pdp11.movb' && \
   grep -q '0x0000: movb'       '${CMAKE_BINARY_DIR}/pdp11.movb' && \
   grep -q 'trunc.i8'           '${CMAKE_BINARY_DIR}/pdp11.movb' && \
   grep -q 'sext.i16'           '${CMAKE_BINARY_DIR}/pdp11.movb' && \
   "$E" upcl decode \"$P\" 0x03 0x70 > '${CMAKE_BINARY_DIR}/pdp11.mul' && \
   grep -q '0x0000: mul'        '${CMAKE_BINARY_DIR}/pdp11.mul' && \
   "$E" upcl decode \"$P\" 0x01 0xf4 > '${CMAKE_BINARY_DIR}/pdp11.addf' && \
   grep -q '0x0000: addf'       '${CMAKE_BINARY_DIR}/pdp11.addf' && \
   grep -q 'fadd '              '${CMAKE_BINARY_DIR}/pdp11.addf'")
set_tests_properties(upcl.pdp11 PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")

# PDP-10 effective address (index + indirect chain). The 36-bit word is op:9 ac:4 i:1 x:4 y:18.
#   MOVE AC1, @100(2)  = 0o200062000100  (op=0o200 ac=1 I=1 X=2 Y=0o100): the @ea macro forms the
#     INDEX step E = Y + right18(AC[X]) -- the AC[X] read, the right-half mask (& 0x3ffff), the
#     trunc to 18 bits, and Y added -- then enters the INDIRECT CHAIN (a while loop that re-reads
#     %M[E] and re-extracts I/X/Y). The word is one cell (word-addressed): "(1 word(s))".
#   MOVE AC1, 200(3)   = 0o200043000200  (I=0 X=3): index only, the chain test is taken as false.
#   JRST 100           = 0o254000000100  (op=0o254): E is the BARE jump target -- @ea used as an
#     address (pc <- E), not as %M[E], proving the same macro serves value and address contexts.
add_test(NAME upcl.pdp10-ea COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$P\" 0o200062000100 > '${CMAKE_BINARY_DIR}/pdp10.ind' && \
   grep -q '0x0000: move'          '${CMAKE_BINARY_DIR}/pdp10.ind' && \
   grep -q 'i=0x1'                 '${CMAKE_BINARY_DIR}/pdp10.ind' && \
   grep -q 'x=0x2'                 '${CMAKE_BINARY_DIR}/pdp10.ind' && \
   grep -q '(1 word'               '${CMAKE_BINARY_DIR}/pdp10.ind' && \
   grep -q 'and v1'                '${CMAKE_BINARY_DIR}/pdp10.ind' && \
   grep -q '0x3ffff'               '${CMAKE_BINARY_DIR}/pdp10.ind' && \
   grep -q 'trunc.i18'             '${CMAKE_BINARY_DIR}/pdp10.ind' && \
   grep -q 'while.head'            '${CMAKE_BINARY_DIR}/pdp10.ind' && \
   grep -q 'while.body'            '${CMAKE_BINARY_DIR}/pdp10.ind' && \
   grep -q 'load.i36'              '${CMAKE_BINARY_DIR}/pdp10.ind' && \
   \"$E\" upcl decode \"$P\" 0o200043000200 > '${CMAKE_BINARY_DIR}/pdp10.idx' && \
   grep -q '0x0000: move'          '${CMAKE_BINARY_DIR}/pdp10.idx' && \
   grep -q 'i=0x0'                 '${CMAKE_BINARY_DIR}/pdp10.idx' && \
   grep -q 'x=0x3'                 '${CMAKE_BINARY_DIR}/pdp10.idx' && \
   grep -q '(1 word'               '${CMAKE_BINARY_DIR}/pdp10.idx' && \
   grep -q '0x3ffff'               '${CMAKE_BINARY_DIR}/pdp10.idx' && \
   \"$E\" upcl decode \"$P\" 0o254000000100 > '${CMAKE_BINARY_DIR}/pdp10.jrst' && \
   grep -q '0x0000: jrst'          '${CMAKE_BINARY_DIR}/pdp10.jrst' && \
   grep -q 'put r16 <- '           '${CMAKE_BINARY_DIR}/pdp10.jrst'")
set_tests_properties(upcl.pdp10-ea PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")

# PDP-10 full-word data transmission (MOVE/MOVS/MOVN/MOVM families, EXCH, BLT). The 36-bit word
# (op:9 ac:4 i:1 x:4 y:18, MSB-first) is given as one octal value. The low two bits of the opcode
# select the operand mode (00=basic, 01=Immediate, 10=Memory, 11=Self). The words below carry
# ac=1, i=0, x=0, y=0o100 (=0x40), except the indexed MOVS which carries ac=2, x=3. Opcodes are
# sourced from SIMH pdp10_sys.c / the DEC PDP-10 Hardware Reference Manual: MOVE=0o200, MOVS=0o204,
# MOVN=0o210, MOVM=0o214 (with +1/+2/+3 for the I/M/S modes); EXCH=0o250, BLT=0o251.
add_test(NAME upcl.decode-pdp10 COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$P\" 0o200040000100 > '${CMAKE_BINARY_DIR}/pdp10.move' && \
   grep -q '0x0000: move'  '${CMAKE_BINARY_DIR}/pdp10.move' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.move' && \
   grep -q 'y=0x40'        '${CMAKE_BINARY_DIR}/pdp10.move' && \
   \"$E\" upcl decode \"$P\" 0o201040000100 > '${CMAKE_BINARY_DIR}/pdp10.movei' && \
   grep -q '0x0000: movei' '${CMAKE_BINARY_DIR}/pdp10.movei' && \
   \"$E\" upcl decode \"$P\" 0o202040000100 > '${CMAKE_BINARY_DIR}/pdp10.movem' && \
   grep -q '0x0000: movem' '${CMAKE_BINARY_DIR}/pdp10.movem' && \
   \"$E\" upcl decode \"$P\" 0o213040000100 > '${CMAKE_BINARY_DIR}/pdp10.movns' && \
   grep -q '0x0000: movns' '${CMAKE_BINARY_DIR}/pdp10.movns' && \
   \"$E\" upcl decode \"$P\" 0o204103000100 > '${CMAKE_BINARY_DIR}/pdp10.movs' && \
   grep -q '0x0000: movs'  '${CMAKE_BINARY_DIR}/pdp10.movs' && \
   grep -q 'acn=0x2'       '${CMAKE_BINARY_DIR}/pdp10.movs' && \
   grep -q 'x=0x3'         '${CMAKE_BINARY_DIR}/pdp10.movs' && \
   \"$E\" upcl decode \"$P\" 0o250040000100 > '${CMAKE_BINARY_DIR}/pdp10.exch' && \
   grep -q '0x0000: exch'  '${CMAKE_BINARY_DIR}/pdp10.exch' && \
   \"$E\" upcl decode \"$P\" 0o251040000100 > '${CMAKE_BINARY_DIR}/pdp10.blt' && \
   grep -q '0x0000: blt'   '${CMAKE_BINARY_DIR}/pdp10.blt'")
set_tests_properties(upcl.decode-pdp10 PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")

# PDP-10 halfword data transmission (H{L,R}{L,R}{-,Z,O,E} families x 4 modes). The 36-bit word
# (op:9 ac:4 i:1 x:4 y:18, MSB-first) is given as one octal value. Opcodes sourced from SIMH
# pdp10_sys.c / DEC HRM: HLL=0o500, HRL=0o504, HLLZ=0o510, HRLZ=0o514, HLLO=0o520, HRLO=0o524,
# HLLE=0o530, HRLE=0o534, HRR=0o540, HLR=0o544, HRRZ=0o550, HLRZ=0o554, HRRO=0o560, HLRO=0o564,
# HRRE=0o570, HLRE=0o574.  Each base +1/+2/+3 for I/M/S mode. Test words carry ac=N, i=0, x=0.
add_test(NAME upcl.decode-pdp10-halfword COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$P\" 0o540040000100 > '${CMAKE_BINARY_DIR}/pdp10.hrr' && \
   grep -q '0x0000: hrr'   '${CMAKE_BINARY_DIR}/pdp10.hrr' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.hrr' && \
   grep -q 'y=0x40'        '${CMAKE_BINARY_DIR}/pdp10.hrr' && \
   \"$E\" upcl decode \"$P\" 0o504100000200 > '${CMAKE_BINARY_DIR}/pdp10.hrl' && \
   grep -q '0x0000: hrl'   '${CMAKE_BINARY_DIR}/pdp10.hrl' && \
   grep -q 'acn=0x2'       '${CMAKE_BINARY_DIR}/pdp10.hrl' && \
   grep -q 'y=0x80'        '${CMAKE_BINARY_DIR}/pdp10.hrl' && \
   \"$E\" upcl decode \"$P\" 0o550140000300 > '${CMAKE_BINARY_DIR}/pdp10.hrrz' && \
   grep -q '0x0000: hrrz'  '${CMAKE_BINARY_DIR}/pdp10.hrrz' && \
   grep -q 'acn=0x3'       '${CMAKE_BINARY_DIR}/pdp10.hrrz' && \
   \"$E\" upcl decode \"$P\" 0o574200000400 > '${CMAKE_BINARY_DIR}/pdp10.hlre' && \
   grep -q '0x0000: hlre'  '${CMAKE_BINARY_DIR}/pdp10.hlre' && \
   grep -q 'acn=0x4'       '${CMAKE_BINARY_DIR}/pdp10.hlre' && \
   \"$E\" upcl decode \"$P\" 0o500040000100 > '${CMAKE_BINARY_DIR}/pdp10.hll' && \
   grep -q '0x0000: hll'   '${CMAKE_BINARY_DIR}/pdp10.hll' && \
   \"$E\" upcl decode \"$P\" 0o554100000150 > '${CMAKE_BINARY_DIR}/pdp10.hlrz' && \
   grep -q '0x0000: hlrz'  '${CMAKE_BINARY_DIR}/pdp10.hlrz' && \
   \"$E\" upcl decode \"$P\" 0o560240000500 > '${CMAKE_BINARY_DIR}/pdp10.hrro' && \
   grep -q '0x0000: hrro'  '${CMAKE_BINARY_DIR}/pdp10.hrro' && \
   \"$E\" upcl decode \"$P\" 0o530300000600 > '${CMAKE_BINARY_DIR}/pdp10.hlle' && \
   grep -q '0x0000: hlle'  '${CMAKE_BINARY_DIR}/pdp10.hlle'")
set_tests_properties(upcl.decode-pdp10-halfword PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|error:")

# PDP-10 boolean instructions (16 functions x 4 modes, opcodes 0o400-0o477).
# Truth table: SETZ=0, AND=AC&M, ANDCA=~AC&M, SETM=M, ANDCM=AC&~M, SETA=AC, XOR=AC^M,
# IOR=AC|M, ANDCB=~AC&~M, EQV=~(AC^M), SETCA=~AC, ORCA=~AC|M, SETCM=~M, ORCM=AC|~M,
# ORCB=~AC|~M, SETO=all-ones. Four modes: basic/I/M/S (+0/+1/+2/+3 on group base).
# Opcodes sourced from SIMH pdp10_sys.c / DEC HRM.  Test words carry i=0, x=0.
add_test(NAME upcl.decode-pdp10-boolean COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$P\" 0o404040000100 > '${CMAKE_BINARY_DIR}/pdp10.and' && \
   grep -q '0x0000: and'   '${CMAKE_BINARY_DIR}/pdp10.and' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.and' && \
   grep -q 'y=0x40'        '${CMAKE_BINARY_DIR}/pdp10.and' && \
   \"$E\" upcl decode \"$P\" 0o405040000200 > '${CMAKE_BINARY_DIR}/pdp10.andi' && \
   grep -q '0x0000: andi'  '${CMAKE_BINARY_DIR}/pdp10.andi' && \
   \"$E\" upcl decode \"$P\" 0o434100000300 > '${CMAKE_BINARY_DIR}/pdp10.ior' && \
   grep -q '0x0000: ior'   '${CMAKE_BINARY_DIR}/pdp10.ior' && \
   grep -q 'acn=0x2'       '${CMAKE_BINARY_DIR}/pdp10.ior' && \
   \"$E\" upcl decode \"$P\" 0o400140000100 > '${CMAKE_BINARY_DIR}/pdp10.setz' && \
   grep -q '0x0000: setz'  '${CMAKE_BINARY_DIR}/pdp10.setz' && \
   grep -q 'acn=0x3'       '${CMAKE_BINARY_DIR}/pdp10.setz' && \
   \"$E\" upcl decode \"$P\" 0o474200000200 > '${CMAKE_BINARY_DIR}/pdp10.seto' && \
   grep -q '0x0000: seto'  '${CMAKE_BINARY_DIR}/pdp10.seto' && \
   grep -q 'acn=0x4'       '${CMAKE_BINARY_DIR}/pdp10.seto' && \
   \"$E\" upcl decode \"$P\" 0o430240000300 > '${CMAKE_BINARY_DIR}/pdp10.xor' && \
   grep -q '0x0000: xor'   '${CMAKE_BINARY_DIR}/pdp10.xor' && \
   grep -q 'acn=0x5'       '${CMAKE_BINARY_DIR}/pdp10.xor' && \
   \"$E\" upcl decode \"$P\" 0o444300000400 > '${CMAKE_BINARY_DIR}/pdp10.eqv' && \
   grep -q '0x0000: eqv'   '${CMAKE_BINARY_DIR}/pdp10.eqv' && \
   \"$E\" upcl decode \"$P\" 0o420340000500 > '${CMAKE_BINARY_DIR}/pdp10.andcm' && \
   grep -q '0x0000: andcm' '${CMAKE_BINARY_DIR}/pdp10.andcm' && \
   \"$E\" upcl decode \"$P\" 0o414040000100 > '${CMAKE_BINARY_DIR}/pdp10.setm' && \
   grep -q '0x0000: setm'  '${CMAKE_BINARY_DIR}/pdp10.setm' && \
   \"$E\" upcl decode \"$P\" 0o470100000200 > '${CMAKE_BINARY_DIR}/pdp10.orcb' && \
   grep -q '0x0000: orcb'  '${CMAKE_BINARY_DIR}/pdp10.orcb' && \
   \"$E\" upcl decode \"$P\" 0o433040000100 > '${CMAKE_BINARY_DIR}/pdp10.xors' && \
   grep -q '0x0000: xors'  '${CMAKE_BINARY_DIR}/pdp10.xors' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.xors' && \
   \"$E\" upcl decode \"$P\" 0o437100000200 > '${CMAKE_BINARY_DIR}/pdp10.iors' && \
   grep -q '0x0000: iors'  '${CMAKE_BINARY_DIR}/pdp10.iors' && \
   grep -q 'acn=0x2'       '${CMAKE_BINARY_DIR}/pdp10.iors'")
set_tests_properties(upcl.decode-pdp10-boolean PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|error:")

# PDP-10 fixed-point arithmetic (ADD/SUB/MUL/IMUL/IDIV/DIV x4 modes, double DADD/DSUB/DMUL/DDIV,
# shifts ASH/LSH/ROT/JFFO/ASHC/LSHC/ROTC). Opcodes sourced from SIMH pdp10_sys.c / DEC HRM.
# Test words: i=0, x=0, ac=1, y=0o100. op:9 ac:4 i:1 x:4 y:18 MSB-first, 36-bit word.
# ADD=0o270, SUB=0o274, IMUL=0o220, IDIV=0o230, ASH=0o240, LSH=0o242, ROT=0o241.
add_test(NAME upcl.decode-pdp10-arith COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$P\" 0o270040000100 > '${CMAKE_BINARY_DIR}/pdp10.add' && \
   grep -q '0x0000: add'   '${CMAKE_BINARY_DIR}/pdp10.add' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.add' && \
   grep -q 'y=0x40'        '${CMAKE_BINARY_DIR}/pdp10.add' && \
   \"$E\" upcl decode \"$P\" 0o271040000100 > '${CMAKE_BINARY_DIR}/pdp10.addi' && \
   grep -q '0x0000: addi'  '${CMAKE_BINARY_DIR}/pdp10.addi' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.addi' && \
   \"$E\" upcl decode \"$P\" 0o274040000100 > '${CMAKE_BINARY_DIR}/pdp10.sub' && \
   grep -q '0x0000: sub'   '${CMAKE_BINARY_DIR}/pdp10.sub' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.sub' && \
   \"$E\" upcl decode \"$P\" 0o275040000100 > '${CMAKE_BINARY_DIR}/pdp10.subi' && \
   grep -q '0x0000: subi'  '${CMAKE_BINARY_DIR}/pdp10.subi' && \
   \"$E\" upcl decode \"$P\" 0o220040000100 > '${CMAKE_BINARY_DIR}/pdp10.imul' && \
   grep -q '0x0000: imul'  '${CMAKE_BINARY_DIR}/pdp10.imul' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.imul' && \
   \"$E\" upcl decode \"$P\" 0o221040000100 > '${CMAKE_BINARY_DIR}/pdp10.imuli' && \
   grep -q '0x0000: imuli' '${CMAKE_BINARY_DIR}/pdp10.imuli' && \
   \"$E\" upcl decode \"$P\" 0o230040000100 > '${CMAKE_BINARY_DIR}/pdp10.idiv' && \
   grep -q '0x0000: idiv'  '${CMAKE_BINARY_DIR}/pdp10.idiv' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.idiv' && \
   \"$E\" upcl decode \"$P\" 0o231040000100 > '${CMAKE_BINARY_DIR}/pdp10.idivi' && \
   grep -q '0x0000: idivi' '${CMAKE_BINARY_DIR}/pdp10.idivi' && \
   \"$E\" upcl decode \"$P\" 0o240040000100 > '${CMAKE_BINARY_DIR}/pdp10.ash' && \
   grep -q '0x0000: ash'   '${CMAKE_BINARY_DIR}/pdp10.ash' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.ash' && \
   grep -q 'y=0x40'        '${CMAKE_BINARY_DIR}/pdp10.ash' && \
   \"$E\" upcl decode \"$P\" 0o242040000100 > '${CMAKE_BINARY_DIR}/pdp10.lsh' && \
   grep -q '0x0000: lsh'   '${CMAKE_BINARY_DIR}/pdp10.lsh' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.lsh' && \
   \"$E\" upcl decode \"$P\" 0o241040000100 > '${CMAKE_BINARY_DIR}/pdp10.rot' && \
   grep -q '0x0000: rot'   '${CMAKE_BINARY_DIR}/pdp10.rot' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.rot'")
set_tests_properties(upcl.decode-pdp10-arith PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|error:")

# TEST/COMPARE/SKIP class (Task 11): T{R,L,D,S}{N,Z,C,O}{-,E,A,N} (64 instructions, 0o6xx),
# CAI/CAM (0o3xx/0o31x), SKIP/AOS/SOS (0o33x/0o35x/0o37x), AOBJP/AOBJN (0o252/0o253).
# Opcodes sourced from SIMH pdp10_sys.c / DEC PDP-10 HRM; word encoding verified by hand.
add_test(NAME upcl.decode-pdp10-test COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$P\" 0o600040000100 > '${CMAKE_BINARY_DIR}/pdp10.trn' && \
   grep -q '0x0000: trn'    '${CMAKE_BINARY_DIR}/pdp10.trn' && \
   grep -q 'acn=0x1'        '${CMAKE_BINARY_DIR}/pdp10.trn' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.trn' && \
   \"$E\" upcl decode \"$P\" 0o602100000200 > '${CMAKE_BINARY_DIR}/pdp10.trne' && \
   grep -q '0x0000: trne'   '${CMAKE_BINARY_DIR}/pdp10.trne' && \
   grep -q 'acn=0x2'        '${CMAKE_BINARY_DIR}/pdp10.trne' && \
   grep -q 'y=0x80'         '${CMAKE_BINARY_DIR}/pdp10.trne' && \
   \"$E\" upcl decode \"$P\" 0o621140000377 > '${CMAKE_BINARY_DIR}/pdp10.tlz' && \
   grep -q '0x0000: tlz'    '${CMAKE_BINARY_DIR}/pdp10.tlz' && \
   grep -q 'acn=0x3'        '${CMAKE_BINARY_DIR}/pdp10.tlz' && \
   grep -q 'y=0xff'         '${CMAKE_BINARY_DIR}/pdp10.tlz' && \
   \"$E\" upcl decode \"$P\" 0o640200177777 > '${CMAKE_BINARY_DIR}/pdp10.trc' && \
   grep -q '0x0000: trc'    '${CMAKE_BINARY_DIR}/pdp10.trc' && \
   grep -q 'acn=0x4'        '${CMAKE_BINARY_DIR}/pdp10.trc' && \
   grep -q 'y=0xffff'       '${CMAKE_BINARY_DIR}/pdp10.trc' && \
   \"$E\" upcl decode \"$P\" 0o306040000005 > '${CMAKE_BINARY_DIR}/pdp10.cain' && \
   grep -q '0x0000: cain'   '${CMAKE_BINARY_DIR}/pdp10.cain' && \
   grep -q 'acn=0x1'        '${CMAKE_BINARY_DIR}/pdp10.cain' && \
   grep -q 'y=0x5'          '${CMAKE_BINARY_DIR}/pdp10.cain' && \
   \"$E\" upcl decode \"$P\" 0o312140000100 > '${CMAKE_BINARY_DIR}/pdp10.came' && \
   grep -q '0x0000: came'   '${CMAKE_BINARY_DIR}/pdp10.came' && \
   grep -q 'acn=0x3'        '${CMAKE_BINARY_DIR}/pdp10.came' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.came' && \
   \"$E\" upcl decode \"$P\" 0o331040000100 > '${CMAKE_BINARY_DIR}/pdp10.skipl' && \
   grep -q '0x0000: skipl'  '${CMAKE_BINARY_DIR}/pdp10.skipl' && \
   grep -q 'acn=0x1'        '${CMAKE_BINARY_DIR}/pdp10.skipl' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.skipl' && \
   \"$E\" upcl decode \"$P\" 0o354100000200 > '${CMAKE_BINARY_DIR}/pdp10.aosa' && \
   grep -q '0x0000: aosa'   '${CMAKE_BINARY_DIR}/pdp10.aosa' && \
   grep -q 'acn=0x2'        '${CMAKE_BINARY_DIR}/pdp10.aosa' && \
   grep -q 'y=0x80'         '${CMAKE_BINARY_DIR}/pdp10.aosa' && \
   \"$E\" upcl decode \"$P\" 0o253100000100 > '${CMAKE_BINARY_DIR}/pdp10.aobjn' && \
   grep -q '0x0000: aobjn'  '${CMAKE_BINARY_DIR}/pdp10.aobjn' && \
   grep -q 'acn=0x2'        '${CMAKE_BINARY_DIR}/pdp10.aobjn' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.aobjn'")
set_tests_properties(upcl.decode-pdp10-test PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|error:")

# PDP-10 jump/control/stack instructions (Task 12): JRST (plain jump + halt), JUMPA/JUMPL,
# AOJ/SOJ (add/sub-one-to-AC + conditional jump), PUSHJ/POPJ/PUSH/POP (stack), JSP, JFCL.
# Opcodes sourced from DEC PDP-10 HRM and SIMH pdp10_sys.c.
# Instruction word encoding (op:9 ac:4 i:1 x:4 y:18, MSB-first, 36-bit octal):
#   JRST jump  (AC=0):  0o254000000100 = op=0o254 ac=0 y=0o100
#   JRST halt  (AC=4):  0o254200000000 = op=0o254 ac=4 y=0
#   JUMPA      (AC=1):  0o324040000100 = op=0o324 ac=1 y=0o100  (JUMP+always, op=0o320+4)
#   AOJA       (AC=1):  0o344040000100 = op=0o344 ac=1 y=0o100  (AOJ+always, op=0o340+4)
#   SOJA       (AC=1):  0o364040000100 = op=0o364 ac=1 y=0o100  (SOJ+always, op=0o360+4)
#   PUSHJ      (AC=1):  0o260040000100 = op=0o260 ac=1 y=0o100
#   POPJ       (AC=1):  0o263040000000 = op=0o263 ac=1 y=0
#   PUSH       (AC=1):  0o261040000100 = op=0o261 ac=1 y=0o100
#   POP        (AC=1):  0o262040000100 = op=0o262 ac=1 y=0o100
#   JSP        (AC=1):  0o265040000100 = op=0o265 ac=1 y=0o100
#   JFCL       (AC=1):  0o255040000100 = op=0o255 ac=1 y=0o100
add_test(NAME upcl.decode-pdp10-jump COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$P\" 0o254000000100 > '${CMAKE_BINARY_DIR}/pdp10.jrst' && \
   grep -q '0x0000: jrst'   '${CMAKE_BINARY_DIR}/pdp10.jrst' && \
   grep -q 'acn=0x0'        '${CMAKE_BINARY_DIR}/pdp10.jrst' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.jrst' && \
   \"$E\" upcl decode \"$P\" 0o254200000000 > '${CMAKE_BINARY_DIR}/pdp10.halt' && \
   grep -q 'jrst_halt'      '${CMAKE_BINARY_DIR}/pdp10.halt' && \
   \"$E\" upcl decode \"$P\" 0o324040000100 > '${CMAKE_BINARY_DIR}/pdp10.jumpa' && \
   grep -q '0x0000: jumpa'  '${CMAKE_BINARY_DIR}/pdp10.jumpa' && \
   grep -q 'acn=0x1'        '${CMAKE_BINARY_DIR}/pdp10.jumpa' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.jumpa' && \
   \"$E\" upcl decode \"$P\" 0o344040000100 > '${CMAKE_BINARY_DIR}/pdp10.aoja' && \
   grep -q '0x0000: aoja'   '${CMAKE_BINARY_DIR}/pdp10.aoja' && \
   grep -q 'acn=0x1'        '${CMAKE_BINARY_DIR}/pdp10.aoja' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.aoja' && \
   \"$E\" upcl decode \"$P\" 0o364040000100 > '${CMAKE_BINARY_DIR}/pdp10.soja' && \
   grep -q '0x0000: soja'   '${CMAKE_BINARY_DIR}/pdp10.soja' && \
   grep -q 'acn=0x1'        '${CMAKE_BINARY_DIR}/pdp10.soja' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.soja' && \
   \"$E\" upcl decode \"$P\" 0o260040000100 > '${CMAKE_BINARY_DIR}/pdp10.pushj' && \
   grep -q '0x0000: pushj'  '${CMAKE_BINARY_DIR}/pdp10.pushj' && \
   grep -q 'acn=0x1'        '${CMAKE_BINARY_DIR}/pdp10.pushj' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.pushj' && \
   \"$E\" upcl decode \"$P\" 0o263040000000 > '${CMAKE_BINARY_DIR}/pdp10.popj' && \
   grep -q '0x0000: popj'   '${CMAKE_BINARY_DIR}/pdp10.popj' && \
   grep -q 'acn=0x1'        '${CMAKE_BINARY_DIR}/pdp10.popj' && \
   \"$E\" upcl decode \"$P\" 0o261040000100 > '${CMAKE_BINARY_DIR}/pdp10.push' && \
   grep -q '0x0000: push'   '${CMAKE_BINARY_DIR}/pdp10.push' && \
   grep -q 'acn=0x1'        '${CMAKE_BINARY_DIR}/pdp10.push' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.push' && \
   \"$E\" upcl decode \"$P\" 0o262040000100 > '${CMAKE_BINARY_DIR}/pdp10.pop' && \
   grep -q '0x0000: pop'    '${CMAKE_BINARY_DIR}/pdp10.pop' && \
   grep -q 'acn=0x1'        '${CMAKE_BINARY_DIR}/pdp10.pop' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.pop' && \
   \"$E\" upcl decode \"$P\" 0o265040000100 > '${CMAKE_BINARY_DIR}/pdp10.jsp' && \
   grep -q '0x0000: jsp'    '${CMAKE_BINARY_DIR}/pdp10.jsp' && \
   grep -q 'acn=0x1'        '${CMAKE_BINARY_DIR}/pdp10.jsp' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.jsp' && \
   \"$E\" upcl decode \"$P\" 0o255040000100 > '${CMAKE_BINARY_DIR}/pdp10.jfcl' && \
   grep -q '0x0000: jfcl'   '${CMAKE_BINARY_DIR}/pdp10.jfcl' && \
   grep -q 'acn=0x1'        '${CMAKE_BINARY_DIR}/pdp10.jfcl' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.jfcl'")
set_tests_properties(upcl.decode-pdp10-jump PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|error:")

# PDP-10 byte instructions (Task 13): LDB/DPB/ILDB/IDPB/IBP.
# Opcodes sourced from DEC PDP-10 HRM and SIMH pdp10_cpu.c:
#   IBP=0o133, ILDB=0o134, LDB=0o135, IDPB=0o136, DPB=0o137.
# Instruction word encoding (op:9 ac:4 i:1 x:4 y:18, MSB-first, 36-bit octal).
# Test words use ac=1 (acn=1), i=0, x=0, y=0o100 (=0x40):
#   LDB  (ac=1): 0o135040000100 = op=0o135 ac=1 y=0o100
#   DPB  (ac=1): 0o137040000100 = op=0o137 ac=1 y=0o100
#   IBP  (ac=1): 0o133040000100 = op=0o133 ac=1 y=0o100
#   ILDB (ac=1): 0o134040000100 = op=0o134 ac=1 y=0o100
#   IDPB (ac=1): 0o136040000100 = op=0o136 ac=1 y=0o100
add_test(NAME upcl.decode-pdp10-byte COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$P\" 0o135040000100 > '${CMAKE_BINARY_DIR}/pdp10.ldb' && \
   grep -q '0x0000: ldb'   '${CMAKE_BINARY_DIR}/pdp10.ldb' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.ldb' && \
   grep -q 'y=0x40'        '${CMAKE_BINARY_DIR}/pdp10.ldb' && \
   \"$E\" upcl decode \"$P\" 0o137040000100 > '${CMAKE_BINARY_DIR}/pdp10.dpb' && \
   grep -q '0x0000: dpb'   '${CMAKE_BINARY_DIR}/pdp10.dpb' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.dpb' && \
   grep -q 'y=0x40'        '${CMAKE_BINARY_DIR}/pdp10.dpb' && \
   \"$E\" upcl decode \"$P\" 0o133040000100 > '${CMAKE_BINARY_DIR}/pdp10.ibp' && \
   grep -q '0x0000: ibp'   '${CMAKE_BINARY_DIR}/pdp10.ibp' && \
   grep -q 'y=0x40'        '${CMAKE_BINARY_DIR}/pdp10.ibp' && \
   \"$E\" upcl decode \"$P\" 0o134040000100 > '${CMAKE_BINARY_DIR}/pdp10.ildb' && \
   grep -q '0x0000: ildb'  '${CMAKE_BINARY_DIR}/pdp10.ildb' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.ildb' && \
   grep -q 'y=0x40'        '${CMAKE_BINARY_DIR}/pdp10.ildb' && \
   \"$E\" upcl decode \"$P\" 0o136040000100 > '${CMAKE_BINARY_DIR}/pdp10.idpb' && \
   grep -q '0x0000: idpb'  '${CMAKE_BINARY_DIR}/pdp10.idpb' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.idpb' && \
   grep -q 'y=0x40'        '${CMAKE_BINARY_DIR}/pdp10.idpb'")
set_tests_properties(upcl.decode-pdp10-byte PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|error:")

# Per-field big-endian override: bytes 0xAA 0x12 0x34 through a little-endian arch whose
# imm:16 field carries `be` must decode as 0x1234 (not the LE value 0x3412).
add_test(NAME upcl.engine-be COMMAND sh -c
  "'$<TARGET_FILE:lcx>' upcl decode '${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_be.upcl' 0xAA 0x12 0x34 | grep '0x1234'")

# Generic WORD-ADDRESSING (PDP-10 shaped): a 36-bit word-addressed arch (byte_size == word_size ==
# 36, != 8) decodes a single 36-bit instruction word at a WORD offset, extracting MSB-first fields
# (op:9 ac:4 i:1 x:4 y:18) straight from the word value -- which the byte path cannot do (36 bits
# span no whole octet). The octal word 0o201040000005 = op 0o201 (movei), ac 1, y 5; assert the
# decode AND that the reported length is 1 word (not bytes). The byte ISAs are unaffected: their
# byte_size is 8 (or unset), so WordAddressed stays false and the byte path is byte-identical.
add_test(NAME upcl.engine-wordaddr COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_wordaddr.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$M\" 0o201040000005 > '${CMAKE_BINARY_DIR}/wordaddr.out' && \
   grep -q '0x0000: movei' '${CMAKE_BINARY_DIR}/wordaddr.out' && \
   grep -q 'ac=0x1'        '${CMAKE_BINARY_DIR}/wordaddr.out' && \
   grep -q 'y=0x5'         '${CMAKE_BINARY_DIR}/wordaddr.out' && \
   grep -q '(1 word(s))'   '${CMAKE_BINARY_DIR}/wordaddr.out'")
set_tests_properties(upcl.engine-wordaddr PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")

# WORD-ADDRESSED EXECUTION: a 36-bit value round-trips through word memory. Guest RAM is an array of
# 36-bit word cells (each backed by an 8-byte little-endian host slot); the program image is those
# cells, one per instruction word. The program builds a full 36-bit AC value out of two 18-bit halves
# (movsi ac1, 0o654321 -> high half; iori ac1, 0o123456 -> low half), stores it to memory WORD 0o144
# (= 100 decimal; movem ac1, 0o144), loads it back into a DIFFERENT AC (move ac2, 0o144), then halts. ac2 must equal the
# whole 36-bit value 0d6344a72e -- proving the word cell holds all 36 bits (a byte-array / 32-bit
# access would drop the top 4 bits). Exercises word-index %M[] Load/Store + word-cell instruction
# fetch on the interpreter backend. The five 8-byte LE word cells are emitted as octal printf escapes.
if(TARGET interp_backend)
  add_test(NAME upcl.engine-wordmem COMMAND sh -c
    "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_wordaddr.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\321\\130\\203\\050\\004\\000\\000\\000\\056\\247\\200\\110\\004\\000\\000\\000\\144\\000\\200\\020\\004\\000\\000\\000\\144\\000\\000\\001\\004\\000\\000\\000\\000\\000\\000\\140\\005\\000\\000\\000' > '${CMAKE_BINARY_DIR}/wordmem.bin' && \
     \"$E\" run '${CMAKE_BINARY_DIR}/wordmem.bin' --arch \"upcl:$M\" | grep -q 'ac2=0d6344a72e'")

  # E-AC: REGISTER/LOW-MEMORY ALIASING. On the PDP-10 the 16 accumulators (AC0-AC15) ARE
  # memory words 0-17 (octal) -- the same physical storage. This test program verifies BOTH
  # directions of the alias on a word-addressed arch where AC is declared `aliases_memory 0`:
  #
  #   Direction A (AC -> %M): write a literal 42 into ac3 (movei ac=3, y=42), then load
  #     %M[3] into ac9 (move ac=9, y=3).  With aliasing ac9 must equal 42.
  #   Direction B (%M -> AC): write literal 99 into ac5 (movei ac=5, y=99), then store ac5
  #     to %M[11] (movem ac=5, y=11), which with aliasing sets ac11=99.  Without aliasing
  #     ac11 stays 0 (registers and RAM are disjoint).
  #
  # Program bytes (5 * 8-byte LE word cells):
  #   \052\000\200\011\004\000\000\000  -- movei ac=3, y=42
  #   \003\000\200\004\004\000\000\000  -- move  ac=9, y=3
  #   \143\000\200\012\004\000\000\000  -- movei ac=5, y=99
  #   \013\000\200\022\004\000\000\000  -- movem ac=5, y=11
  #   \000\000\000\140\005\000\000\000  -- halt
  add_test(NAME upcl.engine-ac-alias COMMAND sh -c
    "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_wordaddr.upcl'; E='$<TARGET_FILE:lcx>'; \
     printf '\\052\\000\\200\\011\\004\\000\\000\\000\\003\\000\\200\\004\\004\\000\\000\\000\\143\\000\\200\\012\\004\\000\\000\\000\\013\\000\\200\\022\\004\\000\\000\\000\\000\\000\\000\\140\\005\\000\\000\\000' > '${CMAKE_BINARY_DIR}/acalias.bin' && \
     OUT=\"$(\"$E\" run '${CMAKE_BINARY_DIR}/acalias.bin' --arch \"upcl:$M\")\"; \
     echo \"$OUT\" | grep -q 'ac9=000000002a' && echo \"$OUT\" | grep -q 'ac11=0000000063'")
endif()

# WORD-ADDRESSED BACKEND GUARD: running a word-addressed arch on any backend other than the
# interpreter must fail loudly with a clear error rather than silently mis-executing (e.g.
# dropping the top 4 bits of a 36-bit word due to Bits/8 width computation in the backend's
# RAM helpers). The bash backend is always built and is a non-interpreter backend; the guard
# must emit "does not support >8-bit addressable units" and exit non-zero. upcl decode (no
# execution) must still succeed on the same arch to confirm decode/disasm are unaffected.
add_test(NAME upcl.engine-wordmem-guard COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_wordaddr.upcl'; E='$<TARGET_FILE:lcx>'; \
   B='$<TARGET_BUNDLE_DIR:bash_backend>'; \
   printf '\\000\\000\\000\\000\\000\\000\\000\\000' > '${CMAKE_BINARY_DIR}/wordmem-guard.bin' && \
   OUT=\"$(\"$E\" run '${CMAKE_BINARY_DIR}/wordmem-guard.bin' --arch \"upcl:$M\" --backend \"$B\" 2>&1)\"; \
   RC=$?; \
   echo \"$OUT\" | grep -q 'does not support >8-bit addressable units' && test $RC -ne 0")
# Decode (no execution) must be unaffected by the guard -- it must still succeed.
add_test(NAME upcl.engine-wordmem-guard-decode COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_wordaddr.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$M\" 0o201040000005 | grep -q 'movei'")

# Self-describing variable-length displacement (`disp varlen`): the first tail byte's top bits
# select the encoded width (1 byte / 7-bit, 2 bytes / 14-bit, 4 bytes / 30-bit; big-endian,
# sign-extended). The opcode byte 0x00 (op=0,m=0) selects the %M[ r0 + disp varlen ] rule; the
# remaining bytes are the displacement. Asserts all three width boundaries AND sign extension:
#   0x05            -> 1 byte, +5
#   0x7F            -> 1 byte, 7-bit -1  (0xff...ff)
#   0x90 0x00       -> 2 bytes, +0x1000
#   0xA0 0x00       -> 2 bytes, 14-bit value 0x2000 sign-extends NEGATIVE (0xff...e000)
#   0xC0 0x00 0x40 0x00 -> 4 bytes, +0x4000
add_test(NAME upcl.engine-varlen COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_varlen.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$M\" 0x00 0x05 | grep -q 'disp=0x5 ' && \
   \"$E\" upcl decode \"$M\" 0x00 0x05 | grep -q '2 byte' && \
   \"$E\" upcl decode \"$M\" 0x00 0x7F | grep -q 'disp=0xffffffffffffffff' && \
   \"$E\" upcl decode \"$M\" 0x00 0x90 0x00 | grep -q 'disp=0x1000 ' && \
   \"$E\" upcl decode \"$M\" 0x00 0x90 0x00 | grep -q '3 byte' && \
   \"$E\" upcl decode \"$M\" 0x00 0xA0 0x00 | grep -q 'disp=0xffffffffffffe000' && \
   \"$E\" upcl decode \"$M\" 0x00 0xC0 0x00 0x40 0x00 | grep -q 'disp=0x4000 ' && \
   \"$E\" upcl decode \"$M\" 0x00 0xC0 0x00 0x40 0x00 | grep -q '5 byte'")

# Nested scaled-index dispatch (NS32000 [Rn:B/W/D/Q]): one index byte (basegen:5)(ireg:3) recurses
# into the named base addrmode with `basegen`, then adds R[ireg] * scale. The opcode 0x00 (op=0,m=0)
# selects the %M[ @base[index] + %REG[R, ireg] * 4 ] rule. Asserts BOTH the register-base case and
# the absolute-base case where the nested mode consumes its OWN varlen displacement (proving the
# recursion composes with `disp varlen`):
#   0x0A      -> base gen 0x01 (r1), ireg 2     => r1 + r2*4         (length 2)
#   0xAB 0x10 -> base gen 0x15 (absolute), ireg 3, then 1-byte varlen disp 0x10 => @0x10 + r3*4 (len 3)
add_test(NAME upcl.engine-index COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_index.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$M\" 0x00 0x0A | grep -q 'r1+r2\\*4' && \
   \"$E\" upcl decode \"$M\" 0x00 0x0A | grep -q '2 byte' && \
   \"$E\" upcl decode \"$M\" 0x00 0xAB 0x10 | grep -q '@0x10+r3\\*4' && \
   \"$E\" upcl decode \"$M\" 0x00 0xAB 0x10 | grep -q '3 byte'")

# E4 -- addrmode POSITIONAL ARGUMENT BINDING: one `addrmode m ( x )` serves two operand slots via
# two DIFFERENT encode fields (`a:3 -> op1 @m(a), b:3 -> op2 @m(b)`). Byte 0x15 (op=0, a=2, b=5)
# must resolve op1 and op2 to DIFFERENT registers (r2 and r5) -- proving the formal `x` binds per
# slot to its actual field, not to one shared field name.
add_test(NAME upcl.engine-addrarg COMMAND sh -c
  "'$<TARGET_FILE:lcx>' upcl decode '${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_addrarg.upcl' 0x15 | grep -q 'op1=r2 op2=r5'")

# E5 -- FIXED-WIDTH BIG-ENDIAN IMMEDIATE addrmode term: `x == 1 => #i32 imm be ;` reads a 4-byte
# big-endian immediate from the tail. Stream 0x01 0x12 0x34 0x56 0x78 must decode src=0x12345678
# AND consume EXACTLY 5 bytes (1 opcode + 4 immediate) -- proving a fixed operand-width immediate,
# distinct from a self-describing varlen displacement.
add_test(NAME upcl.engine-imm COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_imm.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$M\" 0x01 0x12 0x34 0x56 0x78 | grep -q 'src=0x12345678' && \
   \"$E\" upcl decode \"$M\" 0x01 0x12 0x34 0x56 0x78 | grep -q '5 byte'")

# E5 MIDDLE-ENDIAN IMMEDIATE: `x == 1 => #i32:me imm ;` reads a 4-byte PDP-11-order immediate.
# Middle-endian byte order 2-3-0-1: for stream 0x12 0x34 0x56 0x78,
#   value = (0x34<<24)|(0x12<<16)|(0x78<<8)|0x56 = 0x34127856
# The little-endian read of the same bytes would be 0x78563412, and big-endian 0x12345678;
# 0x34127856 proves the middle-endian path is exercised.  Also assert 5 bytes consumed.
add_test(NAME upcl.engine-me COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_me.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$M\" 0x01 0x12 0x34 0x56 0x78 | grep -q 'src=0x34127856' && \
   \"$E\" upcl decode \"$M\" 0x01 0x12 0x34 0x56 0x78 | grep -q '5 byte'")

# Full type-literal grammar  #<kind><width>(xN)?(:lsb|:msb)?(:le|:me|:be)?.  Two ADDITIVE forms:
#   * `#i32x4`    -- the general xN vector spelling (4 lanes of i32).  The 0xB0 opcode's padd lowers
#                    lane-by-lane; the i128 lane-combine IR (shl/or over a 128-bit value) proves it
#                    is a vector, identical to the legacy `#v4:32`.
#   * `#i16:be`   -- byte-order suffix `:be` on an ADDRMODE-RULE type literal (the WIRED path):
#                    `imm_be { x==0 => #i16:be imm ; }` -- the type-literal `:be` drives big-endian
#                    decode (EndianBig -> ImmBigEndian), NOT a trailing `be` keyword in the encode.
#                    RED: without `:be`, stream 0x12 0x34 decodes little-endian -> src=0x3412.
#                    GREEN: with `:be`, same bytes decode big-endian -> src=0x1234.
add_test(NAME upcl.engine-typegram COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_typegram.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$M\" 0xB0 | grep -q 'const.i128' && \
   \"$E\" upcl decode \"$M\" 0xC0 0x12 0x34 | grep -q 'src=0x1234' && \
   \"$E\" upcl decode \"$M\" 0xC0 0x12 0x34 | grep -q '3 byte'")
set_tests_properties(upcl.engine-typegram PROPERTIES FAIL_REGULAR_EXPRESSION "error|no encoding matched")

# E7 -- `const` declarations: named symbolic constants usable ANYWHERE a literal/constant is. Four
# top-level consts replace the magic numbers of a tiny ISA, each exercised at one of the three target
# sites, with a LITERAL twin instruction proving the name resolves to the exact same value:
#   (a) ENCODE MATCH VALUE   `op:4 = OP_LD`     (OP_LD = 0x2)  -- ldc decodes at opcode nibble 0x2
#   (b) ADDRMODE CONDITION   `x == SEL_IMM`     (SEL_IMM = 0x1) -- the immediate rule fires for x=1
#   (c) SEMANTIC-BODY MASK    `r0 & MASK_LOW`    (MASK_LOW = (1<<4)-1 = 0xf, folded over a prior const)
# The const insn `ldc` (0x21 ..) and the literal twin `ldl` (0x31 ..) over the SAME immediate bytes
# must resolve src=0x12345678 in 5 bytes AND lower the body to the SAME `const.i32 0xf` mask --
# proving each const folds identically to its literal. (RED before E7: `const` is rejected at parse.)
add_test(NAME upcl.engine-const COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_const.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl check \"$M\" | grep -q '2 instruction' && \
   \"$E\" upcl decode \"$M\" 0x21 0x78 0x56 0x34 0x12 | grep -q 'ldc *src=0x12345678' && \
   \"$E\" upcl decode \"$M\" 0x21 0x78 0x56 0x34 0x12 | grep -q '5 byte' && \
   \"$E\" upcl decode \"$M\" 0x21 0x78 0x56 0x34 0x12 | grep -q 'const.i32 0xf' && \
   \"$E\" upcl decode \"$M\" 0x31 0x78 0x56 0x34 0x12 | grep -q 'ldl *src=0x12345678' && \
   \"$E\" upcl decode \"$M\" 0x31 0x78 0x56 0x34 0x12 | grep -q '5 byte' && \
   \"$E\" upcl decode \"$M\" 0x31 0x78 0x56 0x34 0x12 | grep -q 'const.i32 0xf'")
set_tests_properties(upcl.engine-const PROPERTIES FAIL_REGULAR_EXPRESSION "error|no encoding matched")

# E-EXT -- cpu inheritance: `cpu "x" extends "a", "b" { }`. The test file declares three features
# (a/b/c), four cpus (base={a}, mid extends base {b}, top extends mid,c_holder {}, c_holder={c}),
# and one instruction per feature (ia/ib/ic at opcodes 0x0001/0x0002/0x0003).
# Assertions:
#   (1) check passes (extends syntax is valid, no errors)
#   (2) @top disassembles ia (from base via mid -- transitive) -- proves inheritance chain
#   (3) @top disassembles ib (from mid -- direct parent, full union) -- proves ib is included
#   (4) @top disassembles ic (from c_holder -- multiple inheritance) -- proves union
#   (5) @base rejects ib (0x0002 -> "db") -- not in base's feature set
#   (6) @base rejects ic (0x0003 -> "db") -- same
#   (7) the cycle test file _test_extends_cycle.upcl fails check with a cycle diagnostic
add_test(NAME upcl.engine-extends COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_extends.upcl'; \
   MC='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/_test_extends_cycle.upcl'; \
   E='$<TARGET_FILE:lcx>'; \
   printf '\\x01\\x00' > '${CMAKE_BINARY_DIR}/ext_ia.bin' && \
   printf '\\x02\\x00' > '${CMAKE_BINARY_DIR}/ext_ib.bin' && \
   printf '\\x03\\x00' > '${CMAKE_BINARY_DIR}/ext_ic.bin' && \
   \"$E\" upcl check \"$M\" | grep -q '_test_ext' && \
   \"$E\" disasm '${CMAKE_BINARY_DIR}/ext_ia.bin' --count 1 --arch \"upcl:$M@top\" | grep -q 'ia' && \
   \"$E\" disasm '${CMAKE_BINARY_DIR}/ext_ib.bin' --count 1 --arch \"upcl:$M@top\" | grep -q 'ib' && \
   \"$E\" disasm '${CMAKE_BINARY_DIR}/ext_ic.bin' --count 1 --arch \"upcl:$M@top\" | grep -q 'ic' && \
   \"$E\" disasm '${CMAKE_BINARY_DIR}/ext_ib.bin' --count 1 --arch \"upcl:$M@base\" | grep -q 'db' && \
   \"$E\" disasm '${CMAKE_BINARY_DIR}/ext_ic.bin' --count 1 --arch \"upcl:$M@base\" | grep -q 'db' && \
   ! \"$E\" upcl check \"$MC\" 2>/dev/null")
set_tests_properties(upcl.engine-extends PROPERTIES FAIL_REGULAR_EXPRESSION "error:")

# NS32000 arch header + register file: the UPCL description must parse cleanly and report the
# expected arch properties -- 32-bit little-endian word, R0..R7 general-purpose group, dedicated
# registers (pc/sp0/sp1/sp/fp/sb/intbase/mod/cfg), and the PSR flag splitter binding the
# carry (%C), zero (%Z), and negative (%N) meta-flags.
add_test(NAME upcl.check-ns32k COMMAND sh -c
  "'$<TARGET_FILE:lcx>' upcl check '${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/ns32k.upcl' > '${CMAKE_BINARY_DIR}/ns32k.out' && \
   grep -q 'arch \"ns32k\"'  '${CMAKE_BINARY_DIR}/ns32k.out' && \
   grep -q 'flag C'          '${CMAKE_BINARY_DIR}/ns32k.out' && \
   grep -q 'flag Z'          '${CMAKE_BINARY_DIR}/ns32k.out' && \
   grep -q 'flag N'          '${CMAKE_BINARY_DIR}/ns32k.out' && \
   grep -q 'r0 '             '${CMAKE_BINARY_DIR}/ns32k.out' && \
   grep -q 'pc '             '${CMAKE_BINARY_DIR}/ns32k.out'")
set_tests_properties(upcl.check-ns32k PROPERTIES FAIL_REGULAR_EXPRESSION "error|not valid")

# DEC PDP-10 floating-point (Task 14): `feature fp` with FAD/FSB/FMP/FDV (+L/M/B/R modes),
# DFAD/DFSB/DFMP/DFDV (double), UFA/DFN/FSC, FIX/FIXR/FLTR.  Real octal instruction words
# assembled from op:9 ac:4 i:1 x:4 y:18 (MSB-first): acn=1 i=0 x=0 y=0o100 (=64 decimal).
#   FAD  op=0o140  -> 0o140040000100   FADR op=0o144 -> 0o144040000100
#   FMP  op=0o160  -> 0o160040000100   FDV  op=0o170 -> 0o170040000100
#   FSB  op=0o150  -> 0o150040000100   FSC  op=0o132 -> 0o132040000100
#   FIX  op=0o122  -> 0o122040000100   FIXR op=0o126 -> 0o126040000100
#   FLTR op=0o127  -> 0o127040000100   DFAD op=0o110 -> 0o110040000100
# Opcodes verified against SIMH pdp10_sys.c and DEC PDP-10 HRM floating-point chapter.
# Conversion macros @pdp10_to_host / @host_to_pdp10 use integer extraction of the PDP-10
# sign+excess-128-exp8+frac27 format; [ #f64 x ] int-to-float for operands (APPROXIMATE).
add_test(NAME upcl.pdp10-fp COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$P\" 0o140040000100 > '${CMAKE_BINARY_DIR}/pdp10.fad' && \
   grep -q '0x0000: fad'   '${CMAKE_BINARY_DIR}/pdp10.fad' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.fad' && \
   grep -q 'y=0x40'        '${CMAKE_BINARY_DIR}/pdp10.fad' && \
   \"$E\" upcl decode \"$P\" 0o144040000100 > '${CMAKE_BINARY_DIR}/pdp10.fadr' && \
   grep -q '0x0000: fadr'  '${CMAKE_BINARY_DIR}/pdp10.fadr' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.fadr' && \
   \"$E\" upcl decode \"$P\" 0o160040000100 > '${CMAKE_BINARY_DIR}/pdp10.fmp' && \
   grep -q '0x0000: fmp'   '${CMAKE_BINARY_DIR}/pdp10.fmp' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.fmp' && \
   \"$E\" upcl decode \"$P\" 0o170040000100 > '${CMAKE_BINARY_DIR}/pdp10.fdv' && \
   grep -q '0x0000: fdv'   '${CMAKE_BINARY_DIR}/pdp10.fdv' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.fdv' && \
   \"$E\" upcl decode \"$P\" 0o150040000100 > '${CMAKE_BINARY_DIR}/pdp10.fsb' && \
   grep -q '0x0000: fsb'   '${CMAKE_BINARY_DIR}/pdp10.fsb' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.fsb' && \
   \"$E\" upcl decode \"$P\" 0o122040000100 > '${CMAKE_BINARY_DIR}/pdp10.fix' && \
   grep -q '0x0000: fix'   '${CMAKE_BINARY_DIR}/pdp10.fix' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.fix' && \
   \"$E\" upcl decode \"$P\" 0o126040000100 > '${CMAKE_BINARY_DIR}/pdp10.fixr' && \
   grep -q '0x0000: fixr'  '${CMAKE_BINARY_DIR}/pdp10.fixr' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.fixr' && \
   \"$E\" upcl decode \"$P\" 0o127040000100 > '${CMAKE_BINARY_DIR}/pdp10.fltr' && \
   grep -q '0x0000: fltr'  '${CMAKE_BINARY_DIR}/pdp10.fltr' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.fltr' && \
   \"$E\" upcl decode \"$P\" 0o132040000100 > '${CMAKE_BINARY_DIR}/pdp10.fsc' && \
   grep -q '0x0000: fsc'   '${CMAKE_BINARY_DIR}/pdp10.fsc' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.fsc' && \
   \"$E\" upcl decode \"$P\" 0o110040000100 > '${CMAKE_BINARY_DIR}/pdp10.dfad' && \
   grep -q '0x0000: dfad'  '${CMAKE_BINARY_DIR}/pdp10.dfad' && \
   grep -q 'acn=0x1'       '${CMAKE_BINARY_DIR}/pdp10.dfad'")
set_tests_properties(upcl.pdp10-fp PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|error:")

# PDP-10 KL10 EXTENDED ADDRESSING (Task 15; feature `extended`, kl10 only; feature `kl_insns`,
# kl10+ks10).  The same 36-bit word is decoded under various cpu selectors proving feature gates.
# Opcodes sourced from the PDP-10 opcode chart (ultimate.com/phil/pdp10/opcodes.txt) and the KL10
# Hardware Reference:
#   XMOVEI = SETMI (0o415): XMOVEI wins under [@kl10] by feature+priority (extended ON); under
#     [@ka10] / [@ks10] (extended OFF) the SAME word decodes as the base-ISA SETMI.
#   ADJSP  = 0o105 (kl_insns gate; present under kl10 AND ks10; absent under ka10/ki10/pdp6).
#   EXTEND = 0o123 (KL string/decimal PREFIX; present under kl10, absent under ka10/ks10).
# The XMOVEI body resolves the EA through @ea_ext, whose indirect chain dispatches on the fetched
# word's format bit (IFIW bit35=1 -> 18-bit local recompute; EFIW bit35=0 -> 30-bit global), so the
# SSA carries the format-bit test and the while-loop chain.  Words: ac=1, y=0o100; the indirect
# XMOVEI carries i=1, x=2 (0o415062000100) to exercise the IFIW/EFIW dispatch.
add_test(NAME upcl.pdp10-extended COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$P[@kl10]\" 0o415040000100 > '${CMAKE_BINARY_DIR}/pdp10.xmovei' && \
   grep -q '0x0000: xmovei' '${CMAKE_BINARY_DIR}/pdp10.xmovei' && \
   grep -q 'acn=0x1'        '${CMAKE_BINARY_DIR}/pdp10.xmovei' && \
   grep -q 'y=0x40'         '${CMAKE_BINARY_DIR}/pdp10.xmovei' && \
   \"$E\" upcl decode \"$P[@kl10]\" 0o415062000100 > '${CMAKE_BINARY_DIR}/pdp10.xmovei.efiw' && \
   grep -q '0x0000: xmovei' '${CMAKE_BINARY_DIR}/pdp10.xmovei.efiw' && \
   grep -q 'i=0x1'          '${CMAKE_BINARY_DIR}/pdp10.xmovei.efiw' && \
   grep -q 'while.head'     '${CMAKE_BINARY_DIR}/pdp10.xmovei.efiw' && \
   grep -q 'while.body'     '${CMAKE_BINARY_DIR}/pdp10.xmovei.efiw' && \
   grep -q 'load.i36'       '${CMAKE_BINARY_DIR}/pdp10.xmovei.efiw' && \
   \"$E\" upcl decode \"$P[@kl10]\" 0o105040000100 > '${CMAKE_BINARY_DIR}/pdp10.adjsp.kl' && \
   grep -q '0x0000: adjsp'  '${CMAKE_BINARY_DIR}/pdp10.adjsp.kl' && \
   \"$E\" upcl decode \"$P[@ks10]\" 0o105040000100 > '${CMAKE_BINARY_DIR}/pdp10.adjsp.ks' && \
   grep -q '0x0000: adjsp'  '${CMAKE_BINARY_DIR}/pdp10.adjsp.ks' && \
   \"$E\" upcl decode \"$P[@ka10]\" 0o105040000100 > '${CMAKE_BINARY_DIR}/pdp10.adjsp.ka' && \
   grep -q 'no encoding matched' '${CMAKE_BINARY_DIR}/pdp10.adjsp.ka' && \
   \"$E\" upcl decode \"$P[@kl10]\" 0o123040000100 > '${CMAKE_BINARY_DIR}/pdp10.extend.kl' && \
   grep -q '0x0000: extend' '${CMAKE_BINARY_DIR}/pdp10.extend.kl' && \
   \"$E\" upcl decode \"$P[@ka10]\" 0o415040000100 > '${CMAKE_BINARY_DIR}/pdp10.setmi.ka' && \
   grep -q '0x0000: setmi'  '${CMAKE_BINARY_DIR}/pdp10.setmi.ka' && \
   ! grep -q 'xmovei'       '${CMAKE_BINARY_DIR}/pdp10.setmi.ka' && \
   \"$E\" upcl decode \"$P[@ks10]\" 0o415040000100 > '${CMAKE_BINARY_DIR}/pdp10.setmi.ks' && \
   grep -q '0x0000: setmi'  '${CMAKE_BINARY_DIR}/pdp10.setmi.ks' && \
   ! grep -q 'xmovei'       '${CMAKE_BINARY_DIR}/pdp10.setmi.ks' && \
   \"$E\" upcl decode \"$P[@ka10]\" 0o123040000100 > '${CMAKE_BINARY_DIR}/pdp10.extend.ka' && \
   grep -q 'no encoding matched' '${CMAKE_BINARY_DIR}/pdp10.extend.ka'")
set_tests_properties(upcl.pdp10-extended PROPERTIES FAIL_REGULAR_EXPRESSION "error:")

# PDP-10 CPU VARIANT LINEAGE (Task 16 -- cpu "pdp6"/"ka10"/"ki10"/"kl10"/"ks10" via `extends`).
# Verifies the `extends` union correctly propagates features through the lineage:
#   pdp6:  no fp, no extended, no kl_insns -> fad = "no encoding matched"; setmi (0o415) = base SETMI
#   ka10:  fp, no extended, no kl_insns    -> fad decodes; 0o415 = SETMI (no xmovei)
#   ki10:  fp (via extends ka10), no extended, no kl_insns -> fad decodes; 0o415 = SETMI (no xmovei)
#   kl10:  fp + extended + kl_insns (via extends ki10 + body) -> fad decodes; 0o415 = XMOVEI
#   ks10:  fp + kl_insns (Model A, single-section -- NO extended) -> fad decodes; 0o415 = SETMI
# Opcodes: FAD=0o140, XMOVEI/SETMI=0o415 (ac=1 i=0 x=0 y=0o100).
add_test(NAME upcl.pdp10-variants COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl check  \"$P\" > '${CMAKE_BINARY_DIR}/pdp10.v.check' && \
   grep -q 'cpu \"pdp6\"' '${CMAKE_BINARY_DIR}/pdp10.v.check' && \
   grep -q 'cpu \"ka10\"' '${CMAKE_BINARY_DIR}/pdp10.v.check' && \
   grep -q 'cpu \"ki10\"' '${CMAKE_BINARY_DIR}/pdp10.v.check' && \
   grep -q 'cpu \"kl10\"' '${CMAKE_BINARY_DIR}/pdp10.v.check' && \
   grep -q 'cpu \"ks10\"' '${CMAKE_BINARY_DIR}/pdp10.v.check' && \
   \"$E\" upcl decode \"$P[@kl10]\" 0o415040000100 > '${CMAKE_BINARY_DIR}/pdp10.v.xmovei.kl10' && \
   grep -q '0x0000: xmovei' '${CMAKE_BINARY_DIR}/pdp10.v.xmovei.kl10' && \
   \"$E\" upcl decode \"$P[@ks10]\" 0o415040000100 > '${CMAKE_BINARY_DIR}/pdp10.v.setmi.ks10' && \
   grep -q '0x0000: setmi'  '${CMAKE_BINARY_DIR}/pdp10.v.setmi.ks10' && \
   ! grep -q 'xmovei' '${CMAKE_BINARY_DIR}/pdp10.v.setmi.ks10' && \
   \"$E\" upcl decode \"$P[@ka10]\" 0o415040000100 > '${CMAKE_BINARY_DIR}/pdp10.v.setmi.ka10' && \
   grep -q '0x0000: setmi'  '${CMAKE_BINARY_DIR}/pdp10.v.setmi.ka10' && \
   ! grep -q 'xmovei' '${CMAKE_BINARY_DIR}/pdp10.v.setmi.ka10' && \
   \"$E\" upcl decode \"$P[@ki10]\" 0o415040000100 > '${CMAKE_BINARY_DIR}/pdp10.v.setmi.ki10' && \
   grep -q '0x0000: setmi'  '${CMAKE_BINARY_DIR}/pdp10.v.setmi.ki10' && \
   ! grep -q 'xmovei' '${CMAKE_BINARY_DIR}/pdp10.v.setmi.ki10' && \
   \"$E\" upcl decode \"$P[@pdp6]\"  0o415040000100 > '${CMAKE_BINARY_DIR}/pdp10.v.setmi.pdp6' && \
   grep -q '0x0000: setmi'  '${CMAKE_BINARY_DIR}/pdp10.v.setmi.pdp6' && \
   \"$E\" upcl decode \"$P[@ka10]\" 0o140040000100 > '${CMAKE_BINARY_DIR}/pdp10.v.fad.ka10' && \
   grep -q '0x0000: fad'    '${CMAKE_BINARY_DIR}/pdp10.v.fad.ka10' && \
   \"$E\" upcl decode \"$P[@ki10]\" 0o140040000100 > '${CMAKE_BINARY_DIR}/pdp10.v.fad.ki10' && \
   grep -q '0x0000: fad'    '${CMAKE_BINARY_DIR}/pdp10.v.fad.ki10' && \
   \"$E\" upcl decode \"$P[@kl10]\" 0o140040000100 > '${CMAKE_BINARY_DIR}/pdp10.v.fad.kl10' && \
   grep -q '0x0000: fad'    '${CMAKE_BINARY_DIR}/pdp10.v.fad.kl10' && \
   \"$E\" upcl decode \"$P[@ks10]\" 0o140040000100 > '${CMAKE_BINARY_DIR}/pdp10.v.fad.ks10' && \
   grep -q '0x0000: fad'    '${CMAKE_BINARY_DIR}/pdp10.v.fad.ks10' && \
   \"$E\" upcl decode \"$P[@pdp6]\"  0o140040000100 > '${CMAKE_BINARY_DIR}/pdp10.v.fad.pdp6' && \
   grep -q 'no encoding matched' '${CMAKE_BINARY_DIR}/pdp10.v.fad.pdp6'")
set_tests_properties(upcl.pdp10-variants PROPERTIES FAIL_REGULAR_EXPRESSION "error:")

# DEC PDP-10 arch header + register file (Phase 1 / Task 5). Verify: word=36 addr=18 big-endian
# word-addressed arch; group AC with AC/low-memory aliasing (aliases_memory 0); group S with
# 18-bit PC (%PC) and 18-bit pcflags (%PSR) carrying the 13 PC flag bits (AROV->%V, CRY0->%C,
# CRY1, FOV, FPD, USR, UIO, PUB, AFI, TRAP2, TRAP1, FXU, DCK). No instructions yet (Phase 3
# adds @ea; Phase 4+ adds instructions). Bit positions sourced from SIMH pdp10_defs.h F_V_*.
add_test(NAME upcl.check-pdp10 COMMAND sh -c
  "'$<TARGET_FILE:lcx>' upcl check '${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl' > '${CMAKE_BINARY_DIR}/pdp10.out' && \
   grep -q 'arch \"pdp10\"'  '${CMAKE_BINARY_DIR}/pdp10.out' && \
   grep -q 'word=36'         '${CMAKE_BINARY_DIR}/pdp10.out' && \
   grep -q 'addr=18'         '${CMAKE_BINARY_DIR}/pdp10.out' && \
   grep -q 'flag AROV'       '${CMAKE_BINARY_DIR}/pdp10.out' && \
   grep -q 'flag CRY0'       '${CMAKE_BINARY_DIR}/pdp10.out' && \
   grep -q 'flag DCK'        '${CMAKE_BINARY_DIR}/pdp10.out' && \
   grep -q 'ac0 '            '${CMAKE_BINARY_DIR}/pdp10.out' && \
   grep -q 'pc '             '${CMAKE_BINARY_DIR}/pdp10.out'")
set_tests_properties(upcl.check-pdp10 PROPERTIES FAIL_REGULAR_EXPRESSION "error|not valid")

add_test(NAME upcl.check-pdp1 COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl check \"$P\"")
set_tests_properties(upcl.check-pdp1 PROPERTIES FAIL_REGULAR_EXPRESSION "error:|no encoding")

add_test(NAME upcl.decode-pdp1 COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$P\" 0o200100 > '${CMAKE_BINARY_DIR}/pdp1.lac' && \
   grep -q '0x0000: lac' '${CMAKE_BINARY_DIR}/pdp1.lac' && \
   \"$E\" upcl decode \"$P\" 0o240100 > '${CMAKE_BINARY_DIR}/pdp1.dac' && \
   grep -q '0x0000: dac' '${CMAKE_BINARY_DIR}/pdp1.dac' && \
   \"$E\" upcl decode \"$P\" 0o210100 > '${CMAKE_BINARY_DIR}/pdp1.laci' && \
   grep -q 'while' '${CMAKE_BINARY_DIR}/pdp1.laci' && \
   \"$E\" upcl decode \"$P\" 0o400021 > '${CMAKE_BINARY_DIR}/pdp1.add' && \
   grep -q '0x0000: add' '${CMAKE_BINARY_DIR}/pdp1.add' && \
   \"$E\" upcl decode \"$P\" 0o420021 > '${CMAKE_BINARY_DIR}/pdp1.sub' && \
   grep -q '0x0000: sub' '${CMAKE_BINARY_DIR}/pdp1.sub' && \
   \"$E\" upcl decode \"$P\" 0o440021 > '${CMAKE_BINARY_DIR}/pdp1.idx' && \
   grep -q '0x0000: idx' '${CMAKE_BINARY_DIR}/pdp1.idx' && \
   \"$E\" upcl decode \"$P\" 0o460021 > '${CMAKE_BINARY_DIR}/pdp1.isp' && \
   grep -q '0x0000: isp' '${CMAKE_BINARY_DIR}/pdp1.isp' && \
   \"$E\" upcl decode \"$P\" 0o020100 | grep -q ': and' && \
   \"$E\" upcl decode \"$P\" 0o040100 | grep -q ': ior' && \
   \"$E\" upcl decode \"$P\" 0o060100 | grep -q ': xor' && \
   \"$E\" upcl decode \"$P\" 0o340100 | grep -q ': dzm' && \
   \"$E\" upcl decode \"$P\" 0o500100 | grep -q ': sad' && \
   \"$E\" upcl decode \"$P\" 0o520100 | grep -q ': sas' && \
   \"$E\" upcl decode \"$P\" 0o600100 | grep -q ': jmp' && \
   \"$E\" upcl decode \"$P\" 0o620100 | grep -q ': jsp' && \
   \"$E\" upcl decode \"$P\" 0o160100 | grep -q ': cal' && \
   \"$E\" upcl decode \"$P\" 0o170100 | grep -q ': jda' && \
   \"$E\" upcl decode \"$P\" 0o100100 | grep -q ': xct' && \
   \"$E\" upcl decode \"$P\" 0o760000 | grep -q ': opr' && \
   \"$E\" upcl decode \"$P\" 0o761200 | grep -q ': opr' && \
   \"$E\" upcl decode \"$P\" 0o760400 | grep -q 'condbr' && \
   \"$E\" upcl decode \"$P\" 0o640100 | grep -q ': skp' && \
   \"$E\" upcl decode \"$P\" 0o650100 | grep -q ': skp' && \
   \"$E\" upcl decode \"$P\" 0o661000 | grep -q ': sft' && \
   \"$E\" upcl decode \"$P\" 0o700100 | grep -q ': law' && \
   \"$E\" upcl decode \"$P@pdp1\"    0o540100 | grep -q ': mus' && \
   \"$E\" upcl decode \"$P@pdp1mul\" 0o540100 | grep -q ': mul' && \
   \"$E\" upcl decode \"$P\" 0o720007 | grep -q ': iot' && \
   \"$E\" upcl decode \"$P\" 0o100100 > '${CMAKE_BINARY_DIR}/pdp1.xct' && \
   grep -q '0x0000: xct' '${CMAKE_BINARY_DIR}/pdp1.xct' && \
   grep -qF 'syscall 0xe5ec' '${CMAKE_BINARY_DIR}/pdp1.xct'")
set_tests_properties(upcl.decode-pdp1 PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")

add_test(NAME upcl.pdp1-arith COMMAND sh -c
  "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; \
   B='${CMAKE_BINARY_DIR}/pdp1arith.raw'; \
   python3 '${CMAKE_CURRENT_SOURCE_DIR}/test/pdp1_asm.py' \
     --org 0 0o200020 0o400021 0o240022 0o760400 \
     --at 0o20 0o000005 0o777775 > \"$B\" && \
   \"$E\" run \"$B\" --arch \"upcl:$X@pdp1\" --raw18 --start 0 --dump 0o22 \
     | grep -q '0o22. = 0x00003'")
set_tests_properties(upcl.pdp1-arith PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")

add_test(NAME upcl.pdp1-shift COMMAND sh -c
  "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; \
   B='${CMAKE_BINARY_DIR}/pdp1shift.raw'; \
   python3 '${CMAKE_CURRENT_SOURCE_DIR}/test/pdp1_asm.py' \
     --org 0 0o200020 0o661007 0o240021 0o760400 --at 0o20 0o000001 > \"$B\" && \
   \"$E\" run \"$B\" --arch \"upcl:$X@pdp1\" --raw18 --start 0 --dump 0o21 \
     | grep -q '0o21. = 0x00008'")
set_tests_properties(upcl.pdp1-shift PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")

add_test(NAME upcl.pdp1-rim COMMAND sh -c
  "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; \
   B='${CMAKE_BINARY_DIR}/pdp1.rim'; \
   python3 '${CMAKE_CURRENT_SOURCE_DIR}/test/pdp1_asm.py' --rim \
     --dio 0o30 0o000007 --dio 0o31 0o760400 --jmp 0o31 > \"$B\" && \
   \"$E\" run \"$B\" --arch \"upcl:$X@pdp1\" --rim --dump 0o30 | grep -q '0o30. = 0x00007'")
set_tests_properties(upcl.pdp1-rim PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")

# Type 30 CRT display (dev 007): LAC X / LIO Y / IOT 0o7 / HLT, with M[0o20]=0o123400 (X)
# and M[0o21]=0o056700 (Y). The host traps IOT and, with PDP1_DPY_DUMP=1, prints a DPY line.
add_test(NAME upcl.pdp1-display COMMAND sh -c
  "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; \
   B='${CMAKE_BINARY_DIR}/pdp1dpy.raw'; \
   python3 '${CMAKE_CURRENT_SOURCE_DIR}/test/pdp1_asm.py' \
     --org 0 0o200020 0o220021 0o720007 0o760400 --at 0o20 0o123400 0o056700 > \"$B\" && \
   PDP1_DPY_DUMP=1 \"$E\" run \"$B\" --arch \"upcl:$X@pdp1\" --raw18 --start 0 | grep -q 'DPY '")
set_tests_properties(upcl.pdp1-display PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")

# NS32000 gen addressing modes: parse + decode. Each gen mode is carried by a REAL `movd
# <gen>, r0` instruction (Format 4: op=0x5, i=11, dst=r0). The original `_probe` scaffold
# (Task 5) was removed in Task 11; driving gen modes through `movd` is strictly better
# coverage: it proves gen decode through a production instruction. The bytes below are in
# REAL little-endian memory order (`endian little word`): the 2-byte Format-4 base word is
# stored low byte first, so it begins with 0x17 (the low byte = op[5]|i[11]|gen2=r0) and the
# (gen1 << 3) high byte follows, then the gen tail (displacement/immediate).
# Assertions (base word = 0x17 <hi>, then tail):
#   0x17 0x00              -> movd r0 -> src=r0 (register-direct)
#   0x17 0xA8 0x10         -> absolute  src=[0x10]
#   0x17 0x40 0x05         -> register-relative r0+5 -> src=[r0+0x5]
#   0x17 0xC0 0x20         -> FP-relative src=[fp+0x20]
#   0x17 0xF0 0x0A         -> scaled-index r2*4
#   0x17 0xA0 0x00..0x42   -> immediate src=0x42 (6-byte, E5 fixed-width)
# Hard modes (Task 6):
#   0x17 0x80 0x04 0x08    -> memory-relative FP disp2(disp1(FP)), 4-byte
#   0x17 0x88 0x04 0x08    -> memory-relative SP, 4-byte
#   0x17 0x90 0x04 0x08    -> memory-relative SB, 4-byte
#   0x17 0xB0 0x04 0x08    -> external EXT(disp1)+disp2 (MOD-based), 4-byte
#   0x17 0xB8              -> TOS source [sp] post sp+=4, 2-byte
add_test(NAME upcl.ns32k-gen-modes COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/ns32k.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl check \"$M\" > /dev/null && \
   \"$E\" upcl decode \"$M\" 0x17 0x00 | grep -q 'movd   dst=r0 src=r0' && \
   \"$E\" upcl decode \"$M\" 0x17 0xA8 0x10 | grep -q 'src=\\[0x10\\]' && \
   \"$E\" upcl decode \"$M\" 0x17 0x40 0x05 | grep -q 'r0+0x5' && \
   \"$E\" upcl decode \"$M\" 0x17 0xC0 0x20 | grep -q 'fp+0x20' && \
   \"$E\" upcl decode \"$M\" 0x17 0xF0 0x0A | grep -q 'r2\\*4' && \
   \"$E\" upcl decode \"$M\" 0x17 0xA0 0x00 0x00 0x00 0x42 | grep -q 'src=0x42' && \
   \"$E\" upcl decode \"$M\" 0x17 0xA0 0x00 0x00 0x00 0x42 | grep -q '6 byte' && \
   \"$E\" upcl decode \"$M\" 0x17 0x80 0x04 0x08 | grep -q 'fp' && \
   \"$E\" upcl decode \"$M\" 0x17 0x80 0x04 0x08 | grep -q '0x4' && \
   \"$E\" upcl decode \"$M\" 0x17 0x80 0x04 0x08 | grep -q '0x8' && \
   \"$E\" upcl decode \"$M\" 0x17 0x80 0x04 0x08 | grep -q '4 byte' && \
   \"$E\" upcl decode \"$M\" 0x17 0x88 0x04 0x08 | grep -q '4 byte' && \
   \"$E\" upcl decode \"$M\" 0x17 0x90 0x04 0x08 | grep -q 'sb' && \
   \"$E\" upcl decode \"$M\" 0x17 0x90 0x04 0x08 | grep -q '4 byte' && \
   \"$E\" upcl decode \"$M\" 0x17 0xB0 0x04 0x08 | grep -q 'mod' && \
   \"$E\" upcl decode \"$M\" 0x17 0xB0 0x04 0x08 | grep -q '4 byte' && \
   \"$E\" upcl decode \"$M\" 0x17 0xB8 | grep -q 'sp' && \
   \"$E\" upcl decode \"$M\" 0x17 0xB8 | grep -q '2 byte'")
set_tests_properties(upcl.ns32k-gen-modes PROPERTIES FAIL_REGULAR_EXPRESSION "error|not valid")

# NS32000 integer data-movement + ALU (Formats 2/4/6/7). Each byte sequence is the real NS32000
# encoding (opcode seeds + bit layout from binutils include/opcode/ns32k.h + opcodes/ns32k-dis.c):
# gen1 = first/source general operand, gen2 = destination, the 2-bit `i` length field selects
# byte/word/double. The NS32000 stores its multi-byte opcode word as a little-endian integer
# (`endian little word`), so the bytes below are in REAL memory order: the base word is byte-
# reversed relative to its hex word value (movd word 0x0057 -> bytes 0x57 0x00; lshd word 0x00574E ->
# 0x4E 0x57 0x00). Each pins the mnemonic AND the resolved src/dst operands so a wrong opcode/length/
# operand-binding fails. Covers movd, addd, cmpd (with flag body), the quick forms addqd/movqd, and
# the extended muld -- plus negd (Format 6), divd, and movzbd/movxbw (the zero/sign-extend pairs).
# Task 8 decode byte-sequence derivation (word values from binutils ns32k_opcodes[] seeds, then
# emitted low byte first):
# FORMAT 4 (16-bit word): gen1:5 gen2:5 op:4 i:2
#   andd  r0,r1: gen1=0,gen2=1,op=0xa,i=3 -> word 0x006B -> LE bytes 0x6B 0x00
#   tbitd r0,r1: gen1=0,gen2=1,op=0xd,i=3 -> word 0x0077 -> LE bytes 0x77 0x00
#   lshd  r0,r1: Format 6 (fmt=0x4e), word 0x00574E -> LE bytes 0x4E 0x57 0x00
#   extd  r1,r0,r2: Format 8, word 0x00562E -> LE bytes 0x2E 0x56 0x00
# Task 8 review fixes -- decode assertions:
#   cvtp r2,r0,r1: word 0x00536E -> LE bytes 0x6E 0x53 0x00
#   extsd r1,r0,{G=8,g=2}: Format 7 word 0x080FCE -> LE bytes 0xCE 0x0F 0x08, then tail 0x3A (4 bytes)
#   ffsd r0,r1: Format 6 word 0x00476E -> LE bytes 0x6E 0x47 0x00
add_test(NAME upcl.decode-ns32k COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/ns32k.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$M\" 0x57 0x00 | grep -q 'movd   dst=r1 src=r0' && \
   \"$E\" upcl decode \"$M\" 0xC3 0x10 | grep -q 'addd   dst=r3 src=r2' && \
   \"$E\" upcl decode \"$M\" 0x47 0x00 | grep -q 'cmpd   dst=r1 src=r0' && \
   \"$E\" upcl decode \"$M\" 0x8F 0x0A | grep -q 'addqd  dst=r1 quick=0x5' && \
   \"$E\" upcl decode \"$M\" 0xDF 0x17 | grep -q 'movqd  dst=r2 quick=0xf' && \
   \"$E\" upcl decode \"$M\" 0xCE 0x63 0x00 | grep -q 'muld   dst=r1 src=r0' && \
   \"$E\" upcl decode \"$M\" 0x4E 0x63 0x00 | grep -q 'negd   dst=r1 src=r0' && \
   \"$E\" upcl decode \"$M\" 0xCE 0x7F 0x00 | grep -q 'divd   dst=r1 src=r0' && \
   \"$E\" upcl decode \"$M\" 0xCE 0x58 0x00 | grep -q 'movzbd dst=r1 src=r0' && \
   \"$E\" upcl decode \"$M\" 0xCE 0x50 0x00 | grep -q 'movxbw dst=r1 src=r0' && \
   \"$E\" upcl decode \"$M\" 0x6B 0x00 | grep -q 'andd   dst=r1 src=r0' && \
   \"$E\" upcl decode \"$M\" 0x4E 0x57 0x00 | grep -q 'lshd   dst=r1 src=r0' && \
   \"$E\" upcl decode \"$M\" 0x77 0x00 | grep -q 'tbitd  dst=r1 src=r0' && \
   \"$E\" upcl decode \"$M\" 0x2E 0x56 0x00 | grep -q 'extd   dst=r0 rn=0x2 src=r1' && \
   \"$E\" upcl decode \"$M\" 0x6E 0x53 0x00 | grep -q 'cvtp' && \
   \"$E\" upcl decode \"$M\" 0xCE 0x0F 0x08 0x3A | grep -q 'extsd' && \
   \"$E\" upcl decode \"$M\" 0xCE 0x0F 0x08 0x3A | grep -q '4 byte' && \
   \"$E\" upcl decode \"$M\" 0x6E 0x47 0x00 | grep -q 'ffsd' && \
   \"$E\" upcl decode \"$M\" 0x0E 0x03 0x00 | grep -q 'movsd' && \
   \"$E\" upcl decode \"$M\" 0x0E 0x03 0x00 | grep -q '3 byte' && \
   \"$E\" upcl decode \"$M\" 0x4E 0x3F 0x00 | grep -q 'addpd' && \
   \"$E\" upcl decode \"$M\" 0x4E 0x2F 0x00 | grep -q 'subpd' && \
   \"$E\" upcl decode \"$M\" 0xEE 0x03 0x00 | grep -q 'checkd' && \
   \"$E\" upcl decode \"$M\" 0x2E 0x07 0x00 | grep -q 'indexd'")
set_tests_properties(upcl.decode-ns32k PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|error|not valid")

# NS32000 CONTROL TRANSFER / module calls / traps (Task 10). Every byte sequence is the real
# NS32000 encoding from binutils include/opcode/ns32k.h ns32k_opcodes[] (seeds cross-checked with
# opcodes/ns32k-dis.c). The key structural property proven here is the VARIABLE-LENGTH branch
# displacement: the same self-describing big-endian scheme as gen displacements (E2). The target
# is rendered as [pc+0xNN] because it is bound through a one-rule addrmode `%M[pc + disp varlen]`
# whose @lea yields pc + disp. Derivation:
#   Bcond (Format 0): byte = cond<<4 | 0xA. beq cond=0 -> 0x0A; bne cond=1 -> 0x1A; bgt cond=6 ->
#     0x6A; blo cond=0xA -> 0xAA; br cond=0xE -> 0xEA. Then a varlen PC-relative displacement.
#   VARLEN PROOF: br with a 1-byte disp (0xEA 0x10) is 2 bytes; br with a 2-byte (14-bit) disp
#     (0xEA 0x80 0x40, top bits 10 -> 2-byte form) is 3 bytes -- the decoded length tracks the
#     displacement encoding, not a fixed width. bgt likewise 0x6A 0x10 -> 2 bytes.
#   BSR (Format 1): 0x02 + disp. high nibble 0x0, low 0x2. 0x02 0x10 -> 2 bytes.
#   RET (Format 1): 0x12 + varlen stack-adjust disp. 0x12 0x04 -> 2 bytes.
#   SAVE (Format 1): 0x62 + reglist byte. 0x62 0x05 -> 2 bytes (mask=0x05 -> r0,r2).
#   ENTER (Format 1): 0x82 + reglist byte + varlen frame disp. 0x82 0x03 0x08 -> 3 bytes.
#   NOP/SVC/BPT (Format 1): 0xA2 / 0xE2 / 0xF2 -> 1 byte (SVC/BPT are @trap).
#   Bytes below are REAL little-endian memory order (`endian little word`): Format-0/1 use a
#   single base byte (reverses to itself), so only the 2-byte Format-2/3 words are byte-swapped.
#   JUMP (Format 3): word = gen:5 (bits15:11) : opcode11:11. jump opc=0x27f, gen=r0 -> word 0x027F
#     -> LE bytes 0x7F 0x02. jsr opc=0x67f -> 0x7F 0x06. cased opc=0x77f -> 0x7F 0x07.
#   ACBd (Format 2): word = gen2:5 quick:4 op[4:1]=0x9 op[0]=1 i:2. acbd gen2=r0 quick=5 i=3 ->
#     word 0x02CF -> LE bytes 0xCF 0x02, then varlen disp 0x10 -> 3 bytes; the disasm pins
#     quick=0x5 (the immediate is read in the body) AND the [pc+0x10] target.
add_test(NAME upcl.decode-ns32k-ctl COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/ns32k.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl check \"$M\" > /dev/null && \
   \"$E\" upcl decode \"$M\" 0x0A 0x10 | grep -q 'beq' && \
   \"$E\" upcl decode \"$M\" 0x0A 0x10 | grep -q 'pc+0x10' && \
   \"$E\" upcl decode \"$M\" 0x0A 0x10 | grep -q '2 byte' && \
   \"$E\" upcl decode \"$M\" 0x1A 0x10 | grep -q 'bne' && \
   \"$E\" upcl decode \"$M\" 0x6A 0x10 | grep -q 'bgt' && \
   \"$E\" upcl decode \"$M\" 0xAA 0x10 | grep -q 'blo' && \
   \"$E\" upcl decode \"$M\" 0xEA 0x10 | grep -q 'br' && \
   \"$E\" upcl decode \"$M\" 0xEA 0x10 | grep -q '2 byte' && \
   \"$E\" upcl decode \"$M\" 0xEA 0x80 0x40 | grep -q 'br' && \
   \"$E\" upcl decode \"$M\" 0xEA 0x80 0x40 | grep -q 'pc+0x40' && \
   \"$E\" upcl decode \"$M\" 0xEA 0x80 0x40 | grep -q '3 byte' && \
   \"$E\" upcl decode \"$M\" 0x02 0x10 | grep -q 'bsr' && \
   \"$E\" upcl decode \"$M\" 0x02 0x10 | grep -q 'pc+0x10' && \
   \"$E\" upcl decode \"$M\" 0x12 0x04 | grep -q 'ret' && \
   \"$E\" upcl decode \"$M\" 0x62 0x05 | grep -q 'save' && \
   \"$E\" upcl decode \"$M\" 0x62 0x05 | grep -q 'mask=0x5' && \
   \"$E\" upcl decode \"$M\" 0x82 0x03 0x08 | grep -q 'enter' && \
   \"$E\" upcl decode \"$M\" 0x82 0x03 0x08 | grep -q '3 byte' && \
   \"$E\" upcl decode \"$M\" 0xA2 | grep -q 'nop' && \
   \"$E\" upcl decode \"$M\" 0xE2 | grep -q 'svc' && \
   \"$E\" upcl decode \"$M\" 0xF2 | grep -q 'bpt' && \
   \"$E\" upcl decode \"$M\" 0x7F 0x02 | grep -q 'jump   dst=r0' && \
   \"$E\" upcl decode \"$M\" 0x7F 0x06 | grep -q 'jsr    dst=r0' && \
   \"$E\" upcl decode \"$M\" 0x7F 0x07 | grep -q 'cased  tbl=r0' && \
   \"$E\" upcl decode \"$M\" 0xCF 0x02 0x10 | grep -q 'acbd' && \
   \"$E\" upcl decode \"$M\" 0xCF 0x02 0x10 | grep -q 'quick=0x5' && \
   \"$E\" upcl decode \"$M\" 0xCF 0x02 0x10 | grep -q 'pc+0x10' && \
   \"$E\" upcl decode \"$M\" 0xCF 0x02 0x10 | grep -q '3 byte'")
set_tests_properties(upcl.decode-ns32k-ctl PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|error|not valid")

# NS32000 LONG-FORM Bcond (E8 decode-priority tie-break). A first byte whose low nibble is 0xA is
# ALWAYS a Format-0 branch on real hardware, never a Format-4 ALU word -- but a long (multi-byte)
# branch displacement makes the leading bytes ALSO match a wider Format-4 word that fixes MORE
# constant bits (op:4 + i:2 = 6 vs the Bcond's op:4 = 4), so the most-constant-bits matcher used to
# mis-decode `beq` (0x0A) with a 14-bit disp as `addb`. With `priority 1` on the Format-0/Format-2
# encodings the branch now wins regardless of the bit count. RED before E8 (decoded addb), GREEN now.
#   beq 14-bit disp: 0x0A 0x80 0x40 -- top disp bits 10 => 2-byte disp form => 3-byte insn, pc+0x40.
#   beq 30-bit disp: 0x0A 0xC0 0x00 0x00 0x40 -- top bits 11 => 4-byte disp form => 5-byte insn.
#   bgt long: 0x6A 0x80 0x40 -- the single-flag (N) condition, long disp; 3-byte, pc+0x40.
# The short-disp forms (already covered by decode-ns32k-ctl) stay `beq`/`bgt`; the genuine Format-4
# ALU words (addb/addd/movd/cmpd, low nibble != 0xA) are re-asserted here to prove they are unchanged.
add_test(NAME upcl.ns32k-branch-long COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/ns32k.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl check \"$M\" > /dev/null && \
   \"$E\" upcl decode \"$M\" 0x0A 0x80 0x40 | grep -q 'beq' && \
   \"$E\" upcl decode \"$M\" 0x0A 0x80 0x40 | grep -q 'pc+0x40' && \
   \"$E\" upcl decode \"$M\" 0x0A 0x80 0x40 | grep -q '3 byte' && \
   \"$E\" upcl decode \"$M\" 0x0A 0xC0 0x00 0x00 0x40 | grep -q 'beq' && \
   \"$E\" upcl decode \"$M\" 0x0A 0xC0 0x00 0x00 0x40 | grep -q 'pc+0x40' && \
   \"$E\" upcl decode \"$M\" 0x0A 0xC0 0x00 0x00 0x40 | grep -q '5 byte' && \
   \"$E\" upcl decode \"$M\" 0x0A 0x10 | grep -q 'beq' && \
   \"$E\" upcl decode \"$M\" 0x0A 0x10 | grep -q '2 byte' && \
   \"$E\" upcl decode \"$M\" 0x6A 0x80 0x40 | grep -q 'bgt' && \
   \"$E\" upcl decode \"$M\" 0x6A 0x80 0x40 | grep -q 'pc+0x40' && \
   \"$E\" upcl decode \"$M\" 0x6A 0x80 0x40 | grep -q '3 byte' && \
   \"$E\" upcl decode \"$M\" 0x57 0x00 | grep -q 'movd   dst=r1 src=r0' && \
   \"$E\" upcl decode \"$M\" 0xC3 0x10 | grep -q 'addd   dst=r3 src=r2' && \
   \"$E\" upcl decode \"$M\" 0x47 0x00 | grep -q 'cmpd   dst=r1 src=r0'")
set_tests_properties(upcl.ns32k-branch-long PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|error|not valid")

# NS32000 PROCESSOR-CONTROL instructions (Task 11). Byte sequences derived from binutils
# include/opcode/ns32k.h (ns32k_opcodes[]) and gas/config/tc-ns32k.c (cpureg_032 table).
#
# LPR/SPR (Format 2-style, 16-bit): gen:5 : reg4:4 : op5:5 : i:2
#   lprb=0x6c lprw=0x6d lprd=0x6f  -> op5=0x1b, i={0,1,3}
#   sprb=0x2c sprw=0x2d sprd=0x2f  -> op5=0x0b, i={0,1,3}
#   Bytes below are REAL little-endian memory order (`endian little word`): each base word is
#   emitted low byte first.
#   lprd psr(0xd),r0: gen2=0 reg4=0xd op5=0x1b i=3 -> word 0x06ef -> LE bytes 0xef 0x06
#   sprd r0,psr(0xd): gen2=0 reg4=0xd op5=0x0b i=3 -> word 0x06af -> LE bytes 0xaf 0x06
#   lprd sp(0x9),r1: gen2=1 reg4=0x9 op5=0x1b i=3 -> word 0x0cef -> LE bytes 0xef 0x0c
#
# ADJSP (16-bit, 11-bit opcode): gen:5 : op9:9 : i:2
#   adjspd=0x057f -> op9=0x15f i=3; gen=r0(0): word 0x057f -> LE bytes 0x7f 0x05
#
# BISPSR/BICPSR (16-bit, 11-bit opcode): gen:5 : op9:9 : i:2
#   bispsrw=0x037d -> op9=0xdf i=1; gen=r0(0): word 0x037d -> LE bytes 0x7d 0x03
#   bicpsrw=0x017d -> op9=0x5f i=1; gen=r0(0): word 0x017d -> LE bytes 0x7d 0x01
#
# SETCFG (24-bit): gen(0):5 : opts:4 : op7:7 : fmt:8(=0x0e)
#   setcfg seed=0x0b0e; opts=0: word 0x000b0e -> LE bytes 0x0e 0x0b 0x00
add_test(NAME upcl.ns32k-sys COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/ns32k.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl check \"$M\" > /dev/null && \
   \"$E\" upcl decode \"$M\" 0xef 0x06 | grep -q 'lprd' && \
   \"$E\" upcl decode \"$M\" 0xef 0x06 | grep -q 'dst=r0' && \
   \"$E\" upcl decode \"$M\" 0xef 0x06 | grep -q 'reg=0xd' && \
   \"$E\" upcl decode \"$M\" 0xef 0x06 | grep -q '2 byte' && \
   \"$E\" upcl decode \"$M\" 0xef 0x0c | grep -q 'lprd' && \
   \"$E\" upcl decode \"$M\" 0xef 0x0c | grep -q 'dst=r1' && \
   \"$E\" upcl decode \"$M\" 0xef 0x0c | grep -q 'reg=0x9' && \
   \"$E\" upcl decode \"$M\" 0xaf 0x06 | grep -q 'sprd' && \
   \"$E\" upcl decode \"$M\" 0xaf 0x06 | grep -q 'src=r0' && \
   \"$E\" upcl decode \"$M\" 0xaf 0x06 | grep -q 'reg=0xd' && \
   \"$E\" upcl decode \"$M\" 0x7f 0x05 | grep -q 'adjspd' && \
   \"$E\" upcl decode \"$M\" 0x7f 0x05 | grep -q 'src=r0' && \
   \"$E\" upcl decode \"$M\" 0x7f 0x05 | grep -q '2 byte' && \
   \"$E\" upcl decode \"$M\" 0x7d 0x03 | grep -q 'bispsrw' && \
   \"$E\" upcl decode \"$M\" 0x7d 0x03 | grep -q 'src=r0' && \
   \"$E\" upcl decode \"$M\" 0x7d 0x01 | grep -q 'bicpsrw' && \
   \"$E\" upcl decode \"$M\" 0x7d 0x01 | grep -q 'src=r0' && \
   \"$E\" upcl decode \"$M\" 0x0e 0x0b 0x00 | grep -q 'setcfg' && \
   \"$E\" upcl decode \"$M\" 0x0e 0x0b 0x00 | grep -q 'opts=0x0' && \
   \"$E\" upcl decode \"$M\" 0x0e 0x0b 0x00 | grep -q '3 byte'")
set_tests_properties(upcl.ns32k-sys PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|error|not valid")

# NS32081/NS32381 FPU (feature `fpu`). Byte sequences derived from GNU binutils
# `include/opcode/ns32k.h` seeds (ns32k_opcodes[]), verified against the NS32081 (July 1988)
# and NS32381 (April 1991) datasheets.
#
# ENCODING NOTES (24-bit instructions; word values below, then emitted LOW BYTE FIRST per
# `endian little word` -- so the FPU escape byte 0xBE/0x3E/0xFE is at the LOWEST address):
#   Format 11 (FMT11=0xBE): gen1:5 gen2:5 op:4 f:2 fmt:8(=0xBE)
#     addf  gen1=0 gen2=0 op=0 f=1: word 0x0001BE -> LE bytes 0xBE 0x01 0x00
#     movf  gen1=1 gen2=2 op=1 f=1: word 0x0885BE -> LE bytes 0xBE 0x85 0x08
#     cmpf  gen1=0 gen2=1 op=2 f=1: word 0x0049BE -> LE bytes 0xBE 0x49 0x00
#     movl  gen1=0 gen2=2 op=1 f=0: word 0x0084BE -> LE bytes 0xBE 0x84 0x00
#   Format 9 (FMT9=0x3E): gen1:5 gen2:5 op:3 f:1 i:2 fmt:8(=0x3E)
#     movbf gen1=0 gen2=0 op=0 f=1 i=0: word 0x00043E -> LE bytes 0x3E 0x04 0x00
#     movdf gen1=0 gen2=0 op=0 f=1 i=3: word 0x00073E -> LE bytes 0x3E 0x07 0x00
#     movfl gen1=0 gen2=2 sub=0x1b:     word 0x009B3E -> LE bytes 0x3E 0x9B 0x00
#     lfsr  gen1=0 op=0x0f3e:            word 0x000F3E -> LE bytes 0x3E 0x0F 0x00
#     sfsr  gen1=r0 gen2=0 op=6 f=1 i=3: word 0x00373E -> LE bytes 0x3E 0x37 0x00
#   Format 12 (FMT12=0xFE): gen1:5 gen2:5 op:4 f:2 fmt:8(=0xFE)
#     polyf gen1=0 gen2=1 op=2 f=1:  word 0x0049FE -> LE bytes 0xFE 0x49 0x00
#     logbf gen1=0 gen2=1 op=5 f=1:  word 0x0055FE -> LE bytes 0xFE 0x55 0x00
add_test(NAME upcl.ns32k-fpu COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/ns32k.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl check \"$M\" > /dev/null && \
   \"$E\" upcl decode \"$M\" 0xbe 0x01 0x00 | grep -q 'addf' && \
   \"$E\" upcl decode \"$M\" 0xbe 0x01 0x00 | grep -q 'src=f0' && \
   \"$E\" upcl decode \"$M\" 0xbe 0x01 0x00 | grep -q 'dst=f0' && \
   \"$E\" upcl decode \"$M\" 0xbe 0x01 0x00 | grep -q '3 byte' && \
   \"$E\" upcl decode \"$M\" 0xbe 0x85 0x08 | grep -q 'movf' && \
   \"$E\" upcl decode \"$M\" 0xbe 0x85 0x08 | grep -q 'src=f1' && \
   \"$E\" upcl decode \"$M\" 0xbe 0x85 0x08 | grep -q 'dst=f2' && \
   \"$E\" upcl decode \"$M\" 0xbe 0x49 0x00 | grep -q 'cmpf' && \
   \"$E\" upcl decode \"$M\" 0xbe 0x49 0x00 | grep -q 'src=f0' && \
   \"$E\" upcl decode \"$M\" 0xbe 0x49 0x00 | grep -q 'dst=f1' && \
   \"$E\" upcl decode \"$M\" 0xbe 0x84 0x00 | grep -q 'movl' && \
   \"$E\" upcl decode \"$M\" 0xbe 0x84 0x00 | grep -q 'src=f0' && \
   \"$E\" upcl decode \"$M\" 0xbe 0x84 0x00 | grep -q 'dst=f2' && \
   \"$E\" upcl decode \"$M\" 0x3e 0x04 0x00 | grep -q 'movbf' && \
   \"$E\" upcl decode \"$M\" 0x3e 0x04 0x00 | grep -q 'src=r0' && \
   \"$E\" upcl decode \"$M\" 0x3e 0x04 0x00 | grep -q 'dst=f0' && \
   \"$E\" upcl decode \"$M\" 0x3e 0x07 0x00 | grep -q 'movdf' && \
   \"$E\" upcl decode \"$M\" 0x3e 0x07 0x00 | grep -q 'src=r0' && \
   \"$E\" upcl decode \"$M\" 0x3e 0x07 0x00 | grep -q 'dst=f0' && \
   \"$E\" upcl decode \"$M\" 0x3e 0x9b 0x00 | grep -q 'movfl' && \
   \"$E\" upcl decode \"$M\" 0x3e 0x9b 0x00 | grep -q 'src=f0' && \
   \"$E\" upcl decode \"$M\" 0x3e 0x9b 0x00 | grep -q 'dst=f2' && \
   \"$E\" upcl decode \"$M\" 0x3e 0x0f 0x00 | grep -q 'lfsr' && \
   \"$E\" upcl decode \"$M\" 0x3e 0x0f 0x00 | grep -q 'src=r0' && \
   \"$E\" upcl decode \"$M\" 0x3e 0x37 0x00 | grep -q 'sfsr' && \
   \"$E\" upcl decode \"$M\" 0x3e 0x37 0x00 | grep -q 'dst=r0' && \
   \"$E\" upcl decode \"$M\" 0xfe 0x49 0x00 | grep -q 'polyf' && \
   \"$E\" upcl decode \"$M\" 0xfe 0x55 0x00 | grep -q 'logbf'")
set_tests_properties(upcl.ns32k-fpu PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|error|not valid")

# NS32082/NS32382 MMU + NS32532/NS32732 cache (feature `mmu` + feature `cache`).
# Encodings from GNU binutils `include/opcode/ns32k.h` (ns32k_opcodes[]) and
# `gas/config/tc-ns32k.c` (mmureg_032 / mmureg_532 tables).
#
# ENCODING NOTES (24-bit instructions; word values below, then emitted LOW BYTE FIRST per
# `endian little word` -- so the format escape byte 0x1e/0xae is at the LOWEST address):
#   Format 14 (FMT14=0x1e): gen:5 | field:4 | op7:7 | fmt:8(=0x1e)
#     lmr  gen=r1 mreg=ptb0(0xc) op7=0x0b: word 0x0e0b1e -> LE bytes 0x1e 0x0b 0x0e
#     smr  gen=r0 mreg=msr(0xa)  op7=0x0f: word 0x050f1e -> LE bytes 0x1e 0x0f 0x05
#     rdval gen=r0: word 0x00031e -> LE bytes 0x1e 0x03 0x00
#     wrval gen=r0: word 0x00071e -> LE bytes 0x1e 0x07 0x00
#   Format 14 (CINV, 14-bit opcode): gen:5 | opts:4 | zero:1 | op14:14(=0x271e)
#     cinv gen=r0 opts=CINV_I(2): word 0x01271e -> LE bytes 0x1e 0x27 0x01
#   Format 16 (FMT16=0xae): gen1:5 | gen2:5 | op14:14
#     movsud gen1=r1(src) gen2=r2(dst): word 0x088fae -> LE bytes 0xae 0x8f 0x08
#     movusd gen1=r0(src) gen2=r0(dst): word 0x001fae -> LE bytes 0xae 0x1f 0x00
add_test(NAME upcl.ns32k-mmu COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/ns32k.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl check \"$M\" > /dev/null && \
   \"$E\" upcl decode \"$M\" 0x1e 0x0b 0x0e | grep -q 'lmr' && \
   \"$E\" upcl decode \"$M\" 0x1e 0x0b 0x0e | grep -q 'dst=r1' && \
   \"$E\" upcl decode \"$M\" 0x1e 0x0b 0x0e | grep -q 'mreg=0xc' && \
   \"$E\" upcl decode \"$M\" 0x1e 0x0b 0x0e | grep -q '3 byte' && \
   \"$E\" upcl decode \"$M\" 0x1e 0x0f 0x05 | grep -q 'smr' && \
   \"$E\" upcl decode \"$M\" 0x1e 0x0f 0x05 | grep -q 'src=r0' && \
   \"$E\" upcl decode \"$M\" 0x1e 0x0f 0x05 | grep -q 'mreg=0xa' && \
   \"$E\" upcl decode \"$M\" 0x1e 0x03 0x00 | grep -q 'rdval' && \
   \"$E\" upcl decode \"$M\" 0x1e 0x03 0x00 | grep -q 'ea=r0' && \
   \"$E\" upcl decode \"$M\" 0x1e 0x07 0x00 | grep -q 'wrval' && \
   \"$E\" upcl decode \"$M\" 0x1e 0x07 0x00 | grep -q 'ea=r0' && \
   \"$E\" upcl decode \"$M\" 0xae 0x8f 0x08 | grep -q 'movsud' && \
   \"$E\" upcl decode \"$M\" 0xae 0x8f 0x08 | grep -q 'src=r1' && \
   \"$E\" upcl decode \"$M\" 0xae 0x8f 0x08 | grep -q 'dst=r2' && \
   \"$E\" upcl decode \"$M\" 0xae 0x1f 0x00 | grep -q 'movusd' && \
   \"$E\" upcl decode \"$M\" 0x1e 0x27 0x01 | grep -q 'cinv' && \
   \"$E\" upcl decode \"$M\" 0x1e 0x27 0x01 | grep -q 'opts=0x2' && \
   printf '\\x1e\\x27\\x01' > '${CMAKE_BINARY_DIR}/cinv.bin' && \
   \"$E\" disasm '${CMAKE_BINARY_DIR}/cinv.bin' --count 1 --arch \"upcl:\$M@ns32532\" | grep -q 'cinv' && \
   ! \"$E\" disasm '${CMAKE_BINARY_DIR}/cinv.bin' --count 1 --arch \"upcl:\$M@ns32032\" | grep -q 'cinv'")
set_tests_properties(upcl.ns32k-mmu PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|error|not valid")

# NS32000 PER-VARIANT feature selection (Task 14). The four cpu models each select a feature set;
# this test pins the FEATURE-GATING behaviour through the `@cpu` selector on the `--arch upcl:`
# path (the `disasm`/`run`/`decode` cpu suffix). CINV lives in `feature cache`, selected only by
# ns32532/ns32732; the FPU lives in `feature fpu`, selected by all four. Bytes are real LE memory
# order (`endian little word`): cinv word 0x01271e -> 0x1e 0x27 0x01; addf word 0x0001be -> 0xbe 0x01 0x00.
#   - CINV must DECODE under ns32532 + ns32732 (cache present) and FALL THROUGH (db 0x1e, no `cinv`)
#     under ns32032 + ns32332 (no cache) -- proving per-variant feature gating in BOTH directions.
#   - ADDF (fpu) must decode under ALL FOUR models -- proving fpu is present in every variant.
add_test(NAME upcl.ns32k-variants COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/ns32k.upcl'; E='$<TARGET_FILE:lcx>'; \
   B='${CMAKE_BINARY_DIR}'; \
   printf '\\x1e\\x27\\x01' > \"$B/ns32k-cinv.bin\" && \
   printf '\\xbe\\x01\\x00' > \"$B/ns32k-addf.bin\" && \
   \"$E\" disasm \"$B/ns32k-cinv.bin\" --count 1 --arch \"upcl:\$M@ns32532\" | grep -q 'cinv' && \
   \"$E\" disasm \"$B/ns32k-cinv.bin\" --count 1 --arch \"upcl:\$M@ns32732\" | grep -q 'cinv' && \
   ! \"$E\" disasm \"$B/ns32k-cinv.bin\" --count 1 --arch \"upcl:\$M@ns32032\" | grep -q 'cinv' && \
   ! \"$E\" disasm \"$B/ns32k-cinv.bin\" --count 1 --arch \"upcl:\$M@ns32332\" | grep -q 'cinv' && \
   \"$E\" disasm \"$B/ns32k-addf.bin\" --count 1 --arch \"upcl:\$M@ns32032\" | grep -q 'addf' && \
   \"$E\" disasm \"$B/ns32k-addf.bin\" --count 1 --arch \"upcl:\$M@ns32332\" | grep -q 'addf' && \
   \"$E\" disasm \"$B/ns32k-addf.bin\" --count 1 --arch \"upcl:\$M@ns32532\" | grep -q 'addf' && \
   \"$E\" disasm \"$B/ns32k-addf.bin\" --count 1 --arch \"upcl:\$M@ns32732\" | grep -q 'addf'")
set_tests_properties(upcl.ns32k-variants PROPERTIES FAIL_REGULAR_EXPRESSION "error|not valid")

# NS32000 EXECUTION (run-equivalent, Task 14). Drives a REAL little-endian-ordered ns32k program
# through the JIT and observes the final register state -- the maximal execution check available
# without a syscall personality (NS32000 has none yet; a stdout `lcx run` test is deferred to the
# pc532 libnix personality follow-on). The program proves MOVQD + ADDD + a forward Bcond all EXECUTE
# (not merely decode). All bytes are real LE memory order (`endian little word`):
#   0x00 movqd $5,r0   DF 02   r0 = 5
#   0x02 movqd $2,r1   5F 09   r1 = 2
#   0x04 addd  r0,r1   43 00   r1 = 7         (register-register ADD executes)
#   0x06 cmpqd $7,r1   9F 0B   Z = 1          (quick is SIGNED 4-bit; +7 is in range; r1==7 -> Z)
#   0x08 beq   +2      0A 02   TAKEN          (target = successor(0x0a) + 2 = 0x0c; Bcond evaluates Z)
#   0x0a movqd $0,r1   5F 08   SKIPPED        (would zero r1; the taken branch jumps over it)
#   0x0c bpt           F2      trap -> stop   (no syscall handler: the run loop halts here)
# Expected end state: r0 = 5, r1 = 7 (the beq was taken, so r1 keeps the ADDD result, not 0).
# NOTE: the NS32000 Bcond target is modelled SUCCESSOR-relative (`@lea` over `%M[pc + disp]` bakes
# the decode-time NextPc); real hardware is instruction-START-relative. Forward conditional control
# flow executes correctly; a BACKWARD loop edge is not yet re-entered by the JIT region CFG (a JIT
# plumbing gap, orthogonal to the frontend) -- both deferrals are documented for the pc532 follow-on.
add_test(NAME upcl.ns32k-run COMMAND sh -c
  "M='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/ns32k.upcl'; E='$<TARGET_FILE:lcx>'; \
   B='${CMAKE_BINARY_DIR}'; \
   printf '\\xdf\\x02\\x5f\\x09\\x43\\x00\\x9f\\x0b\\x0a\\x02\\x5f\\x08\\xf2' > \"$B/ns32k-run.bin\" && \
   \"$E\" run \"$B/ns32k-run.bin\" --arch upcl:\"$M\" > \"$B/ns32k-run.out\" && \
   grep -q 'r0 =00000005' \"$B/ns32k-run.out\" && \
   grep -q 'r1 =00000007' \"$B/ns32k-run.out\"")

# PDP-11 TRANSLATION: drive the UPCL description through the JIT against real programs. A loop summing
# 1..5 with SOB (a backward branch + per-iteration register decrement) must leave R0 = 15; an EIS MUL
# of 7*6 must place the low word 42 in R1 and store it to memory; an FP11 round trip (int->float, ADDF,
# float->int) of 2 + 3 must leave R2 = 5. Proves base, EIS and FP11 all execute, not just decode.
add_test(NAME upcl.pdp11-run COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp11.upcl'; E='$<TARGET_FILE:lcx>'; \
   B='${CMAKE_BINARY_DIR}'; \
   printf '\\100\\140\\102\\176\\000\\000' > \"$B/pdp11-loop.bin\" && \
   \"$E\" run \"$B/pdp11-loop.bin\" --arch upcl:\"$P\" --reg 0=0 --reg 1=5 > \"$B/pdp11-loop.out\" && \
   grep -q 'r0 =000f' \"$B/pdp11-loop.out\" && \
   printf '\\003\\160\\112\\020\\000\\000' > \"$B/pdp11-mul.bin\" && \
   \"$E\" run \"$B/pdp11-mul.bin\" --arch upcl:\"$P\" --reg 0=7 --reg 3=6 --reg 2=0x40 --dump 0x40 > \"$B/pdp11-mul.out\" && \
   grep -q 'r1 =002a' \"$B/pdp11-mul.out\" && \
   grep -q '.0x40. = 0x002a' \"$B/pdp11-mul.out\" && \
   printf '\\000\\376\\101\\376\\001\\364\\002\\373\\000\\000' > \"$B/pdp11-fp.bin\" && \
   \"$E\" run \"$B/pdp11-fp.bin\" --arch upcl:\"$P\" --reg 0=2 --reg 1=3 > \"$B/pdp11-fp.out\" && \
   grep -q 'r2 =0005' \"$B/pdp11-fp.out\" && \
   printf '\\114\\011\\000\\000\\200\\012\\205\\000' > \"$B/pdp11-jsr.bin\" && \
   \"$E\" run \"$B/pdp11-jsr.bin\" --arch upcl:\"$P\" --reg 0=10 --reg 4=4 --reg 6=0x100 > \"$B/pdp11-jsr.out\" && \
   grep -q 'r0 =000b' \"$B/pdp11-jsr.out\" && \
   grep -q 'sp =0100' \"$B/pdp11-jsr.out\"")

# The generalized 16-bit a.out loader: a PDP-11 OMAGIC (0407) exec -- 16-byte little-endian header
# (magic, text=6, data, bss, syms, entry, unused, flag) followed by 6 bytes of text (ADD R1,R0;
# MOV R0,R2; HALT) -- must load (header stripped, text at 0) and run to R0 = 8 from R0=5,R1=3.
add_test(NAME upcl.pdp11-aout COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp11.upcl'; E='$<TARGET_FILE:lcx>'; B='${CMAKE_BINARY_DIR}'; \
   printf '\\007\\001\\006\\000\\000\\000\\000\\000\\000\\000\\000\\000\\000\\000\\000\\000\\100\\140\\002\\020\\000\\000' > \"$B/pdp11-aout.bin\" && \
   \"$E\" run \"$B/pdp11-aout.bin\" --arch upcl:\"$P\" --load aout --reg 0=5 --reg 1=3 > \"$B/pdp11-aout.out\" && \
   grep -q 'loaded pdp11 a.out (OMAGIC)' \"$B/pdp11-aout.out\" && \
   grep -q 'r0 =0008' \"$B/pdp11-aout.out\" && \
   grep -q 'r2 =0008' \"$B/pdp11-aout.out\"")

# PDP-11 UNIX personality through libnix: a 0407 a.out whose text issues write(1,"hi\\n",3) via an
# inline-argument `sys 4` (TRAP) then exit via `sys 1`, run under --abi unixv7. The write must reach
# the host through libnix's nix_write (printing 'hi') and return 3 in r0; exit then stops the guest.
add_test(NAME upcl.pdp11-unix COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp11.upcl'; E='$<TARGET_FILE:lcx>'; B='${CMAKE_BINARY_DIR}'; \
   printf '\\007\\001\\017\\000\\000\\000\\000\\000\\000\\000\\000\\000\\000\\000\\000\\000\\004\\211\\001\\000\\014\\000\\003\\000\\001\\211\\000\\000\\150\\151\\012' > \"$B/pdp11-unix.bin\" && \
   \"$E\" run \"$B/pdp11-unix.bin\" --arch upcl:\"$P\" --load aout --abi unixv7 > \"$B/pdp11-unix.out\" 2>&1 && \
   grep -q '^hi' \"$B/pdp11-unix.out\" && \
   grep -q 'r0 =0003' \"$B/pdp11-unix.out\"")

# PDP-10 .SAV binary loader + run-to-HALT (the word-addressed execution path). The checked-in
# fixture pdp10-add.sav is a classic DEC SAV image: one load block (IOWD -3,,0o137) of three
# 36-bit words placed at origin 0o140 -- MOVEI AC1,5 ; ADDI AC1,3 ; JRST 4,(HALT) -- followed by
# the start word JRST,,0o140. The loader (auto-detected from the .sav extension on a word-
# addressed arch) lays each block's words into the 36-bit word memory at their octal addresses,
# sets the PC from the start word, and the interpreter runs to the HALT and stops cleanly. This
# proves the loader's job -- parse the IOWD blocks, place the words at the right origin (memory
# word 0o140 = byte 0x300 ends up holding the MOVEI word, low bits 0x0005), set the entry, and
# run to completion (exit 0). The full AC-state assertions are Task 18.
add_test(NAME upcl.pdp10-load COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl'; E='$<TARGET_FILE:lcx>'; B='${CMAKE_BINARY_DIR}'; \
   \"$E\" run '${CMAKE_CURRENT_SOURCE_DIR}/test/pdp10-add.sav' --arch upcl:\"$P\"@kl10 --dump 96 > \"$B/pdp10-load.out\" 2>&1 && \
   grep -q 'loaded pdp10 .SAV: 99 word(s), entry=0140' \"$B/pdp10-load.out\" && \
   grep -q '.0o140. = 0x408800005' \"$B/pdp10-load.out\" && \
   grep -q 'pc =' \"$B/pdp10-load.out\"")
set_tests_properties(upcl.pdp10-load PROPERTIES FAIL_REGULAR_EXPRESSION "cannot|error|could not|unexpected word|no JRST")

# PDP-10 EXECUTION VERIFICATION (Task 18): a real PDP-10 program computed correctly through the
# interpreter, with its final AC/memory state asserted -- the PDP-10 analog of the ns32k r0/r1 run
# test. The checked-in fixture pdp10-compute.sav is a classic DEC SAV image (one IOWD block of fifteen
# 36-bit words at origin 0o140, then the start word JRST,,0o140) hand-assembled per the engine field
# layout (op:9 ac:4 i:1 x:4 y:18):
#   0140 MOVEI AC1,5      ; AC1 = 5            (immediate -> AC)
#   0141 ADDI  AC1,3      ; AC1 = 8            (immediate add -> AC)
#   0142 MOVEI AC2,4      ; AC2 = 4            (loop counter)
#   0143 ADDI  AC1,10     ; AC1 += 8 (octal)   (loop body)
#   0144 SOJG  AC2,0143   ; AC2--; if >0 jump back to 0143  (count-down control-flow loop)
#   0145 MOVEM AC1,0200   ; M[0200] = AC1                   (store through REAL memory, word 128)
#   0146 MOVE  AC3,0200   ; AC3 = M[0200]                   (load back from REAL memory)
#   0147 HRRZI AC4,0123   ; AC4 = 0123 (right-half immediate, left zeroed)
#   0150 HRLZI AC7,020    ; AC7 = 020,,0   (left half sets the I bit, bit 22)
#   0151 HRRI  AC7,0202   ; AC7 = 020,,0202 = an indirect word pointing at 0o202
#   0152 MOVEM AC7,0201   ; M[0201] = the indirect word
#   0153 MOVEI AC6,0525   ; AC6 = 0525
#   0154 MOVEM AC6,0202   ; M[0202] = 0525
#   0155 MOVE  AC5,@0201  ; AC5 = M[@0201]: follow the chain M[0201]->0202, M[0202]=0525 (INDIRECT EA)
#   0156 JRST 4,          ; HALT
# The loop runs four times, so AC1 = 8 + 4*8 = 40 = 0x28; AC2 counts down to 0; the MOVEM/MOVE pair
# round-trips 40 through word 0o200 (byte 0x400, NOT an AC-aliased low word) into AC3; HRRZI puts
# 0o123 = 0x53 in AC4; the indirect MOVE walks a one-deep indirect chain (the @ea while-loop runs an
# iteration) so AC5 = 0o525 = 0x155 (AC6/AC7 hold the staged value/pointer). The dump prints registers
# in hex. This exercises arithmetic-to-AC, a real backward-branch loop, AC<->low-memory aliasing
# (AC1..AC7 alias words 1..7), a non-aliased memory round-trip, AND a multi-iteration indirect
# effective-address chain -- all run to the HALT. The root-cause fix that made this compute correctly:
# loop-carried body locals (the @ea macro's e/ci/cx/w/wy) are promoted to synthesised scratch
# registers (Semantics.cpp PromoteLoopLocals), so the @ea while-loop carries its locals across the
# head/body/end blocks -- both the zero-iteration case (which previously read an undefined loop-body
# temp and yielded AC1=0 instead of 8) and the >=1-iteration indirect chain now compute correctly.
add_test(NAME upcl.pdp10-run COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp10.upcl'; E='$<TARGET_FILE:lcx>'; B='${CMAKE_BINARY_DIR}'; \
   \"$E\" run '${CMAKE_CURRENT_SOURCE_DIR}/test/pdp10-compute.sav' --arch upcl:\"$P\"@kl10 --dump 128 > \"$B/pdp10-run.out\" 2>&1 && \
   grep -q 'loaded pdp10 .SAV' \"$B/pdp10-run.out\" && \
   grep -q 'ac1=0000000028' \"$B/pdp10-run.out\" && \
   grep -q 'ac2=0000000000' \"$B/pdp10-run.out\" && \
   grep -q 'ac3=0000000028' \"$B/pdp10-run.out\" && \
   grep -q 'ac4=0000000053' \"$B/pdp10-run.out\" && \
   grep -q 'ac5=0000000155' \"$B/pdp10-run.out\" && \
   grep -q '.0o200. = 0x000000028' \"$B/pdp10-run.out\"")
set_tests_properties(upcl.pdp10-run PROPERTIES FAIL_REGULAR_EXPRESSION "cannot|error|could not|unexpected word|no JRST")

# Semantics translation: drive an emitter from an instruction body. `add` must derive the
# full arithmetic flag set (carry as an unsigned-compare, overflow, aux); `inc` must set Z
# but NOT carry (the `!C` exclusion -- the defining inc/add difference); `cbw` must SIGN-
# extend al into ax. Proves the translator emits hardware-correct x86 semantics.
set(_def8086 "${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/8086.upcl")
add_test(NAME upcl.emit COMMAND sh -c
  "set -e; E='$<TARGET_FILE:lcx>'; D='${_def8086}'; \
   \"$E\" upcl emit \"$D\" add > '${CMAKE_BINARY_DIR}/emit.add' && \
   grep -q 'add v1, v0'   '${CMAKE_BINARY_DIR}/emit.add' && \
   grep -q 'zext.i17'     '${CMAKE_BINARY_DIR}/emit.add' && \
   grep -q 'const.i17 0x10' '${CMAKE_BINARY_DIR}/emit.add' && \
   grep -q 'setflag C'    '${CMAKE_BINARY_DIR}/emit.add' && \
   grep -q 'setflag O'    '${CMAKE_BINARY_DIR}/emit.add' && \
   grep -q 'setflag A'    '${CMAKE_BINARY_DIR}/emit.add' && \
   \"$E\" upcl emit \"$D\" inc > '${CMAKE_BINARY_DIR}/emit.inc' && \
   grep -q 'setflag Z'    '${CMAKE_BINARY_DIR}/emit.inc' && \
   ! grep -q 'setflag C'  '${CMAKE_BINARY_DIR}/emit.inc' && \
   \"$E\" upcl emit \"$D\" cbw | grep -q 'sext.i16' && \
   \"$E\" upcl emit \"$D\" aaa > '${CMAKE_BINARY_DIR}/emit.aaa' && \
   grep -q 'condbr'  '${CMAKE_BINARY_DIR}/emit.aaa' && \
   grep -q 'if.then' '${CMAKE_BINARY_DIR}/emit.aaa' && \
   grep -q 'if.else' '${CMAKE_BINARY_DIR}/emit.aaa' && \
   \"$E\" upcl emit \"$D\" movsb > '${CMAKE_BINARY_DIR}/emit.movsb' && \
   grep -q 'getflag D' '${CMAKE_BINARY_DIR}/emit.movsb' && \
   grep -q 'condbr'    '${CMAKE_BINARY_DIR}/emit.movsb'")

# Knowledge-library (ZOO/zstd solid) archive: pack several real source files, list them
# (asserting the solid container holds them), then extract one and diff it against the
# original -- a full create/ls/extract round-trip proving the format is lossless.
set(_klib "${CMAKE_BINARY_DIR}/test.klib")
add_test(NAME klib.roundtrip COMMAND sh -c
  "set -e; \
   '$<TARGET_FILE:lcx>' klib create '${_klib}' \
      '${CMAKE_CURRENT_SOURCE_DIR}/test/dos21.xml' \
      '${CMAKE_CURRENT_SOURCE_DIR}/test/v20.upcl' \
      '${CMAKE_CURRENT_SOURCE_DIR}/test/broken.upcl'; \
   '$<TARGET_FILE:lcx>' klib ls '${_klib}'; \
   '$<TARGET_FILE:lcx>' klib extract '${_klib}' v20.upcl -o '${CMAKE_BINARY_DIR}/v20.extracted'; \
   cmp '${CMAKE_CURRENT_SOURCE_DIR}/test/v20.upcl' '${CMAKE_BINARY_DIR}/v20.extracted'")
set_tests_properties(klib.roundtrip PROPERTIES PASS_REGULAR_EXPRESSION "solid zstd")
list(APPEND LIBCPU_CHECK_DEPS lcx)

# ZOO raw-member round-trip: the format extension that makes the archive a directly-
# mappable code cache. Builds a small harness against core/ZooArchive.cpp + vendored zstd
# and checks page-aligned zero-copy mapping, compressed-slot inflate, and solid coexistence.
add_executable(zooraw test/zooraw.cpp core/ZooArchive.cpp third_party/zstd/zstd.c)
target_include_directories(zooraw PRIVATE . third_party/zstd)
target_compile_definitions(zooraw PRIVATE LIBCPU_HAVE_ZSTD)
target_link_libraries(zooraw PRIVATE LibCPU)
add_test(NAME zoo.raw COMMAND zooraw "${CMAKE_BINARY_DIR}/test-raw.zoo")
set_tests_properties(zoo.raw PROPERTIES PASS_REGULAR_EXPRESSION "RESULT: PASS"
                                        FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
list(APPEND LIBCPU_CHECK_DEPS zooraw)

# Content-addressed code cache (the DBM-like store on the ZOO substrate): content-hash +
# host-fingerprint keying, profile persistence, multi-target sharing, and cross-run
# accretion. Builds against core/CodeCache.cpp + core/ZooArchive.cpp + vendored zstd.
add_executable(codecache test/codecache.cpp core/CodeCache.cpp core/ZooArchive.cpp third_party/zstd/zstd.c)
target_include_directories(codecache PRIVATE . third_party/zstd)
target_compile_definitions(codecache PRIVATE LIBCPU_HAVE_ZSTD)
target_link_libraries(codecache PRIVATE LibCPU)
add_test(NAME cache.codecache COMMAND codecache "${CMAKE_BINARY_DIR}/test.codecache")
set_tests_properties(cache.codecache PROPERTIES PASS_REGULAR_EXPRESSION "RESULT: PASS"
                                                FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
list(APPEND LIBCPU_CHECK_DEPS codecache)

# Host-target fingerprint (ICpuBackendTarget): a host-specific backend must report a non-zero
# fingerprint that differs between the native and baseline feature levels (the host half of the
# cache key). Checked for whichever optimizing/native backends were built.
foreach (_fpbe asmjit llvm-22)
  if (EXISTS "${CMAKE_BINARY_DIR}/${_fpbe}.backend")
    add_test(NAME cache.fingerprint-${_fpbe} COMMAND lcx cache fingerprint "${CMAKE_BINARY_DIR}/${_fpbe}.backend")
    set_tests_properties(cache.fingerprint-${_fpbe} PROPERTIES PASS_REGULAR_EXPRESSION "RESULT: PASS"
                                                               FAIL_REGULAR_EXPRESSION "FAIL:")
  endif ()
endforeach ()

# Shadow CFG: a MINIMAL straight-line backend drives a guest basic block's control flow via the
# shadow layer (no native branches, no machine capabilities of its own) -- the universal path
# that makes any backend machine-capable. Each representative backend across the language span
# (native JITs, C compilers, scripting, managed runtimes) must pass the same self-test.
foreach (_sb interp asmjit sljit lightning mir llvm-22 gccjit-15 cc qbe js jscore wasm
              bash cmake cmd lua perl python ruby tcl tclemb fish zsh ksh tcsh pwsh swift java dotnet jvm clr cpython)
  if (EXISTS "${CMAKE_BINARY_DIR}/${_sb}.backend")
    add_test(NAME shadow.${_sb} COMMAND lcx shadow --backend "${CMAKE_BINARY_DIR}/${_sb}.backend")
    set_tests_properties(shadow.${_sb} PROPERTIES PASS_REGULAR_EXPRESSION "RESULT: PASS"
                                                  FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
  endif ()
endforeach ()

# Shadow CFG drives the WHOLE machine: force the shadow path (LCX_SHADOW) on the interpreter and
# run the Olivetti M24 BIOS -- it must reach the POST diagnostics (Resident Diagnostics + DMA Timer
# Pass) entirely through GenerateShadowUnit + scratch-register dispatch + the interpreter REP
# fallback. Proves any non-CFG backend can drive the machine (the interp here stands in for the
# leaf backends, which use the identical path). Bounded budget = the early diagnostics (~25s); the
# full 640 KB RAM count is left out to keep the test quick.
if (EXISTS "${CMAKE_BINARY_DIR}/interp.backend"
    AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/roms/system-bios/olivetti-m24/BIOS121-combined.ROM")
  add_test(NAME machine.m24-shadow COMMAND sh -c
    "LCX_SHADOW=1 '$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' \
       --bundles '${CMAKE_BINARY_DIR}' --backend '${CMAKE_BINARY_DIR}/interp.backend' \
       --bios '${CMAKE_CURRENT_SOURCE_DIR}/roms/system-bios/olivetti-m24/BIOS121-combined.ROM' \
       --steps 1500000 > '${CMAKE_BINARY_DIR}/machine-m24-shadow.out' 2>&1 && \
     grep -q 'Resident Diagnostics' '${CMAKE_BINARY_DIR}/machine-m24-shadow.out' && \
     grep -qE 'DMA Timer +Pass' '${CMAKE_BINARY_DIR}/machine-m24-shadow.out'")
  set_tests_properties(machine.m24-shadow PROPERTIES TIMEOUT 180 FAIL_REGULAR_EXPRESSION "lcx machine:")
endif ()

# Symbol reader (knowledge-library v2 step b): derive a library's exported-symbol set
# from a real toolchain artifact. Two formats, two hermetic fixtures:
#  - TBD text stub: a checked-in tapi-tbd v4 file; assert a known export is read.
#  - Mach-O binary: our own interp.backend bundle; assert its module entry symbol
#    (_LibCPUModuleCreateBackend, the C export the loader dlsym's) is read.
add_test(NAME symbols.tbd COMMAND lcx klib symbols
  "${CMAKE_CURRENT_SOURCE_DIR}/test/libknow.tbd" --grep _know_ioctl)
set_tests_properties(symbols.tbd PROPERTIES PASS_REGULAR_EXPRESSION "_know_ioctl")
if(TARGET interp_backend)
  add_test(NAME symbols.macho COMMAND sh -c
    "'$<TARGET_FILE:lcx>' klib symbols '$<TARGET_FILE:interp_backend>' --grep LibCPUModuleCreateBackend")
  set_tests_properties(symbols.macho PROPERTIES PASS_REGULAR_EXPRESSION "_LibCPUModuleCreateBackend")
  # Mach-O install-name capture (LC_ID_DYLIB): a real dylib reports its install name.
  add_test(NAME symbols.installname COMMAND sh -c
    "'$<TARGET_FILE:lcx>' klib symbols '$<TARGET_FILE:structdemo>'")
  set_tests_properties(symbols.installname PROPERTIES PASS_REGULAR_EXPRESSION "install-name:")
endif()

# Symbol extraction from crafted minimal fixtures (one exported symbol each), verifying
# the a.out, ELF (section-header symtab), and PE/COFF (COFF symbol table) extractors.
add_test(NAME symbols.aout COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.aout")
set_tests_properties(symbols.aout PROPERTIES PASS_REGULAR_EXPRESSION "_aout_sym")
add_test(NAME symbols.elf COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.elf")
set_tests_properties(symbols.elf PROPERTIES PASS_REGULAR_EXPRESSION "_elf_sym")
add_test(NAME symbols.pe COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.pe")
set_tests_properties(symbols.pe PROPERTIES PASS_REGULAR_EXPRESSION "_pe_sym")
# Stripped image (no COFF symbol table): exports come from the PE export directory, with
# RVA->file-offset translation through the section table.
add_test(NAME symbols.pedll COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.dll")
set_tests_properties(symbols.pedll PROPERTIES PASS_REGULAR_EXPRESSION "_dll_export")
# OMF PUBDEF records and the NE resident-name table (16-bit Windows/OS2 exports).
add_test(NAME symbols.omf COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.omf")
set_tests_properties(symbols.omf PROPERTIES PASS_REGULAR_EXPRESSION "_omf_pub")
add_test(NAME symbols.ne COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.ne")
set_tests_properties(symbols.ne PROPERTIES PASS_REGULAR_EXPRESSION "_ne_export")
# Bare Windows COFF object and Microsoft /bigobj COFF (20-byte symbol records).
add_test(NAME symbols.wincoff COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.wincoff")
set_tests_properties(symbols.wincoff PROPERTIES PASS_REGULAR_EXPRESSION "_wincoff")
add_test(NAME symbols.bigobj COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.bigobj")
set_tests_properties(symbols.bigobj PROPERTIES PASS_REGULAR_EXPRESSION "_bigobj")
# XCOFF (big-endian COFF symtab), AmigaOS hunk (HUNK_EXT defs), and PEF (loader exports).
add_test(NAME symbols.xcoff COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.xcoff")
set_tests_properties(symbols.xcoff PROPERTIES PASS_REGULAR_EXPRESSION "_xcoff_s")
add_test(NAME symbols.hunk COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.hunk")
set_tests_properties(symbols.hunk PROPERTIES PASS_REGULAR_EXPRESSION "_hunk_def")
add_test(NAME symbols.pef COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.pef")
set_tests_properties(symbols.pef PROPERTIES PASS_REGULAR_EXPRESSION "_pef_sym")
# 32/64-bit variants and more legacy formats: Mach-O 32-bit, XCOFF64, LE, LX, NLM, ECOFF.
add_test(NAME symbols.macho32 COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.macho32")
set_tests_properties(symbols.macho32 PROPERTIES PASS_REGULAR_EXPRESSION "_macho32")
add_test(NAME symbols.xcoff64 COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.xcoff64")
set_tests_properties(symbols.xcoff64 PROPERTIES PASS_REGULAR_EXPRESSION "_xcoff64")
add_test(NAME symbols.le COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.le")
set_tests_properties(symbols.le PROPERTIES PASS_REGULAR_EXPRESSION "_le_export")
add_test(NAME symbols.lx COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.lx")
set_tests_properties(symbols.lx PROPERTIES PASS_REGULAR_EXPRESSION "_lx_export")
add_test(NAME symbols.nlm COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.nlm")
set_tests_properties(symbols.nlm PROPERTIES PASS_REGULAR_EXPRESSION "_nlm_pub")
# Plan 9 a.out (global text symbol, source-path entry skipped) and PDP-10 .SAV (RADIX-50
# symbol reached via the JOBSYM pointer at 0o116).
add_test(NAME symbols.plan9 COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.plan9")
set_tests_properties(symbols.plan9 PROPERTIES PASS_REGULAR_EXPRESSION "_plan9")
add_test(NAME symbols.sav COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.sav")
set_tests_properties(symbols.sav PROPERTIES PASS_REGULAR_EXPRESSION "PDP10S")
# HP-UX SOM (universal symbols) and MINIX a.out (external nlist).
add_test(NAME symbols.som COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.som")
set_tests_properties(symbols.som PROPERTIES PASS_REGULAR_EXPRESSION "_som_sym")
add_test(NAME symbols.minix COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.minix")
set_tests_properties(symbols.minix PROPERTIES PASS_REGULAR_EXPRESSION "_minix")
# GEMDOS/DRI (Atari ST/TT/Falcon, CP/M-68K) and NASM RDOFF2.
add_test(NAME symbols.gemdos COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.gemdos")
set_tests_properties(symbols.gemdos PROPERTIES PASS_REGULAR_EXPRESSION "_gemdos")
add_test(NAME symbols.rdoff COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.rdoff")
set_tests_properties(symbols.rdoff PROPERTIES PASS_REGULAR_EXPRESSION "_rdoff")
# IBM OS/360 object deck (EBCDIC ESD) and OpenVMS image GST (EIHD->EIHS->EGSD/EGST).
add_test(NAME symbols.os360 COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.os360")
set_tests_properties(symbols.os360 PROPERTIES PASS_REGULAR_EXPRESSION "OS360SYM")
add_test(NAME symbols.vms COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.vms")
set_tests_properties(symbols.vms PROPERTIES PASS_REGULAR_EXPRESSION "_VMS_SYM")
# Intel i960 b.out (a.out variant).
add_test(NAME symbols.bout COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.bout")
set_tests_properties(symbols.bout PROPERTIES PASS_REGULAR_EXPRESSION "_bout_sym")
# OSF/ROSE (OSF/1 "Mach-O", magic 0xbeefface): LDC_SYMBOLS/LDC_STRINGS, SI_EXPORT_F.
add_test(NAME symbols.rose COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.rose")
set_tests_properties(symbols.rose PROPERTIES PASS_REGULAR_EXPRESSION "_rose_sym")
# XENIX x.out (XR_SAOUT nlist form).
add_test(NAME symbols.xout COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.xout")
set_tests_properties(symbols.xout PROPERTIES PASS_REGULAR_EXPRESSION "_xenix")
# CP/M-8000 (Z8000) command file: type-3 (global) symbols, table is the last section.
add_test(NAME symbols.cpmz8k COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.cpmz8k")
set_tests_properties(symbols.cpmz8k PROPERTIES PASS_REGULAR_EXPRESSION "_z8ksym")
# CP/M-VAX: little-endian 0x601A header (byte-distinct from big-endian GEMDOS); LE symbol table.
add_test(NAME symbols.cpmvax COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.cpmvax")
set_tests_properties(symbols.cpmvax PROPERTIES PASS_REGULAR_EXPRESSION "_vaxsym")
# Unix ar static library: member objects are harvested by recursing through the registry.
add_test(NAME symbols.ar COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.a")
set_tests_properties(symbols.ar PROPERTIES PASS_REGULAR_EXPRESSION "_arsym")
# ELF common symbol (SHN_COMMON): a tentative definition must be reported as exported.
add_test(NAME symbols.elfcommon COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.elfcommon")
set_tests_properties(symbols.elfcommon PROPERTIES PASS_REGULAR_EXPRESSION "_elfcommon")
# OMF library (.lib): PUBDEF symbols are harvested across member modules.
add_test(NAME symbols.omflib COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.omflib")
set_tests_properties(symbols.omflib PROPERTIES PASS_REGULAR_EXPRESSION "_omflib")
# CP/M LBR archive: member filenames are listed from the directory.
add_test(NAME symbols.lbr COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.lbr")
set_tests_properties(symbols.lbr PROPERTIES PASS_REGULAR_EXPRESSION "HELLO.COM")
# AmigaOS link library (HUNK_UNIT modules): EXT_DEF symbols from every unit.
add_test(NAME symbols.amigalib COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.amigalib")
set_tests_properties(symbols.amigalib PROPERTIES PASS_REGULAR_EXPRESSION "_amigaB")
# Metrowerks CodeWarrior MWOB: library -> object -> Global* hunk names via the name table.
add_test(NAME symbols.mwob COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.mwob")
set_tests_properties(symbols.mwob PROPERTIES
    PASS_REGULAR_EXPRESSION "_gentry"
    FAIL_REGULAR_EXPRESSION "_local|_import")
# MPW object/library: extern module + entry-point names via the dictionary; locals excluded.
add_test(NAME symbols.mpwobj COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.mpwobj")
set_tests_properties(symbols.mpwobj PROPERTIES
    PASS_REGULAR_EXPRESSION "_mpwhelper"
    FAIL_REGULAR_EXPRESSION "_local0")
# X68000 .X (Human68k): defined symbols from the symbol table; external references excluded.
add_test(NAME symbols.xfile COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.xfile")
set_tests_properties(symbols.xfile PROPERTIES
    PASS_REGULAR_EXPRESSION "_xmain"
    FAIL_REGULAR_EXPRESSION "_ximport")
# ECOFF (32-bit MIPS): defined external symbols via the symbolic header; undefined excluded.
add_test(NAME symbols.ecoff COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.ecoff")
set_tests_properties(symbols.ecoff PROPERTIES
    PASS_REGULAR_EXPRESSION "_ecoffdef"
    FAIL_REGULAR_EXPRESSION "_ecoffundef")
# GOFF (IBM z/OS): SD/LD names from ESD records (EBCDIC, continuation-spanning); ER excluded.
add_test(NAME symbols.goff COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.goff")
set_tests_properties(symbols.goff PROPERTIES
    PASS_REGULAR_EXPRESSION "GOFFLONGNAME12345"
    FAIL_REGULAR_EXPRESSION "GOFFER")
# IEEE-695 (MUFOM): NI (defined) names from the external part; NX references excluded.
add_test(NAME symbols.ieee COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.ieee")
set_tests_properties(symbols.ieee PROPERTIES
    PASS_REGULAR_EXPRESSION "_ieeedef"
    FAIL_REGULAR_EXPRESSION "_ieeeundef")
# ARM AOF: global-definition symbols from OBJ_SYMT; local defs and references excluded.
add_test(NAME symbols.aof COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.aof")
set_tests_properties(symbols.aof PROPERTIES
    PASS_REGULAR_EXPRESSION "_aofglobal"
    FAIL_REGULAR_EXPRESSION "_aoflocal|_aofref")
# Acorn Library Format: LIB_DATA members recursed back through the AOF reader.
add_test(NAME symbols.alf COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.alf")
set_tests_properties(symbols.alf PROPERTIES PASS_REGULAR_EXPRESSION "_alfmemberB")
# WebAssembly: export-section names; imports (section 2) are excluded.
add_test(NAME symbols.wasm COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.wasm")
set_tests_properties(symbols.wasm PROPERTIES
    PASS_REGULAR_EXPRESSION "mem"
    FAIL_REGULAR_EXPRESSION "_imported")
# Dalvik .dex: defined classes' methods (ClassDescriptor->method) via the class_data tables.
add_test(NAME symbols.dex COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.dex")
set_tests_properties(symbols.dex PROPERTIES PASS_REGULAR_EXPRESSION "LMyClass;->myMethod")
# Java class: declared methods/fields via the constant pool (real javac output).
add_test(NAME symbols.class COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.class")
set_tests_properties(symbols.class PROPERTIES PASS_REGULAR_EXPRESSION "Foo.add")
# LLVM bitcode: module function/global names via the bitstream + STRTAB (real clang output).
add_test(NAME symbols.bc COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.bc")
set_tests_properties(symbols.bc PROPERTIES PASS_REGULAR_EXPRESSION "add")
# C++ (Itanium) demangling: nested name + member const, and a non-mangled name passes through.
add_test(NAME symbols.demangle COMMAND lcx klib demangle _ZNK2ns6Widget4areaEii _Z6freefnim plain_c)
set_tests_properties(symbols.demangle PROPERTIES PASS_REGULAR_EXPRESSION "ns::Widget::area.int, int. const")
# DRI CMD command file (CP/M-86 / FlexOS 186/286): no inline symbols -> detection only.
add_test(NAME symbols.cmd286 COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.cmd286")
set_tests_properties(symbols.cmd286 PROPERTIES PASS_REGULAR_EXPRESSION "dri-cmd")
# GEOS geode (PC/GEOS): detected by signature; geode name recovered as the install name.
add_test(NAME symbols.geo COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.geo")
set_tests_properties(symbols.geo PROPERTIES PASS_REGULAR_EXPRESSION "GeoCalc")
# Commodore 64 GEOS file: detection only.
add_test(NAME symbols.geosc64 COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.geosc64")
set_tests_properties(symbols.geosc64 PROPERTIES PASS_REGULAR_EXPRESSION "geos-c64")
# Palm OS resource database / application (.prc): install name + full resource-type listing.
add_test(NAME symbols.prc COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.prc")
set_tests_properties(symbols.prc PROPERTIES PASS_REGULAR_EXPRESSION "tAIB #1000")
# Palm OS record database (.pdb): detected; database name -> install name.
add_test(NAME symbols.pdb COMMAND lcx klib symbols "${CMAKE_CURRENT_SOURCE_DIR}/test/sample.pdb")
set_tests_properties(symbols.pdb PROPERTIES PASS_REGULAR_EXPRESSION "MemoDB")

# Format detection sweep: craft each format's magic and assert the reader names it. Proves
# the detection registry across the object/executable format family (extraction varies).
add_test(NAME symbols.detect
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/test/detect_formats.sh" "$<TARGET_FILE:lcx>")
set_tests_properties(symbols.detect PROPERTIES FAIL_REGULAR_EXPRESSION "MISMATCH")

# Header parser (knowledge-library v2 step c): function signatures + struct layouts from a
# C header. Uses libclang when present (dlopen'd, not linked), else a built-in scanner --
# either way the self-contained fixture yields know_open() and struct know_stat.
add_test(NAME headers.parse COMMAND lcx klib headers "${CMAKE_CURRENT_SOURCE_DIR}/test/know.h")
set_tests_properties(headers.parse PROPERTIES PASS_REGULAR_EXPRESSION "know_open")
# C++ layouts: a class (Rect, via its 'Point origin' member) is captured like a struct, and a
# namespaced struct is reported with its qualified name (ui::Color). Needs libclang.
add_test(NAME headers.cpp COMMAND sh -c
  "'$<TARGET_FILE:lcx>' klib headers '${CMAKE_CURRENT_SOURCE_DIR}/test/cppshapes.hpp' | grep -q 'Point origin' && \
   '$<TARGET_FILE:lcx>' klib headers '${CMAKE_CURRENT_SOURCE_DIR}/test/cppshapes.hpp' | grep -q 'struct ui::Color'")
# The built-in fallback (libclang forced off) must still recover the same declarations,
# proving the no-hard-dependency promise holds.
add_test(NAME headers.fallback COMMAND sh -c
  "'$<TARGET_FILE:lcx>' klib headers '${CMAKE_CURRENT_SOURCE_DIR}/test/know.h' | grep -q 'know_open(' && \
   '$<TARGET_FILE:lcx>' klib headers '${CMAKE_CURRENT_SOURCE_DIR}/test/know.h' | grep -q '^built-in:'")
set_tests_properties(headers.fallback PROPERTIES ENVIRONMENT "LIBCPU_NO_CLANG=1")

# Derivation engine (knowledge-library v2 step d + e): join the header signatures with the
# library's exported symbols into a host-call catalog, save it as a ZOO/zstd archive, and
# reload it. know.h declares 7 functions; libknow.tbd exports 4 of them -- so the catalog
# must report exactly 4 exported, proving the join (and the archive round-trip) work.
add_test(NAME know.derive COMMAND sh -c
  "'$<TARGET_FILE:lcx>' know derive '${CMAKE_CURRENT_SOURCE_DIR}/test/know.h' \
       '${CMAKE_CURRENT_SOURCE_DIR}/test/libknow.tbd' -o '${CMAKE_BINARY_DIR}/know.klib' && \
   '$<TARGET_FILE:lcx>' know catalog '${CMAKE_BINARY_DIR}/know.klib'")
set_tests_properties(know.derive PROPERTIES PASS_REGULAR_EXPRESSION "4 exported")

# C++ catalog end-to-end: the header parser descends into namespace gfx and extern "C"
# (qualified name gfx::draw); the catalog demangles the stub's mangled exports to match all
# three prototypes and records each mangled symbol to bind to, surviving the archive round-trip.
add_test(NAME know.cppmatch COMMAND sh -c
  "'$<TARGET_FILE:lcx>' know derive '${CMAKE_CURRENT_SOURCE_DIR}/test/cppmatch.h' \
       '${CMAKE_CURRENT_SOURCE_DIR}/test/cppmatch.tbd' -o '${CMAKE_BINARY_DIR}/cppmatch.klib' && \
   '$<TARGET_FILE:lcx>' know catalog '${CMAKE_BINARY_DIR}/cppmatch.klib'")
set_tests_properties(know.cppmatch PROPERTIES PASS_REGULAR_EXPRESSION "gfx::draw  -> __ZN3gfx4drawEii")

# C++ member functions: the parser qualifies each method with its class, gives non-static
# methods an implicit "this" (const-qualified for const methods) and static methods none.
add_test(NAME headers.cppmethods COMMAND sh -c
  "'$<TARGET_FILE:lcx>' klib headers '${CMAKE_CURRENT_SOURCE_DIR}/test/cppmethods.hpp' | grep -q 'area(const ui::Widget' && \
   '$<TARGET_FILE:lcx>' klib headers '${CMAKE_CURRENT_SOURCE_DIR}/test/cppmethods.hpp' | grep -q 'create(void)'")

# C++ overload disambiguation end-to-end: value(int) and value(double) share a name but the
# catalog matches each prototype's full signature to its own mangled export -- they must bind
# to distinct symbols (...5valueEi vs ...5valueEd), surviving the archive round-trip.
add_test(NAME know.cppmethods COMMAND sh -c
  "'$<TARGET_FILE:lcx>' know derive '${CMAKE_CURRENT_SOURCE_DIR}/test/cppmethods.hpp' \
       '${CMAKE_CURRENT_SOURCE_DIR}/test/cppmethods.tbd' -o '${CMAKE_BINARY_DIR}/cppmethods.klib' && \
   '$<TARGET_FILE:lcx>' know catalog '${CMAKE_BINARY_DIR}/cppmethods.klib' | grep -q 'value  -> __ZN2ui6Widget5valueEi' && \
   '$<TARGET_FILE:lcx>' know catalog '${CMAKE_BINARY_DIR}/cppmethods.klib' | grep -q 'value  -> __ZN2ui6Widget5valueEd'")

# 8-bit immediate moves (B0..B7, MOV reg8,imm8) on the V20 frontend: a fixture of four
# such moves (incl. the high-byte forms ah/bh) must disassemble correctly.
add_test(NAME disasm.mov8 COMMAND lcx disasm "${CMAKE_CURRENT_SOURCE_DIR}/test/mov8.v20.bin" --arch v20 --count 4)
set_tests_properties(disasm.mov8 PROPERTIES PASS_REGULAR_EXPRESSION "mov ah,0x80")

# Intel 8080 frontend, executed through the JIT: MVI A,5 ; MVI B,3 ; ADD B ; HLT -> A = 8.
if(TARGET interp_backend)
  add_test(NAME run.8080 COMMAND sh -c
    "printf '\\x3e\\x05\\x06\\x03\\x80\\x76' > '${CMAKE_BINARY_DIR}/add8080.bin' && \
     '$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/add8080.bin' --arch 8080 | grep -q 'A  =08'")

  # 8086 ALU group + grp1 immediates: MOV AX,0x0F ; AND AX,6 (83/4) ; OR AX,0x30 (83/1) -> 0x36.
  add_test(NAME run.alu8086 COMMAND sh -c
    "printf '\\xb8\\x0f\\x00\\x83\\xe0\\x06\\x83\\xc8\\x30\\xf4' > '${CMAKE_BINARY_DIR}/alu8086.bin' && \
     '$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/alu8086.bin' --arch v20 | grep -q 'AX =0036'")

  # NEC V20/V30-specific bit instruction SET1 r/m,CL: MOV AX,0 ; MOV CX,4 ; SET1 AX,CL -> bit 4.
  add_test(NAME run.nec.set1 COMMAND sh -c
    "printf '\\xb8\\x00\\x00\\xb9\\x04\\x00\\x0f\\x14\\xc0\\xf4' > '${CMAKE_BINARY_DIR}/set1.bin' && \
     '$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/set1.bin' --arch v20 | grep -q 'AX =0010'")

  # 8-bit operand forms: MOV AL,5 ; MOV BL,3 ; ADD AL,BL -> AL = 8 (the byte r/m machinery).
  add_test(NAME run.byte8086 COMMAND sh -c
    "printf '\\xb0\\x05\\xb3\\x03\\x00\\xd8\\xf4' > '${CMAKE_BINARY_DIR}/byte8086.bin' && \
     '$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/byte8086.bin' --arch v20 | grep -q 'AX =0008'")

  # grp2 shifts: MOV AX,3 ; SHL AX,1 ; SHL AX,1 ; SHR AX,1 -> 6.
  add_test(NAME run.shift8086 COMMAND sh -c
    "printf '\\xb8\\x03\\x00\\xd1\\xe0\\xd1\\xe0\\xd1\\xe8\\xf4' > '${CMAKE_BINARY_DIR}/shift8086.bin' && \
     '$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/shift8086.bin' --arch v20 | grep -q 'AX =0006'")

  # grp3 division: MOV AX,100 ; MOV BL,7 ; DIV BL -> AL=14 (quotient), AH=2 (remainder) = 0x020E.
  add_test(NAME run.grp3 COMMAND sh -c
    "printf '\\xb8\\x64\\x00\\xb3\\x07\\xf6\\xf3\\xf4' > '${CMAKE_BINARY_DIR}/grp3.bin' && \
     '$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/grp3.bin' --arch v20 | grep -q 'AX =020e'")

  # String ops + Direction flag: MOV ES,0 ; MOV DI,0x200 ; MOV AX,0x1234 ; CLD ; STOSW ; STOSW
  # advances DI forward to 0x204 (with STD it would go backward).
  add_test(NAME run.string COMMAND sh -c
    "printf '\\xb8\\x00\\x00\\x8e\\xc0\\xbf\\x00\\x02\\xb8\\x34\\x12\\xfc\\xab\\xab\\xf4' > '${CMAKE_BINARY_DIR}/string.bin' && \
     '$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/string.bin' --arch v20 | grep -q 'DI =0204'")

  # REP prefix as a real loop: MOV ES,0 ; MOV DI,0x200 ; MOV CX,4 ; CLD ; REP STOSW -> CX=0,
  # DI=0x208 (four words stored). Exercises a frontend-emitted block loop on the JIT.
  add_test(NAME run.rep COMMAND sh -c
    "printf '\\xb8\\x00\\x00\\x8e\\xc0\\xbf\\x00\\x02\\xb9\\x04\\x00\\xfc\\xf3\\xab\\xf4' > '${CMAKE_BINARY_DIR}/rep.bin' && \
     '$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/rep.bin' --arch v20 > '${CMAKE_BINARY_DIR}/rep.out' && \
     grep -q 'DI =0208' '${CMAKE_BINARY_DIR}/rep.out' && grep -q 'CX =0000' '${CMAKE_BINARY_DIR}/rep.out'")

  # Misc ops: MOV BX,0x10 ; LEA AX,[BX+4] -> AX = 0x14 ; XCHG AX,BX -> AX=0x10, BX=0x14.
  add_test(NAME run.misc COMMAND sh -c
    "printf '\\xbb\\x10\\x00\\x8d\\x47\\x04\\x93\\xf4' > '${CMAKE_BINARY_DIR}/misc.bin' && \
     '$<TARGET_FILE:lcx>' run '${CMAKE_BINARY_DIR}/misc.bin' --arch v20 > '${CMAKE_BINARY_DIR}/misc.out' && \
     grep -q 'AX =0010' '${CMAKE_BINARY_DIR}/misc.out' && grep -q 'BX =0014' '${CMAKE_BINARY_DIR}/misc.out'")
endif()

# Segment-override prefix + string ops disassemble.
add_test(NAME disasm.string COMMAND sh -c
  "printf '\\x26\\x8b\\x07\\xfc\\xab\\xa4\\xae' > '${CMAKE_BINARY_DIR}/str.bin' && \
   '$<TARGET_FILE:lcx>' disasm '${CMAKE_BINARY_DIR}/str.bin' --arch v20 --count 5 > '${CMAKE_BINARY_DIR}/str.out' && \
   grep -q 'mov ax,es:' '${CMAKE_BINARY_DIR}/str.out' && grep -q 'stosw' '${CMAKE_BINARY_DIR}/str.out' && \
   grep -q 'movsb' '${CMAKE_BINARY_DIR}/str.out'")

# grp3/grp5 disassemble (mul, neg, indirect call, inc r/m).
add_test(NAME disasm.grp COMMAND sh -c
  "printf '\\xf6\\xe3\\xf7\\xd8\\xff\\xd0\\xfe\\xc0' > '${CMAKE_BINARY_DIR}/grp.bin' && \
   '$<TARGET_FILE:lcx>' disasm '${CMAKE_BINARY_DIR}/grp.bin' --arch v20 --count 4 > '${CMAKE_BINARY_DIR}/grp.out' && \
   grep -q 'mul bl' '${CMAKE_BINARY_DIR}/grp.out' && grep -q 'neg ax' '${CMAKE_BINARY_DIR}/grp.out' && \
   grep -q 'call ax' '${CMAKE_BINARY_DIR}/grp.out'")

# The NEC V20/V30-specific instruction set disassembles (bit ops, BCD-string, nibble, bit-field).
add_test(NAME disasm.nec COMMAND sh -c
  "printf '\\x0f\\x14\\xc3\\x0f\\x28\\xc3\\x0f\\x20\\x0f\\x33\\xc1' > '${CMAKE_BINARY_DIR}/nec.bin' && \
   '$<TARGET_FILE:lcx>' disasm '${CMAKE_BINARY_DIR}/nec.bin' --arch v20 --count 4 > '${CMAKE_BINARY_DIR}/nec.out' && \
   grep -q 'set1' '${CMAKE_BINARY_DIR}/nec.out' && grep -q 'rol4' '${CMAKE_BINARY_DIR}/nec.out' && \
   grep -q 'add4s' '${CMAKE_BINARY_DIR}/nec.out' && grep -q 'ext' '${CMAKE_BINARY_DIR}/nec.out'")

# NEC V20 8080 emulation mode: BRKEM drops into 8080 code decoded by the SAME shared i8080 core,
# RETEM returns to native V20. One core, reused -- not a parallel implementation.
add_test(NAME disasm.brkem COMMAND sh -c
  "printf '\\x0f\\xff\\x00\\x3e\\x05\\x80\\xed\\xfd\\xf4' > '${CMAKE_BINARY_DIR}/brkem.bin' && \
   '$<TARGET_FILE:lcx>' disasm '${CMAKE_BINARY_DIR}/brkem.bin' --arch v20 --count 5 > '${CMAKE_BINARY_DIR}/brkem.out' && \
   grep -q 'brkem' '${CMAKE_BINARY_DIR}/brkem.out' && \
   grep -q 'mvi a,0x05' '${CMAKE_BINARY_DIR}/brkem.out' && \
   grep -q 'retem' '${CMAKE_BINARY_DIR}/brkem.out'")

# The V30 is the V20's instruction-set twin; it disassembles 8086 code under the v30 arch name.
add_test(NAME disasm.v30 COMMAND sh -c
  "printf '\\xb8\\x34\\x12\\xf4' > '${CMAKE_BINARY_DIR}/v30.bin' && \
   '$<TARGET_FILE:lcx>' disasm '${CMAKE_BINARY_DIR}/v30.bin' --arch v30 --count 2 | grep -q 'mov ax,0x1234'")

# Device tree: compile a machine .dts to a blob and decompile it back. The "&uart0"
# reference must materialize -- the target node gets an auto-assigned phandle and the
# referrer's stdout-path becomes that phandle cell, exactly as the reference compiler does.
add_test(NAME dt.roundtrip COMMAND sh -c
  "'$<TARGET_FILE:lcx>' dt compile '${CMAKE_CURRENT_SOURCE_DIR}/test/dt-machine.dts' -o '${CMAKE_BINARY_DIR}/dt-machine.dtb' && \
   '$<TARGET_FILE:lcx>' dt decompile '${CMAKE_BINARY_DIR}/dt-machine.dtb' | grep -q 'phandle = <0x1>' && \
   '$<TARGET_FILE:lcx>' dt decompile '${CMAKE_BINARY_DIR}/dt-machine.dtb' | grep -q 'stdout-path = <0x1>'")

# Device-tree fragment composition: a fragment adds a property to an existing node and a whole
# new node; both must appear in the composed tree.
add_test(NAME dt.overlay COMMAND sh -c
  "'$<TARGET_FILE:lcx>' dt compile '${CMAKE_CURRENT_SOURCE_DIR}/test/dt-machine.dts' -o '${CMAKE_BINARY_DIR}/dt-ovl-base.dtb' && \
   '$<TARGET_FILE:lcx>' dt compile '${CMAKE_CURRENT_SOURCE_DIR}/test/dt-frag.dts' -o '${CMAKE_BINARY_DIR}/dt-ovl-frag.dtb' && \
   '$<TARGET_FILE:lcx>' dt overlay '${CMAKE_BINARY_DIR}/dt-ovl-base.dtb' '${CMAKE_BINARY_DIR}/dt-ovl-frag.dtb' | grep -q 'clock-frequency' && \
   '$<TARGET_FILE:lcx>' dt overlay '${CMAKE_BINARY_DIR}/dt-ovl-base.dtb' '${CMAKE_BINARY_DIR}/dt-ovl-frag.dtb' | grep -q 'rtc@20000000'")

# Cross-format via the shared model: compile the machine to an Apple device tree (.adt, the
# distinct XNU/iBoot binary), then read it back. The node tree -- including the materialized
# phandle -- must survive the DTS -> Apple-binary -> model -> DTS round trip.
add_test(NAME dt.apple COMMAND sh -c
  "'$<TARGET_FILE:lcx>' dt compile '${CMAKE_CURRENT_SOURCE_DIR}/test/dt-machine.dts' -o '${CMAKE_BINARY_DIR}/dt-machine.adt' && \
   '$<TARGET_FILE:lcx>' dt dump '${CMAKE_BINARY_DIR}/dt-machine.adt' | grep -q 'serial@10000000' && \
   '$<TARGET_FILE:lcx>' dt dump '${CMAKE_BINARY_DIR}/dt-machine.adt' | grep -q 'phandle = <0x1>'")

# Open Firmware textual dump: decompile a blob to the OF ".properties" form (hex words, no
# ';') and parse it back. The phandle emitted as the hex word 00000001 must read back as the
# cell <0x1>, proving the OF emitter and parser both work through the shared model.
add_test(NAME dt.openfirmware COMMAND sh -c
  "'$<TARGET_FILE:lcx>' dt compile '${CMAKE_CURRENT_SOURCE_DIR}/test/dt-machine.dts' -o '${CMAKE_BINARY_DIR}/dt-of.dtb' && \
   '$<TARGET_FILE:lcx>' dt decompile '${CMAKE_BINARY_DIR}/dt-of.dtb' -o '${CMAKE_BINARY_DIR}/dt-of.ofd' && \
   grep -q 'phandle = 00000001' '${CMAKE_BINARY_DIR}/dt-of.ofd' && \
   '$<TARGET_FILE:lcx>' dt dump '${CMAKE_BINARY_DIR}/dt-of.ofd' | grep -q 'phandle = <0x1>'")

# Device-tree public C API, exercised from a C program: build/mutate/emit/parse/navigate
# through the LC-prefixed surface. Proves the library is genuinely usable from C.
add_test(NAME dt.capi COMMAND dtapi)
set_tests_properties(dt.capi PROPERTIES PASS_REGULAR_EXPRESSION "RESULT: PASS")

# Machine assembly by bundle matching: the builder reads each .device bundle's Info.plist
# (LCDeviceMatch) and binds it to the PC/XT device tree. The 8259 PIC bundle must match the
# pic@20 node on its "intel,i8259" compatible and expose the port-device capability via COM.
add_test(NAME machine.match COMMAND sh -c
  "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcxt.dts' --bundles '${CMAKE_BINARY_DIR}' > '${CMAKE_BINARY_DIR}/machine-match.out' && \
   grep -q 'pic@20 .* i8259 .*intel,i8259' '${CMAKE_BINARY_DIR}/machine-match.out' && \
   grep -q 'dma@0 .* i8237' '${CMAKE_BINARY_DIR}/machine-match.out' && \
   grep -q 'ppi@60 .* i8255' '${CMAKE_BINARY_DIR}/machine-match.out' && \
   grep -q 'display@3b0 .* mc6845' '${CMAKE_BINARY_DIR}/machine-match.out' && \
   grep -q 'memory@0 .* ram' '${CMAKE_BINARY_DIR}/machine-match.out' && \
   grep -q 'rom@fe000 .* rom' '${CMAKE_BINARY_DIR}/machine-match.out' && \
   grep -q 'fdc@3f0 .* i8272 .*intel,i8272' '${CMAKE_BINARY_DIR}/machine-match.out' && \
   grep -q 'hdc@320 .* st506 .*ibm,pc-xt-hdc' '${CMAKE_BINARY_DIR}/machine-match.out' && \
   grep -q 'speaker .* pcspeaker .*signal-sink.*ibm,pc-speaker' '${CMAKE_BINARY_DIR}/machine-match.out' && \
   grep -q 'ppi@60 .* i8255 .*signal-source' '${CMAKE_BINARY_DIR}/machine-match.out' && \
   grep -q 'ide@300 .* xtide .*xtide' '${CMAKE_BINARY_DIR}/machine-match.out' && \
   grep -q 'components: 12 matched' '${CMAKE_BINARY_DIR}/machine-match.out'")
set_tests_properties(machine.match PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

# The display-adapter family: each of CGA/EGA/VGA binds to its machine's display node and exposes
# the display capability via COM. The CGA run renders the sign-on banner at the card's own 0xB8000
# framebuffer (vs the MDA's 0xB0000), proving the framebuffer base comes from the bound component.
add_test(NAME machine.video COMMAND sh -c
  "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pccga.dts' --bundles '${CMAKE_BINARY_DIR}' --tree | grep -q 'display@3d0 .*cga .*display' && \
   '$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcega.dts' --bundles '${CMAKE_BINARY_DIR}' --tree | grep -q 'display@3c0 .*ega .*display' && \
   '$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcvga.dts' --bundles '${CMAKE_BINARY_DIR}' --tree | grep -q 'display@3c0 .*vga .*display' && \
   '$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcherc.dts' --bundles '${CMAKE_BINARY_DIR}' --tree | grep -q 'display@3b0 .*hercules .*display' && \
   '$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcincolor.dts' --bundles '${CMAKE_BINARY_DIR}' --tree | grep -q 'display@3b0 .*incolor .*display'")
set_tests_properties(machine.video PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

if(TARGET interp_backend)
  add_test(NAME machine.cga COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pccga.dts' --bundles '${CMAKE_BINARY_DIR}' --run > '${CMAKE_BINARY_DIR}/machine-cga.out' && \
     grep -q 'framebuffer 0xb8000' '${CMAKE_BINARY_DIR}/machine-cga.out' && \
     grep -q '0x000b8000..0x000bbfff  RW  MC6845 CRTC .CGA.' '${CMAKE_BINARY_DIR}/machine-cga.out' && \
     grep -q 'LIBCPU PC/XT 5160' '${CMAKE_BINARY_DIR}/machine-cga.out'")

  # Olivetti M24 / AT&T PC 6300: an XT-class 8086 machine with enhanced-CGA video and a uPD765
  # floppy controller. It assembles (CGA framebuffer at 0xB8000, FDC + ST-506 fitted) and runs the
  # power-on demo to the colour text screen, just like the IBM XT but on the M24's chipset.
  add_test(NAME machine.m24 COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' --run > '${CMAKE_BINARY_DIR}/machine-m24.out' && \
     grep -q 'Olivetti M24' '${CMAKE_BINARY_DIR}/machine-m24.out' && \
     grep -q 'uPD765/8272A FDC' '${CMAKE_BINARY_DIR}/machine-m24.out' && \
     grep -q 'Intel 8087 FPU' '${CMAKE_BINARY_DIR}/machine-m24.out' && \
     grep -q 'Parallel Port .SPP.' '${CMAKE_BINARY_DIR}/machine-m24.out' && \
     grep -q 'ST-506 HDC .20 MB: 615/4/17.' '${CMAKE_BINARY_DIR}/machine-m24.out' && \
     grep -q 'Hard Disk .20 MB: 615/4/17.' '${CMAKE_BINARY_DIR}/machine-m24.out' && \
     grep -q 'framebuffer 0xb8000' '${CMAKE_BINARY_DIR}/machine-m24.out' && \
     grep -q 'LIBCPU PC/XT 5160' '${CMAKE_BINARY_DIR}/machine-m24.out'")
  set_tests_properties(machine.m24 PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # Option-ROM bootstrap: --boot runs a minimal BIOS POST that scans the UMA for the 0x55 0xAA
  # signature and far-calls each ROM's init (FF /3). With a real Seagate ST-11 disk BIOS loaded on
  # the M24's controller, the bootstrap finds and enters it -- the scan report confirms it at 0xc8000.
  add_test(NAME machine.boot COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' --boot --steps 300 --rom 'hdc@320=${CMAKE_CURRENT_SOURCE_DIR}/roms/option-roms/Seagate/Seagate ST11 - BIOS version 2.1.bin' > '${CMAKE_BINARY_DIR}/machine-boot.out' && \
     grep -q 'loaded 6144 bytes for hdc@320 at 0xc8000' '${CMAKE_BINARY_DIR}/machine-boot.out' && \
     grep -q 'option ROM found at 0xc8000.*checksum OK' '${CMAKE_BINARY_DIR}/machine-boot.out'")
  set_tests_properties(machine.boot PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # ST-506 hard disk: the controller is a bus adapter over a generic block medium. A guest programs
  # DMA channel 3 and writes the 6-byte READ control block; the controller reads sector 0 from the
  # mounted 20 MB medium, DMAs it to memory, and raises IRQ5. The seeded sector-0 text comes back.
  add_test(NAME machine.hdc COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' --demo-hdc > '${CMAKE_BINARY_DIR}/machine-hdc.out' && \
     grep -q '1 IRQ5' '${CMAKE_BINARY_DIR}/machine-hdc.out' && \
     grep -q 'sector 0 at 0x12000 = .LIBCPU HARD DISK SECTOR 0' '${CMAKE_BINARY_DIR}/machine-hdc.out'")
  set_tests_properties(machine.hdc PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # M24 console: the XT/M24-class machine drives the typewriter through the 8255 PPI keyboard (no
  # 8042) onto the CGA display (which now reports IDisplayMode). Scripted keys "hi" appear on screen.
  add_test(NAME machine.m24console COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' --console --keys hi > '${CMAKE_BINARY_DIR}/machine-m24c.out' && \
     grep -q 'headless/scripted' '${CMAKE_BINARY_DIR}/machine-m24c.out' && \
     grep -q 'TYPE: .*hi' '${CMAKE_BINARY_DIR}/machine-m24c.out'")
  set_tests_properties(machine.m24console PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # The real Olivetti M24 system BIOS runs its full "Resident Diagnostics" POST on our model and
  # renders the result screen to CGA. This exercises the whole stack: the cycle-driven PIT (so the
  # "DMA Timer" test sees a [4,14] latch delta instead of halting at F000:E05B with "DMA Timer Fail"),
  # in-guest software-INT vectoring (so the BIOS's own INT 10h paints the screen and INT 13h probes
  # disks), the MC146818 RTC, the DMA/PIC/RAM checks, and FDC presence. We assert the passing report.
  # (It then tries INT 19h and prints "READ ERROR" -- no bootable floppy image is attached yet.)
  add_test(NAME machine.m24post COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' \
       --bios '${CMAKE_CURRENT_SOURCE_DIR}/roms/system-bios/olivetti-m24/BIOS121-combined.ROM' \
       --steps 500000 > '${CMAKE_BINARY_DIR}/machine-m24post.out' 2>&1 && \
     grep -q 'Resident Diagnostics' '${CMAKE_BINARY_DIR}/machine-m24post.out' && \
     grep -qE 'DMA Timer +Pass' '${CMAKE_BINARY_DIR}/machine-m24post.out' && \
     grep -qE '640 kb RAM +Pass' '${CMAKE_BINARY_DIR}/machine-m24post.out'")
  set_tests_properties(machine.m24post PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # Tiered execution: the same M24 POST, but with "--jit" naming an optimizing JIT (LLVM) as tier 1.
  # The machine runs cold code on the interpreter and promotes hot ROM regions to the JIT (which now
  # implements the system/syscall emitters, so it drives the device bus). Proves the JIT path produces
  # identical, correct results -- the full POST screen -- without the per-window compile cost that made
  # a JIT-only run unusably slow. Only registered when an LLVM backend bundle was built.
  if (EXISTS "${CMAKE_BINARY_DIR}/llvm-22.backend")
    add_test(NAME machine.m24jit COMMAND sh -c
      "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' \
         --jit '${CMAKE_BINARY_DIR}/llvm-22.backend' \
         --bios '${CMAKE_CURRENT_SOURCE_DIR}/roms/system-bios/olivetti-m24/BIOS121-combined.ROM' \
         --steps 500000 > '${CMAKE_BINARY_DIR}/machine-m24jit.out' 2>&1 && \
       grep -q 'Resident Diagnostics' '${CMAKE_BINARY_DIR}/machine-m24jit.out' && \
       grep -qE 'DMA Timer +Pass' '${CMAKE_BINARY_DIR}/machine-m24jit.out' && \
       grep -qE '640 kb RAM +Pass' '${CMAKE_BINARY_DIR}/machine-m24jit.out'")
    set_tests_properties(machine.m24jit PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")
  endif ()

  # The same, with the sljit REGISTER-ASSEMBLER JIT as tier 1. sljit is not an IR builder like LLVM;
  # its backend now implements the full CFG (CreateBlock/Branch/CondBranch via labels + deferred-jump
  # patching), the SMC guard / indirect-branch dispatcher, and the port-I/O + system + syscall trap
  # emitters -- so a register-assembler JIT can drive the whole device-emulated machine, not just the
  # standalone translation matrix. Cold code runs on the interpreter; hot regions promote to sljit and
  # produce the identical POST screen.
  if (EXISTS "${CMAKE_BINARY_DIR}/sljit.backend")
    add_test(NAME machine.m24jitsl COMMAND sh -c
      "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' \
         --jit '${CMAKE_BINARY_DIR}/sljit.backend' \
         --bios '${CMAKE_CURRENT_SOURCE_DIR}/roms/system-bios/olivetti-m24/BIOS121-combined.ROM' \
         --steps 500000 > '${CMAKE_BINARY_DIR}/machine-m24jitsl.out' 2>&1 && \
       grep -q 'Resident Diagnostics' '${CMAKE_BINARY_DIR}/machine-m24jitsl.out' && \
       grep -qE 'DMA Timer +Pass' '${CMAKE_BINARY_DIR}/machine-m24jitsl.out' && \
       grep -qE '640 kb RAM +Pass' '${CMAKE_BINARY_DIR}/machine-m24jitsl.out'")
    set_tests_properties(machine.m24jitsl PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")
  endif ()

  # Same proof for the other JITs given full CFG support, driving the device-emulated machine as tier-1
  # hot backends and producing the identical POST screen: GNU Lightning and AsmJit (slot/register
  # assemblers) plus MIR. MIR is an optimizing IR builder (SSA + register allocation), so like LLVM it
  # is practical only TIERED -- the interpreter runs cold code and MIR compiles just the hot loops; a
  # MIR-only run recompiles every window and is as slow as LLVM-only. (MIR_gen also needed a large stack
  # for its recursive CFG DFS and opt level 0 to avoid an AArch64 codegen bug -- see MirBackend.cpp.)
  foreach (_jit lightning asmjit mir)
    if (EXISTS "${CMAKE_BINARY_DIR}/${_jit}.backend")
      add_test(NAME machine.m24jit-${_jit} COMMAND sh -c
        "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' \
           --jit '${CMAKE_BINARY_DIR}/${_jit}.backend' \
           --bios '${CMAKE_CURRENT_SOURCE_DIR}/roms/system-bios/olivetti-m24/BIOS121-combined.ROM' \
           --steps 500000 > '${CMAKE_BINARY_DIR}/machine-m24jit-${_jit}.out' 2>&1 && \
         grep -q 'Resident Diagnostics' '${CMAKE_BINARY_DIR}/machine-m24jit-${_jit}.out' && \
         grep -qE 'DMA Timer +Pass' '${CMAKE_BINARY_DIR}/machine-m24jit-${_jit}.out' && \
         grep -qE '640 kb RAM +Pass' '${CMAKE_BINARY_DIR}/machine-m24jit-${_jit}.out'")
      set_tests_properties(machine.m24jit-${_jit} PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")
    endif ()
  endforeach ()

  # MULTI-TIER ladder: --jit takes a comma-separated cold->hot list, and a hot region climbs it in the
  # BACKGROUND (interp -> asmjit -> sljit -> mir), each tier optimizing harder at a higher run threshold.
  # Proves the tier engine + async promotion drive the machine to the identical POST screen.
  if (EXISTS "${CMAKE_BINARY_DIR}/asmjit.backend" AND EXISTS "${CMAKE_BINARY_DIR}/sljit.backend" AND EXISTS "${CMAKE_BINARY_DIR}/mir.backend")
    add_test(NAME machine.m24jit-multitier COMMAND sh -c
      "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' \
         --jit '${CMAKE_BINARY_DIR}/asmjit.backend,${CMAKE_BINARY_DIR}/sljit.backend,${CMAKE_BINARY_DIR}/mir.backend' \
         --bios '${CMAKE_CURRENT_SOURCE_DIR}/roms/system-bios/olivetti-m24/BIOS121-combined.ROM' \
         --steps 500000 > '${CMAKE_BINARY_DIR}/machine-m24jit-multitier.out' 2>&1 && \
       grep -q 'Resident Diagnostics' '${CMAKE_BINARY_DIR}/machine-m24jit-multitier.out' && \
       grep -qE 'DMA Timer +Pass' '${CMAKE_BINARY_DIR}/machine-m24jit-multitier.out' && \
       grep -qE '640 kb RAM +Pass' '${CMAKE_BINARY_DIR}/machine-m24jit-multitier.out'")
    set_tests_properties(machine.m24jit-multitier PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")
  endif ()

  # OPTIMIZATION at a higher tier: a hot region reaches an OPTIMIZING backend (LLVM / GCC JIT) that the
  # tiered driver tells to compile at the tier's level (ICpuBackendOptimize -> O2 here), in the
  # background. Proves opt-options end-to-end: the machine POSTs with the optimized hot code.
  foreach (_opt llvm-22 gccjit-15)
    if (EXISTS "${CMAKE_BINARY_DIR}/asmjit.backend" AND EXISTS "${CMAKE_BINARY_DIR}/${_opt}.backend")
      add_test(NAME machine.m24jit-opt-${_opt} COMMAND sh -c
        "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' \
           --jit '${CMAKE_BINARY_DIR}/asmjit.backend,${CMAKE_BINARY_DIR}/${_opt}.backend' \
           --bios '${CMAKE_CURRENT_SOURCE_DIR}/roms/system-bios/olivetti-m24/BIOS121-combined.ROM' \
           --steps 500000 > '${CMAKE_BINARY_DIR}/machine-m24jit-opt-${_opt}.out' 2>&1 && \
         grep -q 'Resident Diagnostics' '${CMAKE_BINARY_DIR}/machine-m24jit-opt-${_opt}.out' && \
         grep -qE 'DMA Timer +Pass' '${CMAKE_BINARY_DIR}/machine-m24jit-opt-${_opt}.out' && \
         grep -qE '640 kb RAM +Pass' '${CMAKE_BINARY_DIR}/machine-m24jit-opt-${_opt}.out'")
      set_tests_properties(machine.m24jit-opt-${_opt} PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")
    endif ()
  endforeach ()

  # PURE JIT (no interpreter tier): the whole M24 BIOS -- cold code and all -- runs on the JIT. This
  # is the strongest test: it requires the JIT to be a complete, correct frontend target (every ALU
  # op incl. division, a bounded temp frame, full CFG + traps) AND the unified per-instruction clock
  # (ICpuClockEmitter) so PIT timing matches the interpreter. Every register-assembler JIT that drives
  # the machine -- sljit, GNU Lightning, and AsmJit (rewritten onto the low-level a64::Assembler with a
  # manual slot frame, since its Compiler's register allocator cannot reconcile our externally-built
  # CFG) -- POSTs the full screen this way, with no interpreter help.
  foreach (_jit sljit lightning asmjit)
    if (EXISTS "${CMAKE_BINARY_DIR}/${_jit}.backend")
      add_test(NAME machine.m24pure-${_jit} COMMAND sh -c
        "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' \
           --backend '${CMAKE_BINARY_DIR}/${_jit}.backend' \
           --bios '${CMAKE_CURRENT_SOURCE_DIR}/roms/system-bios/olivetti-m24/BIOS121-combined.ROM' \
           --steps 600000 > '${CMAKE_BINARY_DIR}/machine-m24pure-${_jit}.out' 2>&1 && \
         grep -q 'Resident Diagnostics' '${CMAKE_BINARY_DIR}/machine-m24pure-${_jit}.out' && \
         grep -qE 'DMA Timer +Pass' '${CMAKE_BINARY_DIR}/machine-m24pure-${_jit}.out' && \
         grep -qE '640 kb RAM +Pass' '${CMAKE_BINARY_DIR}/machine-m24pure-${_jit}.out'")
      set_tests_properties(machine.m24pure-${_jit} PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")
    endif ()
  endforeach ()

  # CROSS-ARCH: run the pure AsmJit machine through the backend's x86-64 slice under Rosetta 2. AsmJit
  # is target-specific, so this exercises a DIFFERENT code path than the native arm64 slice -- the whole
  # x86-64 emitter (FuncFrame SysV prologue, RDX:RAX division, every ALU op) emitting real x86-64 that
  # Rosetta executes. Registered only on an Apple Silicon host that built an x86_64 slice and has a
  # working `arch -x86_64` (Rosetta). Slower (binary translation), so a generous step budget.
  if (APPLE AND "x86_64" IN_LIST LIBCPU_UNIVERSAL_ARCHS AND EXISTS "${CMAKE_BINARY_DIR}/asmjit.backend")
    execute_process(COMMAND arch -x86_64 /usr/bin/true RESULT_VARIABLE _rosetta_rc ERROR_QUIET OUTPUT_QUIET)
    if (_rosetta_rc EQUAL 0)
      add_test(NAME machine.m24pure-asmjit-x86_64 COMMAND sh -c
        "arch -x86_64 '$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' \
           --backend '${CMAKE_BINARY_DIR}/asmjit.backend' \
           --bios '${CMAKE_CURRENT_SOURCE_DIR}/roms/system-bios/olivetti-m24/BIOS121-combined.ROM' \
           --steps 600000 > '${CMAKE_BINARY_DIR}/machine-m24pure-asmjit-x86_64.out' 2>&1 && \
         grep -q 'Resident Diagnostics' '${CMAKE_BINARY_DIR}/machine-m24pure-asmjit-x86_64.out' && \
         grep -qE 'DMA Timer +Pass' '${CMAKE_BINARY_DIR}/machine-m24pure-asmjit-x86_64.out' && \
         grep -qE '640 kb RAM +Pass' '${CMAKE_BINARY_DIR}/machine-m24pure-asmjit-x86_64.out'")
      set_tests_properties(machine.m24pure-asmjit-x86_64 PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:" TIMEOUT 180)
    endif ()
  endif ()

  # WINDOWS ABIs via Wine: cross-compile the AsmJit codegen harness for Windows x86-64 and x86 with
  # MinGW-w64 and run both under Wine, proving the backend executes under the Microsoft x64 (Win64 /
  # UEFI x64) and cdecl (Win32 / UEFI IA-32) conventions -- the ABIs the native arm64 / Rosetta-SysV
  # runs cannot exercise. Registered only when both MinGW-w64 cross-compilers and a Wine are present.
  find_program(_mingw_x64 x86_64-w64-mingw32-g++)
  find_program(_mingw_x86 i686-w64-mingw32-g++)
  find_program(_wine_sys wine)
  if (_mingw_x64 AND _mingw_x86 AND (_wine_sys OR EXISTS "$ENV{HOME}/bin/wine"))
    add_test(NAME asmjit.windows-wine COMMAND sh
      "${CMAKE_CURRENT_SOURCE_DIR}/test/run-asmjit-wine.sh" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    set_tests_properties(asmjit.windows-wine PROPERTIES TIMEOUT 300)

    # libnix's host-op core, cross-built for win32 with MinGW-w64 and run under Wine -- proves the
    # win32 host backend of the user-space syscall layer (the m88k/OpenBSD emulation builds on it).
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/run-libnix-wine.sh")
      add_test(NAME libnix.windows-wine COMMAND sh
        "${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/run-libnix-wine.sh" "${CMAKE_BINARY_DIR}/libnix-win")
      set_tests_properties(libnix.windows-wine PROPERTIES TIMEOUT 300 FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
    endif ()
  endif ()

  # libnix host-portability cross-check: build the haiku / os2 / vms source SELECTIONS (CORE + BSD
  # family, via -DNIX_HOST_PROFILE) on this POSIX/BSD-family build host and run the host-op
  # conformance test (nixtest) for each. Proves those hosts' selection compiles, links and passes
  # the conformance surface (file ops, time, hostname, credentials, signals, loopback sockets)
  # even where their own toolchain is unavailable. Needs only cmake + a POSIX host (not Wine/MinGW).
  if (NOT WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/run-libnix-profiles.sh")
    add_test(NAME libnix.host-profiles COMMAND sh
      "${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/run-libnix-profiles.sh" "${CMAKE_BINARY_DIR}/libnix-prof")
    set_tests_properties(libnix.host-profiles PROPERTIES TIMEOUT 300
      FAIL_REGULAR_EXPRESSION "FAIL:" PASS_REGULAR_EXPRESSION "PASS: libnix host profile 'vms'")
  endif ()

  # Native Haiku compile: build libnix with the real Haiku cross toolchain (x86_64-unknown-haiku-gcc)
  # against Haiku's actual system headers. Skips cleanly when the toolchain is absent (set
  # HAIKU_CROSS_DIR + HAIKU_SRC), exactly as the win32 gate skips without MinGW/Wine. This is the
  # genuine native-toolchain compile of the Haiku port (where the header/symbol portability bugs live).
  if (NOT WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/run-libnix-haiku.sh")
    add_test(NAME libnix.haiku-native COMMAND sh
      "${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/run-libnix-haiku.sh" "${CMAKE_BINARY_DIR}/libnix-haiku")
    set_tests_properties(libnix.haiku-native PROPERTIES TIMEOUT 300 FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
  endif ()

  # OS/2 header-accurate compile: build libnix against the real OS/2 libc (kLIBC) headers with a
  # 32-bit x86 GCC. Auto-fetches the open kLIBC headers; skips cleanly without an x86 GCC. Same
  # standard as the Haiku gate (the runnable-binary step additionally needs the bitwiseworks OS/2
  # GCC's LX backend, which runs on OS/2).
  if (NOT WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/run-libnix-os2.sh")
    add_test(NAME libnix.os2-native COMMAND sh
      "${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/run-libnix-os2.sh" "${CMAKE_BINARY_DIR}/libnix-os2")
    set_tests_properties(libnix.os2-native PROPERTIES TIMEOUT 300 FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
  endif ()

  # End-to-end M24 boot: attach a bootable 360 KB diskette image (generated by mk_m24boot.py) to the
  # floppy controller, then let the real M24 BIOS POST, run its INT 13h boot-strap (reset + read C0
  # H0 S1 to 0000:7C00 over the FDC/DMA), and jump into the loaded boot sector, which prints
  # "M24 BOOTS LibCPU!" via INT 10h. This exercises FDC reset/IRQ6, DMA terminal-count, the 8237, and
  # in-guest software-INT vectoring together -- the whole machine from power-on to running guest code.
  # The step budget covers the real BIOS's hard-disk controller probe (it now selects and command-
  # handshakes the ST-506 before concluding no fixed disk is bootable) ahead of the floppy boot-strap.
  add_test(NAME machine.m24boot COMMAND sh -c
    "python3 '${CMAKE_CURRENT_SOURCE_DIR}/test/mk_m24boot.py' '${CMAKE_BINARY_DIR}/m24-boot.img' && \
     sed 's#libcpu,sectors = <9>;#libcpu,sectors = <9>; libcpu,image = \"${CMAKE_BINARY_DIR}/m24-boot.img\";#' \
       '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' > '${CMAKE_BINARY_DIR}/m24-boot.dts' && \
     '$<TARGET_FILE:lcx>' machine '${CMAKE_BINARY_DIR}/m24-boot.dts' --bundles '${CMAKE_BINARY_DIR}' \
       --bios '${CMAKE_CURRENT_SOURCE_DIR}/roms/system-bios/olivetti-m24/BIOS121-combined.ROM' \
       --steps 4000000 > '${CMAKE_BINARY_DIR}/machine-m24boot.out' 2>&1 && \
     grep -q 'M24 BOOTS LibCPU' '${CMAKE_BINARY_DIR}/machine-m24boot.out'")
  set_tests_properties(machine.m24boot PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # M24 BIOS 1.36 fixed-disk detection: unlike 1.21 (which does not drive the ST-506), BIOS >= 1.36
  # resets the Xebec controller, polls it for the command-ready status, issues init/diagnostic
  # commands, and waits for the completion interrupt -- exercising the controller phase machine, the
  # open-bus controller-presence scan, and the 8259 IRR latch that holds the masked completion IRQ5
  # until the BIOS unmasks it. POST then reports the disk Ready. (A regression here re-hangs the BIOS
  # at the controller status poll, the original symptom.)
  add_test(NAME machine.m24hdc COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' \
       --backend '${CMAKE_BINARY_DIR}/interp.backend' \
       --bios '${CMAKE_CURRENT_SOURCE_DIR}/roms/system-bios/olivetti-m24/BIOS136-combined.ROM' \
       --steps 8000000 > '${CMAKE_BINARY_DIR}/machine-m24hdc.out' 2>&1 && \
     grep -q 'Disk(s) Ready' '${CMAKE_BINARY_DIR}/machine-m24hdc.out'")
  set_tests_properties(machine.m24hdc PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # The CLR (.NET) backend drives the full machine. It has native CFG but none of the machine
  # emitters (port I/O / INT / clock), so the System routes it through the shadow path (which
  # synthesizes them); it supplies a real cycle clock (ICpuClockEmitter) and a guarded division
  # (divisor 0 -> 0, never a host trap), and accesses guest RAM through a native pointer (no
  # per-block copy). With all of that it POSTs the M24 BIOS through its diagnostics. Each regression
  # has a distinct failure: no shadow routing -> an infinite SMC-guard loop (hang); no clock ->
  # halts at the DMA-Timer test; unguarded division -> a .NET DivideByZeroException. Guarded only by
  # the bundle existing, since the backend needs a .NET toolchain at build time.
  if (EXISTS "${CMAKE_BINARY_DIR}/clr.backend")
    add_test(NAME machine.m24-clr COMMAND sh -c
      "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/m24.dts' --bundles '${CMAKE_BINARY_DIR}' \
         --backend '${CMAKE_BINARY_DIR}/clr.backend' \
         --bios '${CMAKE_CURRENT_SOURCE_DIR}/roms/system-bios/olivetti-m24/BIOS121-combined.ROM' \
         --steps 500000 > '${CMAKE_BINARY_DIR}/machine-m24-clr.out' 2>&1 && \
       grep -qE 'DMA Timer +Pass' '${CMAKE_BINARY_DIR}/machine-m24-clr.out'")
    set_tests_properties(machine.m24-clr PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:|Unhandled exception")
  endif ()

  # 8042 keyboard-controller self-test: a guest issues the controller self-test command (0xAA to
  # 0x64) and reads the result from 0x60 -- the 8042 answers 0x55 ('U'), which the program prints as
  # "KBC=U" on the serial console. End to end through the AT-class machine's device bus.
  add_test(NAME machine.kbc COMMAND sh -c
    "printf '\\xBA\\xF8\\x03\\xB0\\x4B\\xEE\\xB0\\x42\\xEE\\xB0\\x43\\xEE\\xB0\\x3D\\xEE\\xB0\\xAA\\xE6\\x64\\xE4\\x60\\xEE\\xB0\\x0A\\xEE\\xF4' > '${CMAKE_BINARY_DIR}/kbc.bin' && \
     '$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcvga.dts' --bundles '${CMAKE_BINARY_DIR}' --image '${CMAKE_BINARY_DIR}/kbc.bin' --load 0x0600 > '${CMAKE_BINARY_DIR}/kbc.out' && \
     grep -q 'kbc@60 .* i8042 .*intel,i8042' '${CMAKE_BINARY_DIR}/kbc.out' && \
     grep -q 'KBC=U' '${CMAKE_BINARY_DIR}/kbc.out'")
  set_tests_properties(machine.kbc PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # 8042 keyboard IRQ1 end to end: a scan code seeded in the device tree is delivered by the 8042
  # as IRQ1, routed through the PIC, and vectored to an INT 9 ISR that reads it from 0x60. The run
  # reports one interrupt delivered and the scan code (0x1E) the ISR recovered.
  add_test(NAME machine.kbd COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcvga.dts' --bundles '${CMAKE_BINARY_DIR}' --demo-kbd > '${CMAKE_BINARY_DIR}/kbd.out' && \
     grep -q 'press a key... [*]' '${CMAKE_BINARY_DIR}/kbd.out' && \
     grep -q '1 interrupt.s. delivered; ISR read scan code 0x1E' '${CMAKE_BINARY_DIR}/kbd.out'")
  set_tests_properties(machine.kbd PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # Console seam end to end: the interactive typewriter guest runs while a headless console scripts
  # the keys "hello". Each key crosses the in-process console channel as a KEY message, is injected
  # into the 8042 (IKeyboardSink), fires IRQ1, and the ISR echoes the glyph to VGA text memory; the
  # bridge streams the framebuffer back as FRAME messages, which the console renders -- so "hello"
  # appears on the screen the test prints. Proves both directions of the protocol.
  add_test(NAME machine.console COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcvga.dts' --bundles '${CMAKE_BINARY_DIR}' --console --keys hello > '${CMAKE_BINARY_DIR}/console.out' && \
     grep -q 'headless/scripted' '${CMAKE_BINARY_DIR}/console.out' && \
     grep -q 'TYPE: .*hello' '${CMAKE_BINARY_DIR}/console.out'")
  set_tests_properties(machine.console PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # MC146818 RTC/CMOS: a guest selects each clock register through 0x70 and reads it from 0x71;
  # the seeded BCD date/time (12:00:00 on 2026-06-16) comes back through the device bus.
  add_test(NAME machine.rtc COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcvga.dts' --bundles '${CMAKE_BINARY_DIR}' --demo-rtc > '${CMAKE_BINARY_DIR}/rtc.out' && \
     grep -q 'rtc@70 .* mc146818' '${CMAKE_BINARY_DIR}/rtc.out' && \
     grep -q 'RTC/CMOS: 12:00:00  2026-06-16' '${CMAKE_BINARY_DIR}/rtc.out'")
  set_tests_properties(machine.rtc PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # Slave 8259 cascade: the RTC's periodic interrupt (IRQ8) is vectored by the slave PIC (ICW2 base
  # 0x70 -> INT 0x70) and presented to the master on IRQ2; the ISR fires for each tick. Proves the
  # two-chip cascade and IRQ8-15 routing.
  add_test(NAME machine.slavepic COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcvga.dts' --bundles '${CMAKE_BINARY_DIR}' --demo-rtc-irq > '${CMAKE_BINARY_DIR}/slavepic.out' && \
     grep -q 'pic@a0 .* i8259' '${CMAKE_BINARY_DIR}/slavepic.out' && \
     grep -q 'RTC IRQ8: !!!' '${CMAKE_BINARY_DIR}/slavepic.out' && \
     grep -q '3 interrupt.s. via the slave 8259' '${CMAKE_BINARY_DIR}/slavepic.out' && \
     grep -q 'master ISR in handler = 0x04' '${CMAKE_BINARY_DIR}/slavepic.out'")
  set_tests_properties(machine.slavepic PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # 8254 counter-latch read-back: a guest programs channel 0 (reload 0x1234), latches it, and reads
  # the count back LSB-then-MSB -- proving the access-mode sequencing and latch path (the old model
  # always read 0). The latch advances the live count once, so 0x1234 reads back as 0x1233.
  add_test(NAME machine.pit COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcvga.dts' --bundles '${CMAKE_BINARY_DIR}' --demo-pit > '${CMAKE_BINARY_DIR}/pit.out' && \
     grep -q 'PIT ch0 latched count = 0x1231' '${CMAKE_BINARY_DIR}/pit.out'")
  set_tests_properties(machine.pit PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # DMA-driven floppy read: the guest programs DMA channel 2 and issues the FDC Read Data command;
  # the DMA bridge moves sector 0 from the controller's backing image into memory at 0x2000 and the
  # FDC raises IRQ6. Proves the FDC <-> 8237 <-> memory data path end to end.
  add_test(NAME machine.fdc COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcxt.dts' --bundles '${CMAKE_BINARY_DIR}' --demo-fdc > '${CMAKE_BINARY_DIR}/fdc.out' && \
     grep -q '1 IRQ6' '${CMAKE_BINARY_DIR}/fdc.out' && \
     grep -q 'sector at 0x12000 .page 1. = .LIBCPU FLOPPY SECTOR 0 - DMA TRANSFER OK' '${CMAKE_BINARY_DIR}/fdc.out'")
  set_tests_properties(machine.fdc PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # FDC write path: the guest fills a buffer, DMAs it out to the disk image via Write Data, then
  # reads the same sector back into a different buffer. The pattern surviving the round trip proves
  # the controller writes the image (memory -> FDC -> image -> FDC -> memory).
  add_test(NAME machine.fdcwrite COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcxt.dts' --bundles '${CMAKE_BINARY_DIR}' --demo-fdc-write > '${CMAKE_BINARY_DIR}/fdcw.out' && \
     grep -q 'read back from disk = .DMA-WRITE-ROUNDTRIP-OK' '${CMAKE_BINARY_DIR}/fdcw.out'")
  set_tests_properties(machine.fdcwrite PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # Priority-resolving 8259: the IRQ0 handler reads the in-service register through an OCW3 select
  # and sees bit 0 set; EOI clears it so all five ticks are delivered. Proves ISR/IRR/OCW3/EOI.
  add_test(NAME machine.pic COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcvga.dts' --bundles '${CMAKE_BINARY_DIR}' --demo-pic > '${CMAKE_BINARY_DIR}/pic.out' && \
     grep -q '5 IRQ0; in-service register read inside the handler = 0x01' '${CMAKE_BINARY_DIR}/pic.out'")
  set_tests_properties(machine.pic PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # XT-IDE PIO read: the guest programs the ATA task file (LBA 0, READ SECTORS) and reads the sector
  # back through the 8-bit data path (low byte 0x300, latched high byte 0x308). Sector 0's pattern
  # arriving at 0x4000 proves the task file + PIO transfer + high-byte latch.
  add_test(NAME machine.ide COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcxt.dts' --bundles '${CMAKE_BINARY_DIR}' --demo-ide > '${CMAKE_BINARY_DIR}/ide.out' && \
     grep -q 'sector 0 at 0x4000 = .LIBCPU XT-IDE SECTOR 0 - PIO READ OK' '${CMAKE_BINARY_DIR}/ide.out'")
  set_tests_properties(machine.ide PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # Controller firmware (real vendor option ROMs): the device tree points the ST-506 controller's
  # ROM node at a real Seagate ST-11 BIOS and a SCSI ROM node at a real Future Domain BIOS (fetched
  # into roms/). They load, the guest reads the 0x55 0xAA signature, and the BIOS-style UMA scan
  # finds both with their declared sizes and valid checksums.
  add_test(NAME machine.rom COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcrom.dts' --bundles '${CMAKE_BINARY_DIR}' --demo-rom > rom.out && \
     grep -q 'firmware mapped: 16384 bytes at 0xc8000' rom.out && \
     grep -q 'guest read option-ROM signature at 0xc8000: 0x55 0xAA' rom.out && \
     grep -q 'option ROM found at 0xc8000: 6144 bytes, checksum OK' rom.out && \
     grep -q 'option ROM found at 0xd0000: 6144 bytes, checksum OK' rom.out")
  set_tests_properties(machine.rom PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

  # Command-line firmware with auto-assigned addresses: "--rom <device>=<path>" loads a real ROM
  # for the named controller into the next free option-ROM slot. The ST-11 (for st506) lands at
  # 0xC8000 and the Future Domain ROM (for xtide) packs in at 0xC9800; both scan as valid.
  add_test(NAME machine.romcli COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcxt.dts' --bundles '${CMAKE_BINARY_DIR}' --demo-rom \
       --rom 'st506=${CMAKE_CURRENT_SOURCE_DIR}/roms/option-roms/Seagate/Seagate ST11 - BIOS version 2.0.0.bin' \
       --rom 'xtide=${CMAKE_CURRENT_SOURCE_DIR}/roms/option-roms/Future Domain/Future Domain - SCSI Disk Drive ROM BIOS V6.01.bin' > romcli.out && \
     grep -q 'st506: loaded 6144 bytes for hdc@320 at 0xc8000' romcli.out && \
     grep -q 'xtide: loaded 6144 bytes for ide@300 at 0xc9800' romcli.out && \
     grep -q 'option ROM found at 0xc8000: 6144 bytes, checksum OK' romcli.out && \
     grep -q 'option ROM found at 0xc9800: 6144 bytes, checksum OK' romcli.out")
  set_tests_properties(machine.romcli PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")
endif()

# "lcx machine <dts> --roms" lists the expansion cards that can host an option ROM (the valid
# --rom targets) and their conventional addresses; motherboard chips (PIC/PIT/...) are excluded.
add_test(NAME machine.romlist COMMAND sh -c
  "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcxt.dts' --bundles '${CMAKE_BINARY_DIR}' --roms > '${CMAKE_BINARY_DIR}/romlist.out' && \
   grep -q 'ide@300 .*xtide .*conventional ROM @ 0xc8000' '${CMAKE_BINARY_DIR}/romlist.out' && \
   grep -q 'hdc@320 .*st506 .*conventional ROM @ 0xc8000' '${CMAKE_BINARY_DIR}/romlist.out' && \
   grep -q 'display@3b0 .*mc6845 .*conventional ROM @ 0xc0000' '${CMAKE_BINARY_DIR}/romlist.out' && \
   ! grep -q 'pic@20' '${CMAKE_BINARY_DIR}/romlist.out'")
set_tests_properties(machine.romlist PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

if(TARGET interp_backend)
  # Bank switching + "no card, no memory": the demo writes both Hercules pages, flips the displayed
  # page via the mode register (so the rendered screen is page 1, scanned out of the card's own
  # VRAM at 0xB8000), and probes an unmapped address -- which reads back 0xFF (open bus).
  add_test(NAME machine.bank COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcherc.dts' --bundles '${CMAKE_BINARY_DIR}' --demo > '${CMAKE_BINARY_DIR}/machine-bank.out' && \
     grep -q 'unmapped 0xA0000 -> 0xFF' '${CMAKE_BINARY_DIR}/machine-bank.out' && \
     grep -q 'no card, no memory' '${CMAKE_BINARY_DIR}/machine-bank.out' && \
     grep -q 'framebuffer 0xb8000' '${CMAKE_BINARY_DIR}/machine-bank.out' && \
     grep -q 'PAGE 1 -- bank-switched in' '${CMAKE_BINARY_DIR}/machine-bank.out'")
endif()

# The --tree view renders the assembled machine as a true tree: box-drawing connectors, each node
# annotated with its bound component/capabilities and the signal wiring resolved to source(line).
add_test(NAME machine.tree COMMAND sh -c
  "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pc5150.dts' --bundles '${CMAKE_BINARY_DIR}' --tree > '${CMAKE_BINARY_DIR}/machine-tree.out' && \
   grep -q '└──' '${CMAKE_BINARY_DIR}/machine-tree.out' && \
   grep -q 'pic@20 .*i8259.*irq-ctrl' '${CMAKE_BINARY_DIR}/machine-tree.out' && \
   grep -q 'cassette .*signal-sink.*ppi@60.line1.' '${CMAKE_BINARY_DIR}/machine-tree.out' && \
   grep -q 'speaker .*ppi@60.line1. pit@40.line2.' '${CMAKE_BINARY_DIR}/machine-tree.out'")
set_tests_properties(machine.tree PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

# The IBM PC 5150: the cassette interface the XT dropped binds as a signal sink (tapping PPI
# port B's motor-relay bit), proving the board-level signal-line wiring across machine variants.
add_test(NAME machine.match5150 COMMAND sh -c
  "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pc5150.dts' --bundles '${CMAKE_BINARY_DIR}' > '${CMAKE_BINARY_DIR}/machine-5150.out' && \
   grep -q 'cassette .* cassette .*signal-sink.*ibm,pc-cassette' '${CMAKE_BINARY_DIR}/machine-5150.out' && \
   grep -q 'speaker .* pcspeaker .*signal-sink' '${CMAKE_BINARY_DIR}/machine-5150.out' && \
   ! grep -q 'hdc@' '${CMAKE_BINARY_DIR}/machine-5150.out' && \
   grep -q 'components: 11 matched' '${CMAKE_BINARY_DIR}/machine-5150.out'")
set_tests_properties(machine.match5150 PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

# Run the assembled machine on the V20: a power-on program drives the matched 8250 UART
# component (port 0x3F8) through the device bus, so its banner reaches the serial console --
# end to end: device tree -> bundle match -> COM component -> CPU port I/O -> stdout.
if(TARGET interp_backend)
  add_test(NAME machine.run COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcxt.dts' --bundles '${CMAKE_BINARY_DIR}' --run > '${CMAKE_BINARY_DIR}/machine-run.out' && \
     grep -q 'PC/XT OK' '${CMAKE_BINARY_DIR}/machine-run.out' && \
     grep -q '\\.\\.\\.\\.\\.' '${CMAKE_BINARY_DIR}/machine-run.out' && \
     grep -q 'System RAM' '${CMAKE_BINARY_DIR}/machine-run.out' && \
     grep -q 'BIOS ROM' '${CMAKE_BINARY_DIR}/machine-run.out' && \
     grep -q 'speaker. ON' '${CMAKE_BINARY_DIR}/machine-run.out' && \
     ! grep -q 'cassette' '${CMAKE_BINARY_DIR}/machine-run.out' && \
     grep -q 'LIBCPU PC/XT 5160' '${CMAKE_BINARY_DIR}/machine-run.out'")

  # The 5150 run drives the same power-on program: the speaker beeps AND the cassette motor
  # cycles on/off -- the PPI port-B writes reach both signal sinks through the board wiring.
  add_test(NAME machine.run5150 COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pc5150.dts' --bundles '${CMAKE_BINARY_DIR}' --run > '${CMAKE_BINARY_DIR}/machine-5150run.out' && \
     grep -q 'speaker. ON' '${CMAKE_BINARY_DIR}/machine-5150run.out' && \
     grep -q 'cassette. motor ON' '${CMAKE_BINARY_DIR}/machine-5150run.out' && \
     grep -q 'cassette. motor OFF' '${CMAKE_BINARY_DIR}/machine-5150run.out'")
endif()

# Catalog into dispatch: a guest INT 21h/AH=10h is dispatched to the real libc toupper(),
# bound dynamically through the derived catalog (not a built-in handler). The guest sets
# AL='a'; after the run AL must be 'A', proving native call + argument/result marshalling.
if(TARGET interp_backend)
  add_test(NAME know.bind COMMAND lcx know bind
    "${CMAKE_CURRENT_SOURCE_DIR}/test/hostcall.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/test/libc_subset.tbd"
    "${CMAKE_CURRENT_SOURCE_DIR}/test/hostcall.xml")
  set_tests_properties(know.bind PROPERTIES FAIL_REGULAR_EXPRESSION "RESULT: FAIL")

  # Struct marshalling: a real demo shared library (built here) whose out-parameter call
  # fills a 32-bit-field struct; the dispatcher marshals it back to the guest's 16-bit
  # packed layout by the catalog's field offsets. Proves struct-conversion end to end.
  add_library(structdemo SHARED test/structdemo.c)
  add_test(NAME know.marshal COMMAND lcx know marshal
    "${CMAKE_CURRENT_SOURCE_DIR}/test/structdemo.h"
    "$<TARGET_FILE:structdemo>"
    "${CMAKE_CURRENT_SOURCE_DIR}/test/structdemo.xml")
  set_tests_properties(know.marshal PROPERTIES FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
  list(APPEND LIBCPU_CHECK_DEPS structdemo)

  # C++ member-call capstone: a real C++ class (built here) is derived into a catalog, then
  # its constructor and member functions are actually CALLED through their bound symbols with
  # a real "this" pointer. Proves the this-as-first-arg model is ABI-correct, the recorded
  # struct size sizes the object, and the two add() overloads reach distinct code.
  add_library(countercpp SHARED test/counter.cpp)
  add_test(NAME know.invoke COMMAND lcx know invoke
    "${CMAKE_CURRENT_SOURCE_DIR}/test/counter.hpp"
    "$<TARGET_FILE:countercpp>")
  set_tests_properties(know.invoke PROPERTIES PASS_REGULAR_EXPRESSION "RESULT: PASS")
  list(APPEND LIBCPU_CHECK_DEPS countercpp)

  # C++ class-template instantiation: the parser captures the template's member shapes once,
  # then expands cn::Holder<int> into a concrete catalog entity -- a real layout (T->int at its
  # true offset) and methods whose template-id mangled symbols (__ZN2cn6HolderIiE...) the
  # catalog matches. Asserts both the substituted field at its offset and the const-method bind.
  add_library(tholdercpp SHARED test/tholder.cpp)
  add_test(NAME know.template COMMAND sh -c
    "'$<TARGET_FILE:lcx>' know derive '${CMAKE_CURRENT_SOURCE_DIR}/test/tholder.hpp' \
         '$<TARGET_FILE:tholdercpp>' -o '${CMAKE_BINARY_DIR}/tholder.klib' && \
     '$<TARGET_FILE:lcx>' know catalog '${CMAKE_BINARY_DIR}/tholder.klib' | grep -q '+4    int item' && \
     '$<TARGET_FILE:lcx>' know catalog '${CMAKE_BINARY_DIR}/tholder.klib' | grep -q 'value  -> __ZNK2cn6HolderIiE5valueEv'")
  set_tests_properties(know.template PROPERTIES FAIL_REGULAR_EXPRESSION "lcx know:")
  list(APPEND LIBCPU_CHECK_DEPS tholdercpp)

  # Derived target->host mapping: the DOS INT 21h mapping is generated from a target ABI
  # table (test/dos.abi) joined with a libc catalog -- no hand-authored <syscall> blocks --
  # then drives the canonical "Hello, LibCPU!" program. Proves derivation yields a working
  # mapping (incl. signature-driven auto-fill of fputs's FILE* argument with stdout).
  add_test(NAME know.derivemap COMMAND lcx know derive-map
    "${CMAKE_CURRENT_SOURCE_DIR}/test/libc_min.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/test/libc_min.tbd"
    "${CMAKE_CURRENT_SOURCE_DIR}/test/dos.abi")
  set_tests_properties(know.derivemap PROPERTIES FAIL_REGULAR_EXPRESSION "RESULT: FAIL")
endif()

# `make check` (or `cmake --build build --target check`): build the drivers and
# all present CFG backends, then run the matrix with per-failure output.
add_custom_target(check
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running backend-matrix regression harness"
  USES_TERMINAL)
add_dependencies(check ${LIBCPU_CHECK_DEPS})
