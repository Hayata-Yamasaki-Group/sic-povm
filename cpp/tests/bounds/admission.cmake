# SPDX-License-Identifier: AGPL-3.0-or-later
file(MAKE_DIRECTORY "${WORK}")
foreach(d 2 1000000000000)
  file(WRITE "${WORK}/input.txt" "sic-vector-balls/1.0\nd ${d}\n")
  execute_process(COMMAND "${TOOL}" "${WORK}/input.txt"
    --maximum-precision 144115188075855168 --working-bits 144115188075855168
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc STREQUAL "1")
    message(FATAL_ERROR "ball admission did not refuse safely: ${rc} ${out}${err}")
  endif()
endforeach()
