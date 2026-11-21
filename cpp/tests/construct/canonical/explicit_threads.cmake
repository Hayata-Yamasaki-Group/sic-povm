# SPDX-License-Identifier: AGPL-3.0-or-later
# SLURM lowers the process allowance independently of the explicit CLI count.
file(MAKE_DIRECTORY "${WORK}")
set(output "${WORK}/explicit-threads.sicx")
file(REMOVE "${output}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SLURM_CPUS_PER_TASK=1
  "${PRODUCER}" 4 --threads 8 --memory-mib 8192 --order standard --out "${output}"
  RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 110)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "producer failed: ${result}\n${stdout}\n${stderr}")
endif()
if(NOT "${stdout}\n${stderr}" MATCHES "d=16 [^\n]*threads=8([^0-9]|$)")
  message(FATAL_ERROR "producer did not report eight workers: ${stdout}\n${stderr}")
endif()
if(NOT "${stdout}\n${stderr}" MATCHES "Newton workers: 2 class, 8 kernel")
  message(FATAL_ERROR "producer did not use two class workers and eight kernel workers: ${stdout}\n${stderr}")
endif()
file(SHA256 "${output}" actual)
file(REMOVE "${output}")
if(NOT actual STREQUAL "ace2fd75d9195c5d7606b1b651b4c0a770156ef4bd96ad0a50e310211d7432c4")
  message(FATAL_ERROR "native SHA mismatch: ${actual}")
endif()
