# Run schistory over three fixture releases (given out of order to prove the internal sort) and
# assert the emitted .sc carries the right @since..until ranges and arg types.
execute_process(COMMAND ${SCHISTORY}
    --family gatefix --name "Gate Fixture" --bae EFAULT --out ${OUT}
    7.9=${DIR}/rel-7.9.master 2.0=${DIR}/rel-2.0.master 3.6=${DIR}/rel-3.6.master
  RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "schistory failed (rc=${rc})")
endif()
file(READ ${OUT} txt)
foreach(needle
    "4 intptr write (word, ptr, intptr) @2.0"
    "42 word oldcall (word) @2.0..3.6"
    "73 word munmap (ptr, intptr) @3.6"
    "336 word pinsyscall (word, ptr, intptr) @7.9")
  string(FIND "${txt}" "${needle}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "schistory output missing: ${needle}\n--- got ---\n${txt}")
  endif()
endforeach()
