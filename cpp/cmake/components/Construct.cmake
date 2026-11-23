# SPDX-License-Identifier: AGPL-3.0-or-later
# Native SICX producer: stabilizer-adapted, SICX-only (d = 2 via its closed form).
add_library(sic_construct STATIC
  "${PROJECT_SOURCE_DIR}/src/construct/components/branch_admission.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/components/component_registry.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/components/components.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/components/exact_services.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/components/generator_choice.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/components/production_policy.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/model/entry_order.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/model/memory_plan.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/model/options.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/output/action_elements.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/output/fresh_representative.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/output/guarded_writer.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/output/output.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/output/output_plan.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/output/reencode.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/output/representative_leaves.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/output/representative_memory.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/output/resumed_representative.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/output/transported_values.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/model/worker_policy.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/runtime/command_line.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/runtime/construction.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/runtime/observations/clock.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/runtime/observations/progress.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/runtime/resources/construction_model.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/runtime/resources/final_memory.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/runtime/resources/direct_rows.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/runtime/resources/resident_memory.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/runtime/resources/resource_report.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/runtime/resources/table_memory.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/runtime/runtime.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/runtime/session/field_values.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/runtime/session_state.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/selection/alignment.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/selection/alignment_search.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/selection/alignment_selector.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/selection/generators/generator_values.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/branch/branch_values.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/branch_path.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/dimension_two.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/entries.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/entries/eta_values.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/entries/leaf_values.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/entries/recognition_memory.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/entries/representative_channels.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/final_pass.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/group_stage.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/initialize.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/precision.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/sic_check.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/step_data.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/structure.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/table/analytic_table.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/table/class_refinement.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/table/newton_table.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/table/class_schedule.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/table/newton_admission.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/table/newton_seed.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/table/newton_diagnostics.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/table/newton_cache_plan.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/table/table_cache.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/tower.cpp"
  "${PROJECT_SOURCE_DIR}/src/construct/stages/tower/step_values.cpp"
)
add_library(sic::construct ALIAS sic_construct)
target_include_directories(sic_construct PRIVATE "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(sic_construct PRIVATE sic_exact sic_sicx sic_construct_output)


if(SIC_BUILD_TESTS)
  sic_add_sicx_app(sic-test-construct-components "${PROJECT_SOURCE_DIR}/tests/construct/components.cpp")
  target_link_libraries(sic-test-construct-components PRIVATE sic_construct sic_exact)
  add_test(NAME sic_test_construct_component_conformance COMMAND sic-test-construct-components)
  set_tests_properties(sic_test_construct_component_conformance PROPERTIES LABELS "sicx;unit;conformance")
endif()


target_link_libraries(sic_construct PRIVATE sic_bounds)

if(SIC_BUILD_TESTS)
  sic_add_sicx_app(sic-test-construct-guarded-writer "${PROJECT_SOURCE_DIR}/tests/construct/guarded_writer.cpp")
  target_link_libraries(sic-test-construct-guarded-writer PRIVATE sic_construct sic_exact)
  sic_add_sicx_app(sic-test-construct-branch-admission "${PROJECT_SOURCE_DIR}/tests/construct/branch_admission.cpp")
  target_link_libraries(sic-test-construct-branch-admission PRIVATE sic_construct)
  add_test(NAME sic_test_construct_completed_replay_authentication COMMAND sic-test-construct-guarded-writer "${_generated_d4}")
  set_tests_properties(sic_test_construct_completed_replay_authentication PROPERTIES FIXTURES_REQUIRED sicx_generated_d4)
  add_test(NAME sic_test_construct_branch_scratch_admission COMMAND sic-test-construct-branch-admission)
  if(SIC_ENABLE_TEST_HOOKS)
  add_test(NAME sic_test_construct_quarantine_sic_failure COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:sic-construct>
    -DRADICALS=${PROJECT_SOURCE_DIR}/../data/d2/d2.sicrad -DWORK=${CMAKE_CURRENT_BINARY_DIR}/produce-quarantine-failure -P ${PROJECT_SOURCE_DIR}/tests/construct/quarantine_failure.cmake)
  set_tests_properties(sic_test_construct_quarantine_sic_failure PROPERTIES LABELS "sicx;conformance;sanitizer" TIMEOUT 120)
  endif()
  set_tests_properties(sic_test_construct_completed_replay_authentication sic_test_construct_branch_scratch_admission PROPERTIES LABELS "sicx;conformance;sanitizer")
endif()

if(SIC_BUILD_TESTS)
  sic_add_sicx_app(sic-test-construct-reencode "${PROJECT_SOURCE_DIR}/tests/construct/reencode.cpp")
  target_link_libraries(sic-test-construct-reencode PRIVATE sic_construct sic_exact)
  add_test(NAME sic_test_construct_file_route_conformance COMMAND sic-test-construct-reencode "${_generated_d4}" "${CMAKE_CURRENT_BINARY_DIR}/producer-reencode")
  set_tests_properties(sic_test_construct_file_route_conformance PROPERTIES FIXTURES_REQUIRED "sicx_generated_d4" LABELS "sicx;conformance;sanitizer")
endif()

if(SIC_BUILD_TESTS)
  add_test(NAME sic_test_construct_file_route_cli COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:sic-container> -DFIXTURES=${PROJECT_BINARY_DIR}/sicx-generated -DWORK=${CMAKE_CURRENT_BINARY_DIR}/producer-reencode-cli -P ${PROJECT_SOURCE_DIR}/tests/construct/reencode_cli.cmake)
  set_tests_properties(sic_test_construct_file_route_cli PROPERTIES FIXTURES_REQUIRED "sicx_generated_d2;sicx_generated_d4;sicx_generated_d8;sicx_generated_d8_maximal;sicx_generated_d16;sicx_generated_d32" LABELS "sicx;conformance;sanitizer")
endif()



if(SIC_BUILD_TESTS)
  sic_add_sicx_app(sic-test-construct-alignment "${PROJECT_SOURCE_DIR}/tests/construct/alignment.cpp")
  target_link_libraries(sic-test-construct-alignment PRIVATE sic_construct sic_exact)
  add_test(NAME sic_test_construct_alignment_search_conformance COMMAND sic-test-construct-alignment)
  set_tests_properties(sic_test_construct_alignment_search_conformance PROPERTIES LABELS "sicx;conformance;sanitizer")
endif()

if(SIC_BUILD_TESTS)
  if(SIC_ENABLE_TEST_HOOKS)
  add_test(NAME sic_test_support_alignment_cli_conformance COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:sic-construct> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/producer-alignment-cli -P ${PROJECT_SOURCE_DIR}/tests/construct/alignment_cli.cmake)
  set_tests_properties(sic_test_support_alignment_cli_conformance PROPERTIES LABELS "sicx;conformance;sanitizer")
  endif()
endif()

if(SIC_BUILD_TESTS)
  foreach(d 16 32 64)
    set(test sic_test_construct_alignment_fixture_d${d})
    add_test(NAME ${test} COMMAND ${CMAKE_COMMAND}
      -DINPUT=${_generated_d${d}} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/producer-fixture-d${d}
      -DVERIFY=$<TARGET_FILE:sic-verify> -DDECODE=$<TARGET_FILE:sic-decode> -DSICX=$<TARGET_FILE:sic-container>
      -DIDENTITY_SCRIPT=${PROJECT_SOURCE_DIR}/tests/decode/run_decode_identity.cmake
      -P ${PROJECT_SOURCE_DIR}/tests/construct/canonical/fixture.cmake)
    set_tests_properties(${test} PROPERTIES LABELS "sicx;conformance;regression"
      FIXTURES_REQUIRED sicx_generated_d${d})
  endforeach()
endif()

if(SIC_BUILD_TESTS)
  sic_add_sicx_app(sic-test-construct-extensibility "${PROJECT_SOURCE_DIR}/tests/construct/extensibility.cpp")
  target_link_libraries(sic-test-construct-extensibility PRIVATE sic_construct sic_exact)
  add_test(NAME sic_test_construct_extensibility COMMAND sic-test-construct-extensibility "${_generated_d4}")
  set_tests_properties(sic_test_construct_extensibility PROPERTIES FIXTURES_REQUIRED "sicx_generated_d4" LABELS "sicx;unit;conformance" TIMEOUT 120)
endif()
if(SIC_BUILD_TESTS)
  sic_add_sicx_app(sic-test-construct-runtime "${PROJECT_SOURCE_DIR}/tests/construct/runtime.cpp")
  target_link_libraries(sic-test-construct-runtime PRIVATE sic_construct sic_exact)
  target_link_libraries(sic-test-construct-runtime PRIVATE sic_construct)
  add_test(NAME sic_test_construct_runtime_isolation COMMAND sic-test-construct-runtime "${PROJECT_BINARY_DIR}/runtime-isolation")
  set_tests_properties(sic_test_construct_runtime_isolation PROPERTIES LABELS "sicx;integration;sanitizer" TIMEOUT 120)
endif()

if(SIC_BUILD_TESTS)
  sic_add_sicx_app(sic-test-construct-test-hooks "${PROJECT_SOURCE_DIR}/tests/construct/hooks.cpp")
  target_link_libraries(sic-test-construct-test-hooks PRIVATE sic_construct sic_exact)
  add_test(NAME sic_test_construct_test_hooks_library COMMAND sic-test-construct-test-hooks)
  set_tests_properties(sic_test_construct_test_hooks_library PROPERTIES LABELS "unit;construct;sanitizer")
  if(SIC_ENABLE_TEST_HOOKS)
    foreach(_fault plan evaluate nonfinite admission stop defer_class defer_entry)
      if(_fault MATCHES "^defer_(.*)$")
        set(_name "sic_test_construct_newton_defer_${CMAKE_MATCH_1}")
      else()
        set(_name "sic_test_construct_direct_eta_fault_${_fault}")
      endif()
      add_test(NAME ${_name} COMMAND ${CMAKE_COMMAND} -DMODE=activation -DFAULT=${_fault}
        -DPROGRAM=$<TARGET_FILE:sic-construct> -DWORK=${PROJECT_BINARY_DIR}/hook-activation/${_fault}
        -P ${PROJECT_SOURCE_DIR}/tests/cli/test_hooks.cmake)
      set_tests_properties(${_name} PROPERTIES LABELS "integration;construct;sanitizer")
    endforeach()
  endif()
endif()

if(SIC_BUILD_TESTS)
  sic_add_sicx_app(sic-test-construct-class-schedule "${PROJECT_SOURCE_DIR}/tests/construct/class_schedule.cpp")
  target_link_libraries(sic-test-construct-class-schedule PRIVATE sic_construct sic_exact sic_test_assertions)
  add_test(NAME sic_test_construct_live_class_admission COMMAND sic-test-construct-class-schedule)
  set_tests_properties(sic_test_construct_live_class_admission PROPERTIES LABELS "unit;construct;sanitizer" TIMEOUT 120)
endif()

if(SIC_BUILD_TESTS)
  sic_add_sicx_app(sic-test-construct-failure-categories "${PROJECT_SOURCE_DIR}/tests/construct/failure_categories/test.cpp")
  target_link_libraries(sic-test-construct-failure-categories PRIVATE sic_construct sic_exact)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_definitions(sic-test-construct-failure-categories PRIVATE SIC_TEST_WRAP_ENV=1)
    target_link_options(sic-test-construct-failure-categories PRIVATE "LINKER:--wrap=setenv,--wrap=unsetenv")
  endif()
  add_test(NAME sic_test_construct_failure_categories COMMAND sic-test-construct-failure-categories "${_generated_d4}" "$<TARGET_FILE:sic-construct>")
  set_tests_properties(sic_test_construct_failure_categories PROPERTIES FIXTURES_REQUIRED "sicx_generated_d4" LABELS "unit;construct;sanitizer" TIMEOUT 120)
endif()

if(SIC_BUILD_TESTS)
  add_test(NAME sic_test_construct_policy_cli COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:sic-construct> -DHOOKS=${SIC_ENABLE_TEST_HOOKS}
    -DWORK=${PROJECT_BINARY_DIR}/construct-policy-cli
    -P ${PROJECT_SOURCE_DIR}/tests/construct/policy/cli.cmake)
  set_tests_properties(sic_test_construct_policy_cli PROPERTIES LABELS "integration;construct" TIMEOUT 120)
endif()
if(SIC_BUILD_TESTS AND SIC_ENABLE_TEST_HOOKS)
  list(APPEND SIC_HOOK_CONTROL_TESTS sic_test_construct_policy_cli)
endif()

if(SIC_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles("#include <malloc.h>
    #ifndef __GLIBC__
    #error requires glibc
    #endif
    int main() { return 0; }" SIC_HAVE_GLIBC)
  if(SIC_HAVE_GLIBC)
    add_library(sic_test_construct_heap_observer SHARED tests/construct/heap_policy/observer.cpp)
    target_link_libraries(sic_test_construct_heap_observer PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
    add_test(NAME sic_test_construct_heap_policy COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:sic-construct> -DOBSERVER=$<TARGET_FILE:sic_test_construct_heap_observer>
      -DWORK=${PROJECT_BINARY_DIR}/heap-policy -P ${PROJECT_SOURCE_DIR}/tests/construct/heap_policy/run.cmake)
    set_tests_properties(sic_test_construct_heap_policy PROPERTIES LABELS "construct;integration" TIMEOUT 120)
    # A preload observer must not precede a sanitizer's allocator/runtime.
    include(${PROJECT_SOURCE_DIR}/cmake/checks/AllocatorTests.cmake)
    sic_disable_test_with_sanitizer(sic_test_construct_heap_policy)
  endif()
endif()
