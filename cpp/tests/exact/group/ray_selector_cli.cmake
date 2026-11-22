# SPDX-License-Identifier: AGPL-3.0-or-later
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
function(invoke expected diagnostic)
  execute_process(COMMAND "${SELECTOR}" ${ARGN}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT status STREQUAL "${expected}" OR NOT errors MATCHES "${diagnostic}")
    message(FATAL_ERROR "sic-ray-selector ${ARGN} returned ${status}: ${errors}")
  endif()
  set(output "${output}" PARENT_SCOPE)
endfunction()

function(refuse expected diagnostic)
  invoke("${expected}" "${diagnostic}" ${ARGN} --receipt "${WORK}/refused.json")
  if(EXISTS "${WORK}/refused.json" OR NOT output STREQUAL "")
    message(FATAL_ERROR "Failed invocation published output: ${ARGN}")
  endif()
endfunction()

# Exercise the process CPU allowance and the explicit override on a one-CPU allocation.
set(ENV{SLURM_CPUS_PER_TASK} 1)
invoke(0 "^$" --version)
set(version_json "${output}")
invoke(0 "^$" derive --dimension-two --receipt "${WORK}/default.json")
if(NOT output STREQUAL "{\"smith\":[2],\"words\":[\"0\"]}\n")
  message(FATAL_ERROR "Unexpected d2 output: ${output}")
endif()
file(READ "${WORK}/default.json" receipt)
string(JSON threads GET "${receipt}" threads)
if(NOT threads STREQUAL "1")
  message(FATAL_ERROR "Default threads did not honor the process CPU allowance: ${threads}")
endif()
foreach(field IN ITEMS version source_commit)
  string(JSON expected GET "${version_json}" "${field}")
  string(JSON actual GET "${receipt}" "${field}")
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Receipt ${field} differs from --version: ${actual}")
  endif()
endforeach()
invoke(0 "^$" derive --dimension-two --threads 2 --receipt "${WORK}/explicit.json")
file(READ "${WORK}/explicit.json" receipt)
string(JSON threads GET "${receipt}" threads)
if(NOT threads STREQUAL "2")
  message(FATAL_ERROR "Explicit threads did not override the process CPU allowance: ${threads}")
endif()
# A receipt publication error must leave stdout empty and preserve the existing receipt.
invoke(1 "\\[io\\]" derive --dimension-two --threads 1 --receipt "${WORK}/explicit.json")
file(READ "${WORK}/explicit.json" existing)
if(NOT output STREQUAL "" OR NOT existing STREQUAL receipt)
  message(FATAL_ERROR "Receipt publication failure changed output")
endif()
foreach(input IN ITEMS
    [[{"words":["0"],"smith":[2],"map":{"source":"metadata"}}]]
    [[{"words":["0"],"smith":[2],"smithSource":{"path":"metadata"}}]])
  file(WRITE "${WORK}/words.json" "${input}\n")
  invoke(0 "^$" check --dimension-two --threads 1 --words "${WORK}/words.json")
endforeach()
foreach(input IN ITEMS
    [[{"smith":[2],"words":["1"]}]]
    [[{"smith":[2],"words":["-0"]}]]
    [[{"smith":[2],"words":["00"]}]]
    [[{"smith":["2"],"words":["0"]}]]
    [[{"smith":[2],"words":["0"],"words":["0"]}]]
    [[{"smith":[2],"words":["0","0"]}]])
  file(WRITE "${WORK}/words.json" "${input}\n")
  refuse(1 "invalid_format" check --dimension-two --threads 1 --words "${WORK}/words.json")
endforeach()
refuse(1 "resource" derive --dimension-two --threads 1 --memory-mib 1)
refuse(2 "invalid_argument" derive --dimension-two --threads 0)
refuse(2 "invalid_argument" derive --dimension-two --threads 1025)
refuse(2 "invalid_argument" derive --dimension-two --group irrelevant --threads 1)
refuse(2 "invalid_argument" derive --dimension-two --threads 1 --words irrelevant)

if(NOT DEFINED PRODUCER)
  return()
endif()
execute_process(COMMAND "${PRODUCER}" 2 --out "${WORK}/d4.sicx"
  --group-cache "${WORK}/group.bin" --threads 1 --progress
  RESULT_VARIABLE status OUTPUT_VARIABLE producer_output ERROR_VARIABLE producer_errors)
if(NOT status STREQUAL "0")
  message(FATAL_ERROR "d4 checkpoint producer returned ${status}: ${producer_output}\n${producer_errors}")
endif()
if(NOT "${producer_output}\n${producer_errors}" MATCHES
    "group closure complete \\[([0-9]+\\.[0-9])s\\].*validated exact group checkpoint saved: [^\n]* \\[([0-9]+\\.[0-9])s\\]" OR
    CMAKE_MATCH_2 LESS CMAKE_MATCH_1)
  message(FATAL_ERROR "Missing or unordered Group checkpoint save stamps")
endif()
set(group "${WORK}/group.bin")
invoke(0 "^$" derive --group "${group}" --threads 1 --receipt "${WORK}/receipt.json")
if(NOT output STREQUAL "{\"smith\":[2,2,2],\"words\":[\"0,0,0\"]}\n")
  message(FATAL_ERROR "Unexpected d4 output: ${output}")
endif()
string(SHA256 hash "${output}")
file(READ "${WORK}/receipt.json" receipt)
string(JSON receipt_hash GET "${receipt}" output_sha256)
if(NOT hash STREQUAL receipt_hash)
  message(FATAL_ERROR "Receipt failed to bind exact stdout")
endif()
file(WRITE "${WORK}/words.json" "${output}")
file(SHA256 "${group}" digest)
invoke(0 "^$" check --group "${group}" --sha256 "${digest}" --threads 1
  --words "${WORK}/words.json")
string(REPEAT "0" 64 wrong_digest)
refuse(1 "integrity.*SHA-256 verification failed" derive --group "${group}"
  --sha256 "${wrong_digest}" --threads 1)

function(copy_bytes)
  execute_process(COMMAND dd ${ARGN} RESULT_VARIABLE status ERROR_VARIABLE errors)
  if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Cannot prepare Group refusal fixture: ${errors}")
  endif()
endfunction()

# SICGROUP v1 starts with 14 uint64 fields and the uint64 source length.
math(EXPR source_offset "(14 + 1) * 8")
configure_file("${group}" "${WORK}/wrong-source.bin" COPYONLY)
file(WRITE "${WORK}/source-byte" "g") # CMake fingerprints start with a hex digit.
copy_bytes("if=${WORK}/source-byte" "of=${WORK}/wrong-source.bin" bs=1
  "seek=${source_offset}" conv=notrunc)
file(SHA256 "${WORK}/wrong-source.bin" wrong_source_pin)
refuse(1 "integrity.*source mismatch" derive --group "${WORK}/wrong-source.bin"
  --sha256 "${wrong_source_pin}" --threads 1)

file(SIZE "${group}" bytes)
math(EXPR short_bytes "${bytes} - 1")
copy_bytes("if=${group}" "of=${WORK}/truncated.bin" bs=1 "count=${short_bytes}")
file(SHA256 "${WORK}/truncated.bin" truncated_pin)
refuse(1 "invalid_format.*length mismatch" derive --group "${WORK}/truncated.bin"
  --sha256 "${truncated_pin}" --threads 1)

file(REMOVE "${group}.sha256")
refuse(1 "io.*cannot open group checkpoint checksum" derive --group "${group}" --threads 1)
file(WRITE "${group}.sha256" "invalid digest\n")
refuse(1 "invalid_format.*invalid group checkpoint checksum" derive --group "${group}" --threads 1)
invoke(0 "^$" derive --group "${group}" --sha256 "${digest}" --threads 1)
file(WRITE "${group}.sha256" "${digest}\n")
file(APPEND "${group}" "trailing bytes")
refuse(1 "invalid_format.*length mismatch" derive --group "${group}" --threads 1)
