# LibCPUBackendsLLVM.cmake -- discover every LLVM (Homebrew + MacPorts) and build
# one llvm-<major>.backend per version, for the arches its libLLVM provides.

function(libcpu_discover_llvm srcdir)
  file(GLOB _configs
    /opt/homebrew/opt/llvm/bin/llvm-config
    /opt/homebrew/opt/llvm@*/bin/llvm-config
    /usr/local/opt/llvm/bin/llvm-config
    /usr/local/opt/llvm@*/bin/llvm-config
    /opt/local/libexec/llvm-*/bin/llvm-config
    /opt/local/bin/llvm-config-mp-*)
  set(_seen "")
  foreach(_cfg ${_configs})
    execute_process(COMMAND ${_cfg} --version
      OUTPUT_VARIABLE _ver RESULT_VARIABLE _rc ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _rc EQUAL 0)
      continue()
    endif()
    string(REGEX REPLACE "[^0-9].*" "" _maj "${_ver}")
    if(_maj STREQUAL "" OR _maj IN_LIST _seen)
      continue()
    endif()
    execute_process(COMMAND ${_cfg} --includedir OUTPUT_VARIABLE _inc OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${_cfg} --libdir     OUTPUT_VARIABLE _lib OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(_dylib "${_lib}/libLLVM-${_maj}.dylib")
    if(NOT EXISTS "${_dylib}")
      set(_dylib "${_lib}/libLLVM.dylib")
    endif()
    libcpu_lib_arches(_archs "${_dylib}")
    if(_archs STREQUAL "")
      message(STATUS "LibCPU: LLVM ${_ver} skipped (no usable dylib arch)")
      continue()
    endif()
    list(APPEND _seen ${_maj})
    message(STATUS "LibCPU: LLVM ${_ver} [${_archs}] -> llvm-${_maj}.backend")
    libcpu_add_backend(llvm${_maj}_backend "llvm-${_maj}"
      ARCHS "${_archs}"
      SOURCES ${srcdir}/backends/llvm/LlvmBackend.cpp ${srcdir}/backends/llvm/LlvmModule.cpp
      INCLUDES ${srcdir}/backends/llvm ${_inc}
      DEFINES __STDC_LIMIT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS
      LIBS "${_dylib}"
      RPATH "${_lib}")
  endforeach()
endfunction()
