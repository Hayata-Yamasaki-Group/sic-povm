# SPDX-License-Identifier: AGPL-3.0-or-later
# Run one native golden case with sic-construct and compare the .sicx bytes with the stored SHA-256.
# -DPROGRAM=<sic-construct> -DCASE_OPTIONS="--option value ..." -DCASE_ARGS="arg ..." -DEXPECTED=<sha256> -DWORK=<dir>
foreach(var PROGRAM CASE_OPTIONS CASE_ARGS EXPECTED WORK)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "run_case.cmake: ${var} is required")
  endif()
endforeach()
separate_arguments(CASE_OPTIONS UNIX_COMMAND "${CASE_OPTIONS}")
separate_arguments(CASE_ARGS UNIX_COMMAND "${CASE_ARGS}")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(output "${WORK}/out.sicx")
set(case_environment)
set(case_cli)
foreach(option IN LISTS CASE_OPTIONS)
  if(option MATCHES "^SIC_DEV_[A-Z_]+=.+$")
    list(APPEND case_environment "${option}")
  else()
    list(APPEND case_cli "${option}")
  endif()
endforeach()
# A clean environment (as in generate-goldens.sh): inherited SIC_* variables must not leak in.
find_program(ENV_PROGRAM env REQUIRED)
execute_process(
  COMMAND "${ENV_PROGRAM}" -i "PATH=$ENV{PATH}" "HOME=$ENV{HOME}" "SIC_THREADS=1" "OMP_NUM_THREADS=1" "OPENBLAS_NUM_THREADS=1" "ASAN_OPTIONS=$ENV{ASAN_OPTIONS}"
    "UBSAN_OPTIONS=$ENV{UBSAN_OPTIONS}" "TSAN_OPTIONS=$ENV{TSAN_OPTIONS}"
    ${case_environment} "${PROGRAM}" ${CASE_ARGS} ${case_cli} --out "${output}"
  RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
file(WRITE "${WORK}/stdout.log" "${stdout}")
file(WRITE "${WORK}/stderr.log" "${stderr}")
if(NOT status EQUAL 0 OR NOT stdout MATCHES "SIC check [^\n]*PASS" OR NOT stdout MATCHES "SICX exact part written")
  message(FATAL_ERROR "native producer failed (exit ${status}):\n${stderr}")
endif()
file(GLOB leftovers "${output}.*")
if(leftovers)
  message(FATAL_ERROR "partial/journal files left next to the output: ${leftovers}")
endif()
file(SHA256 "${output}" actual)
if(NOT actual STREQUAL EXPECTED)
  message(FATAL_ERROR "SICX bytes differ from the golden: ${actual} != ${EXPECTED} (${output})")
endif()
file(REMOVE "${output}")
message(STATUS "golden match ${actual}")
