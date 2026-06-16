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

add_executable(aot6502 test/aot6502.cpp aot/AotGenerator.cpp frontends/m6502/Cpu6502.cpp)
target_include_directories(aot6502 PRIVATE frontends/m6502 test aot)
target_link_libraries(aot6502 PRIVATE LibCPU)

add_executable(aotchip8 test/aotchip8.cpp aot/AotGenerator.cpp frontends/chip8/Chip8.cpp)
target_include_directories(aotchip8 PRIVATE frontends/chip8 test aot)
target_link_libraries(aotchip8 PRIVATE LibCPU)

add_executable(aotcfg6502 test/aotcfg6502.cpp aot/AotGenerator.cpp frontends/m6502/Cpu6502.cpp)
target_include_directories(aotcfg6502 PRIVATE frontends/m6502 test aot)
target_link_libraries(aotcfg6502 PRIVATE LibCPU)

add_executable(aotsmc6502 test/aotsmc6502.cpp aot/AotGenerator.cpp frontends/m6502/Cpu6502.cpp)
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
add_executable(tiered6502 test/tiered6502.cpp aot/AotGenerator.cpp core/TieredEngine.cpp core/PerfTrace.cpp core/ProfiledAot.cpp frontends/m6502/Cpu6502.cpp)
target_include_directories(tiered6502 PRIVATE frontends/m6502 test aot core)
target_link_libraries(tiered6502 PRIVATE LibCPU Threads::Threads)

# Knowledge-library demo: a DOS program's in-line INT 21h calls become host libc calls.
add_executable(dossys test/dossys.cpp aot/AotGenerator.cpp core/KnowledgeLibrary.cpp frontends/v20/CpuV20.cpp frontends/i8080/CpuI8080.cpp)
target_include_directories(dossys PRIVATE frontends/v20 frontends/i8080 test aot core)
target_link_libraries(dossys PRIVATE LibCPU)

# Single-step debugger (line editor + gdb-like commands + dual disassembly).
add_executable(lcdbg test/lcdbg.cpp core/Debugger.cpp core/LineEditor.cpp frontends/v20/CpuV20.cpp frontends/i8080/CpuI8080.cpp frontends/m6502/Cpu6502.cpp)
target_include_directories(lcdbg PRIVATE frontends/v20 frontends/i8080 frontends/m6502 test core)
target_link_libraries(lcdbg PRIVATE LibCPU)

# Disk-cached translation: translate once, reload the serialised artifact thereafter.
add_executable(cachedemo test/cachedemo.cpp core/TranslationCache.cpp aot/AotGenerator.cpp frontends/v20/CpuV20.cpp frontends/i8080/CpuI8080.cpp)
target_include_directories(cachedemo PRIVATE frontends/v20 frontends/i8080 test aot core)
target_link_libraries(cachedemo PRIVATE LibCPU)

# System-level emulation: an 8086 machine with a device bus and timer interrupts.
add_executable(sysdemo test/sysdemo.cpp core/System.cpp aot/AotGenerator.cpp frontends/v20/CpuV20.cpp frontends/i8080/CpuI8080.cpp)
target_include_directories(sysdemo PRIVATE frontends/v20 frontends/i8080 test aot core)
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
add_library(upcl SHARED upcl/Diagnostic.cpp upcl/Lexer.cpp upcl/Parser.cpp upcl/UpclArch.cpp)
set_target_properties(upcl PROPERTIES
  OUTPUT_NAME UPCL
  FRAMEWORK TRUE
  FRAMEWORK_VERSION A
  MACOSX_FRAMEWORK_IDENTIFIER org.libcpu.UPCL
  VERSION 1.0.0 SOVERSION 1
  PUBLIC_HEADER "upcl/Diagnostic.h;upcl/Token.h;upcl/Ast.h;upcl/Lexer.h;upcl/Parser.h;upcl/UpclArch.h")
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
add_executable(lcx test/lcx.cpp
  aot/AotGenerator.cpp core/Debugger.cpp core/LineEditor.cpp core/TranslationCache.cpp
  core/KnowledgeLibrary.cpp core/System.cpp core/NativeAot.cpp core/ZooArchive.cpp
  core/SymbolReader.cpp core/HeaderParser.cpp core/DerivationEngine.cpp core/DeviceTree.cpp
  core/DeviceNode.cpp core/MachineBuilder.cpp
  frontends/v20/CpuV20.cpp frontends/i8080/CpuI8080.cpp frontends/m6502/Cpu6502.cpp
  third_party/zstd/zstd.c)
target_include_directories(lcx PRIVATE frontends/v20 frontends/i8080 frontends/m6502 test aot core third_party/zstd)
target_compile_definitions(lcx PRIVATE LIBCPU_HAVE_ZSTD)
target_link_libraries(lcx PRIVATE LibCPU upcl ${CMAKE_DL_LIBS})   # dl for HeaderParser's dlopen(libclang)
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
libcpu_add_device(pcspeaker_device pcspeaker MATCH "ibm,pc-speaker;pc-speaker;speaker" SOURCES devices/pcspeaker/Speaker.cpp)
libcpu_add_device(cassette_device cassette MATCH "ibm,pc-cassette;cassette" SOURCES devices/cassette/Cassette.cpp)
libcpu_add_device(ram_device ram MATCH "libcpu,ram" SOURCES devices/ram/Ram.cpp)
libcpu_add_device(rom_device rom MATCH "ibm,pc-xt-bios;libcpu,rom" SOURCES devices/rom/Rom.cpp)

# ---- device-tree public C API exercised from C (proves the LC surface is C-callable) ----
add_executable(dtapi test/dtcapi.c core/DeviceTreeApi.cpp core/DeviceTree.cpp)
target_include_directories(dtapi PRIVATE core)

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
set(LIBCPU_CHECK_DEPS aotv20 tiered tiered6502)
foreach(_be ${LIBCPU_CFG_BACKENDS})
  if(TARGET ${_be})
    set(_bundle "$<TARGET_BUNDLE_DIR:${_be}>")
    add_test(NAME aotv20.${_be}     COMMAND aotv20 "${_bundle}")
    add_test(NAME tiered.${_be}     COMMAND tiered "${_bundle}" "${_bundle}")
    add_test(NAME tiered6502.${_be} COMMAND tiered6502 "${_bundle}" "${_bundle}")
    # Belt-and-suspenders: fail the test if any demo prints a FAIL line, even
    # should the driver's exit code ever regress.
    set_tests_properties(aotv20.${_be} tiered.${_be} tiered6502.${_be} PROPERTIES
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
  add_test(NAME upcl.translate COMMAND lcx translate "${CMAKE_CURRENT_SOURCE_DIR}/test/add.v20.bin"
    --arch "upcl:${CMAKE_CURRENT_SOURCE_DIR}/test/v20.upcl")
  set_tests_properties(upcl.translate PROPERTIES PASS_REGULAR_EXPRESSION "ax =0008")
endif()

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
   grep -q 'components: 11 matched' '${CMAKE_BINARY_DIR}/machine-match.out'")
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

  # MC146818 RTC/CMOS: a guest selects each clock register through 0x70 and reads it from 0x71;
  # the seeded BCD date/time (12:00:00 on 2026-06-16) comes back through the device bus.
  add_test(NAME machine.rtc COMMAND sh -c
    "'$<TARGET_FILE:lcx>' machine '${CMAKE_CURRENT_SOURCE_DIR}/test/pcvga.dts' --bundles '${CMAKE_BINARY_DIR}' --demo-rtc > '${CMAKE_BINARY_DIR}/rtc.out' && \
     grep -q 'rtc@70 .* mc146818' '${CMAKE_BINARY_DIR}/rtc.out' && \
     grep -q 'RTC/CMOS: 12:00:00  2026-06-16' '${CMAKE_BINARY_DIR}/rtc.out'")
  set_tests_properties(machine.rtc PROPERTIES FAIL_REGULAR_EXPRESSION "lcx machine:")

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
