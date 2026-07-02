# LibCPUAddLoader.cmake -- helper to build an executable-format loader as a real CFBundle (.loader).
#
# libcpu_add_loader(<target> <outname>
#   [ARCHS a;b] SOURCES ... [INCLUDES ...] [DEFINES ...] [LIBS ...] [RPATH ...])

set(LIBCPU_LOADER_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/LoaderInfo.plist.in)

function(libcpu_add_loader target outname)
  # ARCHS is multi-value: a one-value keyword would capture only the first arch (see the backend
  # helper for the ';'-re-split gotcha).
  cmake_parse_arguments(L "" "" "ARCHS;SOURCES;INCLUDES;DEFINES;LIBS;RPATH" ${ARGN})
  add_library(${target} MODULE ${L_SOURCES})
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME ${outname}
    BUNDLE TRUE
    BUNDLE_EXTENSION "loader"
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    MACOSX_BUNDLE_INFO_PLIST ${LIBCPU_LOADER_PLIST}
    MACOSX_BUNDLE_BUNDLE_NAME ${outname}
    MACOSX_BUNDLE_GUI_IDENTIFIER org.libcpu.loader.${outname}
    MACOSX_BUNDLE_BUNDLE_VERSION 1.0
    MACOSX_BUNDLE_SHORT_VERSION_STRING 1.0)
  if(L_ARCHS)
    set_target_properties(${target} PROPERTIES OSX_ARCHITECTURES "${L_ARCHS}")
  endif()
  if(L_INCLUDES)
    target_include_directories(${target} PRIVATE ${L_INCLUDES})
  endif()
  if(L_DEFINES)
    target_compile_definitions(${target} PRIVATE ${L_DEFINES})
  endif()
  if(L_LIBS)
    target_link_libraries(${target} PRIVATE ${L_LIBS})
  endif()
  if(L_RPATH)
    set_target_properties(${target} PROPERTIES BUILD_RPATH "${L_RPATH}")
  endif()
endfunction()
