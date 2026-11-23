set(_checker_sources
  src/certificate/branches.cpp
  src/certificate/branches_numeric.cpp
  src/cli/command_line.cpp
  src/decode/check.cpp
  src/decode/decoder.cpp
  src/format/ball_codec.cpp
  src/format/canonical_json.cpp
  src/format/container.cpp
  src/format/integer_codec.cpp
  src/format/metadata.cpp
  src/format/sha256.cpp
  src/format/units.cpp
  src/model/input_model.cpp
  src/runtime/diagnostics.cpp
  src/self_test/self_test.cpp
  src/text/text_check.cpp
)
set(_checker_input_sources
  src/format/ball_codec.cpp
  src/format/canonical_json.cpp
  src/format/container.cpp
  src/format/integer_codec.cpp
  src/format/metadata.cpp
  src/format/sha256.cpp
  src/format/units.cpp
  src/model/input_model.cpp
  src/runtime/diagnostics.cpp
)
set(_checker_test_sources
  tests/branch_cases.cpp
  tests/conformance_algebra.cpp
  tests/conformance_codec.cpp
  tests/conformance_container.cpp
  tests/conformance_metadata.cpp
  tests/conformance_refusals.cpp
  tests/fault_seams.cpp
  tests/general_tests.cpp
  tests/guard_tests.cpp
  tests/context.cpp
  tests/writer.cpp
)
# SPDX-License-Identifier: AGPL-3.0-or-later
# The independent SICX second reader (written from docs/sicx.md only; no production code).
# In the main build (cpp/CMakeLists.txt, FLINT on) it links only the external FLINT/GMP/MPFR target and is enrolled
# as an independent checker (cmake/checks/Independence.cmake). Standalone build:
#   cmake -S cpp/checkers/verifier -B /path/build -DCMAKE_BUILD_TYPE=Release [-DFLINT_PREFIX=/flint/install]
#   cmake --build /path/build
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(sic_check_verifier VERSION 1.0.0 LANGUAGES CXX)
  set(SIC_SOURCE_COMMIT "" CACHE STRING "Source commit for exported independent verifier")
  if(SIC_SOURCE_COMMIT STREQUAL "")
    execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse HEAD
      OUTPUT_VARIABLE SIC_SOURCE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  endif()
  string(LENGTH "${SIC_SOURCE_COMMIT}" _commit_length)
  if(NOT SIC_SOURCE_COMMIT MATCHES "^[0-9a-f]+$" OR
      NOT (_commit_length EQUAL 40 OR _commit_length EQUAL 64))
    message(FATAL_ERROR "SIC_SOURCE_COMMIT must be a full lowercase source commit")
  endif()
  add_compile_definitions(SIC_SOURCE_COMMIT="${SIC_SOURCE_COMMIT}")
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
  endif()
  set(FLINT_PREFIX "" CACHE PATH "FLINT 3 / Arb install prefix (empty: system search paths)")
  find_path(SIC_VERIFIER_FLINT_INCLUDE flint/flint.h HINTS "${FLINT_PREFIX}/include")
  find_library(SIC_VERIFIER_FLINT_LIBRARY flint HINTS "${FLINT_PREFIX}/lib")
  if(NOT SIC_VERIFIER_FLINT_INCLUDE OR NOT SIC_VERIFIER_FLINT_LIBRARY)
    message(FATAL_ERROR "FLINT 3 not found; set -DFLINT_PREFIX=<install prefix>")
  endif()
  add_executable(sic-verify-independent main.cpp ${_checker_sources})
  target_include_directories(sic-verify-independent PRIVATE "${SIC_VERIFIER_FLINT_INCLUDE}")
  target_link_libraries(sic-verify-independent PRIVATE "${SIC_VERIFIER_FLINT_LIBRARY}")
  get_filename_component(_libdir "${SIC_VERIFIER_FLINT_LIBRARY}" DIRECTORY)
  set_target_properties(sic-verify-independent PROPERTIES BUILD_RPATH "${_libdir}")
else()
  add_executable(sic-verify-independent main.cpp ${_checker_sources})
  target_link_libraries(sic-verify-independent PRIVATE SicFLINT::SicFLINT)
endif()
target_compile_options(sic-verify-independent PRIVATE -Wall -Wextra)

# Self-generated fixtures are independent of the production writer and release data.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR SIC_BUILD_TESTS)
  enable_testing()
  if(NOT COMMAND sic_add_verdict_test)
    find_program(SIC_NODE_EXECUTABLE node REQUIRED)
    get_filename_component(SIC_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/checks/ExitStatus.cmake")
  endif()
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  add_executable(sic-test-verifier-fixtures tests/tests.cpp ${_checker_sources} ${_checker_test_sources})
  target_compile_definitions(sic-test-verifier-fixtures PRIVATE SIC_CHECKER_FAULT_SEAMS=1)
  if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    target_include_directories(sic-test-verifier-fixtures PRIVATE "${SIC_VERIFIER_FLINT_INCLUDE}")
    target_link_libraries(sic-test-verifier-fixtures PRIVATE "${SIC_VERIFIER_FLINT_LIBRARY}" OpenSSL::Crypto)
    set_target_properties(sic-test-verifier-fixtures PROPERTIES BUILD_RPATH "${_libdir}")
  else()
    target_link_libraries(sic-test-verifier-fixtures PRIVATE SicFLINT::SicFLINT OpenSSL::Crypto)
  endif()
  add_test(NAME sic_test_verifier_selftest COMMAND sic-verify-independent --selftest)
  add_test(NAME sic_test_verifier_final_fixtures COMMAND sic-test-verifier-fixtures "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
  set_tests_properties(sic_test_verifier_selftest sic_test_verifier_final_fixtures PROPERTIES LABELS "verifier;integration" TIMEOUT 120)
endif()
if(TARGET sic-test-verifier-fixtures)
  set(_verifier_fixture "${CMAKE_CURRENT_BINARY_DIR}/fixtures/d4-valid-final.sicx")
  add_test(NAME sic_test_verifier_option_extra_bits COMMAND ${CMAKE_COMMAND}
    -DCHECKER=$<TARGET_FILE:sic-verify-independent>
    -DFIXTURE=${_verifier_fixture}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/extra_bits_cli.cmake)
  set_tests_properties(sic_test_verifier_option_extra_bits PROPERTIES LABELS integration TIMEOUT 120
    FIXTURES_REQUIRED verifier_fixtures)
  add_test(NAME sic_test_verifier_positional_arity COMMAND ${CMAKE_COMMAND}
    -DCHECKER=$<TARGET_FILE:sic-verify-independent>
    -DFIXTURE=${_verifier_fixture}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/positional_arity.cmake)
  set_tests_properties(sic_test_verifier_positional_arity PROPERTIES LABELS "integration;verifier" TIMEOUT 20
    FIXTURES_REQUIRED verifier_fixtures)
  set_tests_properties(sic_test_verifier_final_fixtures PROPERTIES FIXTURES_SETUP verifier_fixtures)
  set(_verifier_exact "e4915c9c32912b1f0ecd4fe2af5c001fedbe0a3299f4161e251d25684088d7e1")
  sic_add_verdict_test(NAME sic_test_support_cli_full EXIT 0 COMMAND sic-verify-independent "${_verifier_fixture}"
    --decode full --release --expect-exact-root "${_verifier_exact}"
    --expect-file-root 3211dfa1b6a40ca1096c8c4f138f7757c0b3a8120033c703d8b67b86a43aa325
    --expect-sha256 b75b09aa8e8a7a52115c750b58f92dcc51dc531c8770ce6920f210f2d312dbbc)
  sic_add_verdict_test(NAME sic_test_support_cli_partial_trusted EXIT 0 COMMAND sic-verify-independent "${_verifier_fixture}"
    --partial-element 2 --expect-exact-root "${_verifier_exact}")
  sic_add_verdict_test(NAME sic_test_support_cli_partial_untrusted EXIT 0 COMMAND sic-verify-independent "${_verifier_fixture}"
    --partial-element 2)
  set_tests_properties(sic_test_support_cli_full sic_test_support_cli_partial_trusted
    sic_test_support_cli_partial_untrusted PROPERTIES FIXTURES_REQUIRED verifier_fixtures
    LABELS "verifier;integration" TIMEOUT 120)
  set_tests_properties(sic_test_support_cli_full PROPERTIES PASS_REGULAR_EXPRESSION "RESULT: PASS.*release requirements met")
  set_tests_properties(sic_test_support_cli_partial_trusted PROPERTIES PASS_REGULAR_EXPRESSION "RESULT: PARTIAL PASS.*exact values authenticated")
  set_tests_properties(sic_test_support_cli_partial_untrusted PROPERTIES PASS_REGULAR_EXPRESSION "RESULT: PARTIAL PASS.*NOT authenticated")
endif()
if(TARGET sic-test-verifier-fixtures)
  sic_add_verdict_test(NAME sic_test_support_cli_partial_file_trusted EXIT 0 COMMAND sic-verify-independent "${_verifier_fixture}"
    --partial-element 2 --expect-file-root 3211dfa1b6a40ca1096c8c4f138f7757c0b3a8120033c703d8b67b86a43aa325)
  sic_add_verdict_test(NAME sic_test_support_cli_sha_trusted EXIT 0 COMMAND sic-verify-independent "${_verifier_fixture}" --decode off
    --expect-sha256 b75b09aa8e8a7a52115c750b58f92dcc51dc531c8770ce6920f210f2d312dbbc)
  sic_add_verdict_test(NAME sic_test_support_cli_partial_all EXIT 0 COMMAND sic-verify-independent "${_verifier_fixture}"
    --partial-element 0 --partial-element 1 --partial-element 2 --partial-element 3 --partial-element 4)
  set_tests_properties(sic_test_support_cli_partial_file_trusted sic_test_support_cli_sha_trusted
    sic_test_support_cli_partial_all PROPERTIES FIXTURES_REQUIRED verifier_fixtures LABELS "sicx;verifier" TIMEOUT 120)
  set_tests_properties(sic_test_support_cli_partial_file_trusted PROPERTIES PASS_REGULAR_EXPRESSION
    "transitively trusted via file root and full LDIR binding")
  set_tests_properties(sic_test_support_cli_sha_trusted PROPERTIES PASS_REGULAR_EXPRESSION
    "exact root ${_verifier_exact} [(]transitively trusted via whole-file SHA-256")
  set_tests_properties(sic_test_support_cli_partial_all PROPERTIES PASS_REGULAR_EXPRESSION
    "all exact data, global rescaling and canonical branches checked")
  sic_add_verdict_test(NAME sic_test_support_cli_real_odd EXIT 0 COMMAND sic-verify-independent
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures/d4-odd-128.sicx" --decode off --branch-degree-limit 1)
  set_tests_properties(sic_test_support_cli_real_odd PROPERTIES FIXTURES_REQUIRED verifier_fixtures
    LABELS "sicx;verifier" TIMEOUT 120
    PASS_REGULAR_EXPRESSION "canonical branches verified by certified interval comparisons")
endif()

if(SIC_BUILD_TESTS AND COMMAND sic_label_tests)
  sic_label_tests()
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  get_property(_verifier_tests DIRECTORY PROPERTY TESTS)
  foreach(_test IN LISTS _verifier_tests)
    string(REGEX REPLACE "^sic_test_([a-z]+)_.*" "\\1" _component "${_test}")
    set_tests_properties("${_test}" PROPERTIES LABELS "integration;verifier;${_component}" TIMEOUT 120)
  endforeach()
endif()

if(TARGET sic-test-verifier-fixtures AND DEFINED _generated_d64)
  foreach(_case full_low full_sufficient full_mixed auto_low auto_release ratio_low)
    add_test(NAME sic_test_verifier_budget_${_case} COMMAND ${CMAKE_COMMAND}
      -DVERIFY=$<TARGET_FILE:sic-verify-independent>
      -DFIXTURE_D4=${_generated_d4}
      -DFIXTURE_D64=${PROJECT_BINARY_DIR}/sicx-generated/d64/verifier-budget.sicx
      -DCASE=${_case} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/decode_budget.cmake)
    set(fixture sicx_generated_d4)
    if(_case MATCHES "^(full_mixed|auto_release|ratio_low)$")
      set(fixture sicx_generated_d64)
    endif()
    set_tests_properties(sic_test_verifier_budget_${_case} PROPERTIES LABELS "verifier;integration"
      FIXTURES_REQUIRED "${fixture}" TIMEOUT 120)
  endforeach()
endif()

if(COMMAND sic_guard_verdict_status)
  sic_guard_verdict_status()
endif()
