# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
# A valid last path must not hide a surplus missing or empty first path.
foreach(_first "/nonexistent.sicx" "${FIXTURE}" "")
  execute_process(COMMAND "${CHECKER}" "${_first}" "${FIXTURE}" --decode off
    RESULT_VARIABLE _status OUTPUT_VARIABLE _out ERROR_VARIABLE _err TIMEOUT 5)
  if(NOT _status EQUAL 2 OR NOT _err MATCHES "expected at most one input path" OR _out MATCHES "PASS")
    message(FATAL_ERROR "Surplus path accepted: ${_status}: ${_out}${_err}")
  endif()
endforeach()
execute_process(COMMAND "${CHECKER}" "${FIXTURE}" --decode off
  RESULT_VARIABLE _status OUTPUT_VARIABLE _out ERROR_VARIABLE _err TIMEOUT 5)
if(NOT _status EQUAL 0 OR NOT _out MATCHES "PASS")
  message(FATAL_ERROR "Single path refused: ${_status}: ${_out}${_err}")
endif()
