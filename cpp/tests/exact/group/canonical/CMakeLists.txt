# SPDX-License-Identifier: AGPL-3.0-or-later
# Native group-metadata goldens (decision D9): exact class-group arithmetic of sic::exact at d = 16..1024.
add_executable(sic-test-exact-group-metadata group_metadata.cpp)
target_link_libraries(sic-test-exact-group-metadata PRIVATE sic_core sic_exact)
target_include_directories(sic-test-exact-group-metadata PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_options(sic-test-exact-group-metadata PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow>)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/goldens.sha256")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/goldens.sha256" _lines)
foreach(_line IN LISTS _lines)
  if(_line MATCHES "^([0-9a-f]+)  (.+)$")
  add_test(NAME sic_test_exact_group_metadata_${CMAKE_MATCH_2}
      COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:sic-test-exact-group-metadata> -DCASE=${CMAKE_MATCH_2}
        -DEXPECTED=${CMAKE_MATCH_1} -P ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
    set_tests_properties(sic_test_exact_group_metadata_${CMAKE_MATCH_2} PROPERTIES LABELS "regression;e2e")
  endif()
endforeach()
add_test(NAME sic_test_exact_group_metadata_unknown_case_refused COMMAND sic-test-exact-group-metadata standard1024)
set_tests_properties(sic_test_exact_group_metadata_unknown_case_refused PROPERTIES WILL_FAIL TRUE LABELS "regression;e2e")

if(SIC_BUILD_TESTS)
  sic_label_tests()
endif()
