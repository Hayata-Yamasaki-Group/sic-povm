# SPDX-License-Identifier: AGPL-3.0-or-later
if(SIC_BUILD_TESTS)
  foreach(case d4 d8 d8-maximal d16 d32)
    string(REPLACE "-" "_" _sic_test_case_0 "sic_test_transport_parts_${case}")
    string(REPLACE "-" "_" fixture "${case}")
  if(SIC_ENABLE_TEST_HOOKS)
    set(_mode full)
  else()
    string(APPEND _sic_test_case_0 "_roundtrip")
    set(_mode positive)
  endif()
  add_test(NAME ${_sic_test_case_0}
      COMMAND ${CMAKE_COMMAND} -DMODE=${_mode} -DSICX=$<TARGET_FILE:sic-container> -DDUMP=$<TARGET_FILE:sic-inspect>
        -DFIXTURE=${_generated_${case}} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/transport-${case}
        -P "${PROJECT_SOURCE_DIR}/tests/transport/roundtrip.cmake")
    set_tests_properties(${_sic_test_case_0} PROPERTIES LABELS sicx
      FIXTURES_REQUIRED "sicx_generated_${fixture}")
  endforeach()
endif()

