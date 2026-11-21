# Test public full/partial CLI paths using the independent test writer, never the production producer.
execute_process(COMMAND "${WRITER}" --write-fixtures "${OUTPUT}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "fixture writer failed: ${rc}")
endif()
set(input "${OUTPUT}/d8-final.sicx")
file(SIZE "${input}" size)
math(EXPR exact_offset "${size}-112+32")
math(EXPR file_offset "${size}-112+64")
file(READ "${input}" exact OFFSET ${exact_offset} LIMIT 32 HEX)
file(READ "${input}" root OFFSET ${file_offset} LIMIT 32 HEX)
# The generated fixture is this test's mock trusted catalogue; a real server footer is not a trust source.
execute_process(COMMAND "${READER}" "${input}" --exact-root "${exact}" --file-root "${root}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0 OR NOT out MATCHES "FULL verification; exact root trusted [(]direct exact-root match[)]; file root trusted [(]direct file-root match[)]")
  message(FATAL_ERROR "full CLI failed: ${rc}: ${out}${err}")
endif()
execute_process(COMMAND "${READER}" "${input}" --exact-root "${exact}" --partial-chunk 1
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0 OR NOT out MATCHES "PARTIAL verification; exact root trusted [(]direct exact-root match[)]; file root self-consistent, untrusted")
  message(FATAL_ERROR "partial CLI failed: ${rc}: ${out}${err}")
endif()
# A bad expected root must stop the mathematical caller at its input boundary.
execute_process(COMMAND "${CERTIFICATE}" "${input}" --exact-root
  0000000000000000000000000000000000000000000000000000000000000000
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 1 OR NOT out MATCHES "trusted exact root mismatch")
  message(FATAL_ERROR "CRT trusted-root CLI failed: ${rc}: ${out}${err}")
endif()
execute_process(COMMAND "${WRITER}" --write-general-fixtures "${OUTPUT}/general" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "general fixture writer failed: ${rc}")
endif()
foreach(d 3 5)
  set(general "${OUTPUT}/general/d${d}-synthetic-exact.sicx")
  execute_process(COMMAND "${READER}" "${general}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0 OR NOT out MATCHES "FULL verification")
    message(FATAL_ERROR "general container CLI failed: ${rc}: ${out}${err}")
  endif()
  execute_process(COMMAND "${CERTIFICATE}" "${general}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 1 OR NOT out MATCHES "CRT certificate requires d = 2\\^s >= 4")
    message(FATAL_ERROR "general CRT scope refusal failed: ${rc}: ${out}${err}")
  endif()
endforeach()
message(STATUS "PASS CLI: 7 assertions (full/partial trust, CRT trust rejection, general container/CRT scopes)")
