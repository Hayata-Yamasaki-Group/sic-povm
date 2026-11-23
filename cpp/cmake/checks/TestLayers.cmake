# SPDX-License-Identifier: AGPL-3.0-or-later
# Empty means runtime discovery; explicit counts are never capped.
set(SIC_TEST_WORKERS "" CACHE STRING "Test workers (empty: runtime CPU allowance, minimum 3)")
set(_sic_test_workers "${SIC_TEST_WORKERS}")
if(SIC_TEST_WORKERS STREQUAL "")
  set(_sic_test_workers auto)
elseif(NOT SIC_TEST_WORKERS MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "SIC_TEST_WORKERS must be a positive integer")
endif()
# Explicit full controls: never inferred from names or the heavy label.
set(SIC_HOOK_CONTROL_TESTS
  sic_test_support_test_hooks_install_refusal
  sic_test_construct_memory_strategy_identity_s2
  sic_test_construct_memory_strategy_identity_s3
  sic_test_construct_memory_strategy_identity_s4
  sic_test_construct_memory_strategy_identity_s5
  sic_test_construct_memory_strategy_identity_s6
  sic_test_construct_memory_strategy_identity_reduced
  sic_test_construct_native_s2_phased_default_t1
  sic_test_construct_native_s2_phased_default_t1_repeat_2
  sic_test_construct_native_s2_phased_default_t4
  sic_test_construct_native_s2_phased_default_t4_repeat_2
  sic_test_construct_native_s2_phased_standard_t4
  sic_test_construct_native_s3_phased_default_t1
  sic_test_construct_native_s3_phased_default_t1_repeat_2
  sic_test_construct_native_s3_phased_default_t4
  sic_test_construct_native_s3_phased_default_t4_repeat_2
  sic_test_construct_native_s3_phased_standard_t4
  sic_test_construct_native_s4_phased_default_t1
  sic_test_construct_native_s4_phased_default_t1_repeat_2
  sic_test_construct_native_s4_phased_default_t4
  sic_test_construct_native_s4_phased_default_t4_repeat_2
  sic_test_construct_native_s4_phased_standard_t4
  sic_test_construct_native_s5_phased_default_t1
  sic_test_construct_native_s5_phased_default_t1_repeat_2
  sic_test_construct_native_s5_phased_default_t4
  sic_test_construct_native_s5_phased_default_t4_repeat_2
  sic_test_construct_native_s5_phased_standard_t4
  sic_test_construct_native_s6_phased_default_t1
  sic_test_construct_native_s6_phased_default_t1_repeat_2
  sic_test_construct_native_s6_phased_default_t4
  sic_test_construct_native_s6_phased_default_t4_repeat_2
  sic_test_construct_native_s6_phased_standard_t4
  sic_test_construct_native_s7_phased_default_t4
  sic_test_construct_native_s7_phased_default_t4_repeat_2
  sic_test_construct_quarantine_sic_failure
  sic_test_exact_ceil_exception_ownership
  sic_test_exact_parallel_ball_product
  sic_test_sicx_d16_kill_resume
  sic_test_sicx_d4_final_format_kill_resume
  sic_test_sicx_primitives_and_faults
  sic_test_support_alignment_cli_conformance
  sic_test_transport_parts_d16
  sic_test_transport_parts_d32
  sic_test_transport_parts_d4
  sic_test_transport_parts_d8
  sic_test_transport_parts_d8_maximal
  sic_test_construct_cli_injected_acceptance
  sic_test_exact_direct_eta_fault_unwind
  sic_test_construct_direct_eta_fault_plan
  sic_test_construct_direct_eta_fault_evaluate
  sic_test_construct_direct_eta_fault_nonfinite
  sic_test_construct_direct_eta_fault_admission
  sic_test_construct_direct_eta_fault_stop
  sic_test_construct_newton_defer_class
  sic_test_construct_newton_defer_entry
)
# Registration owns the primary layer; this final pass enforces component labels and limits.
function(sic_label_tests)
  set(_unit_tests
    sic_test_core_weyl_heisenberg
    sic_test_core_verify
    sic_test_core_fourier_overlaps
    sic_test_support_sha256_vectors
    sic_test_support_json
    sic_test_support_cli_parser
    sic_test_search_clifford_synthetic
    sic_test_core_policy
    sic_test_construct_quarantine_ownership
    sic_test_bounds_ball_bounds
    sic_test_exact_completed_cache_requirement
    sic_test_exact_output_prefix
    sic_test_exact_modular_selection
    sic_test_exact_parallel_inverse
    sic_test_exact_parallel_ball_product
    sic_test_exact_krawczyk_exclusion
    sic_test_exact_linear_refinement
    sic_test_exact_newton_correction
    sic_test_construct_cpu_allowance
    sic_test_construct_class_native_share
    sic_test_exact_phase_memory
    sic_test_exact_memory_trace
    sic_test_exact_class_memory
    sic_test_support_memory_admission
    sic_test_exact_seed_kernel_memory
    sic_test_exact_newton_resident
    sic_test_exact_completed_class_cache
    sic_test_exact_streamed_cycle
    sic_test_exact_node_slabs
    sic_test_exact_balanced_product
    sic_test_exact_periodic_grid
    sic_test_exact_quadratic_transform
    sic_test_exact_analytic_generator
    sic_test_exact_relative_projection
    sic_test_exact_step_selection
    sic_test_exact_cyclotomic_trace
    sic_test_exact_trace_band
    sic_test_exact_direct_eta
    sic_test_construct_ghost_reconstruction
    sic_test_support_lattice_coordinates
    sic_test_support_flint_matrix_owners
    sic_test_support_fmpq_matrix_unwind
    sic_test_support_acb_matrix_unwind
    sic_test_support_ball_vector_borrows
    sic_test_support_row_worker_team
    sic_test_exact_newton_vectors
    sic_test_exact_selection_arithmetic
    sic_test_exact_ball_file
    sic_test_exact_orbit_table
    sic_test_exact_group_checkpoint
    sic_test_exact_group_closure
    sic_test_exact_table_checkpoint
    sic_test_support_parallel
    sic_test_exact_leaf_transform
    sic_test_exact_fixed_field_tower
    sic_test_exact_leaf_codec
    sic_test_support_shared_admission_and_workers
    sic_test_exact_exception_unwind
    sic_test_exact_header_odr
    sic_test_exact_band_heap_release
    sic_test_support_resources
    sic_test_exact_arithmetic
    sic_test_exact_quadrature_bounds
    sic_test_exact_root_unity_isolation
    sic_test_exact_leaf_store
    sic_test_exact_height_bounds
    sic_test_sicx_primitives_and_faults
    sic_test_decode_enclosure_checks
    sic_test_decode_raw_comparator_fixtures
    sic_test_construct_component_conformance
    sic_test_construct_extensibility
    sic_test_sicx_grouping_invariance
  )
  set(_e2e_tests
    sic_test_sicx_d16_kill_resume
    sic_test_construct_d2
    sic_test_construct_d8_maximal
    sic_test_decode_decode_identity_d2
    sic_test_decode_decode_identity_d4
    sic_test_decode_decode_identity_d8
    sic_test_decode_decode_identity_d8_maximal
    sic_test_decode_decode_identity_d16
    sic_test_decode_decode_identity_d32
    sic_test_construct_native_s2_phased_default_t1
    sic_test_construct_native_s2_phased_default_t4
    sic_test_construct_native_s2_phased_default_t1_repeat_2
    sic_test_construct_native_s2_phased_default_t4_repeat_2
    sic_test_construct_native_s3_phased_default_t1
    sic_test_construct_native_s3_phased_default_t4
    sic_test_construct_native_s3_phased_default_t1_repeat_2
    sic_test_construct_native_s3_phased_default_t4_repeat_2
    sic_test_construct_native_s4_phased_default_t1
    sic_test_construct_native_s4_phased_default_t4
    sic_test_construct_native_s4_phased_default_t1_repeat_2
    sic_test_construct_native_s4_phased_default_t4_repeat_2
    sic_test_construct_native_s5_phased_default_t1
    sic_test_construct_native_s5_phased_default_t4
    sic_test_construct_native_s5_phased_default_t1_repeat_2
    sic_test_construct_native_s5_phased_default_t4_repeat_2
    sic_test_construct_native_s6_phased_default_t1
    sic_test_construct_native_s6_phased_default_t4
    sic_test_construct_native_s6_phased_default_t1_repeat_2
    sic_test_construct_native_s6_phased_default_t4_repeat_2
    sic_test_construct_native_s7_phased_default_t4
    sic_test_construct_native_s7_phased_default_t4_repeat_2
    sic_test_construct_native_s3_auto_default_t1
    sic_test_construct_native_s3_auto_default_t4
    sic_test_construct_native_s4_auto_default_t1
    sic_test_construct_native_s4_auto_default_t4
    sic_test_construct_native_s7_auto_standard_t4
    sic_test_construct_native_s2_phased_standard_t4
    sic_test_construct_native_s3_phased_standard_t4
    sic_test_construct_native_s4_phased_standard_t4
    sic_test_construct_native_s5_phased_standard_t4
    sic_test_construct_native_s6_phased_standard_t4
    sic_test_exact_group_metadata_standard16
    sic_test_exact_group_metadata_standard32
    sic_test_exact_group_metadata_standard64
    sic_test_exact_group_metadata_standard128
    sic_test_exact_group_metadata_maximal128
    sic_test_exact_group_metadata_standard256
    sic_test_exact_group_metadata_maximal512
    sic_test_exact_group_metadata_standard512
    sic_test_exact_group_metadata_maximal1024
    sic_test_exact_group_metadata_unknown_case_refused
  )
  set(_integration_tests
    sic_test_access_remote_access
    sic_test_access_remote_decode
    sic_test_access_remote_faults
    sic_test_access_remote_schedule
    sic_test_access_remote_versions
    sic_test_access_stop_file
    sic_test_bounds_admission
    sic_test_bounds_check_balls_inputs
    sic_test_construct_alignment_fixture_d16
    sic_test_construct_alignment_fixture_d32
    sic_test_construct_alignment_fixture_d64
    sic_test_construct_alignment_search_conformance
    sic_test_construct_branch_scratch_admission
    sic_test_construct_cli
    sic_test_construct_cli_injected_acceptance
    sic_test_construct_completed_replay_authentication
    sic_test_construct_direct_eta_fault_admission
    sic_test_construct_direct_eta_fault_evaluate
    sic_test_construct_direct_eta_fault_nonfinite
    sic_test_construct_direct_eta_fault_plan
    sic_test_construct_direct_eta_fault_stop
    sic_test_construct_file_route_cli
    sic_test_construct_file_route_conformance
    sic_test_construct_memory_selection
    sic_test_construct_memory_strategy_identity_reduced
    sic_test_construct_memory_strategy_identity_s2
    sic_test_construct_memory_strategy_identity_s3
    sic_test_construct_memory_strategy_identity_s4
    sic_test_construct_memory_strategy_identity_s5
    sic_test_construct_memory_strategy_identity_s6
    sic_test_construct_newton_defer_class
    sic_test_construct_newton_defer_entry
    sic_test_construct_quarantine_sic_failure
    sic_test_construct_runtime_isolation
    sic_test_core_fiducial_io
    sic_test_core_interchange_roundtrip
    sic_test_core_read_fiducial_csv
    sic_test_crt_cli_defaults
    sic_test_crt_d16
    sic_test_crt_d16_inject_act
    sic_test_crt_d16_inject_conj
    sic_test_crt_d16_inject_rep
    sic_test_crt_d16_shift_retry
    sic_test_crt_d32
    sic_test_crt_d4
    sic_test_crt_d4_bound_only
    sic_test_crt_d8
    sic_test_crt_d8_maximal
    sic_test_crt_help
    sic_test_crt_input_amendments
    sic_test_crt_input_boundaries
    sic_test_crt_input_cli
    sic_test_crt_input_fault_arb_intersection
    sic_test_crt_input_fault_fmpz_set_str
    sic_test_crt_input_fault_fstat
    sic_test_crt_input_fault_ftello
    sic_test_crt_input_fault_qqbar_roots
    sic_test_crt_input_fuzz
    sic_test_crt_input_general
    sic_test_crt_input_hybrid
    sic_test_crt_input_partial
    sic_test_crt_input_primitives
    sic_test_crt_input_reject
    sic_test_crt_input_targeted
    sic_test_crt_input_valid
    sic_test_crt_option_integral_max_dim
    sic_test_crt_option_partial_metadata
    sic_test_crt_option_prec
    sic_test_crt_option_primes
    sic_test_crt_option_seed
    sic_test_data_hoggar_data
    sic_test_data_hoggar_gp
    sic_test_data_numeric_formats
    sic_test_data_publication_fixture_hashes
    sic_test_data_subcommand_arity
    sic_test_decode_decode_budget_d32
    sic_test_decode_decode_budget_refused_d32
    sic_test_decode_decode_cli
    sic_test_decode_decode_d16
    sic_test_decode_decode_d32
    sic_test_decode_decode_d4
    sic_test_decode_decode_d8
    sic_test_decode_decode_d8_maximal
    sic_test_decode_decode_kernel_refusals
    sic_test_decode_decode_low_precision_d16
    sic_test_decode_decode_precision_ceiling_budget_d32
    sic_test_decode_decode_tower_stream_d32
    sic_test_inspect_json_multiplet
    sic_test_norm_certificate_d16
    sic_test_norm_certificate_d4
    sic_test_norm_certificate_d8
    sic_test_norm_certificate_d8_maximal
    sic_test_norm_certificate_domain_d12
    sic_test_norm_certificate_domain_d3
    sic_test_norm_certificate_domain_d5
    sic_test_norm_certificate_domain_d6
    sic_test_norm_certificate_domain_d7
    sic_test_norm_certificate_domain_d9
    sic_test_norm_certificate_input_d12
    sic_test_norm_certificate_input_d2
    sic_test_norm_certificate_input_d3
    sic_test_norm_certificate_input_d32
    sic_test_norm_certificate_input_d4
    sic_test_norm_certificate_input_d5
    sic_test_norm_certificate_input_d6
    sic_test_norm_certificate_input_d7
    sic_test_norm_certificate_input_d8
    sic_test_norm_certificate_input_d9
    sic_test_norm_certificate_reader
    sic_test_norm_certificate_reader_d8
    sic_test_norm_cli_defaults
    sic_test_norm_option_estimate
    sic_test_norm_option_leaf_estimate
    sic_test_norm_option_max_bits
    sic_test_norm_option_max_dimension
    sic_test_norm_option_max_field_dimension
    sic_test_policy_catalogue_freshness
    sic_test_policy_cpp_format
    sic_test_policy_data_formats
    sic_test_search_clifford_fiducials_d128
    sic_test_search_clifford_fiducials_d2_d64
    sic_test_search_identify_d2
    sic_test_search_identify_d3_no_match
    sic_test_search_identify_usage_refused
    sic_test_search_overlaps_d64
    sic_test_search_stabilizer_d2
    sic_test_search_stabilizer_usage_refused
    sic_test_sicx_d16_fixture
    sic_test_sicx_d2_fixture
    sic_test_sicx_d32_fixture
    sic_test_sicx_d4_final_format_kill_resume
    sic_test_sicx_d4_fixture
    sic_test_sicx_d64_fixture
    sic_test_sicx_d8_fixture
    sic_test_sicx_d8_maximal_fixture
    sic_test_sicx_d8_release_layout
    sic_test_sicx_exact_gp_d2
    sic_test_sicx_exact_gp_d4
    sic_test_sicx_exact_gp_d8
    sic_test_sicx_exact_json_d2
    sic_test_sicx_exact_json_d4
    sic_test_sicx_exact_json_d8
    sic_test_sicx_fixture_hashes
    sic_test_sicx_general_d_conformance
    sic_test_sicx_overlap_reference
    sic_test_sicx_decode_cancellation
    sic_test_sicx_decode_thread_ownership
    sic_test_sicx_conjugation_selection
    sic_test_sicx_conjugation_word_width
    sic_test_sicx_overlap_admission
    sic_test_sicx_branch_precision
    sic_test_sicx_container_resources
    sic_test_sicx_valuation_admission
    sic_test_sicx_graph_width
    sic_test_sicx_decode_memory
    sic_test_sicx_decode_options
    sic_test_sicx_ray_order_d64
    sic_test_sicx_writer_lsan
    sic_test_stabilizer_certify_stabilizer_d16
    sic_test_stabilizer_certify_stabilizer_d2
    sic_test_stabilizer_certify_stabilizer_d4
    sic_test_stabilizer_certify_stabilizer_d8
    sic_test_stabilizer_certify_stabilizer_wrong_element_fails
    sic_test_stabilizer_cli_defaults
    sic_test_stabilizer_json_inputs
    sic_test_stabilizer_text_headers
    sic_test_support_alignment_cli_conformance
    sic_test_support_cli_failure_categories
    sic_test_support_cli_full
    sic_test_support_cli_help_read_fiducial
    sic_test_support_cli_help_sic_check
    sic_test_support_cli_help_sic_check_balls
    sic_test_support_cli_help_sic_construct
    sic_test_support_cli_help_sic_ray_selector
    sic_test_support_cli_help_sic_container
    sic_test_support_cli_help_sic_data
    sic_test_support_cli_help_sic_decode
    sic_test_support_cli_help_sic_identify
    sic_test_support_cli_help_sic_inspect
    sic_test_support_cli_help_sic_overlaps
    sic_test_support_cli_help_sic_stabilizer
    sic_test_support_cli_help_sic_verify
    sic_test_support_cli_partial_all
    sic_test_support_cli_partial_file_trusted
    sic_test_support_cli_partial_trusted
    sic_test_support_cli_partial_untrusted
    sic_test_support_cli_real_odd
    sic_test_support_cli_refusals
    sic_test_support_cli_sha_trusted
    sic_test_support_cli_verify_d3
    sic_test_support_cli_verify_d4_oracle
    sic_test_support_core_installed_consumer
    sic_test_support_crt_independent_sources
    sic_test_support_doc_example_0
    sic_test_support_docs_fixture_inventory
    sic_test_support_docs_links
    sic_test_support_docs_private
    sic_test_support_docs_status
    sic_test_support_independence_gp
    sic_test_support_independence_sic_certify_crt
    sic_test_support_independence_sic_certify_norm
    sic_test_support_independence_sic_certify_stabilizer
    sic_test_support_independence_sic_check_crt_input
    sic_test_support_independence_sic_test_crt_command_line
    sic_test_support_independence_sic_test_crt_external
    sic_test_support_independence_sic_test_crt_faults
    sic_test_support_independence_sic_test_crt_input
    sic_test_support_independence_sic_test_norm_command_line
    sic_test_support_independence_sic_test_stabilizer_command_line
    sic_test_support_independence_sic_verify_independent
    sic_test_support_install_policy
    sic_test_support_norm_certificate_input_cli_d4
    sic_test_support_norm_certificate_input_cli_d8
    sic_test_support_performance_limits
    sic_test_support_performance_parameters
    sic_test_support_test_hooks_install_refusal
    sic_test_support_test_hooks_installed_isolation
    sic_test_transport_parts_d16
    sic_test_transport_parts_d16_roundtrip
    sic_test_transport_parts_d32
    sic_test_transport_parts_d32_roundtrip
    sic_test_transport_parts_d4
    sic_test_transport_parts_d4_roundtrip
    sic_test_transport_parts_d8
    sic_test_transport_parts_d8_maximal
    sic_test_transport_parts_d8_maximal_roundtrip
    sic_test_transport_parts_d8_roundtrip
    sic_test_verifier_budget_auto_low
    sic_test_verifier_budget_auto_release
    sic_test_verifier_budget_full_low
    sic_test_verifier_budget_full_mixed
    sic_test_verifier_budget_full_sufficient
    sic_test_verifier_budget_ratio_low
    sic_test_verifier_d16
    sic_test_verifier_d2
    sic_test_verifier_d32
    sic_test_verifier_d4
    sic_test_verifier_d8
    sic_test_verifier_d8_maximal
    sic_test_verifier_final_fixtures
    sic_test_verifier_option_extra_bits
    sic_test_verifier_release_d16
    sic_test_verifier_release_d2
    sic_test_verifier_release_d32
    sic_test_verifier_release_d4
    sic_test_verifier_release_d8
    sic_test_verifier_release_d8_maximal
    sic_test_verifier_selftest
    sic_test_sicx_exact_gp_d16
  )
  set(_heavy_tests
    sic_test_sicx_exact_gp_d16
    sic_test_search_clifford_fiducials_d128
    sic_test_construct_native_s6_phased_default_t1
    sic_test_construct_native_s6_phased_default_t4
    sic_test_construct_native_s6_phased_default_t1_repeat_2
    sic_test_construct_native_s6_phased_default_t4_repeat_2
    sic_test_construct_native_s7_phased_default_t4
    sic_test_construct_native_s7_phased_default_t4_repeat_2
    sic_test_construct_native_s7_auto_standard_t4
    sic_test_construct_native_s6_phased_standard_t4
    sic_test_construct_memory_strategy_identity_s2
    sic_test_construct_memory_strategy_identity_s3
    sic_test_construct_memory_strategy_identity_s4
    sic_test_construct_memory_strategy_identity_s5
    sic_test_construct_memory_strategy_identity_s6
    sic_test_construct_memory_strategy_identity_reduced
  )
  get_property(_tests DIRECTORY PROPERTY TESTS)
  foreach(_test IN LISTS _tests)
    if(NOT _test MATCHES "^sic_test_([a-z]+)_[a-z0-9_]+$")
      message(FATAL_ERROR "Invalid test name: ${_test}")
    endif()
    set(_component "${CMAKE_MATCH_1}")
    if(NOT _component MATCHES "^(access|bounds|construct|core|crt|data|decode|exact|inspect|norm|policy|search|sicx|stabilizer|support|transport|verifier)$")
      message(FATAL_ERROR "Unknown test component: ${_test}")
    endif()
    get_test_property("${_test}" LABELS _labels)
    if(NOT _labels)
      set(_labels "")
    endif()
    list(FILTER _labels EXCLUDE REGEX "^(b2|produce|verification|verifier|sic-container|release|regression|slow|optional|cli|crt-input)$")
    set(_layers "${_labels}")
    list(FILTER _layers INCLUDE REGEX "^(unit|integration|e2e)$")
    list(LENGTH _layers _count)
    if(_count GREATER 1)
      message(FATAL_ERROR "Conflicting primary layers: ${_test}")
    elseif(_count EQUAL 0)
      if(_test IN_LIST _unit_tests)
        list(APPEND _labels unit)
      elseif(_test IN_LIST _e2e_tests)
        list(APPEND _labels e2e)
      elseif(_test IN_LIST _integration_tests)
        list(APPEND _labels integration)
      else()
        message(FATAL_ERROR "Missing explicit primary layer: ${_test}")
      endif()
    endif()
    if(_test IN_LIST SIC_HOOK_CONTROL_TESTS)
      if(NOT SIC_ENABLE_TEST_HOOKS)
        message(FATAL_ERROR "Hook control enrolled in OFF tree: ${_test}")
      endif()
      list(APPEND _labels test_hooks)
    endif()
    list(APPEND _labels "${_component}")
    if(_test IN_LIST _heavy_tests)
      list(APPEND _labels heavy)
    endif()
    list(REMOVE_DUPLICATES _labels)
    get_test_property("${_test}" TIMEOUT _timeout)
    if(_test STREQUAL "sic_test_sicx_exact_gp_d16")
      if(NOT SIC_SICX_GP_D16 OR NOT _test IN_LIST _heavy_tests)
        message(FATAL_ERROR "Long timeout requires explicit opt-in heavy enrollment: ${_test}")
      endif()
      set(_timeout 3600)
    elseif(_timeout GREATER 120)
      message(FATAL_ERROR "Local TIMEOUT exceeds 120 seconds: ${_test}: ${_timeout}")
    elseif(NOT _test MATCHES "^sic_test_policy_" OR NOT _timeout)
      set(_timeout 120)
    endif()
    set_tests_properties("${_test}" PROPERTIES LABELS "${_labels}" TIMEOUT "${_timeout}")
    # The cold d128 control retains all four workers and every golden assertion.
    # Keep its sanitizer overhead isolated within the same two-minute ceiling.
    if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=thread" AND
       _test STREQUAL "sic_test_construct_native_s7_auto_standard_t4")
      set_tests_properties("${_test}" PROPERTIES RUN_SERIAL TRUE)
    endif()
  endforeach()
  # The registry policy reads the final CTest properties, including opt-in mode.
  string(JOIN "\",\"" _hooks ${SIC_HOOK_CONTROL_TESTS})
  set(_enrollments ${_unit_tests} ${_e2e_tests} ${_integration_tests} ${_heavy_tests} ${SIC_HOOK_CONTROL_TESTS})
  list(REMOVE_DUPLICATES _enrollments)
  string(JOIN "\",\"" _enrolled ${_enrollments})
  file(WRITE "${PROJECT_BINARY_DIR}/test-registry-contract.json"
    "{\"enrollments\":[\"${_enrolled}\"],\"hooks\":[\"${_hooks}\"],\"system\":\"${CMAKE_SYSTEM_NAME}\",\"compiler\":\"${CMAKE_CXX_COMPILER_ID}\",\"flint\":\"${SIC_ENABLE_FLINT}\",\"apps\":\"${SIC_BUILD_APPS}\",\"regression\":\"${SIC_BUILD_REGRESSION}\",\"enabled\":${SIC_TEST_HOOKS_MODE},\"gp_d16\":\"${SIC_SICX_GP_D16}\"}\n")
endfunction()
