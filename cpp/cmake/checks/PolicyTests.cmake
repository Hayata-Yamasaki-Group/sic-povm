# SPDX-License-Identifier: AGPL-3.0-or-later
if(NOT SIC_BUILD_TESTS)
  return()
endif()
if(SIC_ENABLE_FLINT AND SIC_BUILD_REGRESSION AND SIC_BUILD_APPS)
  file(WRITE "${PROJECT_BINARY_DIR}/test-hooks-required.txt" "")
  foreach(_name IN LISTS SIC_HOOK_CONTROL_TESTS)
    file(APPEND "${PROJECT_BINARY_DIR}/test-hooks-required.txt" "${_name}\n")
  endforeach()
  add_test(NAME sic_test_support_test_hooks_manifest COMMAND ${CMAKE_COMMAND}
    -DMODE=manifest -DBUILD=${PROJECT_BINARY_DIR} -DENABLED=${SIC_TEST_HOOKS_MODE}
    -P ${PROJECT_SOURCE_DIR}/tests/cli/test_hooks.cmake)
  set_tests_properties(sic_test_support_test_hooks_manifest PROPERTIES LABELS "unit;policy")
endif()
if(SIC_ENABLE_TEST_HOOKS)
  add_test(NAME sic_test_support_test_hooks_install_refusal COMMAND ${CMAKE_COMMAND}
    -DBUILD=${PROJECT_BINARY_DIR} -DWORK=${PROJECT_BINARY_DIR}/hooks-install-refusal
    -DTEST_WORKERS=${_sic_test_workers}
    -P ${PROJECT_SOURCE_DIR}/tests/install/test_hooks.cmake)
  set_tests_properties(sic_test_support_test_hooks_install_refusal PROPERTIES LABELS "integration;policy")
elseif(SIC_ENABLE_FLINT AND SIC_BUILD_APPS)
  add_test(NAME sic_test_support_test_hooks_installed_isolation COMMAND ${CMAKE_COMMAND}
    -DBUILD=${PROJECT_BINARY_DIR} -DWORK=${PROJECT_BINARY_DIR}/hooks-installed-isolation
    -DFIXTURE=${SIC_TEST_FIXTURES_DIR}/d4.sicx
    -DSCRIPT=${PROJECT_SOURCE_DIR}/tests/cli/test_hooks.cmake
    -P ${PROJECT_SOURCE_DIR}/tests/install/test_hooks.cmake)
  set_tests_properties(sic_test_support_test_hooks_installed_isolation PROPERTIES LABELS "integration;policy")
endif()
sic_add_executable(sic-test-core-policy tests/core/policy.cpp)
add_test(NAME sic_test_core_policy COMMAND sic-test-core-policy)
set_tests_properties(sic_test_core_policy PROPERTIES LABELS "unit;policy")
if(SIC_ENABLE_FLINT)
  get_property(_sic_dependency_paths GLOBAL PROPERTY SIC_DEPENDENCY_PATHS)
  list(APPEND _sic_dependency_paths ${FLINT_LIBRARY_DIRS})
  # Library directories can differ from the corresponding include prefix.
  foreach(_library IN ITEMS "${SicFLINT_LIBRARY}" "${SicFLINT_GMP_LIBRARY}"
      "${SicFLINT_MPFR_LIBRARY}" "${SicHDF5_LIBRARY}" "${CURL_LIBRARY_RELEASE}")
    if(IS_ABSOLUTE "${_library}" AND EXISTS "${_library}")
      get_filename_component(_library_dir "${_library}" DIRECTORY)
      list(APPEND _sic_dependency_paths "${_library_dir}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _sic_dependency_paths)
  file(WRITE "${PROJECT_BINARY_DIR}/dependency-paths.txt" "")
  foreach(_path IN LISTS _sic_dependency_paths)
    file(APPEND "${PROJECT_BINARY_DIR}/dependency-paths.txt" "${_path}\n")
  endforeach()
  sic_add_executable(sic-test-sicx-policy tests/sicx/policy.cpp)
  target_link_libraries(sic-test-sicx-policy PRIVATE sic_bounds sic_decode sic_sicx)
  target_link_libraries(sic-test-sicx-policy PRIVATE sic_access sic_bounds)
  add_test(NAME sic_test_sicx_overlap_reference COMMAND sic-test-sicx-policy overlap_reference
    "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx" "${PROJECT_BINARY_DIR}/policy-work/overlap_reference")
  set_tests_properties(sic_test_sicx_overlap_reference PROPERTIES LABELS "sicx;policy" TIMEOUT 120)
  add_test(NAME sic_test_sicx_overlap_admission COMMAND sic-test-sicx-policy overlap_admission
    "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx" "${PROJECT_BINARY_DIR}/policy-work/overlap_admission")
  set_tests_properties(sic_test_sicx_overlap_admission PROPERTIES LABELS "sicx;policy" TIMEOUT 120)
  add_test(NAME sic_test_sicx_branch_precision COMMAND sic-test-sicx-policy branch_precision
    "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx" "${PROJECT_BINARY_DIR}/policy-work/branch_precision")
  set_tests_properties(sic_test_sicx_branch_precision PROPERTIES LABELS "sicx;policy" TIMEOUT 120)
  add_test(NAME sic_test_sicx_container_resources COMMAND sic-test-sicx-policy container_resources
    "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx" "${PROJECT_BINARY_DIR}/policy-work/container_resources")
  set_tests_properties(sic_test_sicx_container_resources PROPERTIES LABELS "sicx;policy" TIMEOUT 120)
  add_test(NAME sic_test_sicx_valuation_admission COMMAND sic-test-sicx-policy valuation_admission
    "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx" "${PROJECT_BINARY_DIR}/policy-work/valuation_admission")
  set_tests_properties(sic_test_sicx_valuation_admission PROPERTIES LABELS "sicx;policy" TIMEOUT 120)
  add_test(NAME sic_test_sicx_graph_width COMMAND sic-test-sicx-policy graph_width
    "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx" "${PROJECT_BINARY_DIR}/policy-work/graph_width")
  set_tests_properties(sic_test_sicx_graph_width PROPERTIES LABELS "sicx;policy" TIMEOUT 120)
  add_test(NAME sic_test_sicx_decode_memory COMMAND sic-test-sicx-policy decode_memory
    "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx" "${PROJECT_BINARY_DIR}/policy-work/decode_memory")
  set_tests_properties(sic_test_sicx_decode_memory PROPERTIES LABELS "sicx;policy" TIMEOUT 120)
  add_test(NAME sic_test_sicx_decode_options COMMAND sic-test-sicx-policy decode_options
    "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx" "${PROJECT_BINARY_DIR}/policy-work/decode_options")
  set_tests_properties(sic_test_sicx_decode_options PROPERTIES LABELS "sicx;policy" TIMEOUT 120)
  add_test(NAME sic_test_sicx_decode_cancellation COMMAND sic-test-sicx-policy decode_cancellation
    "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx" "${PROJECT_BINARY_DIR}/policy-work/decode_cancellation")
  set_tests_properties(sic_test_sicx_decode_cancellation PROPERTIES LABELS "sicx;policy" TIMEOUT 120)
  add_test(NAME sic_test_sicx_decode_thread_ownership COMMAND sic-test-sicx-policy decode_thread_ownership
    "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx" "${PROJECT_BINARY_DIR}/policy-work/decode_thread_ownership")
  set_tests_properties(sic_test_sicx_decode_thread_ownership PROPERTIES LABELS "sicx;policy" TIMEOUT 120)
  add_test(NAME sic_test_sicx_conjugation_selection COMMAND sic-test-sicx-policy conjugation_selection
    "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx" "${PROJECT_BINARY_DIR}/policy-work/conjugation_selection")
  set_tests_properties(sic_test_sicx_conjugation_selection PROPERTIES LABELS "sicx;policy" TIMEOUT 120)
  add_test(NAME sic_test_sicx_conjugation_word_width COMMAND sic-test-sicx-policy conjugation_word_width
    "${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx" "${PROJECT_BINARY_DIR}/policy-work/conjugation_word_width")
  set_tests_properties(sic_test_sicx_conjugation_word_width PROPERTIES LABELS "sicx;policy" TIMEOUT 120)
endif()
if(SIC_ENABLE_FLINT)
  add_test(NAME sic_test_bounds_admission COMMAND "${CMAKE_COMMAND}"
    -DTOOL=$<TARGET_FILE:sic-check-balls> -DWORK=${PROJECT_BINARY_DIR}/policy-release
    -P ${PROJECT_SOURCE_DIR}/tests/bounds/admission.cmake)
  set_tests_properties(sic_test_bounds_admission PROPERTIES LABELS "policy;release")
  if(NOT SIC_ENABLE_TEST_HOOKS)
  add_test(NAME sic_test_support_install_policy COMMAND "${CMAKE_COMMAND}" -DBUILD=${PROJECT_BINARY_DIR}
    -DSOURCE=${PROJECT_SOURCE_DIR}/tests/install/policy -DWORK=${PROJECT_BINARY_DIR}/policy-consumer
    "-DFLINT_PREFIX=${CMAKE_PREFIX_PATH}" "-DCONSUMER_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
    "-DCONSUMER_LINK_FLAGS=${CMAKE_EXE_LINKER_FLAGS}" -DFIXTURE=${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/d4.sicx
    -DREPO=${SIC_REPO_ROOT} -DSCAN=${PROJECT_SOURCE_DIR}/tests/install/scan-paths.mjs
    -DNODE=${SIC_NODE_EXECUTABLE} -DBINDIR=${CMAKE_INSTALL_BINDIR} -DDATADIR=${CMAKE_INSTALL_DATADIR}
    -DDEPENDENCY_PATHS=${PROJECT_BINARY_DIR}/dependency-paths.txt
    -DDEPENDENCY_CONTROL=${SicFLINT_INCLUDE_DIR}
    -P ${PROJECT_SOURCE_DIR}/tests/install/policy/run.cmake)
  set_tests_properties(sic_test_support_install_policy PROPERTIES LABELS "policy;release")
  endif()
endif()
