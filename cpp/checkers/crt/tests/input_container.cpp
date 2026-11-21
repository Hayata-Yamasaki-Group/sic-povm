// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
#include "test_general.hpp"
namespace sic_check::crt::test {
namespace filesystem = std::filesystem;
using namespace sic_check::crt::sicx;
void valid_cases() {
  positive("explicit d2 spec presentation", fixture::d2_spec().encode());
  for (Uint64 d : {2, 4, 8}) {
    fixture::File f(d);
    positive("exact-only d" + std::to_string(d), f.encode());
    fixture::File g(d, true);
    positive("finalized d" + std::to_string(d), g.encode());
  }
  fixture::File f;
  fixture::P prov;
  prov.type = "PROV";
  prov.flags = 0;
  prov.bytes = fixture::text("{\"source\":\"test\"}");
  f.physical.push_back(prov);
  positive("PROV", f.encode());
  f.finalize();
  fixture::P prec;
  prec.type = "PREC";
  prec.flags = 0;
  prec.bytes = fixture::text("{\"delta_bits\":\"64\"}");
  f.physical.push_back(prec);
  positive("PREC", f.encode());
  // The pair transform oracle from §4.4. Test vectors cannot be satisfied by a consistently wrong
  // inverse.
  for (auto [y, k] :
       std::vector<std::pair<int, int>>{{4, 2}, {2, 1}, {-1, -1}, {-2, -2}, {-8, -5}}) {
    Z Y(y), D(5), K;
    kfun(K.v, Y.v, D.v);
    require(fmpz_equal_si(K.v, k), "pair transform golden");
    ++passed;
  }
  for (Int64 y : {-3, -2, -1, 1, 2, 3}) {
    fixture::File b(2);
    b.leaves.back().bytes = fixture::leaf(2, 0, 1, {{0, 1, y}}, -3);
    fixture::replace(b.meta, "\"kind\":\"rep\",\"levels\":[],\"max_bits\":\"1\",\"position\":\"1\"",
                     "\"kind\":\"rep\",\"levels\":[],\"max_bits\":\"2\",\"position\":\"1\"");
    // Set exact attained bound separately for |y|=1.
    if (y == 1 || y == -1)
      fixture::replace(b.meta, "\"max_bits\":\"2\",\"position\":\"1\"",
                       "\"max_bits\":\"1\",\"position\":\"1\"");
    b.rebuild_physical(false);
    Input in;
    in.open(save(b.encode()));
    auto p = in.element(2);
    require(p.size() == 1 && fmpz_equal_si(p[0].x.v, 1) && fmpz_equal_si(p[0].y.v, y),
            "d2 balanced pair inverse");
    ++passed;
  }
  for (bool negative : {false, true})
    for (unsigned tie : {1, 3})
      positive("nearest exact tie to even", fixture::dyadic(false, tie, negative).encode());
  positive("minimum branch precision 256", fixture::dyadic(true).encode());
  positive("odd-degree tower", fixture::odd().encode());
  positive("complete odd action group", fixture::odd(true).encode());
  {
    fixture::File zero;
    zero.leaves.back().bytes = fixture::leaf(3, 0, 1, {});
    fixture::replace(zero.meta, fixture::descriptor("rep", 2, {}, "1", 1),
                     fixture::descriptor("rep", 2, {}, "1", 0));
    zero.rebuild_physical(false);
    positive("zero representative", zero.encode());
  }
  {
    fixture::File f(4, true);
    auto& chunk = f.physical[f.physical.size() - 2];
    auto rays = string(chunk.bytes.begin(), chunk.bytes.end());
    fixture::replace(rays, "\"word\":[\"0\"]", "\"word\":[\"1\"]");
    chunk.bytes = fixture::text(rays);
    positive("c0 with nonzero word", f.encode());
  }
  {
    fixture::File f(4, true);
    auto& chunk = f.physical[f.physical.size() - 2];
    auto rays = string(chunk.bytes.begin(), chunk.bytes.end());
    fixture::replace(
        rays, ",\"rays\":",
        ",\"decoder\":{\"name\":\"sic-decode/"
        "1.0\",\"source_commit\":\"0123456789abcdef0123456789abcdef01234567\"},\"rays\":");
    chunk.bytes = fixture::text(rays);
    positive("decoder source commit", f.encode());
  }
  {
    fixture::File wide(4, true);
    auto& rays = wide.physical[wide.physical.size() - 2];
    auto json = string(rays.bytes.begin(), rays.bytes.end());
    fixture::replace(json, "\"limbs\":\"1\"", "\"limbs\":\"2\"");
    rays.bytes = fixture::text(json);
    auto old = wide.physical.back().bytes;
    fixture::ByteBuffer wider;
    for (size_t off = 0; off < old.size(); off += 40) {
      fixture::append(wider, fixture::ByteBuffer(old.begin() + off, old.begin() + off + 40));
      fixture::word(wider, 0);
    }
    wide.physical.back().bytes = wider;
    positive("nonminimal BALL width", wide.encode());
  }
  {
    fixture::File broad(4, true);
    auto& rays = broad.physical[broad.physical.size() - 2];
    auto json = string(rays.bytes.begin(), rays.bytes.end());
    fixture::replace(json, "\"accuracy_bits\":\"127\"", "\"accuracy_bits\":\"-1\"");
    rays.bytes = fixture::text(json);
    fixture::set(broad.physical.back().bytes, 8, 1);
    fixture::set(broad.physical.back().bytes, 24, 0);
    positive("signed tight accuracy", broad.encode());
  }
  fixture::File shifted;
  fixture::replace(shifted.meta, "\"shifts\":[\"0\"]", "\"shifts\":[\"7\"]");
  positive("authenticated rescaling record", shifted.encode());
  fixture::replace(shifted.meta, "\"shifts\":[\"7\"]", "\"shifts\":[\"999999999999999999\"]");
  positive("large shifts without expansion", shifted.encode());
  {
    Input in;
    in.open(save(shifted.encode()));
    require(in.shifts == vector<Uint64>{999999999999999999ull} &&
                in.integral_shift_hints() == vector<Uint64>{0},
            "large authenticated shifts escaped into CRT expansion hints");
    ++passed;
  }
  {
    Input in;
    in.shifts = {64};
    in.deg = {2};
    require(in.integral_shift_hints() == vector<Uint64>{64}, "small CRT hint changed");
    ++passed;
  }
}

void partial_cases() {
  // Authenticated fixtures: both full validation and deferred RAYS/BALL consumption
  // must reject nonzero starts and skipped labels, independently of production parsing.
  for (bool skipped : {false, true}) {
    fixture::File file(4, true);
    auto& rays = file.physical[file.physical.size() - 2];
    auto json = string(rays.bytes.begin(), rays.bytes.end());
    if (skipped) {
      const auto begin = json.find("[{", json.find("\"rays\"")) + 1;
      const auto end = json.find("}]", begin) + 1;
      auto second = json.substr(begin, end - begin);
      fixture::replace(second, "c0", "c4");
      fixture::replace(second, "\"word\":[\"0\"]", "\"word\":[\"1\"]");
      json.insert(end, "," + second);
      auto ball = file.physical.back();
      ball.object = 1;
      file.physical.push_back(ball);
    } else
      fixture::replace(json, "c0", "c4");
    // push_back above may invalidate the borrowed chunk reference.
    file.physical[file.physical.size() - (skipped ? 3 : 2)].bytes = fixture::text(json);
    for (bool full : {false, true}) {
      bool rejected = false;
      try {
        Input in;
        in.open(save(file.encode()), {}, full);
        if (!full) in.verify_chunk(in.ch.size() - 1);
      } catch (const Reject& e) {
        require(string(e.what()) == "SICX input: ray label must equal array index",
                "nonsequential ray label: wrong rejection: " + string(e.what()));
        rejected = true;
      }
      require(rejected, "nonsequential ray label accepted");
      ++passed;
    }
  }

  {
    fixture::File file;
    // No unread chunk may defer codec admission: partial bootstrap checks the whole PDIR.
    file.physical.back().codec = 1;
    bool caught = false;
    try {
      Input partial;
      partial.open(save(file.encode()), {}, false);
    } catch (const Reject& e) {
      require(string(e.what()).find("unsupported codec") != string::npos,
              "partial bootstrap codec rejection category");
      caught = true;
    }
    require(caught, "partial metadata admitted an unread chunk's unknown codec");
    ++passed;
  }
  fixture::File f(8, true);
  auto bytes = f.encode();
  string path = save(bytes);
  Input full;
  full.open(path);
  Trust trust{hex(full.exact_root), hex(full.merkle), ""};
  {
    Input in;
    in.open(path, trust, false);
    require(!in.scope.full && in.scope.exact_trusted && in.scope.file_trusted &&
                in.scope.chunks.size() == 1 && !in.scope.whole_checked,
            "partial metadata scope");
    in.element(1);
    require(in.scope.elements.count(1) && !in.scope.full, "partial complete element scope");
    ++passed;
  }
  {
    Input in;
    in.open(path, {trust.exact, "", ""}, false);
    require(!in.scope.file_trusted && in.scope.exact_trusted, "exact-only trust scope");
    ++passed;
  }
  {
    Input in;
    in.open(path, {}, false);
    require(!in.scope.exact_trusted && in.scope.describe().find("untrusted") != string::npos,
            "untrusted scope");
    ++passed;
  }
  {
    Input in;
    in.open(path, {"", "", hex(full.file_sha)}, false);
    require(
        in.scope.exact_trusted && in.scope.file_trusted && in.scope.whole_checked && !in.scope.full,
        "trusted full SHA scope");
    ++passed;
  }
  for (int which = 0; which < 3; ++which) {
    Trust wrong = trust;
    if (which == 0) wrong.exact = string(64, '0');
    if (which == 1) wrong.file = string(64, '0');
    if (which == 2) wrong.whole = string(64, '0');
    try {
      Input in;
      in.open(path, wrong, false);
      throw std::runtime_error("wrong trusted digest accepted");
    } catch (const Reject&) {
      ++passed;
    }
  }
  {
    Input in;
    in.open(path, trust, false);
    in.verify_chunk(in.ch.size() - 1);
    require(in.rays.size() == 1 && in.scope.chunks.count(in.ch.size() - 1) &&
                in.scope.chunks.count(in.ch.size() - 2) && !in.scope.full,
            "BALL partial dependency");
    ++passed;
  }
  {
    fixture::File local(4, true);
    fixture::P p;
    p.type = "PREC";
    p.flags = 0;
    p.bytes = fixture::text("{}");
    local.physical.push_back(p);
    Input in;
    in.open(save(local.encode()), {}, false);
    in.verify_chunk(in.ch.size() - 1);
    require(in.scope.chunks.count(in.ch.size() - 1), "partial PREC");
    ++passed;
  }
  {
    auto p = save(bytes);
    Input in;
    in.open(p, {}, false);
    {
      std::ofstream out(p, std::ios::app | std::ios::binary);
      out.put('x');
    }
    try {
      in.whole_file_sha();
      throw std::runtime_error("append ignored by whole-file hash");
    } catch (const Reject&) {
      ++passed;
    }
    try {
      in.element(1);
      throw std::runtime_error("append ignored by partial read");
    } catch (const Reject&) {
      ++passed;
    }
  }
  // Mutate an unread body, retain all published digests. Partial bootstrap succeeds; use/full
  // fails.
  {
    auto bad = bytes;
    Uint64 po = fixture::get(bad, bad.size() - 112 + 16);
    Uint64 chunk = fixture::get(bad, po + 24 + 2 * 112 + 8);
    bad.at(chunk + 16) ^= 1;
    auto p = save(bad);
    Input in;
    in.open(p, trust, false);
    require(in.scope.chunks.size() == 1, "unread payload claimed");
    try {
      in.element(1);
      throw std::runtime_error("corrupt requested payload accepted");
    } catch (const Reject&) {
      ++passed;
    }
    negative("full reads corrupted body", bad);
  }
  // A valid first leaf plus invalid second leaf must produce no result and no checked-chunk mark.
  {
    fixture::File grouped(4, false, true);
    grouped.leaves.back().bytes.push_back(0);
    grouped.rebuild_physical(true);
    Input in;
    in.open(save(grouped.encode()), {}, false);
    size_t ci = in.ch.size() - 1;
    try {
      in.read_chunk(ci);
      throw std::runtime_error("partly consumed grouped chunk");
    } catch (const Reject&) {
      require(!in.scope.chunks.count(ci), "failed chunk marked verified");
      ++passed;
    }
  }
}
}  // namespace sic_check::crt::test
