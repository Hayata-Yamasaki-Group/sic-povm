set(_checker_sources
  src/algebra/algebra.cpp
  src/certificate/embeddings.cpp
  src/certificate/norm.cpp
  src/cli/command_line.cpp
  src/format/canonical.cpp
  src/format/canonical_json.cpp
  src/format/container.cpp
  src/format/input.cpp
  src/format/integer_codec.cpp
  src/format/metadata.cpp
  src/format/quadratic.cpp
  src/model/input_model.cpp
  src/runtime/diagnostics.cpp
)
set(_checker_input_sources
  src/format/canonical.cpp
  src/format/canonical_json.cpp
  src/format/container.cpp
  src/format/input.cpp
  src/format/integer_codec.cpp
  src/format/metadata.cpp
  src/format/quadratic.cpp
  src/model/input_model.cpp
  src/runtime/diagnostics.cpp
)
set(_checker_test_sources
  tests/reader_algebra.cpp
  tests/reader_codec.cpp
  tests/reader_container.cpp
  tests/reader_metadata.cpp
  tests/reader_refusals.cpp
  tests/context.cpp
  tests/writer.cpp
)
# SPDX-License-Identifier: AGPL-3.0-or-later
# Independent norm-argument checker: FLINT and the platform thread library only.
find_package(Threads REQUIRED)
add_executable(sic-certify-norm main.cpp ${_checker_sources})
# Private inline definitions recover the measured rescaling hot path. IPO
# outlines string construction here and increases the instruction count.
set_target_properties(sic-certify-norm PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
  SIC_CHECKER_INLINE_ONLY TRUE)
target_compile_features(sic-certify-norm PRIVATE cxx_std_20)
target_link_libraries(sic-certify-norm PRIVATE SicFLINT::SicFLINT Threads::Threads)
target_compile_options(sic-certify-norm PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow>)

if(SIC_BUILD_TESTS)
  foreach(_case estimate leaf_estimate max_bits max_dimension max_field_dimension)
    add_test(NAME sic_test_norm_option_${_case} COMMAND ${CMAKE_COMMAND}
      -DCHECKER=$<TARGET_FILE:sic-certify-norm> -DFIXTURE=${_generated_d4}
      -DCASE=${_case} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/options_cli.cmake)
    set_tests_properties(sic_test_norm_option_${_case} PROPERTIES LABELS integration TIMEOUT 120
      FIXTURES_REQUIRED sicx_generated_d4)
  endforeach()
  foreach(_case d4 d8 d8-maximal d16)
    set(_rays 1)
    if(_case STREQUAL "d16")
      set(_rays 2)
    endif()
    string(REPLACE "-" "_" _fixture "${_case}")
    string(REPLACE "-" "_" _sic_test_case_0 "sic_test_norm_certificate_${_case}")
  sic_add_verdict_test(NAME ${_sic_test_case_0} EXIT 0 COMMAND sic-certify-norm
      ${_generated_${_case}} --threads 2)
    set_tests_properties(${_sic_test_case_0} PROPERTIES LABELS "exact;independence;integration"
      FIXTURES_REQUIRED "sicx_generated_${_fixture}" PASS_REGULAR_EXPRESSION "summary: ${_rays} rays, ALL CERTIFIED")
  endforeach()
  add_executable(sic-test-norm-reader tests/reader_tests.cpp ${_checker_input_sources} src/algebra/algebra.cpp ${_checker_test_sources})
  target_compile_features(sic-test-norm-reader PRIVATE cxx_std_20)
  target_link_libraries(sic-test-norm-reader PRIVATE SicFLINT::SicFLINT)
  add_test(NAME sic_test_norm_certificate_reader COMMAND sic-test-norm-reader
    ${CMAKE_CURRENT_BINARY_DIR}/reader-fixtures)
  set_tests_properties(sic_test_norm_certificate_reader PROPERTIES LABELS "exact;independence")
  add_test(NAME sic_test_norm_certificate_reader_d8 COMMAND sic-test-norm-reader
    ${CMAKE_CURRENT_BINARY_DIR}/reader-fixtures-d8 8)
  set_tests_properties(sic_test_norm_certificate_reader_d8 PROPERTIES LABELS "exact;independence")
  target_compile_options(sic-test-norm-reader PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow>)
  set_tests_properties(sic_test_norm_certificate_reader PROPERTIES FIXTURES_SETUP f3_exact_input)
  foreach(_d 2 3 4 5 6 7 8 9 12 32)
  sic_add_verdict_test(NAME sic_test_norm_certificate_input_d${_d} EXIT 0 COMMAND sic-certify-norm
      ${CMAKE_CURRENT_BINARY_DIR}/reader-fixtures/d${_d}.sicx --verify-input)
    set_tests_properties(sic_test_norm_certificate_input_d${_d} PROPERTIES LABELS "exact;independence"
      FIXTURES_REQUIRED f3_exact_input PASS_REGULAR_EXPRESSION "FULL SICX input verification: PASS")
  endforeach()
  foreach(_d 3 5 6 7 9 12)
  add_test(NAME sic_test_norm_certificate_domain_d${_d} COMMAND ${CMAKE_COMMAND}
      -DCHECKER=$<TARGET_FILE:sic-certify-norm>
      -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/reader-fixtures/d${_d}.sicx
      -DDOMAIN_ONLY=ON -P ${CMAKE_CURRENT_SOURCE_DIR}/input_cli_test.cmake)
    set_tests_properties(sic_test_norm_certificate_domain_d${_d} PROPERTIES LABELS "exact;independence"
      FIXTURES_REQUIRED f3_exact_input)
  endforeach()
  foreach(_d 4 8)
  add_test(NAME sic_test_support_norm_certificate_input_cli_d${_d} COMMAND ${CMAKE_COMMAND}
      -DCHECKER=$<TARGET_FILE:sic-certify-norm>
      -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/reader-fixtures/d${_d}.sicx
      -P ${CMAKE_CURRENT_SOURCE_DIR}/input_cli_test.cmake)
    set_tests_properties(sic_test_support_norm_certificate_input_cli_d${_d} PROPERTIES LABELS "exact;independence"
      FIXTURES_REQUIRED f3_exact_input)
  endforeach()
endif()


if(SIC_BUILD_TESTS)
  add_executable(sic-test-norm-command-line tests/command_line.cpp src/cli/command_line.cpp)
  target_compile_features(sic-test-norm-command-line PRIVATE cxx_std_20)
  target_link_libraries(sic-test-norm-command-line PRIVATE SicFLINT::SicFLINT)
  sic_add_independent_checker(TARGET sic-test-norm-command-line DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME sic_test_norm_cli_defaults COMMAND ${CMAKE_COMMAND}
    -DCHECKER=$<TARGET_FILE:sic-certify-norm> -DPARSER=$<TARGET_FILE:sic-test-norm-command-line>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/defaults_cli.cmake)
  set_tests_properties(sic_test_norm_cli_defaults PROPERTIES LABELS "norm;integration" TIMEOUT 120)
endif()

if(SIC_BUILD_TESTS AND COMMAND sic_label_tests)
  sic_label_tests()
endif()

if(COMMAND sic_guard_verdict_status)
  sic_guard_verdict_status()
endif()
