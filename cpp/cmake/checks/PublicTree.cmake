# SPDX-License-Identifier: AGPL-3.0-or-later
if(SIC_BUILD_TESTS AND EXISTS "${PROJECT_SOURCE_DIR}/../private/scripts/release/test-public-tree.mjs")
  find_program(SIC_NODE_EXECUTABLE node REQUIRED)
  add_test(NAME sic_test_support_public_tree_export COMMAND "${SIC_NODE_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/../private/scripts/release/test-public-tree.mjs"
    "${PROJECT_BINARY_DIR}/public-tree-test" "${SicFLINT_INCLUDE_DIR}" "${SicFLINT_LIBRARY}")
  set_tests_properties(sic_test_support_public_tree_export PROPERTIES LABELS "release;hygiene" TIMEOUT 120)
endif()
