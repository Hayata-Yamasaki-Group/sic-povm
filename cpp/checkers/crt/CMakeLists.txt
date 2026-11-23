set(_checker_sources
  src/algebra/modular_field.cpp
  src/algebra/ntt.cpp
  src/certificate/archimedean.cpp
  src/certificate/crt_certificate.cpp
  src/certificate/residual.cpp
  src/cli/command_line.cpp
  src/format/canonical_json.cpp
  src/format/container.cpp
  src/format/integer_codec.cpp
  src/format/metadata.cpp
  src/format/sicx_input.cpp
  src/model/input_model.cpp
  src/runtime/diagnostics.cpp
  src/runtime/parallel_for.cpp
)
set(_checker_input_sources
  src/format/canonical_json.cpp
  src/format/container.cpp
  src/format/integer_codec.cpp
  src/format/metadata.cpp
  src/format/sicx_input.cpp
  src/model/input_model.cpp
  src/runtime/diagnostics.cpp
)
set(_checker_test_sources
  tests/branch_writer.cpp
  tests/input_algebra.cpp
  tests/input_codec.cpp
  tests/input_container.cpp
  tests/input_metadata.cpp
  tests/input_refusals.cpp
  tests/context.cpp
  tests/diagnostics.cpp
  tests/general.cpp
  tests/writer.cpp
)
# Independent CRT exactness certificate (see docs/verification.md section 2.8). Built only with SIC_ENABLE_FLINT=ON.
# It is a separate verification tool: nothing in the library, the SICX decoder or `exact-data` depends on it,
# and it must not link the production SICX/exact code (refactoring plan principle 2; checked below).
find_package(Threads REQUIRED)
add_executable(sic-certify-crt main.cpp ${_checker_sources})
target_compile_features(sic-certify-crt PRIVATE cxx_std_20)
target_link_libraries(sic-certify-crt PRIVATE SicFLINT::SicFLINT Threads::Threads)

get_target_property(_crt_libs sic-certify-crt LINK_LIBRARIES)
get_target_property(_crt_incs sic-certify-crt INCLUDE_DIRECTORIES)
foreach(_x IN LISTS _crt_libs _crt_incs)
  if(_x MATCHES "^sic$|^sic_|sic::|/cpp/include$|/cpp/src$")
    message(FATAL_ERROR "sic-certify-crt must stay independent of the production code; found '${_x}'")
  endif()
endforeach()

add_executable(sic-check-crt-input input_check.cpp ${_checker_input_sources})
target_compile_features(sic-check-crt-input PRIVATE cxx_std_20)
target_link_libraries(sic-check-crt-input PRIVATE SicFLINT::SicFLINT)
sic_add_independent_checker(TARGET sic-check-crt-input DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

if(SIC_BUILD_TESTS)
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  add_executable(sic-test-crt-input tests/input.cpp ${_checker_input_sources} ${_checker_test_sources})
  target_compile_features(sic-test-crt-input PRIVATE cxx_std_20)
  target_link_libraries(sic-test-crt-input PRIVATE SicFLINT::SicFLINT OpenSSL::Crypto)
  sic_add_independent_checker(TARGET sic-test-crt-input DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(sic-test-crt-external tests/external.cpp ${_checker_input_sources} tests/writer.cpp tests/branch_writer.cpp)
  target_compile_features(sic-test-crt-external PRIVATE cxx_std_20)
  target_link_libraries(sic-test-crt-external PRIVATE SicFLINT::SicFLINT OpenSSL::Crypto)
  sic_add_independent_checker(TARGET sic-test-crt-external DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(sic-test-crt-faults tests/faults.cpp ${_checker_input_sources} tests/writer.cpp tests/branch_writer.cpp)
  target_compile_features(sic-test-crt-faults PRIVATE cxx_std_20)
  target_link_libraries(sic-test-crt-faults PRIVATE SicFLINT::SicFLINT OpenSSL::Crypto ${CMAKE_DL_LIBS})
  sic_add_independent_checker(TARGET sic-test-crt-faults DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  foreach(_fault ftello fstat fmpz_set_str arb_intersection qqbar_roots)
  add_test(NAME sic_test_crt_input_fault_${_fault} COMMAND sic-test-crt-faults ${_fault})
    set_tests_properties(sic_test_crt_input_fault_${_fault} PROPERTIES LABELS "crt;crt-input" TIMEOUT 60)
  endforeach()
  foreach(_group valid reject boundaries partial amendments primitives hybrid targeted general fuzz)
  add_test(NAME sic_test_crt_input_${_group} COMMAND sic-test-crt-input ${_group})
    set_tests_properties(sic_test_crt_input_${_group} PROPERTIES LABELS "crt;crt-input" TIMEOUT 60)
  endforeach()
  add_test(NAME sic_test_crt_input_cli COMMAND ${CMAKE_COMMAND}
    -DWRITER=$<TARGET_FILE:sic-test-crt-input> -DREADER=$<TARGET_FILE:sic-check-crt-input>
    -DCERTIFICATE=$<TARGET_FILE:sic-certify-crt> -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/input-cli
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.cmake)
  set_tests_properties(sic_test_crt_input_cli PROPERTIES LABELS "crt;crt-input" TIMEOUT 60)
  foreach(_case primes seed prec integral_max_dim partial_metadata)
    set(_checker sic-certify-crt)
    if(_case STREQUAL "partial_metadata")
      set(_checker sic-check-crt-input)
    endif()
    add_test(NAME sic_test_crt_option_${_case} COMMAND ${CMAKE_COMMAND}
      -DCHECKER=$<TARGET_FILE:${_checker}> -DFIXTURE=${_generated_d4} -DCASE=${_case}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/options_cli.cmake)
    set_tests_properties(sic_test_crt_option_${_case} PROPERTIES LABELS "crt;integration" TIMEOUT 120
      FIXTURES_REQUIRED sicx_generated_d4)
  endforeach()
  sic_add_verdict_test(NAME sic_test_crt_d4_bound_only EXIT 0 COMMAND sic-certify-crt ${_generated_d4} --threads 2 --bound-only)
  set_tests_properties(sic_test_crt_d4_bound_only PROPERTIES LABELS "crt;integration" TIMEOUT 120
    FIXTURES_REQUIRED sicx_generated_d4
    PASS_REGULAR_EXPRESSION "split-prime structure checks and bound reconstruction completed[^\n]*residual certificate primes skipped"
    FAIL_REGULAR_EXPRESSION "no modular checks run|RESULT: EXACT SIC")
  foreach(_case d4 d8 d8-maximal d16 d32)
    string(REPLACE "-" "_" _fixture "${_case}")
    string(REPLACE "-" "_" _sic_test_case_2 "sic_test_crt_${_case}")
  sic_add_verdict_test(NAME ${_sic_test_case_2} EXIT 0 COMMAND sic-certify-crt ${_generated_${_case}} --threads 2)
    set_tests_properties(${_sic_test_case_2} PROPERTIES LABELS "crt;integration"
      FIXTURES_REQUIRED "sicx_generated_${_fixture}" PASS_REGULAR_EXPRESSION "RESULT: EXACT SIC[^\n]*\\[PROVED\\]")
  endforeach()
  foreach(_fault rep act conj)  # negative controls: a perturbed rep / action record / conjugation word must fail
  sic_add_verdict_test(NAME sic_test_crt_d16_inject_${_fault} EXIT 1 COMMAND sic-certify-crt ${_generated_d16} --threads 2 --inject ${_fault})
    set_tests_properties(sic_test_crt_d16_inject_${_fault} PROPERTIES LABELS "crt;integration"
      FIXTURES_REQUIRED sicx_generated_d16
      PASS_REGULAR_EXPRESSION "RESULT: FAILED|REFUSED:" FAIL_REGULAR_EXPRESSION "NEGATIVE-CONTROL|\\[PROVED\\]")
  endforeach()
  sic_add_verdict_test(NAME sic_test_crt_d16_shift_retry EXIT 0 COMMAND sic-certify-crt ${_generated_d16} --threads 2 --no-shift-hints)
  set_tests_properties(sic_test_crt_d16_shift_retry PROPERTIES LABELS "crt;integration"
    FIXTURES_REQUIRED sicx_generated_d16
    PASS_REGULAR_EXPRESSION "level 1: G_1 = 2\\^[1-4] g_1 PROVED[^\n]*J=0 not integral.*RESULT: EXACT SIC[^\n]*\\[PROVED\\]")
  sic_add_verdict_test(NAME sic_test_crt_help EXIT 0 COMMAND sic-certify-crt --help)
  set_tests_properties(sic_test_crt_help PROPERTIES LABELS crt PASS_REGULAR_EXPRESSION "exit codes: 0 proved")
  add_test(NAME sic_test_support_crt_independent_sources COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_CURRENT_SOURCE_DIR}
           -P ${CMAKE_CURRENT_SOURCE_DIR}/check_independence.cmake)
  set_tests_properties(sic_test_support_crt_independent_sources PROPERTIES LABELS crt)
endif()


if(SIC_BUILD_TESTS)
  add_executable(sic-test-crt-command-line tests/cli/command_line.cpp src/cli/command_line.cpp)
  target_compile_features(sic-test-crt-command-line PRIVATE cxx_std_20)
  target_link_libraries(sic-test-crt-command-line PRIVATE SicFLINT::SicFLINT)
  sic_add_independent_checker(TARGET sic-test-crt-command-line DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME sic_test_crt_cli_defaults COMMAND ${CMAKE_COMMAND}
    -DCHECKER=$<TARGET_FILE:sic-certify-crt> -DPARSER=$<TARGET_FILE:sic-test-crt-command-line>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/defaults.cmake)
  set_tests_properties(sic_test_crt_cli_defaults PROPERTIES LABELS "crt;integration" TIMEOUT 120)
endif()

if(SIC_BUILD_TESTS AND COMMAND sic_label_tests)
  sic_label_tests()
endif()

if(COMMAND sic_guard_verdict_status)
  sic_guard_verdict_status()
endif()
