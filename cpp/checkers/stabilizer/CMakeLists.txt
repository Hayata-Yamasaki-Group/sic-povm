# SPDX-License-Identifier: AGPL-3.0-or-later
# Ball-arithmetic certification of the stabilizer data files (FLINT/Arb, qqbar for d = 2).
if(SIC_ENABLE_FLINT)
  add_executable(sic-certify-stabilizer main.cpp certificate.cpp command_line.cpp json.cpp)
  target_compile_features(sic-certify-stabilizer PRIVATE cxx_std_20)
  target_link_libraries(sic-certify-stabilizer PRIVATE SicFLINT::SicFLINT)
  if(SIC_BUILD_TESTS)
    add_test(NAME sic_test_stabilizer_text_headers
      COMMAND "${SIC_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/text_headers.mjs"
        "$<TARGET_FILE:sic-certify-stabilizer>" "$<TARGET_FILE:sic-construct>"
        "$<TARGET_FILE:sic-check-balls>" "$<TARGET_FILE:sic-stabilizer>" "${SIC_REPO_ROOT}")
    set_tests_properties(sic_test_stabilizer_text_headers PROPERTIES
      LABELS "integration;independence" TIMEOUT 120)
    add_test(NAME sic_test_stabilizer_json_inputs
      COMMAND "${SIC_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/json_inputs.mjs"
        "$<TARGET_FILE:sic-certify-stabilizer>")
    set_tests_properties(sic_test_stabilizer_json_inputs PROPERTIES
      LABELS "integration;independence" TIMEOUT 120)
    foreach(_d 2 4 8 16)
      add_test(NAME sic_test_stabilizer_certify_stabilizer_d${_d}
        COMMAND ${CMAKE_COMMAND} -DDIM=${_d} -DBIN=$<TARGET_FILE_DIR:sic-inspect>
          -DCHECKER=$<TARGET_FILE:sic-certify-stabilizer> -DREPO=${SIC_REPO_ROOT}
          -DWORK=${PROJECT_BINARY_DIR}/certify-stabilizer-d${_d}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_table.cmake)
      set_tests_properties(sic_test_stabilizer_certify_stabilizer_d${_d} PROPERTIES
        LABELS "integration;independence;stabilizer" TIMEOUT 120)
    endforeach()
    add_test(NAME sic_test_stabilizer_certify_stabilizer_wrong_element_fails
      COMMAND ${CMAKE_COMMAND} -DDIM=8 -DWRONG_ELEMENT=ON -DBIN=$<TARGET_FILE_DIR:sic-inspect>
        -DCHECKER=$<TARGET_FILE:sic-certify-stabilizer> -DREPO=${SIC_REPO_ROOT}
        -DWORK=${PROJECT_BINARY_DIR}/certify-stabilizer-wrong-element
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_table.cmake)
    set_tests_properties(sic_test_stabilizer_certify_stabilizer_wrong_element_fails PROPERTIES
      LABELS "integration;independence;stabilizer" TIMEOUT 120)
  endif()
endif()

if(SIC_BUILD_TESTS)
  add_executable(sic-test-stabilizer-command-line tests/command_line.cpp command_line.cpp)
  target_compile_features(sic-test-stabilizer-command-line PRIVATE cxx_std_20)
  target_link_libraries(sic-test-stabilizer-command-line PRIVATE SicFLINT::SicFLINT)
  sic_add_independent_checker(TARGET sic-test-stabilizer-command-line DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME sic_test_stabilizer_cli_defaults COMMAND ${CMAKE_COMMAND}
    -DCHECKER=$<TARGET_FILE:sic-certify-stabilizer> -DPARSER=$<TARGET_FILE:sic-test-stabilizer-command-line>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/defaults_cli.cmake)
  set_tests_properties(sic_test_stabilizer_cli_defaults PROPERTIES LABELS "stabilizer;integration" TIMEOUT 120)
endif()

if(SIC_BUILD_TESTS AND COMMAND sic_label_tests)
  sic_label_tests()
endif()

if(COMMAND sic_guard_verdict_status)
  sic_guard_verdict_status()
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT)
  add_test(NAME sic_test_stabilizer_supplied_options COMMAND ${CMAKE_COMMAND}
    -DCHECKER=$<TARGET_FILE:sic-certify-stabilizer> -DPARSER=$<TARGET_FILE:sic-test-stabilizer-command-line>
    -DROOT=${SIC_REPO_ROOT} -DBIN=$<TARGET_FILE_DIR:sic-inspect>
    -DWORK=${PROJECT_BINARY_DIR}/stabilizer-supplied-options -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/supplied_options.cmake)
  set_tests_properties(sic_test_stabilizer_supplied_options PROPERTIES LABELS "integration;independence;stabilizer" TIMEOUT 120)
endif()
