# LibCPUAddBackend.cmake -- helper to build a backend as a real CFBundle (.backend).
#
# libcpu_add_backend(<target> <outname>
#   [ARCHS a;b] SOURCES ... [INCLUDES ...] [DEFINES ...] [LIBS ...] [RPATH ...])

set(LIBCPU_BACKEND_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/BackendInfo.plist.in)

function(libcpu_add_backend target outname)
  cmake_parse_arguments(B "" "ARCHS" "SOURCES;INCLUDES;DEFINES;LIBS;RPATH" ${ARGN})
  add_library(${target} MODULE ${B_SOURCES})
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME ${outname}
    BUNDLE TRUE
    BUNDLE_EXTENSION "backend"
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    MACOSX_BUNDLE_INFO_PLIST ${LIBCPU_BACKEND_PLIST}
    MACOSX_BUNDLE_BUNDLE_NAME ${outname}
    MACOSX_BUNDLE_GUI_IDENTIFIER org.libcpu.backend.${outname}
    MACOSX_BUNDLE_BUNDLE_VERSION 1.0
    MACOSX_BUNDLE_SHORT_VERSION_STRING 1.0)
  if(B_ARCHS)
    set_target_properties(${target} PROPERTIES OSX_ARCHITECTURES "${B_ARCHS}")
  endif()
  if(B_INCLUDES)
    target_include_directories(${target} PRIVATE ${B_INCLUDES})
  endif()
  if(B_DEFINES)
    target_compile_definitions(${target} PRIVATE ${B_DEFINES})
  endif()
  if(B_LIBS)
    target_link_libraries(${target} PRIVATE ${B_LIBS})
  endif()
  if(B_RPATH)
    set_target_properties(${target} PROPERTIES BUILD_RPATH "${B_RPATH}")
  endif()
endfunction()
