# LibCPUUniversal.cmake -- universal (multi-arch) detection + Mach-O lib arch checks.

set(LIBCPU_UNIVERSAL_ARCHS "")
if(APPLE)
  # Every architecture macOS / Mac OS X has ever run on; the probe keeps only
  # those the current toolchain/SDK actually accepts (so a modern Apple Silicon
  # toolchain yields arm64;x86_64, an Intel one i386;x86_64, a PowerPC one ppc...).
  # arm64e is deliberately omitted: it is reserved for platform binaries and
  # third-party arm64e modules will not link/load.
  foreach(_arch arm64 x86_64 i386 ppc64 ppc)
    execute_process(
      COMMAND ${CMAKE_CXX_COMPILER} -arch ${_arch} -x c++ -c
              ${CMAKE_CURRENT_SOURCE_DIR}/cmake/archprobe.cpp
              -o ${CMAKE_BINARY_DIR}/.archprobe_${_arch}.o
      RESULT_VARIABLE _rc ERROR_QUIET OUTPUT_QUIET)
    if(_rc EQUAL 0)
      list(APPEND LIBCPU_UNIVERSAL_ARCHS ${_arch})
    endif()
  endforeach()
  if(NOT LIBCPU_UNIVERSAL_ARCHS)
    set(LIBCPU_UNIVERSAL_ARCHS arm64)
  endif()
  message(STATUS "LibCPU: toolchain universal arches = ${LIBCPU_UNIVERSAL_ARCHS}")
  set(CMAKE_OSX_ARCHITECTURES "${LIBCPU_UNIVERSAL_ARCHS}")
endif()

# libcpu_lib_arches(<out> <dylib>): arches the dylib provides intersected with universal set.
function(libcpu_lib_arches outvar libpath)
  set(${outvar} "" PARENT_SCOPE)
  if(NOT EXISTS "${libpath}")
    return()
  endif()
  execute_process(COMMAND lipo -archs "${libpath}"
    OUTPUT_VARIABLE _archs RESULT_VARIABLE _rc ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _rc EQUAL 0)
    return()
  endif()
  separate_arguments(_archs)
  set(_isect "")
  foreach(_a ${LIBCPU_UNIVERSAL_ARCHS})
    if(_a IN_LIST _archs)
      list(APPEND _isect ${_a})
    endif()
  endforeach()
  set(${outvar} "${_isect}" PARENT_SCOPE)
endfunction()
