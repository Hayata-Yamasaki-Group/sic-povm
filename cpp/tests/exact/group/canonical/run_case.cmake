# SPDX-License-Identifier: AGPL-3.0-or-later
# Compare the SHA-256 of `sic-test-exact-group-metadata CASE` stdout with its golden (goldens.sha256).
# -DPROGRAM=<sic-test-exact-group-metadata> -DCASE=<case> -DEXPECTED=<sha256>
find_program(ENV_PROGRAM env REQUIRED)
execute_process(COMMAND "${ENV_PROGRAM}" -i "PATH=$ENV{PATH}" "${PROGRAM}" "${CASE}"
  RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "${CASE} failed (exit ${status}):\n${stderr}")
endif()
string(SHA256 actual "${stdout}")
if(NOT actual STREQUAL EXPECTED)
  message(FATAL_ERROR "${CASE} metadata differs from the golden: ${actual} != ${EXPECTED}\n${stdout}")
endif()
message(STATUS "golden match ${actual}")
