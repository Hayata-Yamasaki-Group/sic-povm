// SPDX-License-Identifier: AGPL-3.0-or-later
#include "general_tests.hpp"
#include "guard_tests.hpp"
#include "test_context.hpp"
using namespace sic_check::verifier::test;
int main(int argc, char** argv) {
  try {
    fixtures_dir = argc > 1 ? argv[1] : "verifier-fixtures";

    std::filesystem::create_directories(fixtures_dir);

    using namespace fixture;

    size_t rejects = 0, valid = 0;

    general_tests(valid, rejects);

    guard_tests(rejects);

    for (unsigned d : {4u, 8u}) {
      string pre = "d" + std::to_string(d) + "-";

      auto a = write(small(d)), b = write(small(d, true));
      DimensionContext dimension{d, pre, rejects};

      require(a.exact == b.exact, "finalization preserves exact root");
      container_envelopes(valid, dimension, pre, a, b);
      exact_metadata_refusals(d, dimension);
      integer_run_canonicality(dimension);
      ray_metadata_refusals(dimension);

      auto siblings = small(d);

      string json = "{\"siblings\":[";
      json_sibling_depth(valid, pre, siblings, json);

      auto deep = small(d);

      string nested(33, '[');
      json_depth_refusals(d, dimension, deep, nested);

      auto bigInt = small(d);

      ByteBuffer encoded{0, 0, 1, 3};
      payload_size_refusals(d, dimension, bigInt, encoded);

      // A bad unread payload remains deferred, while selected payload errors fail closed.
      auto unread = small(d, true);
      partial_payload_authentication(valid, rejects, dimension, pre, b, encoded, unread);
      rescaling_and_entry_contracts(d, valid, rejects, dimension, pre, a, b, json, nested);
      directory_bootstrap_refusals(d, valid, rejects, dimension, pre, b, json);
    }
    multi_leaf_boundaries(valid, rejects);
    wide_rescaling_weights(valid, rejects);
    rescaling_enumeration_oracle(valid, rejects);
    odd_branch_and_action_contracts(valid, rejects);
    exact_decode_refusals(valid, rejects);
    quadratic_square_branches(valid, rejects);
    cubic_zero_branches(valid, rejects);
    signed_base_coordinates(valid, rejects);
    field_dimension_refusals(valid, rejects);
    exact_nearest_even_ties(valid, rejects);
    real_root_certification(valid, rejects);
    text_container_identity(valid, rejects);
    interval_ties_and_source_snapshot(valid, rejects);
    ray_parser_contracts(valid, rejects);
    scalar_parser_contracts(valid, rejects);
    uncertain_rounding_and_short_reads(valid, rejects);
    binary64_and_ball_storage_bounds(valid, rejects);
    ball_exact_consistency(valid, rejects);
    directory_storage_refusals(valid, rejects);
    hash_and_text_bounds(valid, rejects);

    say("FIXTURE TEST PASS: %zu checks, %zu accepted controls, %zu rejected mutations\n", checks,
        valid, rejects);

    return 0;
  } catch (const std::exception& e) {
    fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
