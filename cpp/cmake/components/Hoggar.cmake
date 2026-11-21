# SPDX-License-Identifier: AGPL-3.0-or-later
# Hoggar lines (d = 8, 3-qubit Pauli covariant): verification of the committed data files data/d8/hoggar.
# Compiled exact Gaussian-integer checks; no FLINT.
if(SIC_BUILD_TESTS)
  add_executable(sic-test-data-hoggar-data "${PROJECT_SOURCE_DIR}/tests/data/hoggar_data.cpp")
  target_link_libraries(sic-test-data-hoggar-data PRIVATE sic_data)
  target_include_directories(sic-test-data-hoggar-data PRIVATE "${PROJECT_SOURCE_DIR}/include")
  target_compile_features(sic-test-data-hoggar-data PRIVATE cxx_std_20)
  target_compile_options(sic-test-data-hoggar-data PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow>)
  add_test(NAME sic_test_data_hoggar_data COMMAND sic-test-data-hoggar-data "${PROJECT_SOURCE_DIR}/../data/d8/hoggar")
  find_program(SIC_GP_EXECUTABLE gp)
  if(SIC_GP_EXECUTABLE)
    sic_add_verdict_test(NAME sic_test_data_hoggar_gp EXIT 0 COMMAND "${SIC_GP_EXECUTABLE}" -q -f "${PROJECT_SOURCE_DIR}/checkers/gp/hoggar_verify.gp")
    set_tests_properties(sic_test_data_hoggar_gp PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/.." PASS_REGULAR_EXPRESSION "^PASS")
  else()
    sic_add_verdict_test(NAME sic_test_data_hoggar_gp EXIT 0 COMMAND ${CMAKE_COMMAND} -P "${PROJECT_SOURCE_DIR}/tests/skip_missing_gp.cmake")
    set_tests_properties(sic_test_data_hoggar_gp PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP: gp not found")
  endif()
endif()
