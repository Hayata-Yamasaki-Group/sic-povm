# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED WORK)
  message(FATAL_ERROR "WORK is required")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/source")
file(WRITE "${WORK}/allowlist.txt" "")
function(check expected)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DROOT=${WORK}/source"
    "-DALLOWLIST=${WORK}/allowlist.txt" -P "${CMAKE_CURRENT_LIST_DIR}/hygiene.cmake"
    RESULT_VARIABLE _status OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr TIMEOUT 5)
  if(expected STREQUAL "pass")
    if(NOT _status EQUAL 0 OR NOT _stdout MATCHES "Public hygiene passed:")
      message(FATAL_ERROR "Hygiene positive control failed: ${_stdout}${_stderr}")
    endif()
  elseif(_status EQUAL 0 OR NOT "${_stdout}${_stderr}" MATCHES "${expected}")
    message(FATAL_ERROR "Hygiene negative control failed: ${_stdout}${_stderr}")
  endif()
endfunction()
# Exercise every spelling family, case handling, word boundaries and doc path.
string(CONCAT _private_sample "results" "/123")
set(_samples
  "R9" "R12-AB3" "P7b" "Q12" "S3C" "step 4" "Round 12" "gate E"
  "pre-freeze" "release-freeze" "worktree"
  "legacy" "Historical" "previously" "old format" "formerly" "no longer"
  "goldens-v3" "reader2" "sic_release" "sic_convert" "sicx_reader2" "SicxProducer"
  ".sic3" ".sic4" "${_private_sample}" "docs/cli.md" "docs/clifford-tools.md"
  "docs/conventions.md" "docs/data-example.md" "docs/index.md" "docs/library-api.md"
  "docs/reproducibility.md" "docs/sicx-spec.md" "cpp/README.md" "cpp/docs/example.md"
  "cpp/tools/example/README.md" "cpp/apps/README.md"
  "docs/data-text.md" "cpp/tools/test/README.md"
  "Catalogue v7")
include("${CMAKE_CURRENT_LIST_DIR}/../../private/policy/hygiene_local.cmake" OPTIONAL)
list(APPEND _samples ${_local_hygiene_samples})
# Assemble forbidden prose so the control's own source follows the prose rules.
foreach(_parts "20;26-11-15" "2026/;11/15" "November ;15, 2026"
    "15 ;November 2026" "Nov ;15, 2026" "4 ;Oct 2026"
    "moved ;from" "renamed ;from" "depre;cated" "for backward ;compatibility"
    "compatibility ;alias" "since commit ;abcdef123"
    "kept for ;compatibility" "Retained for source ;compatibility"
    "preserved for backward ;compatibility" "maintained for backwards ;compatibility")
  string(REPLACE ";" "" _sample "${_parts}")
  list(APPEND _samples "${_sample}")
endforeach()
foreach(_parts "orig;inal 1.0 producer" "orig;inal version" "orig;inal build"
    "V;1 fixture" "fixture V;1" "v;1 fixture" "V;1 canonical fixture" "fixture V;2" "experiments release V;1" "tree ;v3" "build sample-v;3"
    "sample-v;3 experiment" "// sample-fast-v;3: description" "right-iota-compose-v;1" "reviewed-enclosure-association-v;1" "d512-premises-v;1.md" "X;4_THREADS" "// v;4: comment" "// selected (v;3)" "as in v;3")
  string(REPLACE ";" "" _sample "${_parts}")
  list(APPEND _samples "${_sample}")
endforeach()
string(CONCAT _branch_label "r" "12-feature")
string(CONCAT _package_label "P-" "WORKERS")
list(APPEND _samples "${_branch_label}" "(${_branch_label})" "${_package_label}" "[${_package_label}]")
foreach(_part "for;mer" "super;seded" "old; name")
  string(REPLACE ";" "" _sample "${_part}")
  list(APPEND _samples "${_sample}")
endforeach()
foreach(_sample IN LISTS _samples)
  file(WRITE "${WORK}/source/value.txt" "${_sample}\n")
  check("Public hygiene: 1 unreviewed hits")
endforeach()
# Exact reviewed line, semicolon/bracket handling and no trailing newline.
set(_line "slong P1{}; // [working precision")
string(SHA256 _digest "${_line}")
file(WRITE "${WORK}/allowlist.txt" "value.txt|1|${_digest}|Working precision variable\n")
file(WRITE "${WORK}/source/value.txt" "${_line}")
check(pass)
file(APPEND "${WORK}/source/value.txt" " R9")
check("Public hygiene: 1 unreviewed hits")
file(WRITE "${WORK}/source/value.txt" "current behavior\n")
check("Unused hygiene allowance:")
file(WRITE "${WORK}/allowlist.txt" "")
file(WRITE "${WORK}/source/value.txt" "p1 r9 P10 P1_value Q1_value S1C_value solvers threshold transform formerly_value superseded_value\n")
file(APPEND "${WORK}/source/value.txt" "prefix${_branch_label} prefix${_package_label} ${_package_label}_value P-ab P-AB r12-9 r12_A\n")
file(APPEND "${WORK}/source/value.txt" "earlier BALLs; original basis; original discriminant; generic gate; history; V1 = V2; cgroup-v2\n")
file(MAKE_DIRECTORY "${WORK}/source/notes" "${WORK}/source/private" "${WORK}/source/build-test")
list(GET _samples 0 _bad_sample)
foreach(_dir notes private build-test)
  file(WRITE "${WORK}/source/${_dir}/value.txt" "${_bad_sample}\n")
endforeach()
check(pass)
# The archive inventory must visit hidden public files too.
file(WRITE "${WORK}/source/.hidden" "${_bad_sample}\n")
check("Public hygiene: 1 unreviewed hits")
file(REMOVE "${WORK}/source/.hidden")
# Numerical payloads and the numerical levels of ray files are exempt; ray-file metadata,
# bundled inputs and manifests in data/ are not.
file(WRITE "${WORK}/source/data/d8/m1/c0.csv" "${_bad_sample}\n")
# The numerical arrays of a ray file are exempt; its other members are scanned.
set(_ray_numeric "\"fp32\":{\"values\":[[\"${_bad_sample}\"]],\"rounding\":\"ROUNDING\"},\"hp\":{\"values\":[[\"${_bad_sample}\"]]}")
set(_ray "{\"d\":\"8\",${_ray_numeric},\"id\":\"d8-m1-c0\",\"labels\":{\"context\":\"LABEL\",\"id\":\"x\"}}\n")
file(WRITE "${WORK}/source/data/d8/m1/c0.json" "${_ray}")
check(pass)
foreach(_place "LABEL" "ROUNDING")
  string(REPLACE "${_place}" "${_bad_sample}" _mutated "${_ray}")
  file(WRITE "${WORK}/source/data/d8/m1/c0.json" "${_mutated}")
  check("Public hygiene: 1 unreviewed hits")
endforeach()
# A ray file that does not parse as JSON is scanned in full.
file(WRITE "${WORK}/source/data/d8/m1/c0.json" "{\"fp32\":{\"values\":[[\"${_bad_sample}\"]]}\n")
check("Public hygiene: 1 unreviewed hits")
file(REMOVE "${WORK}/source/data/d8/m1/c0.json" "${WORK}/source/data/d8/m1/c0.csv")
foreach(_data_path "data/d2/d2.sicrad" "data/d8/m1/manifest.json" "data/catalogue.json")
  file(WRITE "${WORK}/source/${_data_path}" "${_bad_sample}\n")
  check("Public hygiene: 1 unreviewed hits")
  file(REMOVE "${WORK}/source/${_data_path}")
endforeach()
message(STATUS "Hygiene negative, allowance-mutation and positive controls passed")

message(STATUS "SIC_NEGATIVE_CONTROL:hygiene-controls")
