# SPDX-License-Identifier: AGPL-3.0-or-later
if(SIC_BUILD_TESTS)
 add_library(sic_test_assertions STATIC tests/assertions.cpp)
 target_include_directories(sic_test_assertions PUBLIC "${PROJECT_SOURCE_DIR}/tests")
endif()
# SPDX-License-Identifier: AGPL-3.0-or-later
# ---------------------------------------------------------------- tests
if(SIC_BUILD_TESTS)
  enable_testing()
  add_test(NAME sic_test_support_test_hooks_compiler COMMAND ${CMAKE_COMMAND}
    -DMODE=compiler -DCXX=${CMAKE_CXX_COMPILER} -DMSVC_MODE=${MSVC} -DHEADER=${_sic_hooks_header}
    -DENABLED=${SIC_TEST_HOOKS_MODE} -DWORK=${PROJECT_BINARY_DIR}/hooks-compiler
    -P ${PROJECT_SOURCE_DIR}/tests/cli/test_hooks.cmake)
  set_tests_properties(sic_test_support_test_hooks_compiler PROPERTIES LABELS "unit;policy")
  find_program(SIC_NODE_EXECUTABLE node)
  if(SIC_NODE_EXECUTABLE)
    add_test(NAME sic_test_support_performance_limits COMMAND ${SIC_NODE_EXECUTABLE} --test
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/performance/limits.test.mjs)
    set_tests_properties(sic_test_support_performance_limits PROPERTIES TIMEOUT 120)
    find_package(Git QUIET)
    if(Git_FOUND)
      add_test(NAME sic_test_support_performance_parameters COMMAND ${CMAKE_COMMAND} -E env
        "SIC_TEST_GIT=${GIT_EXECUTABLE}" ${SIC_NODE_EXECUTABLE} --test
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/performance/parameters.test.mjs)
    else()
      message(STATUS "Git not found: performance parameters test will be skipped")
      add_test(NAME sic_test_support_performance_parameters COMMAND ${CMAKE_COMMAND} -E echo "SKIP: git not found")
    endif()
    set_tests_properties(sic_test_support_performance_parameters PROPERTIES TIMEOUT 120
      SKIP_REGULAR_EXPRESSION "SKIP: git not found")
  endif()
  if(SIC_ENABLE_FLINT)
    sic_add_bounds_tests()
  endif()
  sic_add_executable(sic-test-core-weyl-heisenberg tests/core/weyl_heisenberg.cpp)
  sic_add_executable(sic-test-core-verify tests/core/verify.cpp)
  sic_add_executable(sic-test-core-fourier-overlaps tests/core/fourier_overlaps.cpp)
  foreach(t sic-test-core-weyl-heisenberg sic-test-core-verify sic-test-core-fourier-overlaps)
    string(REPLACE "-" "_" _test_name "${t}")
    add_test(NAME ${_test_name} COMMAND ${t})
  endforeach()
  sic_add_executable(sic-test-support-sha256 tests/support/sha256.cpp)
  if(SIC_ENABLE_FLINT)
    target_link_libraries(sic-test-support-sha256 PRIVATE sic_exact)
  endif()
  add_test(NAME sic_test_support_sha256_vectors COMMAND sic-test-support-sha256 "${CMAKE_CURRENT_BINARY_DIR}/sha256-fixtures")
  set_tests_properties(sic_test_support_sha256_vectors PROPERTIES LABELS unit)
  add_test(NAME sic_test_data_publication_fixture_hashes COMMAND ${CMAKE_COMMAND}
    -DROOT=${PROJECT_SOURCE_DIR}/tests/fixtures/numeric
    -P ${PROJECT_SOURCE_DIR}/tests/check_fixture_hashes.cmake)
  if(SIC_ENABLE_FLINT)
    sic_add_executable(sic-test-support-parse-records tests/support/parse_records.cpp)
    target_include_directories(sic-test-support-parse-records PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(sic-test-support-parse-records PRIVATE sic_access sic_decode sic_data)
    if(SIC_ENABLE_REMOTE)
      target_compile_definitions(sic-test-support-parse-records PRIVATE SIC_HAVE_CURL=1)
      target_include_directories(sic-test-support-parse-records PRIVATE ${CURL_INCLUDE_DIRS})
    endif()
    add_test(NAME sic_test_support_parse_records COMMAND sic-test-support-parse-records)
    set_tests_properties(sic_test_support_parse_records PROPERTIES LABELS unit)
  endif()
  sic_add_executable(sic-test-support-parse-number tests/support/parse_number.cpp)
  target_link_libraries(sic-test-support-parse-number PRIVATE sic_core sic_data)
  add_test(NAME sic_test_support_parse_number COMMAND sic-test-support-parse-number)
  set_tests_properties(sic_test_support_parse_number PROPERTIES LABELS unit)
  sic_add_executable(sic-test-support-json tests/support/json.cpp)
  add_test(NAME sic_test_support_json COMMAND sic-test-support-json)
  sic_add_executable(sic-test-support-durable-files tests/support/durable_file.cpp)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|aarch64|arm64)$")
    target_compile_definitions(sic-test-support-durable-files PRIVATE
      SIC_TEST_WRAP_IO=1
      SIC_TEST_WRAP_PUBLICATION=1)
    target_compile_definitions(sic-test-support-durable-files PRIVATE
      SIC_TEST_WRAP_FSYNC=1
      SIC_TEST_WRAP_FDATASYNC=1
      SIC_TEST_WRAP_WRITE=1
      SIC_TEST_WRAP_PWRITE=1
      SIC_TEST_WRAP_PREAD=1
      SIC_TEST_WRAP_CLOSE=1)
    target_compile_definitions(sic-test-support-durable-files PRIVATE
      SIC_TEST_WRAP_LINK=1
      SIC_TEST_WRAP_UNLINK=1
      SIC_TEST_WRAP_RENAME=1
      SIC_TEST_WRAP_SYSCALL=1)
    target_link_options(sic-test-support-durable-files PRIVATE
      "LINKER:--wrap=fsync,--wrap=fdatasync,--wrap=write,--wrap=pwrite,--wrap=pread,--wrap=__pread_chk,--wrap=close,--wrap=link,--wrap=unlink,--wrap=rename,--wrap=syscall")
  endif()
  add_test(NAME sic_test_support_durable_files COMMAND sic-test-support-durable-files)
  set_tests_properties(sic_test_support_durable_files PROPERTIES LABELS unit TIMEOUT 60)
  sic_add_executable(sic-test-core-fiducial-io tests/core/fiducial_io.cpp)
  add_test(NAME sic_test_core_fiducial_io COMMAND sic-test-core-fiducial-io ${CMAKE_CURRENT_BINARY_DIR}/fiducial-io-test)
  sic_add_executable(sic-test-core-interchange tests/core/interchange.cpp)
  set(_fixtures ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/numeric)
  add_test(NAME sic_test_core_interchange_roundtrip COMMAND sic-test-core-interchange ${_fixtures}/d8/m2/c0 ${_fixtures}/d16/m1/c1)
  if(SIC_ENABLE_FLINT)
    sic_add_executable(sic-test-exact-completed-cache-requirement tests/exact/storage/completed_cache_requirement.cpp)
    target_link_libraries(sic-test-exact-completed-cache-requirement PRIVATE sic_exact)
    target_include_directories(sic-test-exact-completed-cache-requirement PRIVATE
      ${SIC_EXACT_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME sic_test_exact_completed_cache_requirement COMMAND sic-test-exact-completed-cache-requirement)
    sic_add_executable(sic-test-exact-output-prefix tests/exact/storage/output_prefix.cpp)
    target_link_libraries(sic-test-exact-output-prefix PRIVATE sic_exact sic_sicx)
    target_include_directories(sic-test-exact-output-prefix PRIVATE
      ${SIC_EXACT_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(sic-test-exact-output-prefix PRIVATE Threads::Threads)
    add_test(NAME sic_test_exact_output_prefix COMMAND sic-test-exact-output-prefix)
    sic_add_executable(sic-test-exact-modular-selection tests/exact/certification/selection/modular_selection.cpp)
    target_link_libraries(sic-test-exact-modular-selection PRIVATE sic_exact)
    target_include_directories(sic-test-exact-modular-selection PRIVATE ${SIC_EXACT_DIR})
    add_test(NAME sic_test_exact_modular_selection COMMAND sic-test-exact-modular-selection)
    sic_add_executable(sic-test-exact-parallel-inverse tests/exact/certification/newton/parallel_inverse.cpp)
    target_link_libraries(sic-test-exact-parallel-inverse PRIVATE sic_exact)
    target_include_directories(sic-test-exact-parallel-inverse PRIVATE ${SIC_EXACT_DIR})
    add_test(NAME sic_test_exact_parallel_inverse COMMAND sic-test-exact-parallel-inverse)
    sic_add_executable(sic-test-exact-parallel-ball-product tests/exact/certification/newton/parallel_ball_product.cpp)
    target_link_libraries(sic-test-exact-parallel-ball-product PRIVATE sic_exact)
    target_include_directories(sic-test-exact-parallel-ball-product PRIVATE ${SIC_EXACT_DIR})
    if(SIC_ENABLE_TEST_HOOKS)
      add_test(NAME sic_test_exact_parallel_ball_product COMMAND sic-test-exact-parallel-ball-product full ${_sic_test_workers})
    endif()
    add_test(NAME sic_test_exact_parallel_ball_product_arithmetic COMMAND sic-test-exact-parallel-ball-product arithmetic ${_sic_test_workers})
    set_tests_properties(sic_test_exact_parallel_ball_product_arithmetic PROPERTIES LABELS "unit;exact")
    sic_add_executable(sic-test-exact-krawczyk-exclusion tests/exact/certification/newton/krawczyk_exclusion.cpp)
    target_link_libraries(sic-test-exact-krawczyk-exclusion PRIVATE sic_exact)
    target_include_directories(sic-test-exact-krawczyk-exclusion PRIVATE
      ${SIC_EXACT_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME sic_test_exact_krawczyk_exclusion COMMAND sic-test-exact-krawczyk-exclusion)
    sic_add_executable(sic-test-exact-linear-refinement tests/exact/certification/newton/linear_refinement.cpp)
    target_link_libraries(sic-test-exact-linear-refinement PRIVATE sic_exact)
    target_include_directories(sic-test-exact-linear-refinement PRIVATE
      ${SIC_EXACT_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME sic_test_exact_linear_refinement COMMAND sic-test-exact-linear-refinement)
    sic_add_executable(sic-test-exact-newton-correction tests/exact/certification/newton/newton_correction.cpp)
    target_link_libraries(sic-test-exact-newton-correction PRIVATE sic_exact)
    target_include_directories(sic-test-exact-newton-correction PRIVATE ${SIC_EXACT_DIR})
    add_test(NAME sic_test_exact_newton_correction COMMAND sic-test-exact-newton-correction)
    sic_add_executable(sic-test-construct-class-native-share tests/exact/analytic/class_native_share.cpp)
    target_link_libraries(sic-test-construct-class-native-share PRIVATE sic_exact)
    target_include_directories(sic-test-construct-class-native-share PRIVATE
      ${SIC_EXACT_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME sic_test_construct_class_native_share COMMAND sic-test-construct-class-native-share)
    sic_add_executable(sic-test-construct-cpu-allowance tests/construct/cpu_allowance.cpp)
    target_link_libraries(sic-test-construct-cpu-allowance PRIVATE sic_construct sic_exact)
    target_include_directories(sic-test-construct-cpu-allowance PRIVATE
      ${SIC_EXACT_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME sic_test_construct_cpu_allowance COMMAND sic-test-construct-cpu-allowance)
    sic_add_executable(sic-test-exact-phase-memory tests/exact/runtime/phase_accounting.cpp)
    target_link_libraries(sic-test-exact-phase-memory PRIVATE sic_exact)
    target_include_directories(sic-test-exact-phase-memory PRIVATE
      ${SIC_EXACT_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(sic-test-exact-phase-memory PRIVATE Threads::Threads)
    add_test(NAME sic_test_exact_phase_memory COMMAND sic-test-exact-phase-memory)
    sic_add_executable(sic-test-exact-memory-trace tests/exact/runtime/memory_trace.cpp)
    target_link_libraries(sic-test-exact-memory-trace PRIVATE sic_exact)
    target_include_directories(sic-test-exact-memory-trace PRIVATE
      ${SIC_EXACT_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(sic-test-exact-memory-trace PRIVATE Threads::Threads)
    add_test(NAME sic_test_exact_memory_trace COMMAND sic-test-exact-memory-trace)
    sic_add_executable(sic-test-exact-class-memory tests/exact/runtime/class_memory.cpp)
    target_link_libraries(sic-test-exact-class-memory PRIVATE sic_exact)
    target_include_directories(sic-test-exact-class-memory PRIVATE
      ${SIC_EXACT_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(sic-test-exact-class-memory PRIVATE Threads::Threads)
    add_test(NAME sic_test_exact_class_memory COMMAND sic-test-exact-class-memory)
    sic_add_executable(sic-test-support-memory-admission tests/exact/runtime/memory_admission.cpp)
    target_link_libraries(sic-test-support-memory-admission PRIVATE sic_exact)
    target_include_directories(sic-test-support-memory-admission PRIVATE
      ${SIC_EXACT_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(sic-test-support-memory-admission PRIVATE Threads::Threads)
    add_test(NAME sic_test_support_memory_admission COMMAND sic-test-support-memory-admission)
    sic_add_executable(sic-test-exact-seed-kernel-memory tests/exact/runtime/seed_kernel_memory.cpp)
    target_link_libraries(sic-test-exact-seed-kernel-memory PRIVATE sic_construct sic_exact)
    target_include_directories(sic-test-exact-seed-kernel-memory PRIVATE
      ${SIC_EXACT_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(sic-test-exact-seed-kernel-memory PRIVATE Threads::Threads)
    add_test(NAME sic_test_exact_seed_kernel_memory COMMAND sic-test-exact-seed-kernel-memory)
    sic_add_executable(sic-test-exact-newton-resident tests/construct/resident_memory.cpp)
    target_link_libraries(sic-test-exact-newton-resident PRIVATE sic_construct sic_exact sic_sicx)
    target_include_directories(sic-test-exact-newton-resident PRIVATE
      ${SIC_EXACT_DIR})
    add_test(NAME sic_test_exact_newton_resident COMMAND sic-test-exact-newton-resident)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      sic_add_executable(sic-test-exact-completed-class-cache tests/exact/storage/completed_class_cache.cpp)
      target_link_libraries(sic-test-exact-completed-class-cache PRIVATE sic_exact sic_sicx)
      target_include_directories(sic-test-exact-completed-class-cache PRIVATE ${SIC_EXACT_DIR})
      add_test(NAME sic_test_exact_completed_class_cache COMMAND sic-test-exact-completed-class-cache)
    endif()
    sic_add_executable(sic-test-exact-streamed-cycle tests/exact/analytic/table_traversal.cpp)
    target_link_libraries(sic-test-exact-streamed-cycle PRIVATE sic_exact)
    add_test(NAME sic_test_exact_node_slabs COMMAND sic-test-exact-streamed-cycle nodes)
    sic_add_executable(sic-test-exact-balanced-product tests/exact/analytic/balanced_product.cpp)
    target_link_libraries(sic-test-exact-balanced-product PRIVATE sic_exact)
    sic_add_executable(sic-test-exact-periodic-grid tests/exact/analytic/periodic_grid.cpp)
    target_link_libraries(sic-test-exact-periodic-grid PRIVATE sic_exact)
    sic_add_executable(sic-test-exact-quadratic-transform tests/exact/transform/quadratic_transform.cpp)
    target_link_libraries(sic-test-exact-quadratic-transform PRIVATE sic_exact)
    sic_add_executable(sic-test-exact-analytic-generator tests/exact/analytic/analytic_generator.cpp)
    target_link_libraries(sic-test-exact-analytic-generator PRIVATE sic_exact sic_sicx)
    sic_add_executable(sic-test-exact-relative-projection tests/exact/transform/relative_projection.cpp)
    target_link_libraries(sic-test-exact-relative-projection PRIVATE sic_exact)
    sic_add_executable(sic-test-exact-step-selection tests/exact/analytic/step_selection.cpp)
    target_link_libraries(sic-test-exact-step-selection PRIVATE sic_exact)
    sic_add_executable(sic-test-exact-cyclotomic-trace tests/exact/analytic/cyclotomic_trace.cpp)
    target_link_libraries(sic-test-exact-cyclotomic-trace PRIVATE sic_exact)
    sic_add_executable(sic-test-exact-trace-band tests/exact/transform/trace_band.cpp)
    target_link_libraries(sic-test-exact-trace-band PRIVATE sic_exact)
    if(SIC_ENABLE_TEST_HOOKS)
      add_test(NAME sic_test_exact_direct_eta_fault_unwind COMMAND sic-test-exact-direct-eta fault)
      set_tests_properties(sic_test_exact_direct_eta_fault_unwind PROPERTIES LABELS "unit;exact;sanitizer")
    endif()
    sic_add_executable(sic-test-exact-direct-eta tests/exact/analytic/direct_eta.cpp)
    target_link_libraries(sic-test-exact-direct-eta PRIVATE sic_exact)
    sic_add_executable(sic-test-construct-ghost-reconstruction tests/exact/certification/reconstruction/ghost_reconstruction.cpp)
    target_link_libraries(sic-test-construct-ghost-reconstruction PRIVATE sic_exact)
    sic_add_executable(sic-test-support-lattice-coordinates tests/exact/codec/lattice_coordinates.cpp)
    target_link_libraries(sic-test-support-lattice-coordinates PRIVATE sic_exact sic_sicx)
    sic_add_executable(sic-test-support-row-worker-team tests/support/row_worker_team.cpp)
    target_link_libraries(sic-test-support-row-worker-team PRIVATE sic_exact)
    sic_add_executable(sic-test-exact-newton-vectors tests/exact/certification/newton/newton_vectors.cpp)
    target_link_libraries(sic-test-exact-newton-vectors PRIVATE sic_exact)
    sic_add_executable(sic-test-exact-selection-arithmetic tests/exact/certification/selection/selection_arithmetic.cpp)
    target_link_libraries(sic-test-exact-selection-arithmetic PRIVATE sic_exact)
    sic_add_executable(sic-test-exact-ball-file tests/exact/storage/ball_file.cpp)
    target_link_libraries(sic-test-exact-ball-file PRIVATE sic_exact sic_sicx)
    sic_add_executable(sic-test-exact-orbit-table tests/exact/group/orbit_table.cpp)
    target_link_libraries(sic-test-exact-orbit-table PRIVATE sic_exact)
    sic_add_executable(sic-test-exact-group-checkpoint tests/exact/group/group_checkpoint.cpp)
    target_link_libraries(sic-test-exact-group-checkpoint PRIVATE sic_exact)
    sic_add_executable(sic-test-exact-group-closure tests/exact/group/closure.cpp)
    target_link_libraries(sic-test-exact-group-closure PRIVATE sic_exact)
    add_test(NAME sic_test_exact_group_closure COMMAND sic-test-exact-group-closure)
    set_tests_properties(sic_test_exact_group_closure PROPERTIES LABELS "unit;exact" TIMEOUT 120)
    sic_add_executable(sic-test-exact-table-checkpoint tests/exact/storage/table_checkpoint.cpp)
    target_link_libraries(sic-test-exact-table-checkpoint PRIVATE sic_exact sic_sicx)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|aarch64|arm64)$")
      sic_add_executable(sic-test-exact-storage-publication tests/exact/storage/publication.cpp)
      target_link_libraries(sic-test-exact-storage-publication PRIVATE sic_exact sic_sicx)
      target_include_directories(sic-test-exact-storage-publication PRIVATE ${SIC_EXACT_DIR})
      target_compile_definitions(sic-test-exact-storage-publication PRIVATE
        SIC_TEST_WRAP_IO=1
        SIC_TEST_WRAP_PUBLICATION=1
        SIC_TEST_WRAP_STORAGE=1)
      target_compile_definitions(sic-test-exact-storage-publication PRIVATE
        SIC_TEST_WRAP_FSYNC=1
        SIC_TEST_WRAP_FDATASYNC=1
        SIC_TEST_WRAP_WRITE=1
        SIC_TEST_WRAP_PWRITE=1
        SIC_TEST_WRAP_PREAD=1
        SIC_TEST_WRAP_CLOSE=1
        SIC_TEST_WRAP_OPEN=1
        SIC_TEST_WRAP_MKSTEMP=1
        SIC_TEST_WRAP_MKDTEMP=1
        SIC_TEST_WRAP_FOPEN=1
        SIC_TEST_WRAP_FCLOSE=1
        SIC_TEST_WRAP_FFLUSH=1
        SIC_TEST_WRAP_FWRITE=1
        SIC_TEST_WRAP_FPUTC=1
        SIC_TEST_WRAP_FDOPEN=1
        SIC_TEST_WRAP_READ=1
        SIC_TEST_WRAP_FREAD=1
        SIC_TEST_WRAP_FERROR=1
        SIC_TEST_WRAP_FSTAT=1)
      target_compile_definitions(sic-test-exact-storage-publication PRIVATE
        SIC_TEST_WRAP_LINK=1
        SIC_TEST_WRAP_UNLINK=1
        SIC_TEST_WRAP_RENAME=1
        SIC_TEST_WRAP_SYSCALL=1)
      target_link_options(sic-test-exact-storage-publication PRIVATE
        "LINKER:--wrap=fsync,--wrap=fdatasync,--wrap=write,--wrap=pwrite,--wrap=pread,--wrap=__pread_chk,--wrap=close,--wrap=link,--wrap=unlink,--wrap=rename,--wrap=syscall,--wrap=open,--wrap=mkstemp,--wrap=mkdtemp,--wrap=fopen,--wrap=fclose,--wrap=fflush,--wrap=fwrite,--wrap=fputc,--wrap=fdopen,--wrap=read,--wrap=__read_chk,--wrap=fread,--wrap=ferror,--wrap=fstat")
      add_test(NAME sic_test_exact_storage_publication COMMAND sic-test-exact-storage-publication)
      set_tests_properties(sic_test_exact_storage_publication PROPERTIES LABELS unit TIMEOUT 120)
    endif()
    sic_add_executable(sic-test-support-worker-team tests/support/worker_team.cpp)
    target_link_libraries(sic-test-support-worker-team PRIVATE sic_exact)
    add_test(NAME sic_test_support_worker_team COMMAND sic-test-support-worker-team)
    set_tests_properties(sic_test_support_worker_team PROPERTIES LABELS "unit;support;sanitizer" TIMEOUT 120)
    sic_add_executable(sic-test-support-parallel tests/support/parallel.cpp)
    target_link_libraries(sic-test-support-parallel PRIVATE sic_exact)
    sic_add_executable(sic-test-exact-leaf-transform tests/exact/transform/leaf_transform.cpp)
    target_link_libraries(sic-test-exact-leaf-transform PRIVATE sic_exact)
    sic_add_executable(sic-test-exact-fixed-field-tower tests/exact/transform/fixed_field_tower.cpp)
    target_link_libraries(sic-test-exact-fixed-field-tower PRIVATE sic_exact sic_sicx)
    sic_add_executable(sic-test-exact-leaf-codec tests/exact/storage/exact_leaf_codec.cpp)
    target_link_libraries(sic-test-exact-leaf-codec PRIVATE sic_exact sic_sicx)
    foreach(t sic-test-exact-streamed-cycle sic-test-exact-balanced-product sic-test-exact-periodic-grid sic-test-exact-quadratic-transform sic-test-exact-analytic-generator sic-test-exact-relative-projection sic-test-exact-step-selection sic-test-exact-cyclotomic-trace sic-test-exact-trace-band sic-test-exact-direct-eta sic-test-construct-ghost-reconstruction sic-test-support-lattice-coordinates sic-test-support-row-worker-team sic-test-exact-newton-vectors sic-test-exact-selection-arithmetic sic-test-exact-ball-file sic-test-exact-orbit-table sic-test-exact-group-checkpoint sic-test-exact-table-checkpoint sic-test-support-parallel sic-test-exact-leaf-transform sic-test-exact-fixed-field-tower sic-test-exact-leaf-codec)
      target_include_directories(${t} PRIVATE ${SIC_EXACT_DIR})
      string(REPLACE "-" "_" _test_name "${t}")
      if(t STREQUAL "sic-test-exact-streamed-cycle")
        add_test(NAME ${_test_name} COMMAND ${t} cycles)
      else()
        string(REPLACE "-" "_" _test_name "${t}")
    add_test(NAME ${_test_name} COMMAND ${t})
      endif()
    endforeach()
    sic_add_executable(sic-test-support-admission tests/support/admission.cpp)
    target_link_libraries(sic-test-support-admission PRIVATE sic_decode)
    add_test(NAME sic_test_support_shared_admission_and_workers COMMAND sic-test-support-admission)
    set_tests_properties(sic_test_support_shared_admission_and_workers PROPERTIES LABELS "unit;sanitizer" TIMEOUT 30)
    sic_add_executable(sic-test-exact-unwind tests/exact/analytic/exact_unwind.cpp)
    target_link_libraries(sic-test-exact-unwind PRIVATE sic_construct sic_exact sic_sicx)
    target_include_directories(sic-test-exact-unwind PRIVATE ${SIC_EXACT_DIR})
    add_test(NAME sic_test_exact_exception_unwind COMMAND sic-test-exact-unwind)
    set_tests_properties(sic_test_exact_exception_unwind PROPERTIES LABELS "unit;exact;sanitizer" TIMEOUT 120)
    # Header hygiene: every exact header in two translation units of one program.
    add_executable(sic-test-exact-header-odr tests/exact/header_linkage/odr_a.cpp tests/exact/header_linkage/odr_b.cpp)
    target_link_libraries(sic-test-exact-header-odr PRIVATE sic_exact)
    add_test(NAME sic_test_exact_header_odr COMMAND sic-test-exact-header-odr)
    # Band heap release and trim-before-admission controls.
    sic_add_executable(sic-test-exact-band-heap-release tests/exact/analytic/band_heap_release.cpp)
    target_link_libraries(sic-test-exact-band-heap-release PRIVATE SicFLINT::SicFLINT)
    target_include_directories(sic-test-exact-band-heap-release PRIVATE ${SIC_EXACT_DIR})
    target_link_libraries(sic-test-exact-band-heap-release PRIVATE Threads::Threads)
    add_test(NAME sic_test_exact_band_heap_release COMMAND sic-test-exact-band-heap-release)
    # Sanitizer allocators cannot satisfy glibc malloc_trim RSS-release assertions.
    include(${PROJECT_SOURCE_DIR}/cmake/checks/AllocatorTests.cmake)
    sic_disable_test_with_sanitizer(sic_test_exact_band_heap_release)
    sic_add_executable(sic-test-support-resources tests/support/resources.cpp)
    target_link_libraries(sic-test-support-resources PRIVATE sic_exact)
    target_include_directories(sic-test-support-resources PRIVATE ${SIC_EXACT_DIR})
    add_test(NAME sic_test_support_resources COMMAND sic-test-support-resources)
    sic_add_executable(sic-test-exact-arithmetic tests/exact/arithmetic/arithmetic.cpp)
    target_link_libraries(sic-test-exact-arithmetic PRIVATE sic_exact)
    target_include_directories(sic-test-exact-arithmetic PRIVATE ${SIC_EXACT_DIR})
    add_test(NAME sic_test_exact_arithmetic COMMAND sic-test-exact-arithmetic)
    sic_add_executable(sic-test-exact-quadrature-bounds tests/exact/certification/bounds/quadrature_bounds.cpp)
    target_link_libraries(sic-test-exact-quadrature-bounds PRIVATE sic_exact)
    target_include_directories(sic-test-exact-quadrature-bounds PRIVATE ${SIC_EXACT_DIR})
    add_test(NAME sic_test_exact_quadrature_bounds COMMAND sic-test-exact-quadrature-bounds)
    sic_add_executable(sic-test-exact-root-unity-isolation tests/exact/certification/bounds/root_unity_isolation.cpp)
    target_link_libraries(sic-test-exact-root-unity-isolation PRIVATE sic_exact)
    target_include_directories(sic-test-exact-root-unity-isolation PRIVATE ${SIC_EXACT_DIR})
    add_test(NAME sic_test_exact_root_unity_isolation COMMAND sic-test-exact-root-unity-isolation)
    sic_add_executable(sic-test-exact-leaf-store tests/exact/storage/leaf_store.cpp)
    target_link_libraries(sic-test-exact-leaf-store PRIVATE sic_exact sic_sicx)
    target_include_directories(sic-test-exact-leaf-store PRIVATE ${SIC_EXACT_DIR})
    add_test(NAME sic_test_exact_leaf_store COMMAND sic-test-exact-leaf-store)
    sic_add_executable(sic-test-exact-height-bounds tests/exact/certification/bounds/height_bounds.cpp)
    target_link_libraries(sic-test-exact-height-bounds PRIVATE sic_exact)
    target_include_directories(sic-test-exact-height-bounds PRIVATE ${SIC_EXACT_DIR})
    add_test(NAME sic_test_exact_height_bounds COMMAND sic-test-exact-height-bounds)
  endif()
  add_test(NAME sic_test_support_cli_verify_d3
           COMMAND sic-check ${CMAKE_CURRENT_SOURCE_DIR}/../cpp/tests/fixtures/core/d3_simple.txt)
  if(SIC_ENABLE_FLINT AND SIC_BUILD_REGRESSION)
    # Native SICX producer goldens (sic-construct, defined in cmake/components/Sicx.cmake).
    add_subdirectory(tests/construct/canonical)
    # Native group metadata conformance records.
    add_subdirectory(tests/exact/group/canonical)
  endif()
endif()


if(TARGET sic-test-construct-memory)
 target_link_libraries(sic-test-construct-memory PRIVATE sic_test_assertions sic_construct)
endif()

if(SIC_BUILD_TESTS)
  add_executable(sic-test-construct-quarantine tests/construct/quarantine.cpp)
  target_link_libraries(sic-test-construct-quarantine PRIVATE sic_core sic_construct_output)
  target_include_directories(sic-test-construct-quarantine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME sic_test_construct_quarantine_ownership COMMAND sic-test-construct-quarantine)
  set_tests_properties(sic_test_construct_quarantine_ownership PROPERTIES LABELS "unit;conformance;sanitizer")
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT)
 foreach(topic table_memory resource_model)
  string(REPLACE "_" "-" topic_target "${topic}")
  sic_add_executable(sic-test-construct-${topic_target} tests/construct/${topic}.cpp)
  target_link_libraries(sic-test-construct-${topic_target} PRIVATE sic_construct sic_exact)
  add_test(NAME sic_test_construct_${topic} COMMAND sic-test-construct-${topic_target})
  set_tests_properties(sic_test_construct_${topic} PROPERTIES LABELS "construct;unit;sanitizer" TIMEOUT 120)
 endforeach()
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT)
  sic_add_executable(sic-test-support-flint-owners tests/support/flint_owners.cpp)
  target_link_libraries(sic-test-support-flint-owners PRIVATE sic_support SicFLINT::SicFLINT)
  foreach(_case "matrix;flint_matrix_owners" "rational;fmpq_matrix_unwind"
      "complex;acb_matrix_unwind" "balls;ball_vector_borrows")
    list(GET _case 0 _argument)
    list(GET _case 1 _topic)
    add_test(NAME sic_test_support_${_topic} COMMAND sic-test-support-flint-owners ${_argument})
    set_tests_properties(sic_test_support_${_topic} PROPERTIES LABELS "unit;support;sanitizer" TIMEOUT 120)
  endforeach()
  # ASan-instrumented builds can run the unwind checks with leak detection enabled.
  string(TOUPPER "${CMAKE_BUILD_TYPE}" _owner_configuration)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
      "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_owner_configuration}}" MATCHES "-fsanitize=[^ ]*address")
    foreach(_argument rational complex balls)
      add_test(NAME sic_test_support_flint_owners_lsan_${_argument}
        COMMAND sic-test-support-flint-owners ${_argument})
      set_tests_properties(sic_test_support_flint_owners_lsan_${_argument} PROPERTIES
        LABELS "unit;support;sanitizer" TIMEOUT 120
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1;LSAN_OPTIONS=exitcode=23")
    endforeach()
  endif()
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  sic_add_executable(sic-test-support-exception-ownership tests/support/exception_ownership.cpp)
  target_link_libraries(sic-test-support-exception-ownership PRIVATE sic_data sic_exact sic_transport)
  foreach(_case "exact;ceil" "data;fp64" "transport;parts" "sicx;stream" "exact;transform")
    list(GET _case 0 _component)
    list(GET _case 1 _argument)
    if(_argument STREQUAL "ceil" AND NOT SIC_ENABLE_TEST_HOOKS)
      continue()
    endif()
    add_test(NAME sic_test_${_component}_${_argument}_exception_ownership
      COMMAND sic-test-support-exception-ownership ${_argument})
    set_tests_properties(sic_test_${_component}_${_argument}_exception_ownership PROPERTIES
      LABELS "unit;sanitizer;${_component}" TIMEOUT 120)
  endforeach()
endif()

if(SIC_BUILD_TESTS)
  add_test(NAME sic_test_support_cli_verify_d4_oracle
    COMMAND sic-check "${PROJECT_SOURCE_DIR}/tests/fixtures/core/d4_sic_from_ghost.txt")
  set_tests_properties(sic_test_support_cli_verify_d4_oracle PROPERTIES LABELS "integration")
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  sic_add_executable(sic-test-access-full-cache tests/access/full_cache.cpp)
  target_link_libraries(sic-test-access-full-cache PRIVATE sic_transport sic_access)
  target_include_directories(sic-test-access-full-cache PRIVATE "${PROJECT_SOURCE_DIR}/tests")
  target_compile_definitions(sic-test-access-full-cache PRIVATE
    SIC_TEST_WRAP_READ=1
    SIC_TEST_WRAP_FSYNC=1
    SIC_TEST_WRAP_FDATASYNC=1
    SIC_TEST_WRAP_WRITE=1
    SIC_TEST_WRAP_PWRITE=1
    SIC_TEST_WRAP_PREAD=1
    SIC_TEST_WRAP_CLOSE=1)
  target_link_options(sic-test-access-full-cache PRIVATE
    "LINKER:--wrap=read,--wrap=__read_chk,--wrap=fsync,--wrap=fdatasync,--wrap=write,--wrap=pwrite,--wrap=pread,--wrap=__pread_chk,--wrap=close")
  add_test(NAME sic_test_access_full_cache COMMAND sic-test-access-full-cache "${SIC_TEST_FIXTURES_DIR}/d4.sicx")
  set_tests_properties(sic_test_access_full_cache PROPERTIES LABELS "unit;access;sanitizer" TIMEOUT 60)
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|aarch64|arm64)$")
  sic_add_executable(sic-test-support-io-adapters tests/support/io_adapters.cpp)
  target_link_libraries(sic-test-support-io-adapters PRIVATE sic_transport sic_access)
  target_compile_definitions(sic-test-support-io-adapters PRIVATE
    SIC_TEST_WRAP_PUBLICATION=1)
  target_compile_definitions(sic-test-support-io-adapters PRIVATE
    SIC_TEST_WRAP_FSYNC=1
    SIC_TEST_WRAP_FDATASYNC=1
    SIC_TEST_WRAP_WRITE=1
    SIC_TEST_WRAP_PWRITE=1
    SIC_TEST_WRAP_PREAD=1
    SIC_TEST_WRAP_CLOSE=1)
  target_compile_definitions(sic-test-support-io-adapters PRIVATE
    SIC_TEST_WRAP_LINK=1
    SIC_TEST_WRAP_UNLINK=1
    SIC_TEST_WRAP_RENAME=1
    SIC_TEST_WRAP_SYSCALL=1)
  target_link_options(sic-test-support-io-adapters PRIVATE
    "LINKER:--wrap=fsync,--wrap=fdatasync,--wrap=write,--wrap=pwrite,--wrap=pread,--wrap=__pread_chk,--wrap=close,--wrap=link,--wrap=unlink,--wrap=rename,--wrap=syscall")
  add_test(NAME sic_test_support_io_adapters COMMAND sic-test-support-io-adapters)
  set_tests_properties(sic_test_support_io_adapters PROPERTIES LABELS "unit;support;sanitizer" TIMEOUT 60)
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  sic_add_executable(sic-test-exact-storage-failure-categories tests/exact/storage/failure_categories.cpp)
  target_link_libraries(sic-test-exact-storage-failure-categories PRIVATE sic_exact)
  target_compile_definitions(sic-test-exact-storage-failure-categories PRIVATE
    SIC_TEST_WRAP_FSYNC=1
    SIC_TEST_WRAP_FSEEKO=1
    SIC_TEST_WRAP_FWRITE=1
    SIC_TEST_WRAP_FGETC=1)
  target_link_options(sic-test-exact-storage-failure-categories PRIVATE
    "LINKER:--wrap=fsync,--wrap=fseeko,--wrap=fwrite,--wrap=fgetc")
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles("#include <ios>
#ifndef __GLIBCXX__
#error libstdc++ required
#endif
int main() {}" SIC_TEST_GLIBCXX)
  if(SIC_TEST_GLIBCXX)
    target_compile_definitions(sic-test-exact-storage-failure-categories PRIVATE
      SIC_TEST_WRAP_STREAMS=1)
    target_link_options(sic-test-exact-storage-failure-categories PRIVATE
      "LINKER:--wrap=_ZNSt9basic_iosIcSt11char_traitsIcEE5clearESt12_Ios_Iostate")
  else()
    message(STATUS "Storage badbit fault controls require libstdc++; other storage controls remain enabled")
  endif()
  add_test(NAME sic_test_exact_storage_failure_categories COMMAND sic-test-exact-storage-failure-categories)
  set_tests_properties(sic_test_exact_storage_failure_categories PROPERTIES LABELS "unit;exact;sanitizer" TIMEOUT 120)
endif()

if(SIC_BUILD_TESTS)
  sic_add_executable(sic-test-core-failure-categories tests/core/failure_categories.cpp)
  target_link_libraries(sic-test-core-failure-categories PRIVATE sic_search ${CMAKE_DL_LIBS})
  target_include_directories(sic-test-core-failure-categories PRIVATE "${PROJECT_SOURCE_DIR}/src")
  add_test(NAME sic_test_core_failure_categories COMMAND sic-test-core-failure-categories)
  set_tests_properties(sic_test_core_failure_categories PROPERTIES LABELS "unit;core" TIMEOUT 120)
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT)
  sic_add_executable(sic-test-exact-failure-categories tests/exact/certification/failure_categories.cpp)
  target_link_libraries(sic-test-exact-failure-categories PRIVATE sic_exact)
  target_include_directories(sic-test-exact-failure-categories PRIVATE "${PROJECT_SOURCE_DIR}/tests")
  add_test(NAME sic_test_exact_failure_categories COMMAND sic-test-exact-failure-categories)
  set_tests_properties(sic_test_exact_failure_categories PROPERTIES LABELS "unit;exact;sanitizer" TIMEOUT 120)
  if(SIC_ENABLE_TEST_HOOKS)
    list(APPEND SIC_HOOK_CONTROL_TESTS sic_test_exact_failure_categories)
  endif()
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|aarch64|arm64)$")
  sic_add_executable(sic-test-support-failure-categories tests/support/failure_categories.cpp)
  target_link_libraries(sic-test-support-failure-categories PRIVATE sic_data sic_exact sic_decode sic_bounds)
  target_include_directories(sic-test-support-failure-categories PRIVATE "${PROJECT_SOURCE_DIR}/tests" ${SIC_EXACT_DIR})
  target_compile_definitions(sic-test-support-failure-categories PRIVATE
    SIC_TEST_WRAP_PUBLICATION=1
    SIC_TEST_WRAP_NUMA=1)
  target_compile_definitions(sic-test-support-failure-categories PRIVATE
    SIC_TEST_WRAP_READ=1
    SIC_TEST_WRAP_FSYNC=1
    SIC_TEST_WRAP_FDATASYNC=1
    SIC_TEST_WRAP_WRITE=1
    SIC_TEST_WRAP_PWRITE=1
    SIC_TEST_WRAP_PREAD=1
    SIC_TEST_WRAP_CLOSE=1)
  target_compile_definitions(sic-test-support-failure-categories PRIVATE
    SIC_TEST_WRAP_LINK=1
    SIC_TEST_WRAP_UNLINK=1
    SIC_TEST_WRAP_RENAME=1
    SIC_TEST_WRAP_SYSCALL=1)
  target_link_options(sic-test-support-failure-categories PRIVATE
    "LINKER:--wrap=acb_sqrt,--wrap=read,--wrap=__read_chk,--wrap=fsync,--wrap=fdatasync,--wrap=write,--wrap=pwrite,--wrap=pread,--wrap=__pread_chk,--wrap=close,--wrap=link,--wrap=unlink,--wrap=rename,--wrap=syscall")
  add_test(NAME sic_test_support_failure_categories COMMAND sic-test-support-failure-categories
    "${SIC_TEST_FIXTURES_DIR}/d4.sicx" "${SIC_TEST_FIXTURES_DIR}/d4-c0.json")
  set_tests_properties(sic_test_support_failure_categories PROPERTIES LABELS "unit;support;sanitizer" TIMEOUT 120)
endif()

if(SIC_BUILD_TESTS)
  sic_add_executable(sic-test-core-canonical-lift-contract tests/core/canonical_lift_contract.cpp)
  target_link_libraries(sic-test-core-canonical-lift-contract PRIVATE sic_core)
  add_test(NAME sic_test_core_canonical_lift_contract COMMAND sic-test-core-canonical-lift-contract)
  set_tests_properties(sic_test_core_canonical_lift_contract PROPERTIES LABELS "unit;core" TIMEOUT 120)
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  sic_add_executable(sic-test-decode-public-contract tests/decode/public_contract.cpp)
  target_link_libraries(sic-test-decode-public-contract PRIVATE sic_decode)
  add_test(NAME sic_test_decode_public_contract COMMAND sic-test-decode-public-contract "${SIC_TEST_FIXTURES_DIR}/d4.sicx")
  set_tests_properties(sic_test_decode_public_contract PROPERTIES LABELS "unit;decode" TIMEOUT 120)
endif()

# Mixed-mode category controls participate in the instrumented control selector.
if(SIC_BUILD_TESTS AND SIC_ENABLE_TEST_HOOKS)
  foreach(_control sic_test_construct_failure_categories sic_test_exact_storage_failure_categories)
    if(TEST ${_control})
      list(APPEND SIC_HOOK_CONTROL_TESTS ${_control})
    endif()
  endforeach()
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  sic_add_executable(sic-test-exact-runtime-failure-categories tests/exact/certification/runtime_failures.cpp)
  target_link_libraries(sic-test-exact-runtime-failure-categories PRIVATE sic_exact)
  target_include_directories(sic-test-exact-runtime-failure-categories PRIVATE "${PROJECT_SOURCE_DIR}/tests")
  target_link_options(sic-test-exact-runtime-failure-categories PRIVATE "LINKER:--wrap=sched_setaffinity")
  add_test(NAME sic_test_exact_runtime_failure_categories COMMAND sic-test-exact-runtime-failure-categories)
  set_tests_properties(sic_test_exact_runtime_failure_categories PROPERTIES LABELS "unit;exact;sanitizer" TIMEOUT 120)
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT AND SIC_ENABLE_TEST_HOOKS)
  foreach(_case pairing coarse direct_invalid_argument direct_invalid_format direct_integrity
      direct_io direct_resource direct_precision direct_cancelled direct_unsupported
      direct_construction direct_untyped direct_native output_resume output_filesystem)
    set(_test "sic_test_construct_catch_${_case}")
    add_test(NAME ${_test} COMMAND sic-test-construct-failure-categories
      "${SIC_TEST_FIXTURES_DIR}/d4.sicx" "$<TARGET_FILE:sic-construct>" "${_case}")
    set_tests_properties(${_test} PROPERTIES LABELS "unit;construct;sanitizer" TIMEOUT 120)
    list(APPEND SIC_HOOK_CONTROL_TESTS ${_test})
  endforeach()
endif()

if(SIC_BUILD_TESTS AND SIC_ENABLE_FLINT AND TARGET sic-construct)
  add_test(NAME sic_test_construct_explicit_threads COMMAND "${CMAKE_COMMAND}"
    "-DPRODUCER=$<TARGET_FILE:sic-construct>" "-DWORK=${PROJECT_BINARY_DIR}/explicit-threads-test"
    -P "${PROJECT_SOURCE_DIR}/tests/construct/canonical/explicit_threads.cmake")
  set_tests_properties(sic_test_construct_explicit_threads PROPERTIES LABELS "construct;integration" TIMEOUT 120)
endif()
