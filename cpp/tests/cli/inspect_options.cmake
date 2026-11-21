# SPDX-License-Identifier: AGPL-3.0-or-later
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
foreach(mode checked unchecked)
  set(options)
  if(mode STREQUAL unchecked)
    set(options --no-check)
  endif()
  execute_process(COMMAND "${BIN}/sic-inspect" "${FIXTURE}" --exact-json "${WORK}/${mode}.json"
    --multiplet m1 ${options} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0 OR NOT EXISTS "${WORK}/${mode}.json")
    message(FATAL_ERROR "exact ${mode}: ${rc} ${out} ${err}")
  endif()
  if(mode STREQUAL checked AND NOT out MATCHES "numeric_checked=[1-9]")
    message(FATAL_ERROR "numerical check missing: ${out}")
  elseif(mode STREQUAL unchecked AND NOT out MATCHES "numeric_checked=0")
    message(FATAL_ERROR "numerical check not skipped: ${out}")
  endif()
endforeach()
execute_process(COMMAND "${BIN}/sic-inspect" "${FIXTURE}" --release-layout "${WORK}/release"
  --multiplet 1 --assets-dir "${WORK}/assets" --release-repo example/project --release-tag test-release
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(parts "${WORK}/release/d4/m1/multiplet.sicx.parts.json")
if(NOT rc EQUAL 0 OR NOT EXISTS "${parts}")
  message(FATAL_ERROR "release: ${rc} ${out} ${err}")
endif()
file(READ "${parts}" json)
string(JSON repo GET "${json}" release repository)
string(JSON tag GET "${json}" release tag)
if(NOT repo STREQUAL "example/project" OR NOT tag STREQUAL "test-release")
  message(FATAL_ERROR "release identity override missing")
endif()
# A catalogue must refuse a tree that combines different release identities.
execute_process(COMMAND "${BIN}/sic-inspect" "${FIXTURE8}" --release-layout "${WORK}/release"
  --multiplet 1 --assets-dir "${WORK}/assets" --release-repo example/other --release-tag test-release
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "second release: ${rc} ${out} ${err}")
endif()
execute_process(COMMAND "${BIN}/sic-inspect" --catalogue "${WORK}/release"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 1 OR NOT err MATCHES "\\[integrity\\]" OR EXISTS "${WORK}/release/catalogue.json")
  message(FATAL_ERROR "inconsistent catalogue: ${rc} ${out} ${err}")
endif()
