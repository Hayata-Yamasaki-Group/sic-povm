# SPDX-License-Identifier: AGPL-3.0-or-later
# Binary64 SIC frame and Clifford tools (no FLINT): sic-overlaps, sic-identify, sic-stabilizer,
# Core element application and frame APIs plus separate Clifford search.
# Documented in docs/data.md#stabilizer-table-format. Included at the end of cpp/CMakeLists.txt.
if(SIC_BUILD_TESTS)
  sic_add_executable(sic-test-search-clifford tests/core/clifford.cpp)
  target_link_libraries(sic-test-search-clifford PRIVATE sic_search)
  target_link_libraries(sic-test-search-clifford PRIVATE sic::search)
  add_test(NAME sic_test_search_clifford_synthetic COMMAND sic-test-search-clifford)
  if(TARGET sic-inspect AND TARGET sic-construct)
    set(_search_workers 4)
    if(SIC_TEST_WORKERS AND SIC_TEST_WORKERS LESS 4)
      set(_search_workers "${SIC_TEST_WORKERS}")
    endif()
    set(_cases_d2_d64 "2:1:1:standard;4:2:1:standard;8:3:1:maximal;8:3:2:standard;16:4:1:standard;32:5:1:standard;64:6:1:standard")
    set(_cases_d128 "128:7:1:maximal;128:7:2:standard")
    # The release gate supplies the generated d256 input for the same checks.
    foreach(group d2_d64 d128)
      add_test(NAME sic_test_search_clifford_fiducials_${group} COMMAND ${CMAKE_COMMAND}
        -DBIN=$<TARGET_FILE_DIR:sic-construct> -DTEST=$<TARGET_FILE:sic-test-search-clifford>
        -DFRESH=${PROJECT_BINARY_DIR}/sicx-generated
        -DWORK=${PROJECT_BINARY_DIR}/search-${group}
        -DORACLES=${PROJECT_SOURCE_DIR}/tests/fixtures/numeric
        "-DCASES=${_cases_${group}}" -DTHREADS=${_search_workers}
        -P "${PROJECT_SOURCE_DIR}/tests/data/search_fiducials.cmake")
      set_tests_properties(sic_test_search_clifford_fiducials_${group} PROPERTIES
        LABELS "integration;search" PROCESSORS "${_search_workers}" TIMEOUT 120)
    endforeach()
    set_tests_properties(sic_test_search_clifford_fiducials_d2_d64 PROPERTIES FIXTURES_REQUIRED
      "sicx_generated_d4;sicx_generated_d8;sicx_generated_d8_maximal;sicx_generated_d16;sicx_generated_d32;sicx_generated_d64")
    add_test(NAME sic_test_search_overlaps_d64 COMMAND ${CMAKE_COMMAND}
      -DBIN=$<TARGET_FILE_DIR:sic-construct> -DINPUT=${PROJECT_BINARY_DIR}/sicx-generated/d64/d64.sicx
      -DWORK=${PROJECT_BINARY_DIR}/search-overlaps -DOVERLAPS=ON
      -P "${PROJECT_SOURCE_DIR}/tests/data/search_generated.cmake")
    set_tests_properties(sic_test_search_overlaps_d64 PROPERTIES FIXTURES_REQUIRED sicx_generated_d64)
    add_test(NAME sic_test_search_generated_catalogue COMMAND ${CMAKE_COMMAND}
      -DBIN=$<TARGET_FILE_DIR:sic-construct> -DWORK=${PROJECT_BINARY_DIR}/search-generated-catalogue
      -P "${PROJECT_SOURCE_DIR}/tests/data/search_generated.cmake")
    set_tests_properties(sic_test_search_generated_catalogue PROPERTIES LABELS "integration;data" TIMEOUT 120)
  endif()
  set(_synthetic_catalogue --catalogue "${SIC_TEST_NUMERIC_DIR}/catalogue.tsv" --repo "${SIC_TEST_NUMERIC_DIR}")
  sic_add_verdict_test(NAME sic_test_search_identify_d2 EXIT 0 COMMAND sic-identify
    "${SIC_TEST_NUMERIC_DIR}/d2/fiducial-fp64.csv" ${_synthetic_catalogue})
  set_tests_properties(sic_test_search_identify_d2 PROPERTIES PASS_REGULAR_EXPRESSION "\nMATCH synthetic-d2 ")
  add_test(NAME sic_test_search_stabilizer_d2 COMMAND sic-stabilizer --dim 2 ${_synthetic_catalogue})
  file(WRITE "${PROJECT_BINARY_DIR}/search-empty-catalogue.tsv" "")
  sic_add_verdict_test(NAME sic_test_search_identify_d3_no_match EXIT 1 COMMAND sic-identify "${SIC_TEST_NUMERIC_DIR}/d3/fiducial-fp64.csv" --catalogue "${PROJECT_BINARY_DIR}/search-empty-catalogue.tsv")
  set_tests_properties(sic_test_search_identify_d3_no_match PROPERTIES PASS_REGULAR_EXPRESSION "\nNO_MATCH among 0 ")
  sic_add_verdict_test(NAME sic_test_search_identify_usage_refused EXIT 2 COMMAND sic-identify)
  sic_add_verdict_test(NAME sic_test_search_stabilizer_usage_refused EXIT 2 COMMAND sic-stabilizer)
  set_tests_properties(sic_test_search_identify_usage_refused sic_test_search_stabilizer_usage_refused PROPERTIES
    PASS_REGULAR_EXPRESSION "usage: ")
endif()


if(SIC_ENABLE_FLINT)
 add_subdirectory(checkers/stabilizer)
endif()
