// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
#include "test_general.hpp"
namespace sic_check::crt::test {
namespace filesystem = std::filesystem;
using namespace sic_check::crt::sicx;
void hybrid_cases() {
  for (bool negative : {false, true}) {
    Input in;
    in.open(save(fixture::odd_quadratic_tie(negative).encode()));
    ++passed;
    bool rejected = false;
    try {
      Input bad;
      bad.open(save(fixture::odd_quadratic_tie(negative, true).encode()));
    } catch (const Reject& e) {
      require(string(e.what()).find("canonical exact rounding") != string::npos,
              "wrong mixed half-tie rejection");
      rejected = true;
    }
    require(rejected, "mixed half-tie rounded away from even was accepted");
    ++passed;
  }
  for (bool imaginary : {false, true})
    for (bool negative : {false, true}) {
      auto f = fixture::odd_quadratic(imaginary, false, negative);
      Input in;
      in.limits.algebraic_degree = 16;
      in.open(save(f.encode()));
      require(in.quadratic_branches(), "mixed odd/quadratic path did not certify");
      ++passed;
      // This cap admits the exact cubic prefix but refuses Qbar expansion of the
      // suffix. Success above must therefore use the certified ball induction.
      bool refused = false;
      try {
        in.check_branches_algebraic();
      } catch (const Refuse&) {
        refused = true;
      }
      require(refused, "mixed regression must exceed the exact suffix budget");
      ++passed;
    }
  for (bool negative : {false, true}) {
    auto f = fixture::odd_quadratic(false, true, negative);
    Input in;
    in.open(save(f.encode()));
    require(!in.quadratic_branches(), "uncertain suffix singleton did not request exact fallback");
    in.check_branches_algebraic();
    ++passed;
  }
  for (int kind = 0; kind < 5; ++kind) {
    auto f = fixture::odd_quadratic();
    string expected;
    if (kind == 0) {
      fixture::replace(f.meta, "\"branch_bits\":\"128\"", "\"branch_bits\":\"256\"");
      expected = "minimum canonical precision";
    } else if (kind == 1 || kind == 2) {
      auto from = kind == 1 ? fixture::cbrt2_box() : fixture::root2_box(24);
      auto to = from;
      fixture::replace(to, " 1 -80", " 3 -80");
      fixture::replace(f.meta, from, to);
      expected = "canonical exact rounding";
    } else if (kind == 3) {
      fixture::replace(f.meta, fixture::root2_box(24), "1 0 0 0");
      expected = "canonical exact rounding";
    } else {
      fixture::replace(
          f.meta, fixture::arr({fixture::str(fixture::root2_box(24)), fixture::str("0 0 0 0")}),
          fixture::arr({fixture::str(fixture::root2_box(24)), fixture::str("1 0 0 0")}));
      expected = "no exact root";
    }
    bool rejected = false;
    try {
      Input in;
      in.open(save(f.encode()));
    } catch (const Reject& e) {
      require(string(e.what()).find(expected) != string::npos,
              "wrong mixed rejection: " + string(e.what()));
      rejected = true;
    }
    require(rejected, "mixed branch mutation was accepted");
    ++passed;
  }
}

void targeted_cases() {
  {
    fixture::File f;
    auto branch = fixture::sqrt2_box();
    auto huge = branch;
    fixture::replace(huge, " -7f ", " 100001 ");
    fixture::replace(f.meta, branch, huge);
    refusal(
        "branch component exponent admission", f.encode(), [](auto&) {},
        "branch string size/exponent");
    refusal(
        "odd degree admission", fixture::odd().encode(),
        [](auto& in) { in.limits.algebraic_degree = 2; }, "odd branch degree admission");
    auto odd = fixture::odd();
    odd.leaves[0].bytes = fixture::leaf(0, 0, 1, {{0, 0, 1}});
    odd.rebuild_physical(false);
    refusal(
        "odd coefficient conjugates admission", odd.encode(),
        [](auto& in) { in.limits.algebraic_degree = 4; }, "coefficient-conjugate degree admission");
    refusal(
        "odd resultant height admission", fixture::odd_admission().encode(),
        [](auto& in) { in.limits.algebraic_bits = 512; },
        "odd branch resultant degree/height admission");
  }
  {
    auto b = fixture::File().encode();
    for (bool physical : {false, true}) {
      auto bad = b;
      Uint64 dir = fixture::get(b, b.size() - 112 + (physical ? 16 : 0));
      fixture::set(bad, dir + 16, fixture::get(b, dir + 16) - 1);
      negative("directory count with wrong declared length", bad);
    }
    auto bad = b;
    Uint64 po = fixture::get(b, b.size() - 112 + 16), lo = fixture::get(b, b.size() - 112);
    Uint64 off = fixture::get(b, po + 24 + 112 + 8), length = lo - off + 1;
    fixture::set(bad, po + 24 + 112 + 16, length);
    fixture::set(bad, po + 24 + 112 + 32, length);
    fixture::reseal(bad);
    negative("physical span overlaps directory", bad);
    bad = b;
    bad.insert(bad.begin() + lo, 0);
    fixture::set(bad, bad.size() - 112, lo + 1);
    fixture::set(bad, bad.size() - 112 + 16, po + 1);
    fixture::set(bad, bad.size() - 112 + 96, bad.size());
    negative("unowned byte before directories", bad);
    // A complete zero-length LDIR is rejected before reading its absent count.
    fixture::ByteBuffer empty(b.begin(), b.begin() + lo);
    fixture::append(empty, fixture::text("LDIR"));
    fixture::word(empty, 1, 4);
    fixture::word(empty, 0);
    Uint64 new_po = empty.size();
    fixture::append(empty, fixture::ByteBuffer(b.begin() + po, b.end() - 112));
    fixture::append(empty, fixture::ByteBuffer(b.end() - 112, b.end()));
    fixture::set(empty, empty.size() - 112 + 8, 0);
    fixture::set(empty, empty.size() - 112 + 16, new_po);
    fixture::set(empty, empty.size() - 112 + 96, empty.size());
    negative("directory missing count bytes", empty);
  }
  mutation("BODY after PROV", [](auto& f) {
    fixture::P p;
    p.type = "PROV";
    p.flags = 0;
    p.bytes = fixture::text("{}");
    f.physical.insert(f.physical.end() - 1, p);
  });
  mutation("PROV has non-element object", [](auto& f) {
    fixture::P p;
    p.type = "PROV";
    p.flags = 0;
    p.object = 1;
    p.bytes = fixture::text("{}");
    f.physical.push_back(p);
  });
  {
    fixture::File f(4, false, true);
    f.leaves.resize(f.leaves.size() - 4);
    f.rebuild_physical(false);
    negative("EXMT-derived missing logical leaves", f.encode());
  }
  meta("EXMT-derived logical boundary", fixture::descriptor("rep", 0, {}, "1", 1),
       fixture::descriptor("rep", 0, {0}, "1", 1));
  mutation("extra logical leaf after final element", [](auto& f) {
    auto l = f.leaves.back();
    l.first = 1;
    l.bytes = fixture::leaf(l.element, 1, 1, {{1, 1, 0}});
    f.leaves.push_back(l);
    f.rebuild_physical(false);
  });
  meta("branch two components required",
       "\"branches\":[[" + fixture::str(fixture::sqrt2_box()) + ",\"0 0 0 0\"]]",
       "\"branches\":[\"bad\"]");
  meta("element missing levels", "\"kind\":\"wsq\",\"level\":\"0\",\"levels\":[],",
       "\"kind\":\"wsq\",\"level\":\"0\",");
  {
    auto f = fixture::odd();
    fixture::replace(f.meta, "\"k\":\"0\"", "\"k\":\"1\"");
    negative("odd polynomial coefficient missing", f.encode());
  }
  {
    auto f = fixture::odd();
    fixture::replace(f.meta,
                     "\"branches\":[[" + fixture::str(fixture::cbrt2_box()) + ",\"0 0 0 0\"]]",
                     "\"branches\":[[\"0 0 1 4\",\"0 0 1 4\"]]");
    negative("multiple exact roots in closed branch", f.encode());
  }
  mutation("unused physical byte after checked leaf",
           [](auto& f) { f.physical[1].bytes.push_back(0); });
  mutation("full y above max_bits", [](auto& f) {
    f.leaves.back().bytes = fixture::leaf(3, 0, 1, {{0, 0, 2}});
    f.rebuild_physical(false);
  });
  mutation(
      "PREC value array",
      [](auto& f) {
        fixture::P p;
        p.type = "PREC";
        p.flags = 0;
        p.bytes = fixture::text("{\"delta_bits\":[]}");
        f.physical.push_back(p);
      },
      true);
  raymeta("decoder full length invalid hex", ",\"rays\":",
          ",\"decoder\":{\"name\":\"sic-decode/1.0\",\"source_commit\":"
          "\"gggggggggggggggggggggggggggggggggggggggg\"},\"rays\":");
  meta("branch shape", "\"branches\":[[", "\"branches\":[\"bad\",[");
  {
    fixture::File f;
    auto b = f.encode();
    Uint64 lo = fixture::get(b, b.size() - 112);
    fixture::set(b, lo + 24 + 32, 0);
    fixture::reseal(b);
    negative("EXMT logical hash independent of physical hash", b);
  }
  {
    fixture::File f(4, true);
    auto& b = f.physical[f.physical.size() - 2].bytes;
    string s(b.begin(), b.end());
    fixture::replace(s, "\"rays\":[",
                     "\"rays\":[{\"accuracy_bits\":\"127\",\"anchor\":\"0\",\"label\":\"c0\","
                     "\"word\":[\"1\"]},");
    fixture::replace(s, "\"label\":\"c0\",\"word\":[\"0\"]}]",
                     "\"label\":\"c1\",\"word\":[\"0\"]}]");
    b = fixture::text(s);
    negative("increasing labels with decreasing words", f.encode());
  }
  {
    fixture::File f(4, false, true);
    size_t first = 32;
    f.leaves[first].bytes = fixture::leaf(f.leaves[first].element, 0, 16384, {{0, 1, 0}});
    f.rebuild_physical(false);
    Input in;
    in.open(save(f.encode()), {}, false);
    in.limits.coordinate_pairs = 1;
    try {
      in.element(in.rep.at(0));
      throw std::runtime_error("element aggregate coordinate limit missing");
    } catch (const Refuse& e) {
      require(string(e.what()).find("element nonzero coordinate") != string::npos,
              "aggregate limit category");
      ++passed;
    }
  }
  {
    Input in;
    in.deg = {2};
    in.shifts = {1};
    try {
      in.check_rescaling({});
      throw std::runtime_error("zero-gain shift accepted");
    } catch (const Reject& e) {
      require(string(e.what()).find("zero-gain") != string::npos, "zero gain category");
      ++passed;
    }
  }
  {
    auto f = fixture::dyadic(true);
    fixture::replace(f.meta, "\"branch_bits\":\"256\"", "\"branch_bits\":\"128\"");
    Input in;
    in.open(save(f.encode()), {}, false);
    try {
      in.check_branches_algebraic();
      throw std::runtime_error("algebraic precision minimum missing");
    } catch (const Reject& e) {
      require(string(e.what()).find("disjoint/nonzero") != string::npos,
              "algebraic minimum category");
      ++passed;
    }
  }
}
}  // namespace sic_check::crt::test
