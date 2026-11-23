# SPDX-License-Identifier: AGPL-3.0-or-later
if(NOT SIC_BUILD_TESTS)
  return()
endif()
set(_policy "${PROJECT_SOURCE_DIR}/../scripts/policy")
add_test(NAME sic_test_policy_layout COMMAND "${CMAKE_COMMAND}"
  "-DROOT=${SIC_REPO_ROOT}" "-DBUILD=${PROJECT_BINARY_DIR}" -P "${_policy}/layout.cmake")
set_tests_properties(sic_test_policy_layout PROPERTIES LABELS "unit;policy" TIMEOUT 10)
add_test(NAME sic_test_policy_layout_controls COMMAND "${CMAKE_COMMAND}"
  "-DWORK=${PROJECT_BINARY_DIR}/policy-layout-controls" -P "${_policy}/layout_controls.cmake")
set_tests_properties(sic_test_policy_layout_controls PROPERTIES LABELS "unit;policy" TIMEOUT 10)

# Record the configured production target/link inventory; object locations come
# from the compiler command database, so Make and Ninja use the same checker.
_sic_project_targets("${PROJECT_SOURCE_DIR}" _targets)
set(_production "")
foreach(_target IN LISTS _targets)
  get_target_property(_type ${_target} TYPE)
  if(_target MATCHES "^sic_" AND NOT _target MATCHES "^sic[-_]test[-_]" AND
     _type MATCHES "^(STATIC|SHARED|OBJECT|INTERFACE)_LIBRARY$")
    list(APPEND _production "${_target}")
  endif()
endforeach()
set(_links "")
foreach(_target IN LISTS _production)
  get_target_property(_dependencies ${_target} LINK_LIBRARIES)
  foreach(_dependency IN LISTS _dependencies)
    string(REGEX REPLACE "^\\$<LINK_ONLY:([^>]+)>$" "\\1" _dependency "${_dependency}")
    if(TARGET "${_dependency}")
      get_target_property(_alias "${_dependency}" ALIASED_TARGET)
      if(_alias)
        set(_dependency "${_alias}")
      endif()
    endif()
    if(_dependency IN_LIST _production)
      list(APPEND _links "${_target}|${_dependency}")
    endif()
  endforeach()
endforeach()
file(WRITE "${PROJECT_BINARY_DIR}/policy-symbol-targets.cmake"
  "set(PRODUCTION \"${_production}\")\nset(LINKS \"${_links}\")\n")
find_program(SIC_POLICY_NM NAMES nm llvm-nm REQUIRED)
add_test(NAME sic_test_policy_symbols COMMAND "${CMAKE_COMMAND}"
  "-DBUILD=${PROJECT_BINARY_DIR}" "-DROOT=${SIC_REPO_ROOT}" "-DNM=${SIC_POLICY_NM}"
  -P "${_policy}/symbols.cmake")
set_tests_properties(sic_test_policy_symbols PROPERTIES LABELS "unit;policy" TIMEOUT 30)

add_test(NAME sic_test_policy_hygiene COMMAND "${CMAKE_COMMAND}"
  "-DROOT=${SIC_REPO_ROOT}" -P "${_policy}/hygiene.cmake")
set_tests_properties(sic_test_policy_hygiene PROPERTIES LABELS "unit;policy;hygiene" TIMEOUT 30)
add_test(NAME sic_test_policy_hygiene_controls COMMAND "${CMAKE_COMMAND}"
  "-DWORK=${PROJECT_BINARY_DIR}/policy-hygiene-controls" -P "${_policy}/hygiene_controls.cmake")
set_tests_properties(sic_test_policy_hygiene_controls PROPERTIES LABELS "unit;policy" TIMEOUT 30)

add_test(NAME sic_test_policy_data_layout COMMAND "${SIC_NODE_EXECUTABLE}"
  "${_policy}/data_layout.mjs" "${SIC_REPO_ROOT}")
add_test(NAME sic_test_policy_data_layout_controls COMMAND "${SIC_NODE_EXECUTABLE}"
  "${_policy}/data_layout_controls.mjs" "${SIC_REPO_ROOT}" "${PROJECT_BINARY_DIR}/policy-data-controls")
set_tests_properties(sic_test_policy_data_layout sic_test_policy_data_layout_controls
  PROPERTIES LABELS "unit;policy" TIMEOUT 2)
add_test(NAME sic_test_policy_explicit_data_inputs COMMAND "${SIC_NODE_EXECUTABLE}"
  "${_policy}/contracts/explicit_data_inputs.mjs" "${SIC_REPO_ROOT}")
set_tests_properties(sic_test_policy_explicit_data_inputs PROPERTIES LABELS "unit;policy" TIMEOUT 10)

add_test(NAME sic_test_policy_format_identities COMMAND "${SIC_NODE_EXECUTABLE}"
  "${_policy}/format_identities.mjs" "${SIC_REPO_ROOT}")
set_tests_properties(sic_test_policy_format_identities PROPERTIES LABELS "unit;policy" TIMEOUT 10)

if(TARGET sic-data AND TARGET sic-inspect)
  set(_policy_release "${PROJECT_BINARY_DIR}/policy-generated-release")
  add_test(NAME sic_test_data_generate_release COMMAND "${CMAKE_COMMAND}"
    "-DBIN=$<TARGET_FILE_DIR:sic-inspect>" "-DWORK=${_policy_release}"
    -P "${PROJECT_SOURCE_DIR}/tests/data/generate_release.cmake")
  set_tests_properties(sic_test_data_generate_release PROPERTIES
    LABELS "integration;data;policy" FIXTURES_SETUP policy_generated_release TIMEOUT 30)
  add_test(NAME sic_test_policy_data_formats COMMAND "${SIC_NODE_EXECUTABLE}"
    "${_policy}/data_formats.mjs" "${_policy_release}" "$<TARGET_FILE:sic-data>")
  add_test(NAME sic_test_policy_catalogue_freshness COMMAND "${SIC_NODE_EXECUTABLE}"
    "${_policy}/catalogue_freshness.mjs" "${_policy_release}" "$<TARGET_FILE:sic-inspect>")
  set_tests_properties(sic_test_policy_data_formats sic_test_policy_catalogue_freshness
    PROPERTIES LABELS "integration;policy" FIXTURES_REQUIRED policy_generated_release TIMEOUT 30)
  set_property(TEST sic_test_policy_claim_registry APPEND PROPERTY FIXTURES_REQUIRED policy_generated_release)
  set_tests_properties(sic_test_data_numeric_formats
    PROPERTIES FIXTURES_REQUIRED policy_generated_release)
endif()

add_test(NAME sic_test_policy_newton_workers COMMAND "${SIC_NODE_EXECUTABLE}"
  "${_policy}/newton_workers.mjs" "${PROJECT_SOURCE_DIR}/src/construct/stages/table/newton_table.cpp")
set_tests_properties(sic_test_policy_newton_workers PROPERTIES LABELS "unit;policy" TIMEOUT 10)

add_test(NAME sic_test_policy_header_namespaces COMMAND "${SIC_NODE_EXECUTABLE}"
  "${_policy}/header_namespaces.mjs" "${SIC_REPO_ROOT}")
add_test(NAME sic_test_policy_header_namespaces_controls COMMAND "${SIC_NODE_EXECUTABLE}"
  "${_policy}/header_namespaces_controls.mjs" "${PROJECT_BINARY_DIR}/policy-header-controls")
set_tests_properties(sic_test_policy_header_namespaces sic_test_policy_header_namespaces_controls
  PROPERTIES LABELS "unit;policy" TIMEOUT 10)

add_test(NAME sic_test_policy_test_registry COMMAND "${SIC_NODE_EXECUTABLE}"
  "${_policy}/test_registry.mjs" "${SIC_REPO_ROOT}" "${PROJECT_BINARY_DIR}" "${CMAKE_CTEST_COMMAND}")
set_tests_properties(sic_test_policy_test_registry PROPERTIES LABELS "unit;policy" TIMEOUT 30)

set(_naming_targets "")
foreach(_target IN LISTS _targets)
  get_target_property(_type ${_target} TYPE)
  string(APPEND _naming_targets "${_type}|${_target}\n")
endforeach()
file(WRITE "${PROJECT_BINARY_DIR}/policy-naming-targets.txt" "${_naming_targets}")
add_test(NAME sic_test_policy_naming COMMAND "${SIC_NODE_EXECUTABLE}"
  "${_policy}/naming/check.mjs" "${SIC_REPO_ROOT}" "${PROJECT_BINARY_DIR}/policy-naming-targets.txt")
set_tests_properties(sic_test_policy_naming PROPERTIES LABELS "unit;policy" TIMEOUT 10)

foreach(_contract hex_owner failure_boundaries)
  add_test(NAME sic_test_policy_${_contract} COMMAND "${SIC_NODE_EXECUTABLE}"
    "${_policy}/contracts/${_contract}.mjs" "${SIC_REPO_ROOT}")
  set_tests_properties(sic_test_policy_${_contract} PROPERTIES LABELS "unit;policy" TIMEOUT 10)
endforeach()
