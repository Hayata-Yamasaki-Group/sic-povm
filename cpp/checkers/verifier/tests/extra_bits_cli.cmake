foreach(bits 0 400 1000)
  execute_process(COMMAND "${CHECKER}" "${FIXTURE}" --decode full --extra-bits "${bits}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
  if(NOT rc EQUAL 0 OR NOT out MATCHES "RESULT: PASS" OR NOT out MATCHES "[(]prec ([0-9]+)[)]")
    message(FATAL_ERROR "decode failed: ${rc} ${out} ${err}")
  endif()
  set(prec_${bits} "${CMAKE_MATCH_1}")
endforeach()
if(NOT prec_1000 GREATER prec_0)
  message(FATAL_ERROR "extra bits did not increase decode precision")
endif()
foreach(bad invalid -1 1000000000000000000)
  execute_process(COMMAND "${CHECKER}" "${FIXTURE}" --extra-bits "${bad}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
  if(NOT rc EQUAL 2 OR out MATCHES "RESULT: PASS")
    message(FATAL_ERROR "invalid extra-bits accepted: ${bad} ${rc} ${out} ${err}")
  endif()
endforeach()

execute_process(COMMAND "${CHECKER}" "${FIXTURE}" --extra-bits
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
if(NOT rc EQUAL 2 OR out MATCHES "RESULT: PASS|PROVED")
  message(FATAL_ERROR "missing extra-bits value: ${rc} ${out} ${err}")
endif()

# Omitted extra-bits keeps the same decode precision as explicit 400.
execute_process(COMMAND "${CHECKER}" "${FIXTURE}" --decode full
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
if(NOT rc EQUAL 0 OR NOT out MATCHES "[(]prec ([0-9]+)[)]")
  message(FATAL_ERROR "default decode failed: ${rc} ${out} ${err}")
endif()
if(NOT "${CMAKE_MATCH_1}" STREQUAL "${prec_400}")
  message(FATAL_ERROR "omitted extra-bits differs from explicit 400")
endif()
foreach(option --text --decode --max-steps --extra-bits --ray --partial-element --expect-exact-root --expect-file-root --expect-sha256 --branch-degree-limit)
  execute_process(COMMAND "${CHECKER}" "${FIXTURE}" "${option}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
  if(NOT rc EQUAL 2 OR out MATCHES "RESULT: PASS|PROVED")
    message(FATAL_ERROR "missing value: ${option}: ${rc} ${out} ${err}")
  endif()
endforeach()
