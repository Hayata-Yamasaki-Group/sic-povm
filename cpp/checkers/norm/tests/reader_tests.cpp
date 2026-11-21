// SPDX-License-Identifier: AGPL-3.0-or-later
#include <iostream>

#include "test_context.hpp"
using namespace sic_check::norm::test;
int main(int argc, char** argv) {
  try {
    check(argc == 2 || argc == 3, "reader_tests SCRATCH_DIRECTORY [BASE_DIMENSION]");

    uint64_t base_dimension = argc == 3 ? std::stoull(argv[2]) : 4;

    std::filesystem::path dir = argv[1];

    std::filesystem::create_directories(dir);

    auto scratch = dir / "mutation.sicx";

    std::ofstream cases(dir / "controls.tsv");

    cases << "case\texpected\n";

    size_t accepted = 0, rejected = 0, refused = 0;
    TestContext context{scratch, cases, accepted, rejected, refused};

    codec_primitives(context, dir);
    unit_group_oracles(context, scratch);

    Document original(base_dimension);
    context.base_document = &original;

    auto valid = original.encode();
    context.base_bytes = &valid;

    container_envelopes(context, original, valid);

    auto empty_chunk = original;

    fixture::Physical empty{"TOWR", 1, 0, 0, 1, 0, 0, 0, 0, {}};
    partial_directory_refusals(context, base_dimension, original, empty_chunk, empty);
    metadata_refusals(context, original);

    ReadOptions trust;
    trusted_root_routes(context, scratch, valid, trust);

    auto wrong = trust;
    trusted_root_mismatches(context, valid, trust, wrong);
    whole_hash_mismatch(context, valid, wrong);

    ReadOptions partial;
    authenticated_partial_input(context, scratch, original, valid, empty, partial);
    quadratic_branch_presentations(context, base_dimension, original, valid);

    // General odd-prime grammar and denominator/coefficient action families.
    Document odd(8, true, 1, 3);
    cubic_input(context, odd);

    auto with_action = odd;

    Node delta = Node::o({{"kind", "actdelta"},
                          {"level", "0"},
                          {"levels", Node::a()},
                          {"max_bits", "1"},
                          {"scale", "1"}});
    odd_action_presentations(context, original, with_action, delta);

    auto qaction = original;
    quadratic_action_presentations(context, original, qaction);

    // Multiple logical leaves of the same element at the actual fixed limit.
    Document split(8, true, 15);

    size_t representative_id = 15;
    multi_leaf_layout(split, representative_id);

    std::vector<std::pair<int64_t, int64_t>> a(16384), b(16384);
    multi_leaf_coordinates(split, representative_id, a, b);

    auto grouped = split;
    grouped_leaf_layout(representative_id, grouped);

    ReadOptions large;
    leaf_grouping_and_quadratic_bounds(context, split, grouped, large);

    // Pell identity: x+y*omega is positive and about 2^-279; its square
    // root is rounded to zero at p=128, so all-root zero exclusion forces256.
    auto tiny_quadratic = split;

    Integer fx, fy, trace, norm, temp_integer;
    pell_branch_bounds(context, split, large, tiny_quadratic, fx, fy, trace, norm, temp_integer);

    // The proof-producing interval primitive must refuse uncertain singleton
    // equality and half-integer rounding, rather than treating overlap as equality.
    ComplexBall value, box;
    interval_branch_rounding(value, box);
    resource_and_input_refusals(context, base_dimension, dir, scratch, original, valid, partial,
                                odd, with_action, delta, qaction, split, representative_id, a,
                                grouped, large, fx, fy, value, box);

    auto nonchain = grouped;
    nonchain_smith_presentation(a, nonchain);
    corruption_fuzz_and_root_refusals(context, scratch, original, valid, odd, large, nonchain);
    tower_arithmetic(scratch, valid, a, b);

    std::filesystem::remove(scratch);

    std::cout << "PASS: " << accepted << " accepted, " << rejected << " rejected, " << refused
              << " resource refusals; hash and algebra controls\n";

    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
