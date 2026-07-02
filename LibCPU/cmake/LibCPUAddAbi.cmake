# LibCPUAddAbi.cmake -- build a guest-OS ABI personality as a real CFBundle (.abi).
#
# libcpu_add_abi(<target> <outname>
#   MATCH family;alias;...   family name + legacy aliases (--abi <this>) baked into the plist
#   SOURCES ... [INCLUDES ...] [DEFINES ...] [LIBS ...])
#
# The MATCH strings go into the bundle's Info.plist (LCAbiMatch array), which lcx reads to bind
# `--abi family[:version]` to a bundle -- without loading its code (as with LCDeviceMatch). The
# template path is captured at include time (top-level scope) so a personality subdirectory can
# call this function and still resolve the template.

set(LIBCPU_ABI_PLIST_IN ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AbiInfo.plist.in)

function(libcpu_add_abi target outname)
  cmake_parse_arguments(A "" "" "MATCH;SOURCES;INCLUDES;DEFINES;LIBS" ${ARGN})

  set(LCABI_MATCH_ENTRIES "")
  foreach(m IN LISTS A_MATCH)
    string(APPEND LCABI_MATCH_ENTRIES "    <string>${m}</string>\n")
  endforeach()
  set(OUTNAME "${outname}")
  set(_plist ${CMAKE_CURRENT_BINARY_DIR}/${outname}.abi.Info.plist)
  configure_file(${LIBCPU_ABI_PLIST_IN} ${_plist} @ONLY)

  add_library(${target} MODULE ${A_SOURCES})
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME ${outname}
    BUNDLE TRUE
    BUNDLE_EXTENSION "abi"
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    MACOSX_BUNDLE_INFO_PLIST ${_plist})
  # The exported entry (LibCPUModuleCreateAbi) carries LIBCPU_ABI_EXPORT (default visibility), so it
  # survives the hidden preset; the C personality symbols stay bundle-internal.
  if(A_INCLUDES)
    target_include_directories(${target} PRIVATE ${A_INCLUDES})
  endif()
  if(A_DEFINES)
    target_compile_definitions(${target} PRIVATE ${A_DEFINES})
  endif()
  if(A_LIBS)
    target_link_libraries(${target} PRIVATE ${A_LIBS})
  endif()
endfunction()
