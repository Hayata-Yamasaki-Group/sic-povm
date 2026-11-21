# SPDX-License-Identifier: AGPL-3.0-or-later
# Runs sic-decode in a fresh WORK directory; the tool itself fails unless every coordinate
# overlaps the BALL chunk and the certified binary64 values are identical.
file(REMOVE_RECURSE "${WORK}")
separate_arguments(args UNIX_COMMAND "${ARGS}")
execute_process(COMMAND "${CMD}" "${INPUT}" "${WORK}" ${args} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}")
if(NOT status EQUAL 0 OR NOT out MATCHES "\nPASS")
  message(FATAL_ERROR "decode failed (${status})\n${out}\n${err}")
endif()
