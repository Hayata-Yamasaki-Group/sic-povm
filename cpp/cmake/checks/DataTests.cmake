if(SIC_ENABLE_FLINT)
# SPDX-License-Identifier: AGPL-3.0-or-later
if(SIC_BUILD_TESTS)
  add_test(NAME sic_test_data_numeric_formats COMMAND "${SIC_NODE_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/data/numeric.mjs" "$<TARGET_FILE:sic-data>"
    "$<TARGET_FILE:sic-inspect>" "${SIC_TEST_FIXTURES_DIR}"
    "${PROJECT_SOURCE_DIR}/tests/fixtures/numeric" "${PROJECT_SOURCE_DIR}/../data/d8/hoggar"
    "${PROJECT_BINARY_DIR}/data-formats")
  set_tests_properties(sic_test_data_numeric_formats PROPERTIES LABELS "data;integration;interchange" TIMEOUT 120)
endif()

endif()

if(SIC_BUILD_TESTS)
  sic_add_executable(sic-test-data-catalogue tests/data/catalogue.cpp)
  target_link_libraries(sic-test-data-catalogue PRIVATE sic_data)
  add_test(NAME sic_test_data_catalogue COMMAND sic-test-data-catalogue
    "${SIC_REPO_ROOT}/data" "${PROJECT_BINARY_DIR}/catalogue-controls")
  set_tests_properties(sic_test_data_catalogue PROPERTIES LABELS "unit;data" TIMEOUT 2)
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT)
  sic_add_executable(sic-test-data-publication tests/data/publication.cpp)
  target_link_libraries(sic-test-data-publication PRIVATE sic_data)
  add_test(NAME sic_test_data_publication COMMAND sic-test-data-publication
    "${PROJECT_BINARY_DIR}/data-publication")
  set_tests_properties(sic_test_data_publication PROPERTIES LABELS "unit;data;sanitizer" TIMEOUT 30)
  if(SIC_BUILD_APPS)
    add_test(NAME sic_test_data_publication_commands COMMAND ${CMAKE_COMMAND}
      -DBIN=$<TARGET_FILE_DIR:sic-decode> -DFIXTURE=${SIC_TEST_FIXTURES_DIR}/d4.sicx
      -DWORK=${PROJECT_BINARY_DIR}/data-publication-commands
      -P ${PROJECT_SOURCE_DIR}/tests/data/publication.cmake)
    set_tests_properties(sic_test_data_publication_commands PROPERTIES LABELS "integration;data;sanitizer" TIMEOUT 120)
  endif()
endif()
