# SPDX-License-Identifier: AGPL-3.0-or-later
# Final-format physical-unit validation admits parser scratch and numerical work.
# On Ubuntu 24.04, d32 refused at 14 MiB and passed at 16 MiB with 1 and 4 workers.
# With 16 workers it refused at 16, 17 and 18 MiB and passed from 19 MiB, with
# peak RSS about 12 MiB. Keep 24 MiB for that control and 2 MiB for the refusal.
# Decoder iteration tests: identical outputs for any worker
# count, a hard memory budget (peak RSS checked by --check-budget), the streamed-tower fallback,
# and fail-closed refusal when the budget cannot hold one stage.
# Decode outputs must match byte-for-byte across worker and storage settings.
# Sanitizers retain allocator metadata; only these three success-path tests
# need additional headroom. Production admission and the refusal test are unchanged.
set(_sic_decode_test_budget 16)
set(_sic_decode_test_budget_wide 24)
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=")
  set(_sic_decode_test_budget 256)
  set(_sic_decode_test_budget_wide 256)
endif()
foreach(case d2 d4 d8 d8-maximal d16 d32)
  string(REPLACE "-" "_" _sic_test_case_0 "sic_test_decode_decode_identity_${case}")
  add_test(NAME ${_sic_test_case_0}
    COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:sic-decode> -DINPUT=${_generated_${case}}
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/identity-${case} "-DTHREADS=1;2;3;8;16" "-DARGS=--no-pin"
      -P "${PROJECT_SOURCE_DIR}/tests/decode/run_decode_identity.cmake")
  string(REPLACE "-" "_" fixture "${case}")
  set_tests_properties(${_sic_test_case_0} PROPERTIES LABELS sicx FIXTURES_REQUIRED "sicx_generated_${fixture}")
endforeach()
add_test(NAME sic_test_decode_decode_budget_d32
  COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:sic-decode> -DINPUT=${_generated_d32}
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/budget-d32 "-DTHREADS=1;4;16"
    "-DMEMORY_MIB=${_sic_decode_test_budget};${_sic_decode_test_budget};${_sic_decode_test_budget_wide}"
    "-DARGS=--no-pin --check-budget"
    -P "${PROJECT_SOURCE_DIR}/tests/decode/run_decode_identity.cmake")
set_tests_properties(sic_test_decode_decode_budget_d32 PROPERTIES LABELS sicx FIXTURES_REQUIRED sicx_generated_d32)
# A raised policy ceiling must not reserve unused high-precision output limbs.
# Retain the configured cap, RSS assertion and existing numerical goldens.
add_test(NAME sic_test_decode_decode_precision_ceiling_budget_d32
  COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:sic-decode> -DINPUT=${_generated_d32}
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/ceiling-budget-d32 "-DTHREADS=1;4;16"
    "-DMEMORY_MIB=${_sic_decode_test_budget};${_sic_decode_test_budget};${_sic_decode_test_budget_wide}"
    "-DARGS=--no-pin --maximum-precision 1048576 --check-budget"
    -P "${PROJECT_SOURCE_DIR}/tests/decode/run_decode_identity.cmake")
set_tests_properties(sic_test_decode_decode_precision_ceiling_budget_d32 PROPERTIES LABELS sicx FIXTURES_REQUIRED sicx_generated_d32)
add_test(NAME sic_test_decode_decode_tower_stream_d32
  COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:sic-decode> -DINPUT=${_generated_d32}
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/stream-d32 "-DTHREADS=1;5"
    "-DARGS=--no-pin --tower stream --memory-mib ${_sic_decode_test_budget} --check-budget" -P "${PROJECT_SOURCE_DIR}/tests/decode/run_decode_identity.cmake")
set_tests_properties(sic_test_decode_decode_tower_stream_d32 PROPERTIES LABELS sicx FIXTURES_REQUIRED sicx_generated_d32)
sic_add_verdict_test(NAME sic_test_decode_decode_budget_refused_d32 EXIT 1
  COMMAND $<TARGET_FILE:sic-decode> ${_generated_d32} ${CMAKE_CURRENT_BINARY_DIR}/sicx-work/refused-d32
    --threads 4 --no-pin --memory-mib 2)
set_tests_properties(sic_test_decode_decode_budget_refused_d32 PROPERTIES LABELS sicx TIMEOUT 120
  FIXTURES_REQUIRED sicx_generated_d32
  PASS_REGULAR_EXPRESSION "memory budget too small" FAIL_REGULAR_EXPRESSION "\nPASS")
# d = 2 (in the loop above): imaginary quadratic K = Q(sqrt(-3)) (D0 = -3, iota(omega) = (1 - i sqrt 3)/2,
# balanced pair rule); every coordinate must overlap the BALL chunk with identical certified binary64 values.

sic_add_sicx_app(sic-test-decode-kernels "${PROJECT_SOURCE_DIR}/tests/decode/kernels.cpp")
target_link_libraries(sic-test-decode-kernels PRIVATE sic_decode)
add_test(NAME sic_test_decode_decode_kernel_refusals COMMAND sic-test-decode-kernels)
set_tests_properties(sic_test_decode_decode_kernel_refusals PROPERTIES LABELS sicx TIMEOUT 60)

sic_add_sicx_app(sic-test-decode-finalize "${PROJECT_SOURCE_DIR}/tests/decode/finalize.cpp")
add_test(NAME sic_test_decode_decode_cli
  COMMAND ${CMAKE_COMMAND} -DBIN=$<TARGET_FILE_DIR:sic-decode> -DFIXTURE=${_generated_d4}
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/cli -P "${PROJECT_SOURCE_DIR}/tests/decode/run_decode_cli.cmake")
set_tests_properties(sic_test_decode_decode_cli PROPERTIES LABELS sicx TIMEOUT 120 FIXTURES_REQUIRED sicx_generated_d4)

# Both private declaration closures must link together from two caller translation units.
sic_add_sicx_app(sic-test-decode-header-odr "${PROJECT_SOURCE_DIR}/tests/decode/header_linkage/odr_a.cpp")
target_sources(sic-test-decode-header-odr PRIVATE "${PROJECT_SOURCE_DIR}/tests/decode/header_linkage/odr_b.cpp")
target_link_libraries(sic-test-decode-header-odr PRIVATE sic_construct sic_decode)
add_test(NAME sic_test_decode_header_odr COMMAND sic-test-decode-header-odr)
set_tests_properties(sic_test_decode_header_odr PROPERTIES LABELS "unit;construct;decode;sanitizer" TIMEOUT 120)

sic_add_sicx_app(sic-test-decode-scratch "${PROJECT_SOURCE_DIR}/tests/decode/scratch.cpp")
target_link_libraries(sic-test-decode-scratch PRIVATE sic_decode)
add_test(NAME sic_test_decode_scratch COMMAND sic-test-decode-scratch)
set_tests_properties(sic_test_decode_scratch PROPERTIES LABELS "unit;decode;sanitizer" TIMEOUT 120)

# The private tree also compares reference bytes.
if(EXISTS "${SIC_REPO_ROOT}/private/scripts/release/tests/decode_oracles.cmake")
  include("${SIC_REPO_ROOT}/private/scripts/release/tests/decode_oracles.cmake")
endif()
