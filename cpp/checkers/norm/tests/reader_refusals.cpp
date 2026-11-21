// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
namespace sic_check::norm::test {
using namespace sic_check::norm;
using fixture::Document;
using fixture::Node;
using fixture::num;
using fixture::overwrite;
void resource_and_input_refusals(TestContext& context, uint64_t& base_dimension,
                                 std::filesystem::path& dir, std::filesystem::path& scratch,
                                 Document& original, ByteBuffer& valid, ReadOptions& partial,
                                 Document& odd, Document& with_action, Node& delta,
                                 Document& qaction, Document& split, size_t& representative_id,
                                 std::vector<std::pair<int64_t, int64_t>>& a, Document& grouped,
                                 ReadOptions& large, Integer& fx, Integer& fy, ComplexBall& value,
                                 ComplexBall& box) {
  arb_one(acb_realref(value.v));
  arb_mul_2exp_si(acb_realref(value.v), acb_realref(value.v), 3 * (1 << 20));
  context.expect_resource(
      [&] { interval_component(acb_realref(box.v), acb_realref(value.v), 128, 512); },
      "quadratic rounding integer admission");
  Meta pure;
  pure.d = 4;
  pure.D0 = 5;
  pure.D = 128;
  pure.branch_bits = 128;
  pure.degrees.assign(7, 2);
  pure.branches.resize(7);
  pure.elements.resize(7);
  std::vector<std::vector<Pair>> pure_leaves(7);
  for (size_t i = 0; i < 7; ++i) {
    arb_one(acb_realref(pure.branches[i].v));
    pure.elements[i].kind = "wsq";
    pure.elements[i].level = unsigned(i);
    pure.elements[i].max_bits = i + 1;
    pure_leaves[i].resize(size_t(1) << i);
    fmpz_set_ui(pure_leaves[i][0].x.v, size_t(1) << i);
  }
  check(verify_quadratic_branches(pure, pure_leaves), "pure-axis interval path");
  {
    auto tiny = pure;
    auto data = pure_leaves;
    tiny.elements[0].max_bits = fmpz_bits(fy.v);
    data[0][0].x = fx;
    data[0][0].y = fy;
    arb_zero(acb_realref(tiny.branches[0].v));
    mag_set_ui_2exp_si(arb_radref(acb_realref(tiny.branches[0].v)), 1, -128);
    context.expect_invalid([&] { (void)verify_quadratic_branches(tiny, data); },
                           "quadratic helper refuses nonisolating canonical box");
    auto other = pure;
    other.d = 5;
    other.D0 = 3;
    data = pure_leaves;
    fmpz_zero(data[0][0].x.v);
    fmpz_set_si(data[0][0].y.v, -1);
    std::vector<Integer> f(5);
    fmpz_set_si(f[0].v, -3);
    fmpz_one(f[4].v);
    canonical_ball(acb_realref(other.branches[0].v), positive_real_branch(f, 128));
    check(verify_quadratic_branches(other, data), "other quadratic basis interval path");
  }
  pure.elements[0].max_bits = 1 << 21;
  context.expect_resource([&] { verify_quadratic_branches(pure, pure_leaves); },
                          "quadratic interval workspace admission");
  pure.elements[0].max_bits = 1;
  auto wide_meta = pure;
  wide_meta.D = 65536;
  wide_meta.branch_bits = 65536;
  wide_meta.degrees.assign(16, 2);
  wide_meta.branches.resize(16);
  wide_meta.elements.resize(16);
  std::vector<std::vector<Pair>> wide_leaves(16);
  for (size_t i = 0; i < 16; ++i) {
    arb_one(acb_realref(wide_meta.branches[i].v));
    wide_meta.elements[i].kind = "wsq";
    wide_meta.elements[i].level = unsigned(i);
    wide_meta.elements[i].max_bits = i + 1;
    wide_leaves[i].resize(size_t(1) << i);
    fmpz_set_ui(wide_leaves[i][0].x.v, size_t(1) << i);
  }
  context.expect_resource([&] { verify_quadratic_branches(wide_meta, wide_leaves); },
                          "quadratic interval workspace admission");
  arb_zero(acb_realref(pure.branches[0].v));
  arb_one(acb_imagref(pure.branches[0].v));
  fmpz_set_si(pure_leaves[0][0].x.v, -1);
  fmpz_set_si(pure_leaves[1][1].x.v, -2);  // wsq_1 = 1+i
  check(!verify_quadratic_branches(pure, pure_leaves),
        "mixed complex square requires algebraic fallback");
  fmpz_zero(pure_leaves[0][0].x.v);
  check(!verify_quadratic_branches(pure, pure_leaves),
        "zero square requires algebraic zero-root rejection");
  check(root(split.encode(), 32) == root(grouped.encode(), 32), "grouping changed exact root");
  check(root(split.encode(), 64) != root(grouped.encode(), 64),
        "grouping did not change file root");
  write(dir / "d8-grouped.sicx", grouped.encode());
  write(dir / "d8-split.sicx", split.encode());
  auto cut = large;
  cut.partial = true;
  cut.exact_root = root(split.encode(), 32);
  cut.physical_chunks = {representative_id + 1};
  context.run(split.encode(), "partial one logical leaf", 0, cut);
  auto oversized_partial = split;
  oversized_partial.exmt["elements"].array[representative_id]["max_bits"] = "0";
  auto oversized_bytes = oversized_partial.encode();
  auto partial_height = cut;
  partial_height.exact_root = root(oversized_bytes, 32);
  context.run(oversized_bytes, "partial leaf exceeds declared max_bits", 1, partial_height);
  auto unordered_partial = split;
  auto& unordered = unordered_partial.exmt["elements"].array[representative_id]["levels"].array;
  std::swap(unordered[0], unordered[1]);
  context.partial_bad(unordered_partial, "partial rejects unsorted level set");
  write(scratch, split.encode());
  Reader subset(scratch.string(), cut);
  check(subset.report().checked_elements.size() == 15,
        "partial claims incomplete element canonicality");
  try {
    (void)subset.element_leaves(representative_id);
    throw std::runtime_error("partial element exposed as complete");
  } catch (const InvalidInput&) {
  }
  auto four = [](Document d) {
    d.d = 4;
    d.exmt["d"] = "4";
    d.exmt["galois"]["unit_generator_words"].array.resize(1);
    d.exmt["entry_rule"]["representatives"].array.pop_back();
    d.exmt["elements"].array.pop_back();
    d.leaves.pop_back();
    std::erase_if(d.physical, [](auto& c) { return c.type == "BODY" && c.object == 18; });
    d.get("BALL").count = 4;
    d.get("BALL").payload.resize(4 * 2 * 40);
    return d;
  };
  auto split4 = four(split), grouped4 = four(grouped);
  context.run(split4.encode(), "d4 split logical leaves", 0, large);
  context.run(grouped4.encode(), "d4 grouped logical leaves", 0, large);
  check(root(split4.encode(), 32) == root(grouped4.encode(), 32), "d4 grouping changed exact root");
  check(root(split4.encode(), 64) != root(grouped4.encode(), 64),
        "d4 grouping did not change file root");
  write(dir / "d4-grouped.sicx", grouped4.encode());
  write(dir / "d4-split.sicx", split4.encode());
  auto corrupt_group = grouped;
  corrupt_group.get("BODY").payload[corrupt_group.get("BODY").payload.size() - 3] ^= 1;
  context.run(corrupt_group.encode(), "grouped leaf hash", 1, large);
  auto duplicate = split;
  duplicate.physical[representative_id + 2].first_leaf =
      duplicate.physical[representative_id + 1].first_leaf;
  context.run(duplicate.encode(), "duplicate logical ownership", 1, large);
  auto wrong_group = grouped;
  wrong_group.get("BODY").object = 17;
  context.run(wrong_group.encode(), "group spans elements", 1, large);
  // Canonical JSON prose and UTF-8, nesting refusal, and nested override syntax.
  auto prose = original;
  prose.prov.object["message"] = std::string("quote\" slash/ line\n tab\t ") + "\xc3\xa9";
  context.run(prose.encode(), "canonical UTF8 and escapes");
  auto deep = original;
  Node nested = "x";
  for (unsigned depth = 0; depth < 34; ++depth) nested = Node::a({nested});
  deep.prov.object["nested"] = nested;
  context.run(deep.encode(), "JSON depth resource refusal", 2);

  // Fixtures whose quadratic field and nontrivial C2 generator are genuine.
  // They intentionally do not claim to be SIC vectors; that is a separate check.
  for (uint64_t dimension : {4, 8}) {
    Document field(dimension);
    field.exmt["elements"].array[0]["scale"] = "2";
    field.exmt["elements"].array[0]["max_bits"] = "2";
    field.exmt["tower"]["branches"].array[0].array[0] = sqrt_branch(2, 128);
    Node action = Node::o({{"axis", "0"},
                           {"kind", "act"},
                           {"level", "0"},
                           {"levels", Node::a()},
                           {"max_bits", "2"},
                           {"scale", "2"}});
    field.exmt["elements"].array.insert(field.exmt["elements"].array.begin() + 1, action);
    field.leaves.insert(field.leaves.begin() + 1,
                        {"TOWR", 1, 0, 1, fixture::leaf_bytes(1, 0, {{-1, 0}})});
    rebuild(field);
    // Every representative decodes to 1/2. Publish the uniform unit vector.
    auto& payload = field.get("BALL").payload;
    payload.clear();
    for (uint64_t coordinate = 0; coordinate < dimension; ++coordinate) {
      if (dimension == 4) {
        fixture::put(payload, 0);
        fixture::put(payload, uint64_t(-1));
        fixture::put(payload, 1);
        fixture::put(payload, uint64_t(-128));
        fixture::put(payload, 1);
      } else {
        Integer scaled, q, test, left;
        fmpz_one(scaled.v);
        fmpz_mul_2exp(scaled.v, scaled.v, 257);
        fmpz_sqrt(q.v, scaled.v);
        fmpz_mul_2exp(test.v, q.v, 1);
        fmpz_add_ui(test.v, test.v, 1);
        fmpz_mul(test.v, test.v, test.v);
        fmpz_mul_2exp(left.v, scaled.v, 2);
        if (fmpz_cmp(left.v, test.v) > 0) fmpz_add_ui(q.v, q.v, 1);
        unsigned shift = unsigned(fmpz_val2(q.v));
        fmpz_tdiv_q_2exp(q.v, q.v, shift);
        fixture::put(payload, 0);
        fixture::put(payload, uint64_t(-130 + int64_t(shift)));
        fixture::put(payload, 1);
        fixture::put(payload, uint64_t(-130));
        fixture::put(payload, fmpz_get_ui(q.v));
        fmpz_fdiv_q_2exp(q.v, q.v, 64);
        fixture::put(payload, fmpz_get_ui(q.v));
        fmpz_fdiv_q_2exp(q.v, q.v, 64);
        fixture::put(payload, fmpz_get_ui(q.v));
      }
      for (unsigned j = 0; j < (dimension == 4 ? 5 : 7); ++j) fixture::put(payload, 0);
    }
    if (dimension == 8) {
      field.rays["balls"]["limbs"] = "3";
      field.rays["rays"].array[0]["accuracy_bits"] = "129";
    }
    auto finalized = field.encode();
    context.run(finalized, "genuine quadratic presentation d" + std::to_string(dimension));
    write(dir / ("d" + std::to_string(dimension) + ".sicx"), finalized);
    auto exact = field;
    std::erase_if(exact.physical,
                  [](auto& c) { return c.type == "RAYS" || c.type == "BALL" || c.type == "PREC"; });
    auto before = exact.encode();
    context.run(before, "genuine exact-only presentation");
    write(dir / ("d" + std::to_string(dimension) + "-exact.sicx"), before);
    auto directory_bytes = [](const ByteBuffer& file) {
      size_t off = size_t(little(file, file.size() - 112)),
             len = size_t(little(file, file.size() - 104)) + 16;
      return ByteBuffer(file.begin() + off, file.begin() + off + len);
    };
    check(directory_bytes(before) == directory_bytes(finalized), "finalization changed LDIR bytes");
    check(root(before, 32) == root(finalized, 32), "finalization changed exact root");
    size_t prefix = size_t(little(before, before.size() - 112));
    check(std::equal(before.begin(), before.begin() + prefix, finalized.begin()),
          "finalization changed exact prefix");
  }
  // A supported nonzero recorded shift that the prescribed greedy order does not reproduce.
  auto rescaling_bad = split;
  rescaling_bad.exmt["generator_rescaling"]["shifts"].array[14] = "1";
  context.run(rescaling_bad.encode(), "recorded greedy order mismatch", 1, large);
  auto prov_nested = original;
  prov_nested.prov.object["nested"] = Node::o({{"generator_rescaling", Node::o()}});
  context.run(prov_nested.encode(), "nested PROV rescaling key is provenance");
  prov_nested.prov.object["array"] = Node::a({Node::o({{"generator_rescaling", "ignored"}})});
  context.run(prov_nested.encode(), "array-nested PROV rescaling key is provenance");
  auto no_decoder = original;
  no_decoder.rays.object.erase("decoder");
  context.run(no_decoder.encode(), "non-release optional decoder");
  auto commit64 = original;
  commit64.rays["decoder"]["source_commit"] = std::string(64, 'a');
  context.run(commit64.encode(), "64-digit source commit");
  auto no_c0 = original;
  no_c0.rays["rays"].array[0]["label"] = "c4";
  no_c0.rays["rays"].array[0]["word"].array[0] = "1";
  context.run(no_c0.encode(), "nonzero ray without c0");
  auto gaps = original;
  gaps.rays["rays"].array[0]["label"] = "c2";
  auto next_ray = gaps.rays["rays"].array[0];
  next_ray["label"] = "c10";
  next_ray["word"].array[0] = "1";
  gaps.rays["rays"].array.push_back(next_ray);
  auto second_ball = gaps.get("BALL");
  second_ball.object = 1;
  gaps.physical.insert(gaps.physical.end() - 1, second_ball);
  context.run(gaps.encode(), "numeric label order with gaps");
  auto duplicate_label = gaps;
  duplicate_label.rays["rays"].array[1]["label"] = "c0";
  context.run(duplicate_label.encode(), "distinct ordered words with duplicate label", 1);
  auto bad_words = gaps;
  bad_words.rays["rays"].array[1]["word"] = bad_words.rays["rays"].array[0]["word"];
  context.run(bad_words.encode(), "duplicate ray word", 1);
  bad_words = gaps;
  std::swap(bad_words.rays["rays"].array[0]["word"], bad_words.rays["rays"].array[1]["word"]);
  context.run(bad_words.encode(), "descending ray word", 1);
  // Lexicographic tuple order differs from little-endian Smith-index order.
  auto words = fixture::Document(base_dimension, true, 2);
  words.exmt["galois"]["smith"] = Node::a({"2", "2"});
  for (auto& w : words.exmt["galois"]["unit_generator_words"].array) w = Node::a({"0", "0"});
  words.rays["rays"].array[0]["word"] = Node::a({"0", "1"});
  auto lex_second = words.rays["rays"].array[0];
  lex_second["label"] = "c3";
  lex_second["word"] = Node::a({"1", "0"});
  words.rays["rays"].array.push_back(lex_second);
  auto lex_ball = words.get("BALL");
  lex_ball.object = 1;
  words.physical.insert(words.physical.end() - 1, lex_ball);
  context.run(words.encode(), "multi-axis lexicographic word order");
  std::swap(words.rays["rays"].array[0]["word"], words.rays["rays"].array[1]["word"]);
  context.run(words.encode(), "multi-axis descending words rejected", 1);
  auto reversed = gaps;
  std::swap(reversed.rays["rays"].array[0], reversed.rays["rays"].array[1]);
  context.run(reversed.encode(), "numeric label order", 1);
  auto no_radii = original;
  overwrite(no_radii.get("BALL").payload, 16, 0);
  overwrite(no_radii.get("BALL").payload, 24, 0);
  context.run(no_radii.encode(), "canonical all-zero radii rejected", 1);
  auto unknown_rays = original;
  unknown_rays.rays["rays"].array[0].object["extra"] = "x";
  context.run(unknown_rays.encode(), "unknown ray key", 1);
  auto unknown_ball_key = original;
  unknown_ball_key.rays["balls"].object["extra"] = "x";
  context.run(unknown_ball_key.encode(), "unknown balls key", 1);
  auto wrong_zero_exponent = original;
  overwrite(wrong_zero_exponent.get("BALL").payload, 32, 0);
  overwrite(wrong_zero_exponent.get("BALL").payload, 8, 1);
  context.run(wrong_zero_exponent.encode(), "zero midpoint exponent canonicality", 1);
  auto wide_radius = original;
  overwrite(wide_radius.get("BALL").payload, 16, 1);
  overwrite(wide_radius.get("BALL").payload, 24, 0);
  wide_radius.rays["rays"].array[0]["accuracy_bits"] = "-1";
  context.run(wide_radius.encode(), "anchor interval contains zero", 1);
  auto large_branch = original;
  large_branch.exmt["tower"]["branch_bits"] = "2097152";
  context.run(large_branch.encode(), "branch precision resource refusal", 2);
  auto json_length = original;
  json_length.edit_pdir = [](auto& entries) {
    overwrite(entries[0], 16, (64u << 20) + 1);
    overwrite(entries[0], 32, (64u << 20) + 1);
  };
  context.run(json_length.encode(), "JSON physical resource refusal", 2);
  auto chunk_length = original;
  chunk_length.edit_pdir = [](auto& entries) {
    overwrite(entries[1], 16, (256u << 20) + 1);
    overwrite(entries[1], 32, (256u << 20) + 1);
  };
  context.run(chunk_length.encode(), "decoded physical resource refusal", 2);
  auto unassigned_shift = original;
  unassigned_shift.exmt["generator_rescaling"]["shifts"].array[0] = "1000000000000000000";
  context.run(unassigned_shift.encode(), "shift numeric width", 1);
  for (auto bad : {std::string(63, 'a'), std::string(64, 'A'), std::string(64, 'g')}) {
    try {
      (void)parse_hash(bad);
      throw std::runtime_error("accepted invalid CLI digest");
    } catch (const InvalidInput&) {
    }
  }
  check(parse_hash(hash_hex(root(valid, 32))) == root(valid, 32), "digest CLI roundtrip");
  auto imaginary_anchor = original;
  auto& anchor_payload = imaginary_anchor.get("BALL").payload;
  for (size_t part = 0; part < 4; ++part) {
    overwrite(anchor_payload, part * 40 + 8, uint64_t(-1));
    overwrite(anchor_payload, part * 40 + 32, 1);
  }
  context.run(imaginary_anchor.encode(), "norm-valid nonzero anchor imaginary part", 1);
  auto duplicate_action = qaction;
  duplicate_action.exmt["elements"].array.insert(
      duplicate_action.exmt["elements"].array.begin() + 2,
      duplicate_action.exmt["elements"].array[1]);
  context.run(duplicate_action.encode(), "duplicate quadratic action axis", 1);
  auto unused_delta = odd;
  unused_delta.exmt["elements"].array.insert(unused_delta.exmt["elements"].array.begin() + 3,
                                             delta);
  context.run(unused_delta.encode(), "odd denominator without action", 1);
  auto wrong_k = with_action;
  wrong_k.exmt["elements"].array[5]["k"] = "3";
  context.run(wrong_k.encode(), "odd coefficient index range", 1);
  auto unsorted_levels = split;
  std::swap(unsorted_levels.exmt["elements"].array[15]["levels"].array[0],
            unsorted_levels.exmt["elements"].array[15]["levels"].array[1]);
  context.run(unsorted_levels.encode(), "level set order", 1, large);
  auto extraneous_element_key = original;
  extraneous_element_key.exmt["elements"].array[0].object["extra"] = "x";
  context.run(extraneous_element_key.encode(), "unknown element descriptor key", 1);
  auto too_large_tower = original;
  too_large_tower.exmt["tower"]["degrees"].array.assign(63, "2");
  context.run(too_large_tower.encode(), "too many tower levels", 1);
}

void nonchain_smith_presentation(std::vector<std::pair<int64_t, int64_t>>& a, Document& nonchain) {
  nonchain.exmt["galois"]["smith"] = Node::a({"16384", "2"});
  for (auto& w : nonchain.exmt["galois"]["unit_generator_words"].array) w = Node::a({"0", "0"});
  nonchain.rays["rays"].array[0]["word"] = Node::a({"0", "0"});
}
}  // namespace sic_check::norm::test
