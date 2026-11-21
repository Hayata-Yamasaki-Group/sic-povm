# SPDX-License-Identifier: AGPL-3.0-or-later
# Native producer end to end on a reduced-order case (exact/arithmetic/maximal_order.hpp, epsilon = u^k with k > 1).
# Variables: BIN (tool directory), WORK, S (exponent), ORDER, SHA256 (expected exact part), LABEL, WORD, BALLS,
# ANCHOR. Steps: produce with the release option set; the exact part must have digest SHA256; the producer's
# SIC check must pass; sic-decode --finalize must certify every coordinate of the ray against the
# published balls (overlap and identical binary64); sic-verify must pass on the finalized file.
function(run)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "command failed (${status}): ${ARGN}\n${out}\n${err}")
  endif()
  set(last_output "${out}" PARENT_SCOPE)
endfunction()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
run("${BIN}/sic-construct" ${S} --threads 2 --memory-mib 4000 --order ${ORDER}
  --group-cache ${WORK}/group.bin --out ${WORK}/native.sicx)
if(NOT last_output MATCHES "SIC check \\(deviation < 2\\^-100\\): PASS")
  message(FATAL_ERROR "producer SIC check did not pass\n${last_output}")
endif()
if(NOT last_output MATCHES "reduced order: k=[2-9]")
  message(FATAL_ERROR "the case does not exercise k > 1\n${last_output}")
endif()
file(SHA256 "${WORK}/native.sicx" digest)
if(NOT digest STREQUAL "${SHA256}")
  message(FATAL_ERROR "native exact part ${digest} differs from the pinned ${SHA256}")
endif()
file(COPY_FILE "${WORK}/native.sicx" "${WORK}/final.sicx")
run("${BIN}/sic-decode" "${WORK}/final.sicx" "${WORK}/decode" --finalize --ray ${LABEL} ${WORD} ${ANCHOR}
  --threads 2 --no-pin)
message(STATUS "${last_output}")
if(NOT last_output MATCHES "\nPASS" OR NOT last_output MATCHES "\nFINALIZED ")
  message(FATAL_ERROR "finalize did not certify the ray\n${last_output}")
endif()
run("${BIN}/sic-verify" "${WORK}/final.sicx" --threads 2)
