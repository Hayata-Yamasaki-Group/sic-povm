# SPDX-License-Identifier: AGPL-3.0-or-later
include("${CMAKE_CURRENT_LIST_DIR}/../../data/generate.cmake")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
if(NAME STREQUAL "d2")
  sic_generate_d2("${BIN}" "${WORK}")
else()
  set(options)
  if(NAME STREQUAL "d64")
    list(APPEND options 1 "${WORK}/verifier-budget.sicx")
  endif()
  sic_generate_wh("${BIN}" "${WORK}" "${EXPONENT}" "${ORDER}" ${options})
  file(RENAME "${WORK}/multiplet.sicx" "${WORK}/${NAME}.sicx")
endif()

if(NAME STREQUAL "d64")
  # Fixed verifier-budget test vector: the archived d64 words and gauge anchors
  # preserve the mixed 91/92-step boundary. The ordinary fixture uses the
  # producer ray list and automatic anchors.
  sic_run("${BIN}/sic-decode" "${WORK}/verifier-budget.sicx" "${WORK}/verifier-budget"
    --threads 1 --no-pin --finalize
    --ray c0 1,0,0,24,11 36 --ray c1 1,0,0,24,12 29
    --ray c2 1,1,0,24,11 44 --ray c3 1,1,0,24,12 33)
endif()

if(NAME STREQUAL "d4")
  sic_run("${BIN}/sic-inspect" "${WORK}/d4.sicx" --json-rays "${WORK}/rays" --multiplet m1)
  sic_run("${BIN}/sic-inspect" "${WORK}/d4.sicx" --text-json "${WORK}/d4.jsonl" --threads 1)
endif()

if(EXISTS "${ORACLES}/canonical/${NAME}.provenance.json")
  file(READ "${ORACLES}/canonical/${NAME}.provenance.json" oracle)
  file(READ "${WORK}/exact.sha256" hash)
  string(STRIP "${hash}" hash)
  string(JSON want GET "${oracle}" native_sha256)
  if(NOT hash STREQUAL want)
    message(FATAL_ERROR "generated ${NAME} exact bytes differ from the archive: ${hash} vs ${want}")
  endif()
endif()
