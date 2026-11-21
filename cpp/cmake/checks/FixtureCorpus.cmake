# SPDX-License-Identifier: AGPL-3.0-or-later
  # Assemble a flat test view of the current native corpus.
  set(SIC_TEST_FIXTURES_DIR "${PROJECT_BINARY_DIR}/fixtures/sicx")
  foreach(_fixture
    tests/fixtures/sicx/canonical/d16.sicx
    tests/fixtures/sicx/d2-c0.json
    tests/fixtures/sicx/d2.jsonl
    tests/fixtures/sicx/d2.sicx
    tests/fixtures/sicx/canonical/d32.sicx
    tests/fixtures/sicx/d4-c0.json
    tests/fixtures/sicx/d4.jsonl
    tests/fixtures/sicx/d4.sicx
    tests/fixtures/sicx/canonical/d64.sicx
    tests/fixtures/sicx/d8-c0.json
    tests/fixtures/sicx/d8-maximal.sicx
    tests/fixtures/sicx/d8_release_catalogue.json
    tests/fixtures/sicx/d8.jsonl
    tests/fixtures/sicx/d8.sicx
  )
    get_filename_component(_fixture_name "${_fixture}" NAME)
    configure_file("${PROJECT_SOURCE_DIR}/${_fixture}"
      "${SIC_TEST_FIXTURES_DIR}/${_fixture_name}" COPYONLY)
  endforeach()
