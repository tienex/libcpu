# Run sc2int on gate.sc and assert the emitted descriptors carry the right NIX_VERSION ranges.
execute_process(COMMAND ${SC2INT} ${SC} WORKING_DIRECTORY ${WORKDIR} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "sc2int failed on gate.sc (rc=${rc})")
endif()
file(READ ${OUT} txt)
foreach(needle
    "NIX_VERSION (2, 0, 0), NIX_VERSION_NONE"          # write @2.0
    "NIX_VERSION (2, 0, 0), NIX_VERSION (3, 6, 0)"     # oldcall @2.0..3.6
    "NIX_VERSION (7, 4, 0), NIX_VERSION_NONE"          # pinsyscall @7.4..
    "NULL, NIX_VERSION_NONE, NIX_VERSION_NONE")        # exit (no @) + gap entries
  string(FIND "${txt}" "${needle}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "gate.sc output missing: ${needle}")
  endif()
endforeach()
