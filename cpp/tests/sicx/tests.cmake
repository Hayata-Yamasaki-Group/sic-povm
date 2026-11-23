# SPDX-License-Identifier: AGPL-3.0-or-later
set(_sicx_dir "${PROJECT_SOURCE_DIR}/tests/sicx")
set(_fix "${SIC_TEST_FIXTURES_DIR}")
set(_fresh "${PROJECT_BINARY_DIR}/sicx-generated")
foreach(_case "d2;1;standard" "d4;2;standard" "d8;3;standard" "d8-maximal;3;maximal"
              "d16;4;standard" "d32;5;standard" "d64;6;standard")
  list(GET _case 0 _name)
  list(GET _case 1 _s)
  list(GET _case 2 _order)
  string(REPLACE "-" "_" _key "${_name}")
  add_test(NAME sic_test_sicx_generate_${_key}
    COMMAND ${CMAKE_COMMAND} -DBIN=$<TARGET_FILE_DIR:sic-construct>
      -DWORK=${_fresh}/${_name} -DNAME=${_name} -DEXPONENT=${_s} -DORDER=${_order}
      -DORACLES=${PROJECT_SOURCE_DIR}/tests/fixtures/sicx
      -P "${_sicx_dir}/fixtures/generate_inputs.cmake")
  set_tests_properties(sic_test_sicx_generate_${_key} PROPERTIES
    LABELS "integration;sicx" FIXTURES_SETUP "sicx_generated_${_key}" TIMEOUT 120)
  set(_generated_${_name} "${_fresh}/${_name}/${_name}.sicx")
endforeach()
get_filename_component(_repo "${PROJECT_SOURCE_DIR}/.." ABSOLUTE)

sic_add_sicx_app(sic-test-sicx-rescaling "${_sicx_dir}/rescaling/rule.cpp")
add_test(NAME sic_test_sicx_rescaling COMMAND sic-test-sicx-rescaling
  "${_generated_d16}" "${_generated_d32}" "${_generated_d64}")
set_tests_properties(sic_test_sicx_rescaling PROPERTIES LABELS "unit;sicx;sanitizer" TIMEOUT 120
  FIXTURES_REQUIRED "sicx_generated_d16;sicx_generated_d32;sicx_generated_d64")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  sic_add_sicx_app(sic-test-sicx-rescaling-start-failure "${_sicx_dir}/rescaling/start_failure.cpp")
  target_link_libraries(sic-test-sicx-rescaling-start-failure PRIVATE ${CMAKE_DL_LIBS})
  add_test(NAME sic_test_sicx_rescaling_start_failure COMMAND sic-test-sicx-rescaling-start-failure)
  set_tests_properties(sic_test_sicx_rescaling_start_failure PROPERTIES LABELS "unit;sicx" TIMEOUT 120)
endif()

sic_add_sicx_app(sic-test-sicx-tower-inverse "${_sicx_dir}/tower_inverse.cpp")
add_test(NAME sic_test_sicx_tower_inverse COMMAND sic-test-sicx-tower-inverse)
set_tests_properties(sic_test_sicx_tower_inverse PROPERTIES LABELS "unit;sicx;exact;sanitizer" TIMEOUT 120)

sic_add_sicx_app(sic-test-sicx-primitives "${_sicx_dir}/container.cpp")
target_link_libraries(sic-test-sicx-primitives PRIVATE sic_access sic_transport)
if(SIC_ENABLE_TEST_HOOKS)
add_test(NAME sic_test_sicx_primitives_and_faults
  COMMAND sic-test-sicx-primitives "${_generated_d8}" "${CMAKE_CURRENT_BINARY_DIR}/sicx-work/faults" full)
set_tests_properties(sic_test_sicx_primitives_and_faults PROPERTIES FIXTURES_REQUIRED "sicx_generated_d8" LABELS sicx TIMEOUT 120)
endif()
add_test(NAME sic_test_sicx_primitives COMMAND sic-test-sicx-primitives
  "${_generated_d8}" "${CMAKE_CURRENT_BINARY_DIR}/sicx-work/primitives" positive)
set_tests_properties(sic_test_sicx_primitives PROPERTIES FIXTURES_REQUIRED "sicx_generated_d8" LABELS "unit;sicx" TIMEOUT 120)

function(sic_sicx_test name)
  cmake_parse_arguments(T "" "MODE;FIXTURE;FIXTURE2;GOLDEN_TEXT;GOLDEN_JSON" "" ${ARGN})
  string(REPLACE "-" "_" _sic_test_case_0 "sic-test-sicx-${name}")
  add_test(NAME ${_sic_test_case_0}
    COMMAND ${CMAKE_COMMAND} -DMODE=${T_MODE} -DBIN=$<TARGET_FILE_DIR:sic-inspect>
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/${name} -DNAME=${name}
      -DFIXTURE=${_generated_${T_FIXTURE}} -DFIXTURE2=${_generated_${T_FIXTURE2}}
      -DNODE=${SIC_NODE_EXECUTABLE} -DGOLDEN_TEXT=${T_GOLDEN_TEXT} -DGOLDEN_JSON=${T_GOLDEN_JSON}
      -P "${_sicx_dir}/run_sicx.cmake")
  string(REPLACE "-" "_" fixture "${T_FIXTURE}")
  string(REPLACE "-" "_" fixture2 "${T_FIXTURE2}")
  set(required sicx_generated_${fixture})
  if(fixture2)
    list(APPEND required sicx_generated_${fixture2})
  endif()
  set_tests_properties(${_sic_test_case_0} PROPERTIES LABELS sicx FIXTURES_REQUIRED "${required}")
endfunction()

sic_sicx_test(d4_fixture MODE roundtrip FIXTURE d4
  GOLDEN_TEXT "${_fix}/d4.jsonl" GOLDEN_JSON "${_fix}/d4-c0.json")
sic_sicx_test(d8_fixture MODE roundtrip FIXTURE d8
  GOLDEN_TEXT "${_fix}/d8_native.jsonl" GOLDEN_JSON "${_fix}/d8-c0.json")
# m<N> follows increasing construction conductor: d8-maximal (conductor 1) is m1, d8 (conductor 3) is m2.
sic_sicx_test(d8_release_layout MODE release FIXTURE d8-maximal FIXTURE2 d8
  GOLDEN_TEXT "${_fix}/d8_native.jsonl" GOLDEN_JSON "${_fix}/d8_release_catalogue.json")
foreach(case d8-maximal d16 d32 d64)
  sic_sicx_test(${case}_fixture MODE roundtrip FIXTURE "${case}")
endforeach()

# Certified decoding of the fixtures: every coordinate of every ray must overlap the BALL chunk and give
# the identical certified binary64 value (sic-decode exits nonzero otherwise).
foreach(case d4 d8 d8-maximal d16 d32)
  string(REPLACE "-" "_" _sic_test_case_1 "sic_test_decode_decode_${case}")
  add_test(NAME ${_sic_test_case_1}
    COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:sic-decode> -DINPUT=${_generated_${case}}
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/decode-${case} "-DARGS=--threads 2 --no-pin"
      -P "${PROJECT_SOURCE_DIR}/tests/decode/run_decode.cmake")
  string(REPLACE "-" "_" fixture "${case}")
  set_tests_properties(${_sic_test_case_1} PROPERTIES LABELS sicx FIXTURES_REQUIRED "sicx_generated_${fixture}")
endforeach()
# Forced low precision (target 64, delta 0, base path from 64 bits): the decoder must replay until it
# certifies, and still overlap BALL with identical binary64 values; it never passes an unverified output.
add_test(NAME sic_test_decode_decode_low_precision_d16
  COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:sic-decode> -DINPUT=${_generated_d16}
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/decode-low
    "-DARGS=--threads 2 --no-pin --target-bits 64 --guard-bits 0 --initial-bits 64"
    -P "${PROJECT_SOURCE_DIR}/tests/decode/run_decode.cmake")
set_tests_properties(sic_test_decode_decode_low_precision_d16 PROPERTIES
  LABELS sicx FIXTURES_REQUIRED sicx_generated_d16)

sic_add_sicx_app(sic-test-sicx-ray-order "${_sicx_dir}/ray_order.cpp")
add_test(NAME sic_test_sicx_ray_order_d64 COMMAND ${CMAKE_COMMAND}
  -DWRITER=$<TARGET_FILE:sic-test-sicx-ray-order> -DDECODER=$<TARGET_FILE:sic-decode>
  -DPRODUCER=$<TARGET_FILE:sic-construct>
  -DVERIFY=$<TARGET_FILE:sic-verify> -DREADER2=$<TARGET_FILE:sic-verify-independent>
  -DDUMP=$<TARGET_FILE:sic-inspect>
  -DCRT=$<TARGET_FILE:sic-check-crt-input> -DEXACT=$<TARGET_FILE:sic-certify-norm>
  -DWIRE_FIXTURE=${_fresh}/d64/verifier-budget.sicx -DWORK=${PROJECT_BINARY_DIR}/sicx-work/ray-order
  -P ${_sicx_dir}/ray_order.cmake)
set_tests_properties(sic_test_sicx_ray_order_d64 PROPERTIES FIXTURES_REQUIRED "sicx_generated_d64" LABELS "sicx;release;integration")
if(SIC_ENABLE_TEST_HOOKS)
add_test(NAME sic_test_sicx_d16_kill_resume
  COMMAND ${CMAKE_COMMAND} -DBIN=$<TARGET_FILE_DIR:sic-inspect> -DPRODUCER=$<TARGET_FILE:sic-construct>
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/d16-kill-resume -DS=4
    -P "${_sicx_dir}/run_resume.cmake")
set_tests_properties(sic_test_sicx_d16_kill_resume PROPERTIES LABELS sicx)
endif()

# d = 2: the exact tetrahedral fiducial (K = Q(sqrt(-3))), produced by the native producer (s = 1, closed form); BALL is
# computed from the exact leaves (decoded by sicx_decode_identity_d2 in decoder.cmake).
sic_sicx_test(d2_fixture MODE roundtrip FIXTURE d2
  GOLDEN_TEXT "${_fix}/d2.jsonl" GOLDEN_JSON "${_fix}/d2-c0.json")
add_test(NAME sic_test_construct_d2
  COMMAND ${CMAKE_COMMAND} -DMODE=make_d2 -DBIN=$<TARGET_FILE_DIR:sic-inspect> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/d2_make
    -DNAME=d2 -DFIXTURE=${_fix}/d2.sicx -DRADICALS=${_repo}/data/d2/d2.sicrad -P "${_sicx_dir}/run_exact.cmake")
set_tests_properties(sic_test_construct_d2 PROPERTIES LABELS sicx TIMEOUT 120)
# The general reduced-order rule epsilon = u^k (sic/sicx_reduced_order.hpp): d = 8 maximal is its k = 2 case
# (u = (3+sqrt5)/2). Produce, pin the exact part, then finalize with the generated ray list.
# sic-construct is defined after this file (cmake/components/Sicx.cmake); the generator expression resolves at generate time.
add_test(NAME sic_test_construct_d8_maximal
  COMMAND ${CMAKE_COMMAND} -DBIN=$<TARGET_FILE_DIR:sic-construct> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/d8_maximal_make
    -DS=3 -DORDER=maximal -DSHA256=3173c722d8706f33a76ff905c9354b9a77481aa951c472fc17cb1d9fef8a0f04
    -P "${PROJECT_SOURCE_DIR}/tests/construct/run_construct.cmake")
set_tests_properties(sic_test_construct_d8_maximal PROPERTIES LABELS "sicx;regression")

# exact.json (power basis): generation (with the numerical check against BALL) is deterministic and referenced by
# the ray files; the GP reference script verifies it exactly (optional, skipped without gp). It also runs inside Sage's GP (untested here).
find_program(SIC_GP_EXECUTABLE gp)
set(_gp_script "${PROJECT_SOURCE_DIR}/checkers/gp/sicx_exact_verify.gp")
foreach(case d2 d4 d8)
  add_test(NAME sic_test_sicx_exact_json_${case}
    COMMAND ${CMAKE_COMMAND} -DMODE=exact -DBIN=$<TARGET_FILE_DIR:sic-inspect> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/exact_${case}
      -DNAME=${case} -DFIXTURE=${_generated_${case}} -P "${_sicx_dir}/run_exact.cmake")
  set_tests_properties(sic_test_sicx_exact_json_${case} PROPERTIES
    LABELS "sicx;exact" FIXTURES_REQUIRED "sicx_generated_${case}")
  add_test(NAME sic_test_sicx_exact_gp_${case}
    COMMAND ${CMAKE_COMMAND} -DMODE=gp -DBIN=$<TARGET_FILE_DIR:sic-inspect> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/gp_${case}
      -DNAME=${case} -DFIXTURE=${_generated_${case}} -DGP=${SIC_GP_EXECUTABLE} -DSCRIPT=${_gp_script} -P "${_sicx_dir}/run_exact.cmake")
  set_tests_properties(sic_test_sicx_exact_gp_${case} PROPERTIES LABELS "sicx;optional;gp" FIXTURES_REQUIRED "sicx_generated_${case}" SKIP_REGULAR_EXPRESSION "SKIP: gp not found")
endforeach()
# d = 16 (opt-in, -DSIC_SICX_GP_D16=ON): one autocorrelation shift (a = 0) takes about 7 minutes in GP (N = [L:K] = 512),
# all nine shifts about an hour. This is an empirical timing.
option(SIC_SICX_GP_D16 "Register the slow GP verification of the d=16 exact.json (one shift)" OFF)
if(SIC_SICX_GP_D16)
  add_test(NAME sic_test_sicx_exact_gp_d16
    COMMAND ${CMAKE_COMMAND} -DMODE=gp -DBIN=$<TARGET_FILE_DIR:sic-inspect> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/gp_d16
      -DNAME=d16 -DFIXTURE=${_generated_d16} -DGP=${SIC_GP_EXECUTABLE} -DSCRIPT=${_gp_script} -DSHIFTS=0 -P "${_sicx_dir}/run_exact.cmake")
  set_tests_properties(sic_test_sicx_exact_gp_d16 PROPERTIES LABELS "sicx;gp;heavy" FIXTURES_REQUIRED sicx_generated_d16 TIMEOUT 3600 SKIP_REGULAR_EXPRESSION "SKIP: gp not found")
endif()

# Decoder iteration tests (owned by the decoder).
include("${PROJECT_SOURCE_DIR}/tests/decode/decoder.cmake")

# fixtures.sha256 lists every fixture exactly once with its current SHA-256 (regenerate it in fixtures/ with
# `sha256sum $(find . -maxdepth 1 -type f -printf "%f\n" | sort) > ../fixtures.sha256` after changing a golden).
add_test(NAME sic_test_sicx_fixture_hashes
  COMMAND ${CMAKE_COMMAND} -DLIST=${PROJECT_SOURCE_DIR}/tests/fixtures/sicx/SHA256SUMS -DDIR=${_fix} -P "${_sicx_dir}/run_fixture_hashes.cmake")
set_tests_properties(sic_test_sicx_fixture_hashes PROPERTIES LABELS sicx TIMEOUT 60)

if(SIC_ENABLE_TEST_HOOKS)
  add_test(NAME sic_test_construct_cli_injected_acceptance
    COMMAND ${CMAKE_COMMAND} -DMODE=injected -DPROGRAM=$<TARGET_FILE:sic-construct>
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/producer-cli-injected
      -P ${PROJECT_SOURCE_DIR}/tests/construct/run_construct_cli.cmake)
  set_tests_properties(sic_test_construct_cli_injected_acceptance PROPERTIES LABELS "integration;construct")
endif()
add_test(NAME sic_test_construct_cli
  COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:sic-construct>
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/producer-cli
    -P ${PROJECT_SOURCE_DIR}/tests/construct/run_construct_cli.cmake)
set_tests_properties(sic_test_construct_cli PROPERTIES LABELS "sicx" TIMEOUT 60)
if(SIC_ENABLE_TEST_HOOKS)
add_test(NAME sic_test_sicx_d4_final_format_kill_resume
  COMMAND ${CMAKE_COMMAND} -DBIN=$<TARGET_FILE_DIR:sic-inspect> -DPRODUCER=$<TARGET_FILE:sic-construct>
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/d4-final-kill-resume -DS=2
    -P "${_sicx_dir}/run_resume.cmake")
set_tests_properties(sic_test_sicx_d4_final_format_kill_resume PROPERTIES LABELS sicx)
endif()

sic_add_sicx_app(sic-test-sicx-writer-lsan "${_sicx_dir}/writer_lsan.cpp")
add_test(NAME sic_test_sicx_writer_lsan COMMAND sic-test-sicx-writer-lsan
  "${CMAKE_CURRENT_BINARY_DIR}/sicx-work/writer-lsan")
set_tests_properties(sic_test_sicx_writer_lsan PROPERTIES LABELS "sicx;sanitizer" TIMEOUT 120
  ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")

sic_add_sicx_app(sic-test-sicx-ordered-encoding "${_sicx_dir}/ordered_encoding.cpp")
target_include_directories(sic-test-sicx-ordered-encoding PRIVATE "${PROJECT_SOURCE_DIR}/tests")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_definitions(sic-test-sicx-ordered-encoding PRIVATE SIC_TEST_WRAP_IO=1 SIC_TEST_WRAP_FSYNC=1 SIC_TEST_WRAP_FDATASYNC=1 SIC_TEST_WRAP_WRITE=1 SIC_TEST_WRAP_PWRITE=1 SIC_TEST_WRAP_PREAD=1 SIC_TEST_WRAP_CLOSE=1)
  target_link_options(sic-test-sicx-ordered-encoding PRIVATE
    "LINKER:--wrap=fsync,--wrap=fdatasync,--wrap=write,--wrap=pwrite,--wrap=pread,--wrap=__pread_chk,--wrap=close"
    "LINKER:--wrap=fmpz_get_ui_array,--wrap=flint_set_num_workers,--wrap=flint_reset_num_workers")
  add_test(NAME sic_test_sicx_parallel_encoding_kill_resume COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:sic-test-sicx-ordered-encoding>
    -DWORK=${PROJECT_BINARY_DIR}/sicx-work/parallel-encoding-resume
    -P "${_sicx_dir}/encoding/resume.cmake")
  set_tests_properties(sic_test_sicx_parallel_encoding_kill_resume PROPERTIES
    LABELS "sicx;e2e;sanitizer" TIMEOUT 120)
  add_test(NAME sic_test_sicx_d128_parallel_encoding_kill_resume COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:sic-test-sicx-ordered-encoding>
    -DPRODUCER=$<TARGET_FILE:sic-construct>
    -DWORK=${PROJECT_BINARY_DIR}/sicx-work/d128-parallel-encoding-resume
    -P "${_sicx_dir}/encoding/d128_resume.cmake")
  set_tests_properties(sic_test_sicx_d128_parallel_encoding_kill_resume PROPERTIES
    LABELS "sicx;e2e" TIMEOUT 120)
endif()
foreach(d 4 8 16 32 64)
  add_test(NAME sic_test_sicx_ordered_encoding_d${d} COMMAND sic-test-sicx-ordered-encoding
    "${_generated_d${d}}" "${PROJECT_BINARY_DIR}/sicx-work/ordered-encoding-d${d}")
  set_tests_properties(sic_test_sicx_ordered_encoding_d${d} PROPERTIES FIXTURES_REQUIRED "sicx_generated_d${d}")
  set_tests_properties(sic_test_sicx_ordered_encoding_d${d} PROPERTIES
    LABELS "sicx;unit;sanitizer" TIMEOUT 120)
endforeach()

# General-d format/entry-rule controls use hand-built exact K(i)/K data, not SICs.
sic_add_sicx_app(sic-test-sicx-general-d "${_sicx_dir}/general_d.cpp")
target_link_libraries(sic-test-sicx-general-d PRIVATE sic_decode)
add_test(NAME sic_test_sicx_general_d_conformance COMMAND sic-test-sicx-general-d
  "${CMAKE_CURRENT_BINARY_DIR}/sicx-work/general-d")
set_tests_properties(sic_test_sicx_general_d_conformance PROPERTIES LABELS "sicx;conformance" TIMEOUT 120)

# Validation-harness helper: exact Arb enclosure comparisons, without decoding.
sic_add_sicx_app(sic-test-decode-enclosures "${PROJECT_SOURCE_DIR}/tests/decode/enclosures.cpp")
target_link_libraries(sic-test-decode-enclosures PRIVATE SicFLINT::SicFLINT)
add_test(NAME sic_test_decode_enclosure_checks COMMAND sic-test-decode-enclosures --self-test)
set_tests_properties(sic_test_decode_enclosure_checks PROPERTIES LABELS "sicx;unit" TIMEOUT 30)
add_test(NAME sic_test_decode_raw_comparator_fixtures COMMAND "${CMAKE_COMMAND}"
  "-DCOMPARE=$<TARGET_FILE:sic-test-decode-enclosures>"
  "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/sicx-work/raw-comparator"
  -P "${_sicx_dir}/raw_comparator.cmake")
set_tests_properties(sic_test_decode_raw_comparator_fixtures PROPERTIES LABELS "sicx;unit" TIMEOUT 120)

target_sources(sic-test-sicx-primitives PRIVATE "${PROJECT_SOURCE_DIR}/tests/sicx/codec.cpp" "${PROJECT_SOURCE_DIR}/tests/sicx/metadata.cpp" "${PROJECT_SOURCE_DIR}/tests/sicx/fixtures/fixture.cpp")



if(SIC_BUILD_TESTS)
 foreach(_topic directory finalization verification)
  sic_add_sicx_app(sic-test-sicx-${_topic} "${PROJECT_SOURCE_DIR}/tests/sicx/${_topic}.cpp")
  target_sources(sic-test-sicx-${_topic} PRIVATE "${PROJECT_SOURCE_DIR}/tests/sicx/fixtures/fixture.cpp")
  target_link_libraries(sic-test-sicx-${_topic} PRIVATE sic_access)
  add_test(NAME sic_test_sicx_${_topic}_conformance COMMAND sic-test-sicx-${_topic} "${_generated_d4}" "${PROJECT_BINARY_DIR}/sicx-work/${_topic}")
  set_tests_properties(sic_test_sicx_${_topic}_conformance PROPERTIES FIXTURES_REQUIRED "sicx_generated_d4" LABELS "sicx;unit" TIMEOUT 120)
 endforeach()
endif()
if(SIC_BUILD_TESTS)
  sic_add_sicx_app(sic-test-sicx-grouping "${PROJECT_SOURCE_DIR}/tests/sicx/grouping.cpp")
  add_test(NAME sic_test_sicx_grouping_invariance COMMAND sic-test-sicx-grouping "${PROJECT_BINARY_DIR}/sicx-work/grouping")
  set_tests_properties(sic_test_sicx_grouping_invariance PROPERTIES LABELS "sicx;unit")
endif()

sic_add_sicx_app(sic-test-sicx-failure-helper "${_sicx_dir}/model/failure_helper.cpp")
add_test(NAME sic_test_sicx_failure_helper COMMAND sic-test-sicx-failure-helper)
set_tests_properties(sic_test_sicx_failure_helper PROPERTIES LABELS "unit;sicx" TIMEOUT 120)

sic_add_sicx_app(sic-test-sicx-failure-categories "${_sicx_dir}/failures/categories.cpp")
target_sources(sic-test-sicx-failure-categories PRIVATE
  "${_sicx_dir}/failures/producer_cases.cpp"
  "${_sicx_dir}/failures/finalization_cases.cpp"
  "${PROJECT_SOURCE_DIR}/src/access/source_arguments.cpp"
  "${_sicx_dir}/fixtures/fixture.cpp")
target_link_libraries(sic-test-sicx-failure-categories PRIVATE sic_access sic_transport)
add_test(NAME sic_test_sicx_failure_categories COMMAND sic-test-sicx-failure-categories
  "${_generated_d4}" "${PROJECT_BINARY_DIR}/sicx-work/failure_categories")
set_tests_properties(sic_test_sicx_failure_categories PROPERTIES FIXTURES_REQUIRED "sicx_generated_d4" LABELS "unit;sicx;sanitizer" TIMEOUT 120)
if(SIC_ENABLE_TEST_HOOKS)
  target_link_options(sic-test-sicx-failure-categories PRIVATE
    "-Wl,--wrap=fsync,--wrap=fstat,--wrap=ftruncate,--wrap=getrusage")
  list(APPEND SIC_HOOK_CONTROL_TESTS sic_test_sicx_failure_categories)
  set_property(TEST sic_test_sicx_failure_categories APPEND PROPERTY LABELS test_hooks)
endif()
