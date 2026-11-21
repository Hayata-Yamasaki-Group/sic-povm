// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_general.hpp"

#include "test_context.hpp"
namespace sic_check::crt::test {
namespace filesystem = std::filesystem;
using namespace sic_check::crt::sicx;
Uint64 brute_order(Uint64 a, Uint64 n) {
  Uint64 x = 1, order = 0;
  do {
    x = x * a % n;
    ++order;
  } while (x != 1);
  return order;
}

vector<Input::UnitGenerator> brute_generators(Uint64 d) {
  vector<Uint64> components;
  for (Uint64 q = 2; q <= d; ++q) {
    if (d % q) continue;
    Uint64 p = 2;
    while (q % p) ++p;
    Uint64 residue = q;
    while (residue % p == 0) residue /= p;
    if (residue == 1 && (d / q) % p) components.push_back(q);
  }
  std::sort(components.begin(), components.end(), [](Uint64 a, Uint64 b) {
    auto prime = [](Uint64 q) {
      Uint64 p = 2;
      while (q % p) ++p;
      return p;
    };
    return prime(a) < prime(b);
  });
  vector<Input::UnitGenerator> gs;
  for (Uint64 q : components) {
    vector<Uint64> local;
    if (q % 2 == 0) {
      if (q >= 4) local.push_back(q - 1);
      if (q >= 8) local.push_back(5);
    } else {
      Uint64 phi = 0;
      for (Uint64 v = 1; v < q; ++v)
        if (std::gcd(v, q) == 1) ++phi;
      for (Uint64 v = 2; v < q; ++v)
        if (std::gcd(v, q) == 1 && brute_order(v, q) == phi) {
          local.push_back(v);
          break;
        }
    }
    for (Uint64 a : local) {
      for (Uint64 u = 1; u < d; ++u)
        if (u % q == a && u % (d / q) == 1 % (d / q)) {
          gs.push_back({u, brute_order(a, q)});
          break;
        }
    }
  }
  return gs;
}

void general_cases() {
  const vector<Uint64> dimensions{2, 3, 5, 6, 7, 9, 12, 16, 24, 27, 45, 60};
  const vector<Int64> discriminants{-3, 1, 3, 21, 2, 15, 13, 221, 21, 42, 483, 3477};
  for (size_t v = 0; v < dimensions.size(); ++v) {
    Uint64 d = dimensions[v];
    fixture::File file(d);
    auto gs = brute_generators(d);
    vector<string> words;
    for (size_t j = 0; j < gs.size(); ++j) words.push_back(fixture::nums({j % 2 == 0 ? 1u : 0u}));
    fixture::replace(file.meta, "\"unit_generator_words\":" + fixture::zero_unit_words(d, {0}),
                     "\"unit_generator_words\":" + fixture::arr(words));
    positive("general d" + std::to_string(d), file.encode());
    Input in;
    in.open(save(file.encode()));
    require(in.D0 == discriminants[v], "derived base-field golden d" + std::to_string(d));
    ++passed;
    require(in.unit_generators.size() == gs.size(), "canonical generator count");
    ++passed;
    for (size_t j = 0; j < gs.size(); ++j) {
      require(
          in.unit_generators[j].unit == gs[j].unit && in.unit_generators[j].order == gs[j].order,
          "canonical CRT lift/order disagrees with exhaustive oracle");
      ++passed;
    }
    std::map<Uint64, vector<Uint64>> brute;
    // Independent breadth-wise enumeration with explicit tuples, no mixed-radix indices.
    brute[1] = {};
    for (auto g : gs) {
      auto old = brute;
      brute.clear();
      for (auto [u, tuple] : old) {
        for (Uint64 a = 0, x = u; a < g.order; ++a, x = x * g.unit % d) {
          auto next = tuple;
          next.push_back(a);
          require(brute.emplace(x, next).second, "nonunique unit decomposition");
        }
      }
    }
    for (Uint64 m = 0; m < d; ++m) {
      Uint64 g = std::gcd(m, d), n = d / g, lift = 0;
      for (Uint64 u = 1; u < d; ++u)
        if (std::gcd(u, d) == 1 && (n == 1 || u % n == (m / g) % n)) {
          lift = u;
          break;
        }
      require(lift != 0, "unit lift missing in brute oracle");
      Uint64 digit = 0;
      for (size_t j = 0; j < gs.size(); ++j) digit += brute.at(lift)[j] * (j % 2 == 0);
      require(
          in.entry_position(m) == (m == 0 ? 0 : g) && in.entry_word(m) == vector<Uint64>{digit % 2},
          "entry rule disagrees with brute force at d=" + std::to_string(d) +
              " m=" + std::to_string(m));
      ++passed;
    }
    // Test the second (and later) factors independently, so swapping factor words cannot hide.
    for (size_t j = 0; j < gs.size(); ++j) {
      in.unit_words.assign(gs.size(), vector<Uint64>{0});
      in.unit_words[j][0] = 1;
      for (const auto& [u, tuple] : brute) {
        require(in.entry_word(u) == vector<Uint64>{tuple[j] % 2}, "factor word order");
        ++passed;
      }
    }
    vector<Uint64> orders;
    for (auto g : gs) orders.push_back(g.order);
    if (d <= 16) {
      auto faithful = fixture::faithful_units(d, orders);
      positive("faithful direct-product presentation d" + std::to_string(d), faithful.encode());
      Input full;
      full.open(save(faithful.encode()));
      for (Uint64 m = 0; m < d; ++m) {
        Uint64 divisor = std::gcd(m, d), n = d / divisor, u = 1;
        for (; u < d; ++u)
          if (std::gcd(u, d) == 1 && (n == 1 || u % n == (m / divisor) % n)) break;
        auto expected = gs.empty() ? vector<Uint64>{0} : brute.at(u);
        require(full.entry_word(m) == expected, "faithful full exponent tuple");
        ++passed;
      }
    }
    if (d >= 4) {
      bool half = in.D0 % 4 == 1;
      Int64 x = half ? (in.D0 + 3) / 4 : in.D0 + 1, y = half ? -1 : 2;
      Int64 scale = std::gcd(x, y);
      Uint64 bits = 0;
      for (Uint64 nbits = x; nbits; nbits >>= 1) ++bits;
      fixture::replace(
          file.meta,
          "\"kind\":\"wsq\",\"level\":\"0\",\"levels\":[],\"max_bits\":\"2\",\"scale\":\"2\"",
          "\"kind\":\"wsq\",\"level\":\"0\",\"levels\":[],\"max_bits\":\"" + std::to_string(bits) +
              "\",\"scale\":\"" + std::to_string(scale) + "\"");
      fixture::replace(file.meta, fixture::sqrt2_box(), fixture::base_square_box(in.D0, half));
      file.leaves[0].bytes = fixture::leaf(0, 0, 1, {{0, x / scale, y / scale}}, in.D0);
      file.rebuild_physical(false);
      positive("nonrational integral basis d" + std::to_string(d), file.encode());
      Input exact;
      exact.open(save(file.encode()));
      auto pairs = exact.element(0);
      require(pairs.size() == 1 && fmpz_equal_si(pairs[0].x.v, x / scale) &&
                  fmpz_equal_si(pairs[0].y.v, y / scale),
              "general basis pair transform inverse");
      exact.check_branches_algebraic();
      ++passed;
    }
  }
  {
    fixture::File imaginary(2);
    fixture::replace(
        imaginary.meta,
        "\"kind\":\"wsq\",\"level\":\"0\",\"levels\":[],\"max_bits\":\"2\",\"scale\":\"2\"",
        "\"kind\":\"wsq\",\"level\":\"0\",\"levels\":[],\"max_bits\":\"1\",\"scale\":\"1\"");
    // omega^2=omega-1; the selected root is omega=(1-i sqrt(3))/2.
    string half_im = fixture::sqrt_box(3, 127);
    size_t space = half_im.find(' '), end = half_im.find(' ', space + 1);
    Int64 exponent = std::stoll(half_im.substr(space + 1, end - space - 1), nullptr, 16) - 1;
    fixture::Big exp;
    fmpz_set_si(exp.z, exponent);
    half_im = "-" + half_im.substr(0, space) + " " + fixture::hex(exp.z) + " 1 -80";
    fixture::replace(imaginary.meta,
                     fixture::arr({fixture::str(fixture::sqrt2_box()), fixture::str("0 0 0 0")}),
                     fixture::arr({fixture::str("1 -1 0 0"), fixture::str(half_im)}));
    imaginary.leaves[0].bytes = fixture::leaf(0, 0, 1, {{0, -1, 1}}, -3);
    imaginary.rebuild_physical(false);
    positive("negative imaginary base embedding", imaginary.encode());
  }
  // Rehashed metadata mutations pin the intended new guard, before branch or statistics checks.
  auto bad = [&](fixture::File f, const string& from, const string& to, const string& category) {
    fixture::replace(f.meta, from, to);
    try {
      Input in;
      in.open(save(f.encode()));
    } catch (const Reject& e) {
      require(string(e.what()).find(category) != string::npos,
              "general guard category: " + string(e.what()));
      ++passed;
      return;
    }
    throw std::runtime_error("general MUST guard survived: " + category);
  };
  for (const string& positions :
       {"[]", "[\"0\",\"1\",\"2\",\"3\"]", "[\"0\",\"1\",\"2\",\"3\",\"4\",\"6\",\"12\"]",
        "[\"0\",\"2\",\"1\",\"3\",\"4\",\"6\"]", "[\"0\",\"1\",\"2\",\"3\",\"4\",\"4\"]"})
    bad(fixture::File(12), "\"representatives\":[\"0\",\"1\",\"2\",\"3\",\"4\",\"6\"]",
        "\"representatives\":" + positions, "entry rule representatives");
  bad(fixture::File(12), "\"position\":\"3\"", "\"position\":\"5\"", "expected rep 3");
  for (Uint64 d : {2, 3, 5, 6, 7, 9, 12, 16}) {
    fixture::File f(d);
    bad(f, "sicx-divisors/1.0", "sicx-2power/1",  // identity-negative
        "entry rule name");
    bad(f, "\"unit_generator_words\":", "\"word_5\":", "galois: unknown key word_5");
    bad(f, "\"unit_generator_words\":", "\"word_minus_1\":", "galois: unknown key word_minus_1");
    bad(f, "\"unit_generator_words\":", "\"unit_generator_words\":[[\"0\"]],\"word_5\":",
        "galois: unknown key word_5");
    string old = "\"unit_generator_words\":" + fixture::zero_unit_words(d, {0});
    bad(f, old, "\"unit_generator_words\":" + (d == 2 ? string("[[\"0\"]]") : string("[]")),
        "word count");
  }
  for (const auto& [key, next] :
       std::vector<std::pair<string, string>>{{"field", "format"},
                                              {"embedding", "entry_rule"},
                                              {"primitive_roots", "required_features"},
                                              {"unit_generators", "version"}})
    bad(fixture::File(5),
        "\"" + next + "\":", "\"" + key + "\":[],\"" + next + "\":", "EXMT: unknown key");
  bad(fixture::File(12), "\"smith\":", "\"generators\":[],\"smith\":", "galois: unknown key");
  bad(fixture::File(5), "\"unit_generator_words\":[[\"0\"]]", "\"unit_generator_words\":[\"0\"]",
      "unit generator word: array expected");
  // Residue rejection is separate from the exact order-relation checks in C4 and C3 below.
  bad(fixture::dyadic(), "\"unit_generator_words\":[[\"0\",\"0\"]]",
      "\"unit_generator_words\":[[\"2\",\"0\"]]", "word digit out of range");
  auto cyclic4 = fixture::faithful_units(5, {4});
  fixture::replace(cyclic4.meta, "\"d\":\"5\"", "\"d\":\"3\"");
  bad(cyclic4, "\"unit_generator_words\":[[\"1\"]]", "\"unit_generator_words\":[[\"1\"]]",
      "word order relation");
  auto odd = fixture::odd();
  bad(odd, "\"unit_generator_words\":[[\"0\"]]", "\"unit_generator_words\":[[\"1\"]]",
      "word order relation");
  // All rational coordinates, including actions and representatives, must have y=0.
  for (size_t id : {size_t(0), size_t(1), size_t(2)}) {
    fixture::File f(3);
    f.leaves[id].bytes = fixture::leaf(id, 0, 1, {{0, 1, 1}}, 1);
    f.rebuild_physical(false);
    try {
      Input in;
      in.open(save(f.encode()));
    } catch (const Reject& e) {
      require(string(e.what()).find("rational base requires y = 0") != string::npos,
              "rational rejection");
      ++passed;
      continue;
    }
    throw std::runtime_error("noncanonical rational y accepted");
  }
  {
    Input in;
    in.open(save(fixture::File(12).encode()));
    in.limits.dense_coordinates = 11;
    try {
      in.entry_word(0);
      throw std::runtime_error("unit allocation limit ignored");
    } catch (const Refuse& e) {
      require(string(e.what()).find("unit lookup") != string::npos, "unit limit category");
      ++passed;
    }
    for (bool word : {false, true}) {
      try {
        if (word)
          in.entry_word(12);
        else
          in.entry_position(12);
      } catch (const Reject& e) {
        require(string(e.what()).find("entry coordinate") != string::npos, "entry range category");
        ++passed;
        continue;
      }
      throw std::runtime_error("entry out of range accepted");
    }
  }
  // Word-index order disagrees with tuple lexicographic order for these two rays.
  auto rays = [&](bool correct) {
    auto f = fixture::dyadic();
    f.finalize();
    auto& p = f.physical[f.physical.size() - 2];
    p.bytes = fixture::text(fixture::obj(
        {{"balls",
          fixture::obj({{"limbs", fixture::str("1")}, {"working_bits", fixture::str("128")}})},
         {"rays",
          fixture::arr(
              {fixture::obj({{"accuracy_bits", fixture::str("127")},
                             {"anchor", fixture::str("0")},
                             {"label", fixture::str("c0")},
                             {"word", fixture::nums(correct ? vector<Uint64>{0, 1}
                                                            : vector<Uint64>{1, 0})}}),
               fixture::obj({{"accuracy_bits", fixture::str("127")},
                             {"anchor", fixture::str("0")},
                             {"label", fixture::str("c1")},
                             {"word", fixture::nums(correct ? vector<Uint64>{1, 0}
                                                            : vector<Uint64>{0, 1})}})})}}));
    auto ball = f.physical.back();
    ball.object = 1;
    f.physical.push_back(ball);
    return f;
  };
  positive("first-coordinate-first Smith order c0 nonzero", rays(true).encode());
  auto wrong = rays(false);
  try {
    Input in;
    in.open(save(wrong.encode()));
  } catch (const Reject& e) {
    require(string(e.what()).find("ray words") != string::npos, "Smith order guard");
    ++passed;
    return;
  }
  throw std::runtime_error("little-endian Smith index order accepted");
}
}  // namespace sic_check::crt::test
