# SPDX-License-Identifier: AGPL-3.0-or-later
if(SIC_BUILD_TESTS)
  sic_add_sicx_app(sic-test-decode-remote-decode-library "${PROJECT_SOURCE_DIR}/tests/access/library.cpp")
  target_link_libraries(sic-test-decode-remote-decode-library PRIVATE sic_access sic_decode)
  target_link_libraries(sic-test-decode-remote-decode-library PRIVATE sic_access)
  find_program(SIC_NODE_EXECUTABLE node)
  if(SIC_NODE_EXECUTABLE)
    foreach(case access faults versions decode schedule)
  add_test(NAME sic_test_access_remote_${case}
        COMMAND "${SIC_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/access/test.mjs"
          ${case} "$<TARGET_FILE_DIR:sic-container>" "${SIC_TEST_FIXTURES_DIR}"
          "${CMAKE_CURRENT_BINARY_DIR}/remote-work/${case}" "${SIC_ENABLE_REMOTE}")
      set_tests_properties(sic_test_access_remote_${case} PROPERTIES LABELS remote)
    endforeach()
    set_tests_properties(sic_test_access_remote_schedule PROPERTIES LABELS "remote;docs")
    if(NOT SIC_ENABLE_REMOTE)
      set_tests_properties(sic_test_access_remote_faults PROPERTIES SKIP_REGULAR_EXPRESSION "HTTP disabled: faults skipped")
    endif()
  else()
    message(WARNING "Node not found: SICX remote tests are not registered")
  endif()
endif()
