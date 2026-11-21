# SPDX-License-Identifier: AGPL-3.0-or-later
# Bounded integration regression, run outside CTest's two-minute unit limit.
# d256 is the smallest maximal-order construction with an odd tower prefix.
# PROGRAM must be a SIC_ENABLE_TEST_HOOKS=ON producer; WORK must be fresh.
if(EXISTS "${WORK}")
  message(FATAL_ERROR "trace-band regression requires a fresh WORK directory")
endif()
file(MAKE_DIRECTORY "${WORK}")
foreach(bits 0 1)
  set(cap 100000)
  if(bits EQUAL 1)
    set(cap 1800)
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    SIC_DEV_SPILL=1 "SIC_TEST_REP_BAND_BITS=${bits}"
    "${PROGRAM}" 8 --order maximal --threads 32 --memory-mib "${cap}" --progress
    --out "${WORK}/band${bits}.sicx"
    RESULT_VARIABLE status OUTPUT_FILE "${WORK}/band${bits}.stdout"
    ERROR_FILE "${WORK}/band${bits}.stderr" TIMEOUT 1200)
  file(READ "${WORK}/band${bits}.stdout" out)
  if(NOT status EQUAL 0 OR NOT out MATCHES "SIC check [^\n]*PASS" OR
     NOT out MATCHES "tower: [^\n]*degrees: 3 2")
    message(FATAL_ERROR "mixed-radix fallback regression failed: bits=${bits}, status=${status}")
  endif()
  if(bits EQUAL 1 AND NOT out MATCHES "Newton accelerator unavailable:")
    message(FATAL_ERROR "the constrained run did not exercise quadrature fallback")
  endif()
  if(bits EQUAL 1 AND NOT out MATCHES "representative band: [^\n]*band=0/2")
    message(FATAL_ERROR "the forced banded path was not observed")
  endif()
  file(SHA256 "${WORK}/band${bits}.sicx" digest${bits})
endforeach()
if(NOT digest0 STREQUAL digest1)
  message(FATAL_ERROR "mixed-radix banded/unbanded native bytes differ")
endif()
file(WRITE "${WORK}/identity.sha256" "${digest0}  band0.sicx=band1.sicx\n")
