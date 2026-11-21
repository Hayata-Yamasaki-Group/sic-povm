# SPDX-License-Identifier: AGPL-3.0-or-later
# Binary64 SIC frame and Clifford tools (no FLINT): sic-overlaps, sic-identify, sic-stabilizer,
# Core element application and frame APIs plus separate Clifford search.
# Documented in docs/data.md#stabilizer-table-format. Included at the end of cpp/CMakeLists.txt.
if(SIC_BUILD_TESTS)
  sic_add_executable(sic-test-search-clifford tests/core/clifford.cpp)
  target_link_libraries(sic-test-search-clifford PRIVATE sic_search)
  target_link_libraries(sic-test-search-clifford PRIVATE sic::search)
  add_test(NAME sic_test_search_clifford_synthetic COMMAND sic-test-search-clifford)
  set(_pub "${PROJECT_SOURCE_DIR}/tests/fixtures/numeric")
  set(_catalogue "${PROJECT_SOURCE_DIR}/tests/data/catalogue.tsv")
  set(_catalogue_args --catalogue "${_catalogue}" --repo "${PROJECT_SOURCE_DIR}/tests/data")
  set(_small)
  foreach(_r d2/m1/c0 d4/m1/c0 d8/m2/c0 d8/m1/c0 d16/m1/c1 d16/m1/c0 d32/m1/c1 d32/m1/c0 d64/m1/c2 d64/m1/c0 d64/m1/c3 d64/m1/c1)
    list(APPEND _small "${_pub}/${_r}/fiducial-fp64.csv")
  endforeach()
  set(_d128)
  set(_d256)
  file(STRINGS "${_catalogue}" _entries REGEX "^(128|256)[\t ]")
  foreach(_entry IN LISTS _entries)
    string(REGEX REPLACE "[\t ]+" ";" _fields "${_entry}")
    list(GET _fields 0 _d)
    list(GET _fields 4 _csv)
    list(APPEND _d${_d} "${PROJECT_SOURCE_DIR}/tests/data/${_csv}")
  endforeach()
  add_test(NAME sic_test_search_clifford_fiducials_d2_d64 COMMAND sic-test-search-clifford ${_small})
  add_test(NAME sic_test_search_clifford_fiducials_d128 COMMAND sic-test-search-clifford ${_d128})
  add_test(NAME sic_test_search_clifford_fiducials_d256 COMMAND sic-test-search-clifford ${_d256})
  # App-level checks. A pass regex makes CTest ignore the exit code, so the regexes below match
  # the success lines themselves; tests without a regex rely on exit code 0.
  add_test(NAME sic_test_search_overlaps_d64 COMMAND sic-overlaps "${_pub}/d64/m1/c2/fiducial-fp64.csv" --vectors)
  sic_add_verdict_test(NAME sic_test_search_identify_d16_ordinary_1 EXIT 0 COMMAND sic-identify "${_pub}/d16/m1/c0/fiducial-fp64.csv" ${_catalogue_args})
  set_tests_properties(sic_test_search_identify_d16_ordinary_1 PROPERTIES PASS_REGULAR_EXPRESSION "\nMATCH d16-ordinary-1 ")
  add_test(NAME sic_test_search_stabilizer_d8 COMMAND sic-stabilizer --dim 8 ${_catalogue_args})  # exit 0 only if every check passes
  sic_add_verdict_test(NAME sic_test_search_identify_d3_no_match EXIT 1 COMMAND sic-identify "${SIC_REPO_ROOT}/cpp/tests/fixtures/core/d3_simple.txt" --repo "${SIC_REPO_ROOT}")
  set_tests_properties(sic_test_search_identify_d3_no_match PROPERTIES PASS_REGULAR_EXPRESSION "\nNO_MATCH among 0 ")
  sic_add_verdict_test(NAME sic_test_search_identify_usage_refused EXIT 2 COMMAND sic-identify)
  sic_add_verdict_test(NAME sic_test_search_stabilizer_usage_refused EXIT 2 COMMAND sic-stabilizer)
  set_tests_properties(sic_test_search_identify_usage_refused sic_test_search_stabilizer_usage_refused PROPERTIES
    PASS_REGULAR_EXPRESSION "usage: ")
endif()


if(SIC_ENABLE_FLINT)
 add_subdirectory(checkers/stabilizer)
endif()
