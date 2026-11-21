// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
namespace sic_check::norm::test {
using namespace sic_check::norm;
using fixture::Document;
using fixture::Node;
using fixture::num;
using fixture::overwrite;
void unit_group_oracles(TestContext& context, std::filesystem::path& scratch) {
  // Exhaustive independent oracle: find local generators by multiplying each
  // candidate to its exact order, then enumerate the full product of CRT lifts.
  // It uses neither the reader's modular-power primitive nor its inverse tables.
  for (uint64_t dimension = 2; dimension <= 256; ++dimension) {
    std::vector<uint64_t> generators, orders;
    uint64_t remaining = dimension;
    for (uint64_t prime = 2; prime <= dimension; ++prime) {
      if (remaining % prime) continue;
      uint64_t pp = 1;
      do {
        pp *= prime;
        remaining /= prime;
      } while (remaining % prime == 0);
      std::vector<uint64_t> local;
      if (prime == 2) {
        if (pp >= 4) local.push_back(pp - 1);
        if (pp >= 8) local.push_back(5);
      } else {
        for (uint64_t a = 1; a < pp; ++a) {
          uint64_t power = 1, order = 0;
          do {
            power = power * a % pp;
            ++order;
          } while (power != 1 && order < pp);
          if (order == pp / prime * (prime - 1) && power == 1) {
            local.push_back(a);
            break;
          }
        }
      }
      for (uint64_t a : local) {
        uint64_t power = 1, order = 0, lift = 1;
        do {
          power = power * a % pp;
          ++order;
        } while (power != 1);
        for (; lift < dimension; ++lift)
          if (lift % pp == a && lift % (dimension / pp) == 1 % (dimension / pp)) break;
        generators.push_back(lift);
        orders.push_back(order);
      }
    }
    Meta meta;
    meta.d = dimension;
    meta.units = unit_factors(dimension);
    meta.smith = orders;
    check(meta.units.size() == orders.size(), "canonical generator count oracle");
    for (size_t j = 0; j < orders.size(); ++j) {
      check(meta.units[j].generator == generators[j] && meta.units[j].order == orders[j],
            "smallest primitive generator/CRT lift/order oracle");
      std::vector<uint64_t> w(orders.size(), 0);
      w[j] = 1;
      meta.unit_words.push_back(w);
    }
    std::map<uint64_t, std::vector<uint64_t>> tuples;
    std::vector<uint64_t> tuple(orders.size(), 0);
    std::function<void(size_t, uint64_t)> enumerate = [&](size_t j, uint64_t u) {
      if (j == orders.size()) {
        check(tuples.emplace(u, tuple).second, "unique unit tuple");
        return;
      }
      for (uint64_t a = 0; a < orders[j]; ++a, u = u * generators[j] % dimension) {
        tuple[j] = a;
        enumerate(j + 1, u);
      }
    };
    enumerate(0, 1);
    for (uint64_t coordinate = 0; coordinate < dimension; ++coordinate) {
      uint64_t divisor = std::gcd(coordinate, dimension), n = dimension / divisor, lift = 1;
      for (; lift < dimension; ++lift)
        if (std::gcd(lift, dimension) == 1 && (n == 1 || lift % n == coordinate / divisor)) break;
      auto entry = meta.entry(coordinate);
      check(entry.representative == (divisor == dimension ? 0 : int64_t(divisor)) &&
                entry.word == tuples.at(lift),
            "general entry against brute-force canonical lift");
    }
  }
  for (auto [dimension, radicand] : std::vector<std::pair<uint64_t, int64_t>>{
           {2, -3}, {3, 1}, {4, 5}, {5, 3}, {6, 21}, {7, 2}, {8, 5}, {9, 15}, {12, 13}}) {
    Document d(dimension, false);
    auto b = d.encode();
    write(scratch, b);
    Reader r(scratch.string());
    check(r.meta().D0 == radicand, "base field derived for each dimension");
    check(r.meta().representative.size() == d.exmt["entry_rule"]["representatives"].array.size(),
          "general representative descriptors");
    auto bad = d;
    bad.exmt["galois"].object["word_5"] = Node::a({"0"});
    context.run(bad.encode(), "unsupported galois key rejected", 1);
    bad = d;
    bad.exmt["entry_rule"]["name"] = "sicx-2power/1";  // identity-negative
    context.run(bad.encode(), "unsupported entry rule rejected", 1);
    bad = d;
    bad.exmt["galois"]["unit_generator_words"].array.push_back(Node::a({"0"}));
    context.run(bad.encode(), "extra unit word rejected", 1);
    if (dimension == 2) continue;
    bad = d;
    bad.exmt["galois"]["unit_generator_words"].array.pop_back();
    context.run(bad.encode(), "missing unit word rejected", 1);
    bad = d;
    bad.exmt["galois"]["unit_generator_words"].array[0] = Node::a();
    context.run(bad.encode(), "general unit word rank rejected", 1);
    bad = d;
    bad.exmt["galois"]["unit_generator_words"].array[0].array[0] = "2";
    context.run(bad.encode(), "general unit word residue rejected", 1);
    d.exmt["galois"]["unit_generator_words"].array[0].array[0] = "1";
    context.run(d.encode(), "nontrivial general unit homomorphism");
  }
  Document relation(3, false, 2);  // U3 has order 2, Smith factor is 4.
  relation.exmt["galois"]["unit_generator_words"].array[0].array[0] = "1";
  context.run(relation.encode(), "unit generator order relation rejected", 1);
  for (uint64_t dimension : {5, 7, 9}) {
    Document d(dimension, false);
    int64_t radicand = dimension == 5 ? 3 : dimension == 7 ? 2 : 15;
    d.leaves[0].payload = fixture::leaf_bytes(0, 0, {{0, -1}});
    std::vector<Integer> polynomial(5);
    fmpz_set_si(polynomial[0].v, -radicand);
    fmpz_one(polynomial[4].v);
    d.exmt["tower"]["branches"].array[0].array[0] = positive_real_branch(polynomial, 128);
    d.sync_leaves();
    context.run(d.encode(), "other quadratic basis selects fourth root");
  }
  for (size_t id : {size_t(0), size_t(1)}) {
    Document d(3, false);
    d.leaves[id].payload = fixture::leaf_bytes(id, 0, {{0, 1}});
    d.sync_leaves();
    context.run(d.encode(), "rational nonzero y rejected in tower or representative", 1);
  }
}

void quadratic_branch_presentations(TestContext& context, uint64_t& base_dimension,
                                    Document& original, ByteBuffer& valid) {
  Document irrational(4);
  irrational.exmt["elements"].array[0]["scale"] = "2";
  irrational.exmt["elements"].array[0]["max_bits"] = "2";
  irrational.exmt["tower"]["branches"].array[0].array[0] = sqrt_branch(2, 128);
  context.run(irrational.encode(), "exact irrational branch");
  auto negative_root = irrational;
  negative_root.exmt["tower"]["branches"].array[0].array[0] = sqrt_branch(2, 128, true);
  context.run(negative_root.encode(), "negative exact irrational branch");
  auto not_minimum = irrational;
  not_minimum.exmt["tower"]["branch_bits"] = "256";
  not_minimum.exmt["tower"]["branches"].array[0].array[0] = sqrt_branch(2, 256);
  context.run(not_minimum.encode(), "nonminimum irrational branch", 1);
  auto bad_rounding = irrational;
  bad_rounding.exmt["tower"]["branches"].array[0].array[0].value[0] = '9';
  context.run(bad_rounding.encode(), "wrong exact branch rounding", 1);
  auto rounded_singleton = irrational;
  auto singleton_text = sqrt_branch(2, 128);
  singleton_text.resize(singleton_text.find(' ', singleton_text.find(' ') + 1));
  rounded_singleton.exmt["tower"]["branches"].array[0].array[0] = singleton_text + " 0 0";
  context.run(rounded_singleton.encode(), "rounded irrational component falsely claimed singleton",
              1, {}, "branch exact rounding/root choice");
  Document mixed_singleton(2, false);
  mixed_singleton.leaves[0].payload = fixture::leaf_bytes(0, 0, {{0, 1}});  // wsq = omega
  mixed_singleton.sync_leaves();
  // sqrt(omega) = sqrt(3)/2 - i/2 in the prescribed negative quadratic embedding.
  ComplexBall real_half;
  check(arb_load_str(acb_realref(real_half.v), sqrt_branch(3, 127).c_str()) == 0,
        "fixture sqrt branch parse");
  arb_mul_2exp_si(acb_realref(real_half.v), acb_realref(real_half.v), -1);
  char* half_dump = arb_dump_str(acb_realref(real_half.v));
  mixed_singleton.exmt["tower"]["branches"].array[0] = Node::a({half_dump, "-1 -1 0 0"});
  flint_free(half_dump);
  context.run(mixed_singleton.encode(), "nonzero imaginary singleton with irrational real part");
  auto false_singleton = mixed_singleton;
  false_singleton.exmt["tower"]["branches"].array[0].array[1] = "1 -1 0 0";
  context.run(false_singleton.encode(), "false imaginary singleton equality", 1, {},
              "branch exact rounding/root choice");
  false_singleton = mixed_singleton;
  false_singleton.exmt["tower"]["branches"].array[0].array[0] = "1 -1 0 0";
  context.run(false_singleton.encode(), "false real singleton equality", 1, {},
              "branch exact rounding/root choice");
  // Both towers need the SAME minimum 256 bits. In the first, only zero
  // exclusion fails at 128; in the second, only separation fails at 128.
  for (bool tiny : {true, false}) {
    std::string family = tiny ? "zero exclusion" : "root separation";
    std::vector<Integer> c(4);
    fmpz_one(c[3].v);
    if (tiny) {
      // X^3 + 2^130 X - 1: one tiny positive root, two distant complex roots.
      fmpz_set_si(c[0].v, -1);
      fmpz_one(c[1].v);
      fmpz_mul_2exp(c[1].v, c[1].v, 130);
    } else {
      // (X-1)^2 (X+2^270) - 1: two nonzero roots within 2^-134 of 1.
      Integer a;
      fmpz_one(a.v);
      fmpz_mul_2exp(a.v, a.v, 270);
      fmpz_sub_ui(c[0].v, a.v, 1);
      fmpz_mul_si(c[1].v, a.v, -2);
      fmpz_add_ui(c[1].v, c[1].v, 1);
      fmpz_sub_ui(c[2].v, a.v, 2);
    }
    Document common(base_dimension, false, 2, 3);
    for (size_t k = 0; k < 3; ++k) real_leaf(common, k, {c[k]});
    // The next polynomial is X^3-1 over the first level. Store the exact
    // traces of -1: [-3, c2, 2*c1-c2^2], not primitive-coordinate surrogates.
    std::vector<Integer> traces(3);
    fmpz_set_si(traces[0].v, -3);
    fmpz_set(traces[1].v, c[2].v);
    fmpz_mul(traces[2].v, c[2].v, c[2].v);
    fmpz_neg(traces[2].v, traces[2].v);
    fmpz_addmul_ui(traces[2].v, c[1].v, 2);
    real_leaf(common, 3, traces);
    auto at_precision = [&](uint64_t bits) {
      auto d = common;
      d.exmt["tower"]["branch_bits"] = num(bits);
      d.exmt["tower"]["branches"].array[0].array[0] = positive_real_branch(c, bits);
      return d.encode();
    };
    context.run(at_precision(256), family + " requires common precision 256");
    context.run(at_precision(128), family + " rejects precision 128", 1, {},
                tiny ? "canonical branch rectangles do not isolate all roots"
                     : "ambiguous canonical branch");
    context.run(at_precision(512), family + " rejects nonminimal precision 512", 1, {},
                "branch_bits not minimal");
  }
  Document degree_bomb(base_dimension, false, 1, 31);
  degree_bomb.leaves[0].payload = fixture::leaf_bytes(0, 0, {{1, 0}});
  degree_bomb.leaves[1].payload = fixture::leaf_bytes(1, 0, {{1, 0}});
  degree_bomb.exmt["elements"].array[1]["max_bits"] = "1";
  degree_bomb.sync_leaves();
  context.run(degree_bomb.encode(), "X^31+X+1 component expansion refuses degree 256", 2, {},
              "exact binary pre-expansion bound");
  auto low_budget = ReadOptions{};
  low_budget.algebraic_limits.degree = 16;
  context.run(degree_bomb.encode(), "root isolation degree preflight refusal", 2, low_budget,
              "exact polynomial pre-expansion bound");
  low_budget = ReadOptions{};
  auto tall_contract = original;
  Integer tall;
  fmpz_one(tall.v);
  fmpz_mul_2exp(tall.v, tall.v, 500);
  ByteBuffer tall_payload{0, 0, 1, 3};
  big_integer(tall_payload, tall.v);
  fixture::integer(tall_payload, int64_t(1));
  tall_contract.leaves[0].payload = tall_payload;
  tall_contract.exmt["elements"].array[0]["max_bits"] = "501";
  tall_contract.sync_leaves();
  low_budget.algebraic_limits.height_bits = 512;
  context.run(tall_contract.encode(), "contraction height expansion preflight refusal", 2,
              low_budget, "exact binary pre-expansion bound");
  low_budget.algebraic_limits.height_bits = 128;
  context.run(valid, "branch dyadic height preflight refusal", 2, low_budget,
              "exact dyadic pre-expansion bound");
  low_budget.algebraic_limits.height_bits = 16;
  context.run(irrational.encode(), "arithmetic height preflight refusal", 2, low_budget,
              "exact binary pre-expansion bound");
  low_budget = ReadOptions{};
  low_budget.algebraic_limits.degree = 1;
  context.run(valid, "base quadratic degree preflight refusal", 2, low_budget,
              "exact base square root bound");
  // A post-operation refusal is too late. Check caller-owned outputs remain
  // untouched when admission fails, including on degree/height boundary cases.
  auto preflight = [&](const std::string& name, const std::function<void(Algebraic&)>& op) {
    Algebraic output, sentinel;
    qqbar_set_si(output.v, 7);
    qqbar_set_si(sentinel.v, 7);
    bool refused_before_expansion = false;
    try {
      op(output);
    } catch (const ResourceRefusal&) {
      refused_before_expansion = qqbar_equal(output.v, sentinel.v);
    }
    check(refused_before_expansion, name + ": did not refuse before expansion");
  };
  Algebraic square_root, large_integer;
  qqbar_set_ui(square_root.v, 2);
  qqbar_sqrt(square_root.v, square_root.v);
  qqbar_set_ui(large_integer.v, 1);
  qqbar_mul_2exp_si(large_integer.v, large_integer.v, 20);
  for (const auto& operation : {std::string("add"), std::string("mul"), std::string("div")}) {
    preflight("degree " + operation, [&](auto& out) {
      BoundedAlgebra bounded({2, 1u << 20});
      if (operation == "add") bounded.add(out.v, square_root.v, square_root.v);
      if (operation == "mul") bounded.mul(out.v, square_root.v, square_root.v);
      if (operation == "div") bounded.div(out.v, square_root.v, square_root.v);
    });
    preflight("height " + operation, [&](auto& out) {
      BoundedAlgebra bounded({256, 32});
      if (operation == "add") bounded.add(out.v, large_integer.v, large_integer.v);
      if (operation == "mul") bounded.mul(out.v, large_integer.v, large_integer.v);
      if (operation == "div") bounded.div(out.v, large_integer.v, large_integer.v);
    });
  }
  preflight("dyadic height", [&](auto& out) {
    BoundedAlgebra({256, 32}).shift(out.v, large_integer.v, 20);
  });
  Integer wide;
  fmpz_one(wide.v);
  fmpz_mul_2exp(wide.v, wide.v, 32);
  preflight("integer height", [&](auto& out) { BoundedAlgebra({256, 32}).set(out.v, wide.v); });
  std::vector<Algebraic> tall_polynomial(4);
  qqbar_set_si(tall_polynomial[0].v, -1);
  qqbar_one(tall_polynomial[1].v);
  qqbar_mul_2exp_si(tall_polynomial[1].v, tall_polynomial[1].v, 100);
  qqbar_one(tall_polynomial[3].v);
  bool root_height_refused = false;
  try {
    (void)polynomial_roots(tall_polynomial, {256, 512});
  } catch (const ResourceRefusal& e) {
    root_height_refused =
        std::string(e.what()).find("exact polynomial pre-expansion bound") != std::string::npos;
  }
  check(root_height_refused, "root isolation height preflight refusal");
  // Exact half-integer ties at p=128, positive and negative; an exact dyadic
  // root requires a singleton only when it is a p-grid point.
  for (int sign : {-1, 1}) {
    Document tie(4);
    // g=sign*2^-129, hence c=2^-258. Such rational coefficients cannot be
    // represented by integer lambda at level 0: test the comparison primitive directly.
    Algebraic half;
    qqbar_set_si(half.v, sign);
    qqbar_mul_2exp_si(half.v, half.v, -129);
    ComplexBall rectangle;
    branch_component(acb_realref(rectangle.v), half.v, 128);
    check(arf_is_zero(arb_midref(acb_realref(rectangle.v))) &&
              !mag_is_zero(arb_radref(acb_realref(rectangle.v))),
          "ties to even at zero");
    qqbar_set_si(half.v, 3 * sign);
    qqbar_mul_2exp_si(half.v, half.v, -129);
    branch_component(acb_realref(rectangle.v), half.v, 128);
    Integer man, exp;
    arf_get_fmpz_2exp(man.v, exp.v, arb_midref(acb_realref(rectangle.v)));
    check(fmpz_equal_si(man.v, sign) && fmpz_equal_si(exp.v, -127), "ties to even away from zero");
  }
}

void cubic_input(TestContext& context, Document& odd) {
  context.run(odd.encode(), "odd cubic container");
}

void odd_action_presentations(TestContext& context, Document& original, Document& with_action,
                              Node& delta) {
  with_action.exmt["elements"].array.insert(with_action.exmt["elements"].array.begin() + 3, delta);
  with_action.leaves.insert(with_action.leaves.begin() + 3,
                            {"TOWR", 3, 0, 1, fixture::leaf_bytes(3, 0, {{1, 0}})});
  for (unsigned k = 0; k < 3; ++k) {
    Node a = Node::o({{"axis", "0"},
                      {"k", num(k)},
                      {"kind", "actodd"},
                      {"level", "0"},
                      {"levels", Node::a()},
                      {"max_bits", num(k == 1 ? 1 : 0)},
                      {"scale", "1"}});
    with_action.exmt["elements"].array.insert(with_action.exmt["elements"].array.begin() + 4 + k,
                                              a);
    with_action.leaves.insert(
        with_action.leaves.begin() + 4 + k,
        {"TOWR", 4 + k, 0, 1, fixture::leaf_bytes(4 + k, 0, {{k == 1 ? 1 : 0, 0}})});
  }
  rebuild(with_action);
  context.run(with_action.encode(), "odd complete action family");
  auto absent_delta = with_action;
  absent_delta.exmt["elements"].array.erase(absent_delta.exmt["elements"].array.begin() + 3);
  absent_delta.leaves.erase(absent_delta.leaves.begin() + 3);
  rebuild(absent_delta);
  context.run(absent_delta.encode(), "coherent odd family missing denominator", 1);
  auto extra_element = original;
  extra_element.exmt["elements"].array.push_back(extra_element.exmt["elements"].array.back());
  extra_element.leaves.push_back(extra_element.leaves.back());
  rebuild(extra_element);
  context.run(extra_element.encode(), "coherent extra element and logical owner", 1);
  // The common sign is part of the presentation; no private coset order is checked.
  auto signed_action = with_action;
  for (size_t id : {size_t(3), size_t(5)})
    signed_action.leaves[id].payload = fixture::leaf_bytes(id, 0, {{-1, 0}});
  signed_action.sync_leaves();
  context.run(signed_action.encode(), "simultaneous odd delta and numerator sign reversal");
  check(root(signed_action.encode(), 32) != root(with_action.encode(), 32),
        "signed action presentation changes exact root");
  auto mismatched_sign = signed_action;
  mismatched_sign.leaves[5].payload = fixture::leaf_bytes(5, 0, {{1, 0}});
  mismatched_sign.sync_leaves();
  // This is canonical container data; quotient identities are separate algebraic obligations.
  context.run(mismatched_sign.encode(), "unproved action quotient is not a container claim");
  check(root(mismatched_sign.encode(), 32) != root(signed_action.encode(), 32),
        "individual numerator sign is bound by exact root");
  // Nonzero valuations distinguish all three odd own-weight formulae from 1.
  // Each targeted pair has valuation 1 and weight > 1, so it blocks J=1;
  // all other nonzero own-weight pairs admit J=1. Stored scale is essential.
  for (size_t limiting : {size_t(0), size_t(1), size_t(3), size_t(4), size_t(5), size_t(6)}) {
    auto weighted = with_action;
    std::vector<Integer> c(4);
    fmpz_one(c[3].v);
    fmpz_set_si(c[0].v, limiting == 0 ? -2 : -8);
    if (limiting == 1) fmpz_set_si(c[1].v, 2);
    real_leaf(weighted, 0, {c[0]});
    real_leaf(weighted, 1, {c[1]});
    for (size_t id = 3; id <= 6; ++id) {
      Integer value;
      fmpz_set_ui(value.v, id == limiting ? 2 : 16);
      real_leaf(weighted, id, {value});
    }
    weighted.exmt["tower"]["branches"].array[0].array[0] = positive_real_branch(c, 128);
    context.run(weighted.encode(), "odd valuation weight at element " + std::to_string(limiting));
    weighted.exmt["generator_rescaling"]["shifts"].array[0] = "2";
    context.run(weighted.encode(),
                "odd nonzero recorded shift at element " + std::to_string(limiting));
  }
  auto malformed_action = with_action;
  malformed_action.exmt["elements"].array.erase(malformed_action.exmt["elements"].array.begin() +
                                                3);
  context.run(malformed_action.encode(), "odd action missing denominator", 1);
  malformed_action = with_action;
  malformed_action.exmt["elements"].array.erase(malformed_action.exmt["elements"].array.begin() +
                                                5);
  context.run(malformed_action.encode(), "odd action missing coefficient", 1);
  malformed_action = with_action;
  malformed_action.exmt["elements"].array[5]["axis"] = "1";
  context.run(malformed_action.encode(), "odd action axis out of range", 1);
}

void quadratic_action_presentations(TestContext& context, Document& original, Document& qaction) {
  Node act = Node::o({{"axis", "0"},
                      {"kind", "act"},
                      {"level", "0"},
                      {"levels", Node::a()},
                      {"max_bits", "1"},
                      {"scale", "1"}});
  qaction.exmt["elements"].array.insert(qaction.exmt["elements"].array.begin() + 1, act);
  qaction.leaves.insert(qaction.leaves.begin() + 1,
                        {"TOWR", 1, 0, 1, fixture::leaf_bytes(1, 0, {{1, 0}})});
  rebuild(qaction);
  context.run(qaction.encode(), "quadratic optional action");
  auto badq = qaction;
  badq.exmt["elements"].array[1]["axis"] = "1";
  context.run(badq.encode(), "quadratic action axis out of range", 1);
  badq = qaction;
  badq.exmt["elements"].array[1]["kind"] = "actdelta";
  badq.exmt["elements"].array[1].object.erase("axis");
  context.run(badq.encode(), "quadratic delta", 1);
  // Zero convention, minimal support, and nonmaximal runs on a two-coordinate leaf.
  auto zero = original;
  zero.exmt["elements"].array[1]["max_bits"] = "0";
  zero.leaves[1].payload = fixture::leaf_bytes(1, 0, {{0, 0}});
  zero.sync_leaves();
  context.run(zero.encode(), "zero representative");
  auto nonminimal = original;
  nonminimal.exmt["elements"].array[1]["levels"] = Node::a({"0"});
  nonminimal.leaves[1].count = 2;
  nonminimal.leaves[1].payload = fixture::leaf_bytes(1, 0, {{1, 0}, {0, 0}});
  nonminimal.physical[2].count = 2;
  nonminimal.sync_leaves();
  context.run(nonminimal.encode(), "nonminimal level set", 1);
  nonminimal.leaves[1].payload = {1, 0, 2, 3, 2, 1, 0, 3, 2, 1, 0};
  nonminimal.sync_leaves();
  context.run(nonminimal.encode(), "consecutive data runs", 1);
  nonminimal.leaves[1].payload = {1, 0, 2, 2, 2};
  nonminimal.sync_leaves();
  context.run(nonminimal.encode(), "consecutive zero runs", 1);
  auto zero_bad = zero;
  zero_bad.exmt["elements"].array[1]["scale"] = "2";
  context.run(zero_bad.encode(), "zero scale convention", 1);
}

void multi_leaf_layout(Document& split, size_t& representative_id) {
  auto& descriptor = split.exmt["elements"].array[representative_id];
  descriptor["levels"] = Node::a();
  for (unsigned l = 0; l < 15; ++l) descriptor["levels"].array.push_back(num(l));
}

void multi_leaf_coordinates(Document& split, size_t& representative_id,
                            std::vector<std::pair<int64_t, int64_t>>& a,
                            std::vector<std::pair<int64_t, int64_t>>& b) {
  a[0] = {1, 0};
  b.back() = {1, 0};
  split.leaves[representative_id].count = 16384;
  split.leaves[representative_id].payload = fixture::leaf_bytes(representative_id, 0, a);
  split.leaves.insert(
      split.leaves.begin() + representative_id + 1,
      {"BODY", representative_id, 16384, 16384, fixture::leaf_bytes(representative_id, 16384, b)});
  // Rebuild physical mapping without changing element ids on the second leaf.
  auto tail = split.physical;
  split.physical.resize(1);
  for (size_t i = 0; i < split.leaves.size(); ++i) {
    auto& l = split.leaves[i];
    split.physical.push_back({l.kind, 1, 0, 0, i + 1, 1, l.id, l.first, l.count, l.payload});
  }
  for (auto& c : tail)
    if (c.type == "PROV" || c.type == "RAYS" || c.type == "BALL" || c.type == "PREC")
      split.physical.push_back(c);
}

void grouped_leaf_layout(size_t& representative_id, Document& grouped) {
  auto& group = grouped.physical[representative_id + 1];
  group.leaf_count = 2;
  group.count = 32768;
  fixture::append(group.payload, grouped.physical[representative_id + 2].payload);
  grouped.physical.erase(grouped.physical.begin() + representative_id + 2);
}

void leaf_grouping_and_quadratic_bounds(TestContext& context, Document& split, Document& grouped,
                                        ReadOptions& large) {
  large.max_field_dimension = 32768;
  context.run(split.encode(), "split logical leaves", 0, large);
  context.run(grouped.encode(), "grouped logical leaves", 0, large);
}

void pell_branch_bounds(TestContext& context, Document& split, ReadOptions& large,
                        Document& tiny_quadratic, Integer& fx, Integer& fy, Integer& trace,
                        Integer& norm, Integer& temp_integer) {
  fmpz_fib_ui(fx.v, 401);
  fmpz_fib_ui(fy.v, 402);
  fmpz_mul_ui(trace.v, fx.v, 2);
  fmpz_add(trace.v, trace.v, fy.v);
  fmpz_mul(norm.v, fx.v, fx.v);
  fmpz_addmul(norm.v, fx.v, fy.v);
  fmpz_mul(temp_integer.v, fy.v, fy.v);
  fmpz_sub(norm.v, norm.v, temp_integer.v);
  check(fmpz_is_one(norm.v), "Pell fixture positive norm one");
  std::vector<Integer> pell_polynomial(5);
  pell_polynomial[0] = norm;
  fmpz_neg(pell_polynomial[2].v, trace.v);
  fmpz_one(pell_polynomial[4].v);
  auto tiny_dump = positive_real_branch(pell_polynomial, 256, true);
  auto& tiny_element = tiny_quadratic.exmt["elements"].array[0];
  tiny_element["scale"] = "1";
  tiny_element["max_bits"] = num(fmpz_bits(fy.v));
  ByteBuffer pell_leaf;
  for (uint64_t v : {0, 0, 1, 3}) fixture::var(pell_leaf, v);
  // Since0 < x+y*omega <1 andy>0, the balanced stored r is1.
  fixture::integer(pell_leaf, 1);
  big_integer(pell_leaf, fy.v);
  tiny_quadratic.leaves[0].payload = pell_leaf;
  tiny_quadratic.sync_leaves();
  tiny_quadratic.exmt["tower"]["branch_bits"] = "256";
  tiny_quadratic.exmt["tower"]["branches"].array[0].array[0] = tiny_dump;
  context.run(tiny_quadratic.encode(), "quadratic tiny root requires precision256", 0, large);
  auto fast_bad = split;
  fast_bad.exmt["tower"]["branches"].array[0].array[0] = "3 0 0 0";
  context.run(fast_bad.encode(), "quadratic fast path wrong canonical root", 1, large,
              "quadratic branch exact rounding/root choice");
  fast_bad = split;
  fast_bad.exmt["tower"]["branch_bits"] = "256";
  context.run(fast_bad.encode(), "quadratic fast path nonminimal precision", 1, large,
              "quadratic branch_bits not minimal");
}

void interval_branch_rounding(ComplexBall& value, ComplexBall& box) {
  arb_set_si(acb_realref(value.v), 1);
  check(interval_component(acb_realref(box.v), acb_realref(value.v), 128, 512) &&
            arb_is_exact(acb_realref(box.v)),
        "interval exact singleton");
  arb_add_error_2exp_si(acb_realref(value.v), -300);
  check(!interval_component(acb_realref(box.v), acb_realref(value.v), 128, 512),
        "interval uncertain singleton refuses");
  arb_set_si(acb_realref(value.v), 3);
  arb_mul_2exp_si(acb_realref(value.v), acb_realref(value.v), -129);
  check(interval_component(acb_realref(box.v), acb_realref(value.v), 128, 512) &&
            arf_cmp_2exp_si(arb_midref(acb_realref(box.v)), -127) == 0,
        "interval exact tie uses even integer");
  arb_add_error_2exp_si(acb_realref(value.v), -300);
  check(!interval_component(acb_realref(box.v), acb_realref(value.v), 128, 512),
        "interval uncertain tie refuses");
  arb_sqrt_ui(acb_realref(value.v), 2, 512);
  check(interval_component(acb_realref(box.v), acb_realref(value.v), 128, 512) &&
            !arb_is_exact(acb_realref(box.v)) &&
            mag_cmp_2exp_si(arb_radref(acb_realref(box.v)), -128) == 0,
        "interval irrational component has exact grid radius");
  // All signs and both parity choices at exact half-grid boundaries.
  for (int n : {-3, -1, 1, 3}) {
    arb_set_si(acb_realref(value.v), n);
    arb_mul_2exp_si(acb_realref(value.v), acb_realref(value.v), -129);
    ComplexBall expected;
    arb_set_si(acb_realref(expected.v), n == -3 ? -2 : n == 3 ? 2 : 0);
    arb_mul_2exp_si(acb_realref(expected.v), acb_realref(expected.v), -128);
    check(interval_component(acb_realref(box.v), acb_realref(value.v), 128, 512) &&
              arf_equal(arb_midref(acb_realref(box.v)), arb_midref(acb_realref(expected.v))),
          "interval signed nearest-even ties");
  }
}

void tower_arithmetic(std::filesystem::path& scratch, ByteBuffer& valid,
                      std::vector<std::pair<int64_t, int64_t>>& a,
                      std::vector<std::pair<int64_t, int64_t>>& b) {
  // Exercise base-field arithmetic and cubic duals.
  write(scratch, valid);
  Reader r(scratch.string());
  Tower tower(r.meta());
  QuadraticElement omega;
  fmpq_one(omega.b);
  QuadraticElement w2 = tower.product(omega, omega);
  check(fmpq_is_one(w2.a) && fmpq_is_one(w2.b), "omega relation");
  auto unit = tower.mul(Elt{omega}, tower.inverse(Elt{omega}, 0), 0);
  check(fmpq_is_one(unit[0].a) && fmpq_is_zero(unit[0].b), "base inverse");
  Meta cm;
  cm.D0 = 5;
  cm.degrees = {3};
  Tower cubic(cm);
  cubic.f[0].assign(3, Elt(1));
  fmpq_set_si(cubic.f[0][0][0].a, -2, 1);
  cubic.make_duals(0);
  auto g = cubic.generator(0), identity = cubic.mul(g, cubic.inverse(g, 1), 1);
  check(fmpq_is_one(identity[0].a) && fmpq_is_zero(identity[0].b) &&
            block_zero(identity.data() + 1, 2),
        "cubic inverse");
  Elt traces(3);
  fmpq_set_si(traces[2].a, 6, 1);
  auto recovered = cubic.from_leaves(traces.data(), 1);
  for (size_t j = 0; j < 3; ++j)
    check(fmpq_equal(recovered[j].a, g[j].a) && fmpq_equal(recovered[j].b, g[j].b),
          "cubic trace dual");
}
}  // namespace sic_check::norm::test
