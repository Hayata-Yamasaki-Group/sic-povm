# SPDX-License-Identifier: AGPL-3.0-or-later
get_filename_component(_sic_ball_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_library(sic_bounds "${_sic_ball_root}/src/bounds/overlap_bounds.cpp")
add_library(sic::bounds ALIAS sic_bounds)
set_target_properties(sic_bounds PROPERTIES EXPORT_NAME bounds)
target_compile_features(sic_bounds PUBLIC cxx_std_20)
target_include_directories(sic_bounds
  PUBLIC $<BUILD_INTERFACE:${_sic_ball_root}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE "${_sic_ball_root}/src")
target_link_libraries(sic_bounds PUBLIC sic_support SicFLINT::SicFLINT)
target_sources(sic_bounds PRIVATE "${PROJECT_SOURCE_DIR}/src/bounds/check_balls.cpp")
target_compile_options(sic_bounds PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow>)

function(sic_add_bounds_tests)
  get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
  add_executable(sic-test-bounds-ball-bounds "${root}/tests/bounds/ball_bounds.cpp")
  target_link_libraries(sic-test-bounds-ball-bounds PRIVATE sic::bounds)
  add_test(NAME sic_test_bounds_ball_bounds COMMAND sic-test-bounds-ball-bounds)
  set_tests_properties(sic_test_bounds_ball_bounds PROPERTIES TIMEOUT 120)
  add_test(NAME sic_test_bounds_check_balls_inputs COMMAND "${SIC_NODE_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/bounds/check_balls.mjs" "$<TARGET_FILE:sic-check-balls>"
    "${PROJECT_SOURCE_DIR}/tests/fixtures/numeric/d8/m2/c0/normalized-gauged.balls"
    "${PROJECT_BINARY_DIR}/check-balls-inputs")
  set_tests_properties(sic_test_bounds_check_balls_inputs PROPERTIES LABELS "bounds;integration" TIMEOUT 120)
endfunction()
