// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
#include "test_general.hpp"
namespace sic_check::crt::test {
namespace filesystem = std::filesystem;
using namespace sic_check::crt::sicx;
void amendment_cases() {
  {
    fixture::File f(4, true);
    auto& rays = f.physical[f.physical.size() - 2].bytes;
    string s(rays.begin(), rays.end());
    fixture::replace(s, "\"word\":[\"0\"]", "\"word\":[\"1\"]");
    rays = fixture::text(s);
    positive("c0 need not have word zero", f.encode());
  }
  for (const string& value : {"{}", "\"informative\"", "[]"}) {
    fixture::File f;
    fixture::P p;
    p.type = "PROV";
    p.flags = 0;
    p.bytes = fixture::text("{\"generator_rescaling\":" + value + "}");
    f.physical.push_back(p);
    negative("unconditional top-level rescaling ban", f.encode(), "PROV duplicates");
  }
  {
    fixture::File f;
    fixture::P p;
    p.type = "PROV";
    p.flags = 0;
    p.bytes = fixture::text("{\"history\":{\"generator_rescaling\":{}}}");
    f.physical.push_back(p);
    positive("nested rescaling annotations", f.encode());
  }
  {
    fixture::File f(8, true);
    auto bytes = f.encode();
    Input full;
    full.open(save(bytes));
    Input direct;
    direct.open(save(bytes), {"", hex(full.merkle), ""}, false);
    require(direct.scope.exact_trusted && direct.scope.file_trusted &&
                direct.scope.exact_route.find("transitive via file root") != string::npos,
            "file-root trust did not bind LDIR/exact root");
    ++passed;
    for (size_t ci = 1; ci < full.ch.size(); ++ci) {
      auto bad = bytes;
      bad.at(full.ch[ci].off) ^= 1;
      Input partial;
      partial.open(save(bad), {}, false);
      require(partial.scope.chunks.size() == 1, "bootstrap inspected unread header");
      ++passed;
      try {
        partial.verify_chunk(ci);
        throw std::runtime_error("deferred corrupted header accepted on use");
      } catch (const Reject& e) {
        require(string(e.what()).find("physical header") != string::npos, "header rejection");
        ++passed;
      }
      negative("full checks deferred header", bad, "physical header");
    }
  }
  // Directory bootstrap cannot infer RAYS payload semantics; RAYS consumption must.
  for (bool count : {false, true}) {
    fixture::File f(4, true);
    if (count) {
      auto& b = f.physical[f.physical.size() - 2].bytes;
      string s(b.begin(), b.end());
      fixture::replace(s, "\"rays\":[",
                       "\"rays\":[{\"accuracy_bits\":\"127\",\"anchor\":\"0\",\"label\":\"c0\","
                       "\"word\":[\"0\"]},");
      fixture::replace(s, "\"label\":\"c0\",\"word\":[\"0\"]}]",
                       "\"label\":\"c1\",\"word\":[\"1\"]}]");
      b = fixture::text(s);
    } else
      f.physical.back().bytes.pop_back();
    Input in;
    in.open(save(f.encode()), {}, false);
    ++passed;
    try {
      in.verify_chunk(in.ch.size() - 2);
      throw std::runtime_error("deferred BALL width/count accepted with RAYS");
    } catch (const Reject&) {
      ++passed;
    }
  }
  {
    fixture::File f;
    Input in;
    in.open(save(f.encode()), {}, false);
    for (size_t ci = 1; ci < in.ch.size(); ++ci) in.verify_chunk(ci);
    require(in.scope.exact_checked && !in.scope.full && in.scope.elements.size() == in.el.size(),
            "all partial exact chunks did not complete global checks");
    ++passed;
    fixture::replace(f.meta, fixture::sqrt2_box(), "1 0 0 0");
    Input bad;
    bad.open(save(f.encode()), {}, false);
    try {
      for (size_t ci = 1; ci < bad.ch.size(); ++ci) bad.verify_chunk(ci);
      throw std::runtime_error("all-exact partial branch obligation bypassed");
    } catch (const Reject&) {
      ++passed;
    }
  }
  {
    auto f = fixture::odd(true);
    Input plus;
    plus.open(save(f.encode()));
    auto dp = plus.element(3), np = plus.element(5);
    f.leaves[3].bytes = fixture::leaf(3, 0, 1, {{0, -1, 0}});
    f.leaves[5].bytes = fixture::leaf(5, 0, 1, {{0, -1, 0}});
    f.rebuild_physical(false);
    Input minus;
    minus.open(save(f.encode()));
    require(plus.exact_root != minus.exact_root, "signed odd presentation not bound to exact root");
    auto dm = minus.element(3), nm = minus.element(5);
    require(fmpz_equal_si(dm[0].x.v, -1) && fmpz_equal_si(nm[0].x.v, -1),
            "reader changed stored odd-action signs");
    Z a, b;
    fmpz_mul(a.v, dp[0].x.v, nm[0].x.v);
    fmpz_mul(b.v, dm[0].x.v, np[0].x.v);
    require(fmpz_equal(a.v, b.v), "simultaneous odd sign changed quotient");
    ++passed;
  }
  for (const auto& f : {fixture::File(), fixture::dyadic(true), fixture::dyadic(false, 3, true)}) {
    Input in;
    in.open(save(f.encode()));
    in.check_branches_algebraic();
    ++passed;
  }
}
}  // namespace sic_check::crt::test
