# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
function(produce name)
  set(control)
  if(name STREQUAL "replay" OR (name STREQUAL "resume" AND NOT ARGN MATCHES "65"))
    set(control SIC_DEV_SPILL=1)
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${control} "${PROGRAM}" 4 --out "${WORK}/${name}.sicx" ${ARGN}
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
  set(status "${status}" PARENT_SCOPE)
  set(diagnostic "${out}${err}" PARENT_SCOPE)
endfunction()
produce(reference --threads 2 --alignment-trace)
if(NOT status EQUAL 0 OR NOT diagnostic MATCHES "SIC check [^\n]*PASS")
  message(FATAL_ERROR "reference failed: ${diagnostic}")
endif()
file(SHA256 "${WORK}/reference.sicx" expected)
produce(replay --threads 4 --alignment-cache-mib 0 --alignment-batch-mib 1 --alignment-worker-mib 1 --alignment-trace)
if(NOT status EQUAL 0 OR NOT diagnostic MATCHES "provisional_mode=replay")
  message(FATAL_ERROR "replay/phased failed: ${diagnostic}")
endif()
file(SHA256 "${WORK}/replay.sicx" replay)
if(NOT replay STREQUAL expected)
  message(FATAL_ERROR "storage/cache/worker controls changed canonical bytes")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SIC_TEST_KILL_AFTER_WRITE=1
  "${PROGRAM}" 4 --threads 2 --out "${WORK}/resume.sicx"
  RESULT_VARIABLE killed OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT killed EQUAL 86 OR NOT EXISTS "${WORK}/resume.sicx.sicx-quarantine")
  message(FATAL_ERROR "completed hook did not run: ${killed}\n${out}\n${err}")
endif()
file(SHA256 "${WORK}/resume.sicx.sicx-quarantine" private_before)
produce(resume --resume --threads 2 --alignment-train 65)
file(SHA256 "${WORK}/resume.sicx.sicx-quarantine" private_after)
if(status EQUAL 0 OR NOT private_before STREQUAL private_after OR EXISTS "${WORK}/resume.sicx")
  message(FATAL_ERROR "semantic resume mismatch was not preserved: ${diagnostic}")
endif()
produce(resume --resume --threads 4 --alignment-cache-mib 0)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "cross-strategy/cache resume failed: ${diagnostic}")
endif()
file(SHA256 "${WORK}/resume.sicx" resumed)
if(NOT resumed STREQUAL expected)
  message(FATAL_ERROR "resumed bytes differ")
endif()
produce(budget --threads 2 --alignment-memory-mib 1 --alignment-train 1000000)
if(status EQUAL 0 OR EXISTS "${WORK}/budget.sicx" OR NOT diagnostic MATCHES "alignment")
  message(FATAL_ERROR "alignment budget did not refuse: ${diagnostic}")
endif()
foreach(option "--alignment-train;0" "--alignment-candidates;0" "--alignment-sweeps;65" "--alignment-memory-mib;0" "--alignment-cache-mib;-1" "--generator-selector;unknown")
  produce(invalid ${option})
  if(NOT status EQUAL 2)
    message(FATAL_ERROR "invalid alignment option not refused by shared parser: ${option}\n${diagnostic}")
  endif()
endforeach()
produce(baseline_selector --generator-selector step-selection --threads 2)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "explicit stage-d2 selector failed: ${diagnostic}")
endif()
file(SHA256 "${WORK}/baseline_selector.sicx" baseline_selector)
if(NOT baseline_selector STREQUAL "cc8e1575df6740e40dde15810186b43ce5ae6944197331de05cf5a6376b35b5c")
  message(FATAL_ERROR "explicit stage-d2 selector changed bytes")
endif()
set(retry_memory_mib 1024)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SIC_DEV_KEEP_ALL=1 SIC_TEST_PRECISION_RETRY=1
  "${PROGRAM}" 4 --out "${WORK}/outer-retry.sicx" --threads 4 --memory-mib ${retry_memory_mib}
  RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
file(WRITE "${WORK}/outer-retry.log" "${out}${err}")
if(NOT status EQUAL 0 OR NOT out MATCHES "injected outer final precision retry" OR
    NOT out MATCHES "recognition failed; retrying at" OR NOT out MATCHES "SIC check [^\n]*PASS")
  message(FATAL_ERROR "outer keep-all retry failed: ${status}\n${out}\n${err}")
endif()
string(REGEX MATCHALL "memory census: immutable table captured" captures "${out}")
list(LENGTH captures capture_count)
if(NOT capture_count EQUAL 2)
  message(FATAL_ERROR "outer retry did not refresh exactly one immutable-table census per attempt")
endif()
file(SHA256 "${WORK}/outer-retry.sicx" retried)
if(NOT retried STREQUAL expected)
  message(FATAL_ERROR "outer precision retry changed canonical bytes")
endif()

# A backed retry may rebuild a verified lower-precision seed, without lowering its metadata.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SIC_DEV_SPILL=1 SIC_TEST_PRECISION_RETRY=1
  SIC_NEWTON_MATRIX_BITS=64
  "${PROGRAM}" 4 --out "${WORK}/backed-retry.sicx" --threads 4 --memory-mib ${retry_memory_mib}
  RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
file(WRITE "${WORK}/backed-retry.log" "${out}${err}")
if(NOT status EQUAL 0 OR NOT out MATCHES "retry seed plan=recompute source_bits=157 coarse_bits=144 matrix_hint=64" OR
    NOT out MATCHES "SIC check [^\n]*PASS")
  message(FATAL_ERROR "backed lower-precision seed retry failed: ${status}\n${out}\n${err}")
endif()
if(out MATCHES "memory census: immutable table captured")
  message(FATAL_ERROR "backed table incorrectly reused an immutable-table census")
endif()
file(SHA256 "${WORK}/backed-retry.sicx" retried)
if(NOT retried STREQUAL expected)
  message(FATAL_ERROR "backed seed recomputation changed canonical bytes")
endif()

# Populate a real producer completed-class cache, then consume it under another retention plan.
set(cache_flags --group-cache "${WORK}/cache-group.bin" --analytic-cache "${WORK}/coarse-cache"
  --completed-class-cache "${WORK}/completed-cache" --memory-mib 4096)
produce(cache-seed --threads 2 ${cache_flags})
file(WRITE "${WORK}/cache-seed.log" "${diagnostic}")
if(NOT status EQUAL 0 OR NOT diagnostic MATCHES "completed-class cache published")
  message(FATAL_ERROR "completed cache population failed: ${diagnostic}")
endif()
file(SHA256 "${WORK}/cache-seed.sicx" cache_seed)
if(NOT cache_seed STREQUAL expected)
  message(FATAL_ERROR "completed cache population changed canonical bytes")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SIC_DEV_SPILL=1
  "${PROGRAM}" 4 --out "${WORK}/cache-replay.sicx" --threads 4
  ${cache_flags}
  RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
file(WRITE "${WORK}/cache-replay.log" "${out}${err}")
if(NOT status EQUAL 0 OR NOT out MATCHES "completed-class cache inventory: [^\n]*missing=0" OR
    NOT out MATCHES "no Newton worker admitted" OR NOT out MATCHES "SIC check [^\n]*PASS")
  message(FATAL_ERROR "completed cache replay failed: ${status}\n${out}\n${err}")
endif()
file(SHA256 "${WORK}/cache-replay.sicx" cache_replay)
if(NOT cache_replay STREQUAL expected)
  message(FATAL_ERROR "completed cache replay changed canonical bytes")
endif()

# Cache-only mode uses authenticated native checkpoints and never rebuilds missing inputs.
execute_process(COMMAND "${PROGRAM}" 4 --out "${WORK}/cache-only.sicx"
  ${cache_flags} --require-completed-cache --threads 2
  RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT out MATCHES "cache-only consumer inventory accepted" OR
    NOT out MATCHES "no Newton worker admitted")
  message(FATAL_ERROR "native cache-only replay failed: ${status}\n${out}\n${err}")
endif()
file(SHA256 "${WORK}/cache-only.sicx" cache_only)
if(NOT cache_only STREQUAL expected)
  message(FATAL_ERROR "cache-only replay changed canonical bytes")
endif()
file(RENAME "${WORK}/coarse-cache" "${WORK}/coarse-saved")
file(MAKE_DIRECTORY "${WORK}/coarse-cache")
execute_process(COMMAND "${PROGRAM}" 4 --out "${WORK}/cache-only-refused.sicx"
  ${cache_flags} --require-completed-cache --threads 2
  RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "lost its native analytic checkpoint" OR
    EXISTS "${WORK}/cache-only-refused.sicx")
  message(FATAL_ERROR "cache-only missing analytic input was not refused: ${status}\n${out}\n${err}")
endif()
file(REMOVE_RECURSE "${WORK}/coarse-cache")
file(RENAME "${WORK}/coarse-saved" "${WORK}/coarse-cache")
file(RENAME "${WORK}/completed-cache" "${WORK}/completed-saved")
execute_process(COMMAND "${PROGRAM}" 4 --out "${WORK}/cache-missing.sicx"
  ${cache_flags} --require-completed-cache --threads 2
  RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "target recomputation is forbidden" OR
    EXISTS "${WORK}/cache-missing.sicx")
  message(FATAL_ERROR "cache-only missing classes were not refused: ${status}\n${out}\n${err}")
endif()
file(RENAME "${WORK}/completed-saved" "${WORK}/completed-cache")

# Developer fixed budgets remain private, and may never raise an explicit user cap.
# Keep the precedence check above sanitizer RSS as well: a 384 MiB
# cap forecast 403361412 B under ASan, just beyond the 402653184 B limit.
# The user cap is still strictly below the developer cap and asserted exactly.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SIC_DEV_MEMORY_MB=1280
  "${PROGRAM}" 4 --out "${WORK}/fixed-budget.sicx" --threads 2 --memory-mib ${retry_memory_mib}
  RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
file(WRITE "${WORK}/fixed-budget.log" "${out}${err}")
if(NOT status EQUAL 0 OR NOT out MATCHES "memory final plan: [^\n]*budget=1073741824")
  message(FATAL_ERROR "developer/user budget precedence failed: ${status}\n${out}\n${err}")
endif()
file(SHA256 "${WORK}/fixed-budget.sicx" fixed_budget)
if(NOT fixed_budget STREQUAL expected)
  message(FATAL_ERROR "fixed budget changed canonical bytes")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SIC_DEV_MEMORY_MB=1
  "${PROGRAM}" 4 --out "${WORK}/fixed-budget-refused.sicx" --memory-mib 4096
  RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "resource admission rejected before Group construction" OR
    NOT err MATCHES "process budget=1048576" OR EXISTS "${WORK}/fixed-budget-refused.sicx")
  message(FATAL_ERROR "private fixed-budget admission was not enforced: ${status}\n${out}\n${err}")
endif()
