# SPDX-License-Identifier: AGPL-3.0-or-later
execute_process(COMMAND "${TOOL}" --version RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0 OR NOT err STREQUAL "")
  message(FATAL_ERROR "--version failed: ${rc}: ${err}")
endif()
foreach(field schema version name source_commit)
  string(JSON value ERROR_VARIABLE error GET "${out}" "${field}")
  if(error)
    message(FATAL_ERROR "--version missing ${field}: ${out}")
  endif()
  set("version_${field}" "${value}")
endforeach()
if(NOT version_schema STREQUAL "sic-tool-version/1.0" OR
   NOT version_version STREQUAL "1.0.0" OR NOT version_name STREQUAL NAME)
  message(FATAL_ERROR "Unsupported tool identity: ${out}")
endif()
string(LENGTH "${version_source_commit}" length)
if(NOT version_source_commit MATCHES "^[0-9a-f]+$" OR
   NOT (length EQUAL 40 OR length EQUAL 64))
  message(FATAL_ERROR "Invalid source commit: ${out}")
endif()
execute_process(COMMAND "${TOOL}" --version --help RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 2 OR NOT out STREQUAL "")
  message(FATAL_ERROR "--version accepted extra arguments: ${rc}: ${out}: ${err}")
endif()
execute_process(COMMAND "${TOOL}" --version RESULT_VARIABLE rc OUTPUT_FILE /dev/full ERROR_VARIABLE err)
if(NOT rc EQUAL 1 OR NOT err MATCHES ": \\[io\\] stdout write failed")
  message(FATAL_ERROR "--version ignored stdout write failure: ${rc}: ${err}")
endif()
# Keep the write end open until after the only reader has exited.
execute_process(COMMAND bash -ec [=[
  coproc sink { read -r done; }
  pid=$sink_PID
  exec 3>&"${sink[1]}"
  printf '\n' >&3
  wait "$pid"
  exec "$1" --version >&3
]=] bash "${TOOL}" RESULT_VARIABLE rc ERROR_VARIABLE err)
if(NOT rc EQUAL 1 OR NOT err MATCHES ": \\[io\\] stdout write failed")
  message(FATAL_ERROR "--version failed to classify a closed stdout pipe: ${rc}: ${err}")
endif()
