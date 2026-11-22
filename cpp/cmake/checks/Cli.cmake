# SPDX-License-Identifier: AGPL-3.0-or-later
if(NOT SIC_BUILD_TESTS)
  return()
endif()
sic_add_executable(sic-test-support-cli-parser tests/cli/parser.cpp)
add_test(NAME sic_test_support_cli_parser COMMAND sic-test-support-cli-parser)
set_tests_properties(sic_test_support_cli_parser PROPERTIES LABELS "unit;cli")
foreach(_tool sic-construct sic-check read-fiducial sic-overlaps sic-identify sic-stabilizer
    sic-ray-selector sic-decode sic-verify sic-inspect sic-container sic-data sic-check-balls)
    string(REPLACE "-" "_" _tool_case "${_tool}")
  if(TARGET ${_tool})
  add_test(NAME sic_test_support_cli_help_${_tool_case}
      COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:${_tool}> -DNAME=${_tool}
        -DDOC=${PROJECT_SOURCE_DIR}/../docs/tools.md -P ${PROJECT_SOURCE_DIR}/tests/cli/help.cmake)
    set_tests_properties(sic_test_support_cli_help_${_tool_case} PROPERTIES LABELS cli)
  endif()
endforeach()
add_test(NAME sic_test_support_cli_refusals COMMAND ${CMAKE_COMMAND} -DBIN=$<TARGET_FILE_DIR:sic-check>
  -DFLINT=${SIC_ENABLE_FLINT}
  -DREPO=${SIC_REPO_ROOT} -DWORK=${PROJECT_BINARY_DIR}/cli-refusals -DFIXTURE=${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx -P ${PROJECT_SOURCE_DIR}/tests/cli/refusals.cmake)
set_tests_properties(sic_test_support_cli_refusals PROPERTIES LABELS cli)

if(TARGET sic-ray-selector)
  add_test(NAME sic_test_exact_ray_selector_cli COMMAND ${CMAKE_COMMAND}
    -DSELECTOR=$<TARGET_FILE:sic-ray-selector> -DPRODUCER=$<TARGET_FILE:sic-construct>
    -DWORK=${PROJECT_BINARY_DIR}/ray-selector-test
    -P ${PROJECT_SOURCE_DIR}/tests/exact/group/ray_selector_cli.cmake)
  set_tests_properties(sic_test_exact_ray_selector_cli PROPERTIES LABELS "integration;exact" TIMEOUT 120)
endif()

foreach(_tool sic-check sic-overlaps sic-identify sic-stabilizer sic-construct sic-decode
    sic-ray-selector sic-verify sic-inspect sic-container sic-data sic-check-balls sic-certify-stabilizer
    sic-verify-independent sic-certify-crt sic-certify-norm sic-check-crt-input)
  if(TARGET ${_tool})
    string(REPLACE "-" "_" _tool_case "${_tool}")
    add_test(NAME sic_test_support_cli_version_${_tool_case} COMMAND ${CMAKE_COMMAND}
      -DTOOL=$<TARGET_FILE:${_tool}> -DNAME=${_tool}
      -P ${PROJECT_SOURCE_DIR}/tests/cli/version.cmake)
    set_tests_properties(sic_test_support_cli_version_${_tool_case} PROPERTIES LABELS "cli;unit" TIMEOUT 15)
  endif()
endforeach()

sic_add_executable(sic-test-support-help-defaults tests/cli/sic_test_support_help_defaults.cpp)
target_sources(sic-test-support-help-defaults PRIVATE
  "${PROJECT_SOURCE_DIR}/apps/overlaps/command_line.cpp"
  "${PROJECT_SOURCE_DIR}/apps/identify/command_line.cpp"
  "${PROJECT_SOURCE_DIR}/apps/stabilizer/command_line.cpp")
target_compile_definitions(sic-test-support-help-defaults PRIVATE
  SIC_DATA_ROOT_FROM_BINDIR="${SIC_DATA_ROOT_FROM_BINDIR}")
target_link_libraries(sic-test-support-help-defaults PRIVATE sic_data sic_search)
add_test(NAME sic_test_support_help_defaults COMMAND sic-test-support-help-defaults
  "${SIC_REPO_ROOT}" "${PROJECT_BINARY_DIR}/help-defaults")
set_tests_properties(sic_test_support_help_defaults PROPERTIES LABELS "unit;policy")
if(SIC_ENABLE_FLINT AND SIC_BUILD_APPS)
  target_compile_definitions(sic-test-support-help-defaults PRIVATE SIC_HAVE_FLINT=1)
  target_sources(sic-test-support-help-defaults PRIVATE
    "${PROJECT_SOURCE_DIR}/apps/verify/command_line.cpp"
    "${PROJECT_SOURCE_DIR}/apps/inspect/command_line.cpp"
    "${PROJECT_SOURCE_DIR}/apps/decode/command_line.cpp"
    "${PROJECT_SOURCE_DIR}/apps/container/command_line.cpp"
    "${PROJECT_SOURCE_DIR}/src/access/source_arguments.cpp")
  target_link_libraries(sic-test-support-help-defaults PRIVATE sic_access sic_decode sic_construct sic_transport)
  add_test(NAME sic_test_data_subcommand_arity COMMAND "${SIC_NODE_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/cli/sic_test_data_subcommand_arity.mjs"
    "$<TARGET_FILE_DIR:sic-data>" "${SIC_REPO_ROOT}" "${PROJECT_BINARY_DIR}/data-subcommand-arity")
  set_tests_properties(sic_test_data_subcommand_arity PROPERTIES LABELS "integration;policy")
  add_test(NAME sic_test_inspect_json_multiplet COMMAND "${SIC_NODE_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/cli/sic_test_inspect_json_multiplet.mjs"
    "$<TARGET_FILE:sic-inspect>" "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx"
    "${PROJECT_BINARY_DIR}/inspect-json-multiplet")
  set_tests_properties(sic_test_inspect_json_multiplet PROPERTIES LABELS "integration;policy")
  add_test(NAME sic_test_access_stop_file COMMAND "${SIC_NODE_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/cli/sic_test_access_stop_file.mjs"
    "$<TARGET_FILE_DIR:sic-decode>" "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx"
    "${PROJECT_BINARY_DIR}/access-stop-file" "${SIC_ENABLE_REMOTE}")
  set_tests_properties(sic_test_access_stop_file PROPERTIES LABELS "integration;policy")
endif()
add_test(NAME sic_test_support_default_ownership COMMAND "${SIC_NODE_EXECUTABLE}"
  "${PROJECT_SOURCE_DIR}/tests/cli/sic_test_support_default_ownership.mjs" "${SIC_REPO_ROOT}" "$<TARGET_FILE_DIR:sic-check>")
set_tests_properties(sic_test_support_default_ownership PROPERTIES LABELS "unit;policy")

add_test(NAME sic_test_support_cli_failure_categories COMMAND ${CMAKE_COMMAND}
  -DBIN=$<TARGET_FILE_DIR:sic-check> -DFLINT=${SIC_ENABLE_FLINT}
  -DHOOKS=${SIC_ENABLE_TEST_HOOKS} -DREPO=${SIC_REPO_ROOT}
  -DWORK=${PROJECT_BINARY_DIR}/cli-failure-categories
  -P ${PROJECT_SOURCE_DIR}/tests/cli/failure_categories.cmake)
set_tests_properties(sic_test_support_cli_failure_categories PROPERTIES LABELS "integration;cli")
if(SIC_ENABLE_FLINT AND SIC_ENABLE_TEST_HOOKS)
  list(APPEND SIC_HOOK_CONTROL_TESTS sic_test_support_cli_failure_categories)
endif()

if(SIC_ENABLE_FLINT AND SIC_BUILD_APPS)
  add_test(NAME sic_test_support_cli_observation_options COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE_DIR:sic-decode> -DFIXTURE=${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx
    -DWORK=${PROJECT_BINARY_DIR}/cli-observation-options
    -P ${PROJECT_SOURCE_DIR}/tests/cli/observation_options.cmake)
  set_tests_properties(sic_test_support_cli_observation_options PROPERTIES LABELS "integration" TIMEOUT 120)
endif()

if(SIC_ENABLE_FLINT AND SIC_BUILD_APPS)
  add_test(NAME sic_test_support_cli_transport_options COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE_DIR:sic-container> -DFIXTURE=${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx
    -DWORK=${PROJECT_BINARY_DIR}/cli-transport-options
    -P ${PROJECT_SOURCE_DIR}/tests/cli/transport_options.cmake)
  set_tests_properties(sic_test_support_cli_transport_options PROPERTIES LABELS "integration" TIMEOUT 120)
endif()

if(SIC_ENABLE_FLINT AND SIC_BUILD_APPS)
  add_test(NAME sic_test_support_cli_inspect_options COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE_DIR:sic-inspect> -DFIXTURE=${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx
    -DFIXTURE8=${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d8.sicx
    -DWORK=${PROJECT_BINARY_DIR}/cli-inspect-options
    -P ${PROJECT_SOURCE_DIR}/tests/cli/inspect_options.cmake)
  set_tests_properties(sic_test_support_cli_inspect_options PROPERTIES LABELS "integration" TIMEOUT 120)
endif()

add_test(NAME sic_test_support_cli_clifford_options COMMAND ${CMAKE_COMMAND}
  -DBIN=$<TARGET_FILE_DIR:sic-identify> -DDATA=${PROJECT_SOURCE_DIR}/tests/data
  -DFIDUCIAL=${PROJECT_SOURCE_DIR}/tests/fixtures/numeric/d4/m1/c0/fiducial-fp64.csv
  -DD8=${PROJECT_SOURCE_DIR}/tests/fixtures/numeric/d8/m2/c0/fiducial-fp64.csv -DWORK=${PROJECT_BINARY_DIR}/cli-clifford-options
  -P ${PROJECT_SOURCE_DIR}/tests/cli/clifford_options.cmake)
set_tests_properties(sic_test_support_cli_clifford_options PROPERTIES LABELS "integration" TIMEOUT 120)
