# LibCPUAddDevice.cmake -- build a hardware component as a real CFBundle (.device).
#
# libcpu_add_device(<target> <outname>
#   MATCH a;b;...        device-tree "compatible" strings this component binds to
#   SOURCES ... [INCLUDES ...] [DEFINES ...] [LIBS ...])
#
# The MATCH strings are baked into the bundle's Info.plist (LCDeviceMatch array), which the
# machine builder reads to bind the component to device-tree nodes -- without loading its code.

set(LIBCPU_DEVICE_PLIST_IN ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeviceInfo.plist.in)

function(libcpu_add_device target outname)
  cmake_parse_arguments(D "" "" "MATCH;SOURCES;INCLUDES;DEFINES;LIBS" ${ARGN})

  set(LCDEVICE_MATCH_ENTRIES "")
  foreach(m IN LISTS D_MATCH)
    string(APPEND LCDEVICE_MATCH_ENTRIES "    <string>${m}</string>\n")
  endforeach()
  set(OUTNAME "${outname}")
  set(_plist ${CMAKE_CURRENT_BINARY_DIR}/${outname}.device.Info.plist)
  configure_file(${LIBCPU_DEVICE_PLIST_IN} ${_plist} @ONLY)

  add_library(${target} MODULE ${D_SOURCES})
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME ${outname}
    BUNDLE TRUE
    BUNDLE_EXTENSION "device"
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    MACOSX_BUNDLE_INFO_PLIST ${_plist})
  if(D_INCLUDES)
    target_include_directories(${target} PRIVATE ${D_INCLUDES})
  endif()
  if(D_DEFINES)
    target_compile_definitions(${target} PRIVATE ${D_DEFINES})
  endif()
  if(D_LIBS)
    target_link_libraries(${target} PRIVATE ${D_LIBS})
  endif()
endfunction()
