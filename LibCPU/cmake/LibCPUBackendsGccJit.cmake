# LibCPUBackendsGccJit.cmake -- discover every libgccjit (Homebrew + MacPorts) and
# build one gccjit[-<gccmajor>].backend per distinct library.

function(libcpu_discover_gccjit srcdir)
  file(GLOB _libs
    /opt/homebrew/lib/gcc/*/libgccjit.dylib
    /usr/local/lib/gcc/*/libgccjit.dylib
    /opt/local/lib/gcc*/libgccjit.dylib
    /opt/local/lib/libgccjit.dylib)
  find_path(GCCJIT_INC libgccjit.h PATHS /opt/homebrew/include /usr/local/include /opt/local/include)
  set(_seen "")
  foreach(_glib ${_libs})
    get_filename_component(_real "${_glib}" REALPATH)
    if(NOT GCCJIT_INC OR _real IN_LIST _seen)
      continue()
    endif()
    libcpu_lib_arches(_archs "${_real}")
    if(_archs STREQUAL "")
      continue()
    endif()
    list(APPEND _seen ${_real})
    get_filename_component(_gdir "${_glib}" DIRECTORY)
    get_filename_component(_gname "${_gdir}" NAME)
    if(_gname MATCHES "^[0-9]+$")
      set(_gout "gccjit-${_gname}")
    else()
      set(_gout "gccjit")
    endif()
    get_filename_component(_grpath "${_real}" DIRECTORY)
    string(MAKE_C_IDENTIFIER "${_gout}" _gtarget)
    message(STATUS "LibCPU: libgccjit [${_archs}] -> ${_gout}.backend (${_real})")
    libcpu_add_backend(${_gtarget}_backend "${_gout}"
      ARCHS "${_archs}"
      SOURCES ${srcdir}/backends/gccjit/GccJitBackend.cpp ${srcdir}/backends/gccjit/GccJitModule.cpp
      INCLUDES ${srcdir}/backends/gccjit ${GCCJIT_INC}
      LIBS "${_real}"
      RPATH "${_grpath}")
  endforeach()
endfunction()
