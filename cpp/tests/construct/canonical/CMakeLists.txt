# SPDX-License-Identifier: AGPL-3.0-or-later
set(_golden_file "${CMAKE_CURRENT_SOURCE_DIR}/goldens.sha256")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/cases.txt" "${_golden_file}")
file(STRINGS "${_golden_file}" _goldens)
foreach(_line IN LISTS _goldens)
  if(_line MATCHES "^([0-9a-f]+)  (.+)$")
    set(_golden_${CMAKE_MATCH_2} "${CMAKE_MATCH_1}")
  endif()
endforeach()
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/cases.txt" _cases REGEX "^[^#]")
foreach(_case IN LISTS _cases)
  string(REPLACE "|" ";" _fields "${_case}")
  list(GET _fields 0 _name)
  string(REPLACE "-" "_" _case_name "${_name}")
  list(GET _fields 1 _labels)
  list(GET _fields 2 _env)
  list(GET _fields 3 _args)
  string(REPLACE "," ";" _labels "${_labels}")
  if(_args STREQUAL "")
    message(FATAL_ERROR "malformed native golden case: ${_case}")
  endif()
  if(NOT DEFINED _golden_${_name})
    message(FATAL_ERROR "no golden for native case ${_name} in ${_golden_file}")
  endif()
  if(_env MATCHES "SIC_DEV_" AND NOT SIC_ENABLE_TEST_HOOKS)
    continue()
  endif()
  add_test(NAME sic_test_construct_native_${_case_name}
    COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:sic-construct>
      "-DCASE_OPTIONS=${_env}" "-DCASE_ARGS=${_args}" -DEXPECTED=${_golden_${_name}}
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/${_name}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
  set_tests_properties(sic_test_construct_native_${_case_name} PROPERTIES LABELS "${_labels}")
endforeach()

add_executable(sic-test-construct-memory ../memory.cpp)
target_link_libraries(sic-test-construct-memory PRIVATE sic_exact)
target_include_directories(sic-test-construct-memory PRIVATE ${PROJECT_SOURCE_DIR}/tests)
add_test(NAME sic_test_construct_memory_selection COMMAND sic-test-construct-memory)
set_tests_properties(sic_test_construct_memory_selection PROPERTIES LABELS "sicx")
if(SIC_ENABLE_TEST_HOOKS)
foreach(_identity_case s2 s3 s4 s5 s6 reduced)
  add_test(NAME sic_test_construct_memory_strategy_identity_${_identity_case}
    COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:sic-construct>
      -DIDENTITY_CASE=${_identity_case}
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/memory-strategy-identity/${_identity_case}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/run_strategy_identity.cmake)
  set_tests_properties(sic_test_construct_memory_strategy_identity_${_identity_case}
    PROPERTIES LABELS "sicx;regression;exact" TIMEOUT 120)
endforeach()
endif()

if(SIC_BUILD_TESTS)
  sic_label_tests()
endif()
