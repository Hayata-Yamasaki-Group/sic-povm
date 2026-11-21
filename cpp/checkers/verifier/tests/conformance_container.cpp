// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
namespace sic_check::verifier::test {
using namespace sic_check::verifier;
using namespace fixture;
void container_envelopes(size_t& valid, DimensionContext& dimension, string& pre, Output& a,
                         Output& b) {
  require(a.root != b.root, "file root binds finalization");
  require(run(a, pre + "valid-exact") == 0, "valid exact");
  require(run(b, pre + "valid-final", {}, true) == 0, "valid final full decoding");
  valid += 2;
  for (unsigned off : {0u, 8u, 10u, 12u})
    dimension.mutate("header-" + std::to_string(off),
                     [=](Model& m) { m.change_file = [=](ByteBuffer& f) { f[off] ^= 2; }; });
  for (unsigned off : {0u, 8u, 16u, 24u, 32u, 64u, 96u, 104u})
    dimension.mutate("footer-" + std::to_string(off), [=](Model& m) {
      m.change_file = [=](ByteBuffer& f) { f[f.size() - 112 + off] ^= 1; };
    });
  dimension.mutate("trailing-byte",
                   [](Model& m) { m.change_file = [](ByteBuffer& f) { f.push_back(0); }; });
  dimension.mutate("footer-104-bytes", [](Model& m) {
    m.change_file = [](ByteBuffer& f) { f.erase(f.end() - 112, f.end() - 104); };
  });
  dimension.mutate("footer-64-bytes", [](Model& m) {
    m.change_file = [](ByteBuffer& f) { f.erase(f.end() - 112, f.end() - 64); };
  });
  for (string type : {"META", "INDX", "ABCD", "LDIR", "PDIR", "\x20XXX"})
    for (unsigned flags : {0u, 1u})
      dimension.mutate("unknown-" + hex((const Uint8*)type.data(), 4) + std::to_string(flags),
                       [=](Model& m) {
                         m.extras.push_back({type, flags, {}});
                       });
  dimension.mutate("required-flags",
                   [](Model& m) { m.change_plan = [](auto& p) { p[1].flags = 0; }; });
  dimension.mutate("reserved-flags",
                   [](Model& m) { m.change_plan = [](auto& p) { p[1].flags = 3; }; });
  dimension.mutate("optional-flags", [](Model& m) {
    m.extras.push_back({"PROV", 1, bytes("{}")});
  });
  for (string t : {"PROV", "PREC"})
    dimension.mutate(
        "codec-" + t,
        [=](Model& m) {
          Physical p{t, 0, bytes("{}")};
          p.codec = 1;
          m.extras.push_back(p);
        },
        t == "PREC");
  dimension.mutate("raw-length",
                   [](Model& m) { m.change_plan = [](auto& p) { p[0].decoded_delta = 1; }; });
  dimension.mutate("physical-reserved",
                   [](Model& m) { m.change_plan = [](auto& p) { p[0].reserved = 1; }; });
  for (unsigned off : {0u, 4u, 8u, 16u, 24u, 32u})
    dimension.mutate("ldir-entry-" + std::to_string(off),
                     [=](Model& m) { m.change_ldir = [=](ByteBuffer& b) { b[8 + off] ^= 1; }; });
  for (unsigned off : {0u, 4u, 8u, 16u, 24u, 28u, 32u, 40u, 48u, 56u, 64u, 72u, 80u})
    dimension.mutate("pdir-entry-" + std::to_string(off),
                     [=](Model& m) { m.change_pdir = [=](ByteBuffer& b) { b[8 + off] ^= 1; }; });
  for (string dir : {"ldir", "pdir"})
    dimension.mutate(dir + "-count", [=](Model& m) {
      auto fn = [](ByteBuffer& b) { set64(b, 0, 0); };
      if (dir == "ldir")
        m.change_ldir = fn;
      else
        m.change_pdir = fn;
    });
  dimension.mutate("overflow-offset", [](Model& m) {
    m.change_pdir = [](ByteBuffer& b) { set64(b, 8 + 8, UINT64_MAX); };
  });
  dimension.mutate("physical-header",
                   [](Model& m) { m.change_file = [](ByteBuffer& b) { b[20] ^= 1; }; });
  dimension.mutate("payload-digest",
                   [](Model& m) { m.change_file = [](ByteBuffer& b) { b[32] ^= 1; }; });
  dimension.mutate("missing-element-chunk",
                   [](Model& m) { m.change_plan = [](auto& p) { p.erase(p.begin() + 1); }; });
  dimension.mutate("duplicate-EXMT", [](Model& m) {
    m.change_plan = [](auto& p) { p.insert(p.begin() + 1, p[0]); };
  });
  dimension.mutate("chunk-order",
                   [](Model& m) { m.change_plan = [](auto& p) { std::swap(p[1], p.back()); }; });
}

void partial_payload_authentication(size_t& valid, size_t& rejects, DimensionContext& dimension,
                                    string& pre, Output& /* b */, ByteBuffer& /* encoded */,
                                    Model& unread) {
  unread.change_file = [](ByteBuffer& f) {
    size_t pos = 32;
    Uint64 n = 0;
    for (int i = 0; i < 8; ++i) n |= Uint64(f[24 + i]) << (8 * i);
    f[pos + n + 16 + 3] ^= 1;
  };
  Opts partial;
  partial.partial_elements = {2};
  require(run(write(unread), pre + "partial-unread-corruption", partial) == 0,
          "unread tower not claimed");
  ++valid;
  partial.partial_elements = {0};
  require(run(write(unread), pre + "partial-selected-corruption", partial) == 2,
          "selected tower corruption");
  ++rejects;

  dimension.mutate("missing-EXMT-key", [](Model& m) { m.meta.erase("d"); });
  dimension.meta("format-name", "format", "\"OTHER\"");
  dimension.meta("features-type", "required_features", "{}");
  dimension.meta("integer-19digits", "d", "\"1000000000000000000\"");
  dimension.mutate("scale-4096digits",
                   [](Model& m) { m.elements[0].fields["scale"] = quote(string(4096, '1')); });
  dimension.mutate("scale-signed", [](Model& m) { m.elements[0].fields["scale"] = "\"-1\""; });
  dimension.mutate("maxbits-bound",
                   [](Model& m) { m.elements[0].fields["max_bits"] = "\"8388609\""; });
  dimension.mutate("duplicate-level-set",
                   [](Model& m) { m.elements[2].fields["levels"] = "[\"0\",\"0\"]"; });
  dimension.mutate("nonleaf-coverage", [](Model& m) {
    Physical p{"PROV", 0, bytes("{}")};
    p.first_leaf = 1;
    p.leaf_count = 1;
    m.extras.push_back(p);
  });
  dimension.mutate("leaf-coverage-zero",
                   [](Model& m) { m.change_plan = [](auto& p) { p[1].leaf_count = 0; }; });
  dimension.mutate("physical-cross-element",
                   [](Model& m) { m.change_plan = [](auto& p) { p[1].leaf_count = 2; }; });
  dimension.mutate("ldir-count-bound", [](Model& m) {
    m.change_ldir = [](ByteBuffer& b) { set64(b, 0, (1u << 24) + 1); };
  });
  dimension.mutate("pdir-count-bound", [](Model& m) {
    m.change_pdir = [](ByteBuffer& b) { set64(b, 0, (1u << 24) + 1); };
  });
  dimension.mutate("empty-consistent-LDIR", [](Model& m) {
    m.change_ldir = [](ByteBuffer& b) {
      b.resize(8);
      set64(b, 0, 0);
    };
  });
  dimension.mutate("directory-length-count",
                   [](Model& m) { m.change_ldir = [](ByteBuffer& b) { set64(b, 0, 2); }; });
  for (ByteBuffer encoded :
       vector<ByteBuffer>{{0x80},
                          {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 2},
                          {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0}})
    dimension.mutate("integer-varint-" + hex(encoded.data(), encoded.size()), [=](Model& m) {
      m.elements[0].leaves[0] = {0, 0, 1, 3};
      append(m.elements[0].leaves[0], encoded);
    });
}

void directory_bootstrap_refusals(unsigned& d, size_t& valid, size_t& rejects,
                                  DimensionContext& dimension, string& pre, Output& b,
                                  string& /* json */) {
  for (auto [name, off] : vector<std::pair<string, size_t>>{
           {"offset", 8}, {"first-leaf", 40}, {"object", 56}, {"span", 72}}) {
    auto m = small(d, true);
    m.change_pdir = [=](ByteBuffer& b) {
      set64(b, 8 + 112 + off, le64(b.data() + 8 + 112 + off) + 1);
    };
    dimension.partial_bad("partial-bootstrap-tower-" + name, m);
  }
  for (auto [name, field] : vector<std::pair<string, unsigned>>{{"index", 56}, {"count", 72}}) {
    auto m = small(d, true);
    m.change_pdir = [=](ByteBuffer& b) {
      for (size_t p = 8; p < b.size(); p += 112)
        if (string((const char*)b.data() + p, 4) == "BALL")
          set64(b, p + field, field == 56 ? 1 : d + 1);
    };
    dimension.partial_bad("partial-bootstrap-BALL-" + name, m);
  }
  {
    auto m = small(d);
    Physical p{"PROV", 0, bytes("{}")};
    p.obj = 1;
    m.extras.push_back(p);
    dimension.partial_bad("partial-bootstrap-PROV-object", m);
  }
  dimension.mutate("physical-trailing-byte", [](Model& m) {
    m.change_plan = [](auto& plan) { plan[1].payload.push_back(0); };
  });
  dimension.mutate(
      "BALL-norm-valid-wrong-gauge",
      [](Model& m) {
        auto& b = m.extras[1].payload;
        set64(b, 8, Uint64(-2));
        set64(b, 16, 1);
        set64(b, 24, Uint64(-3));
        set64(b, 32, 3);
        set64(b, 48, Uint64(-1));
        set64(b, 56, 1);
        set64(b, 64, Uint64(-3));
        set64(b, 72, 1);
        string r(m.extras[0].payload.begin(), m.extras[0].payload.end());
        r.replace(r.find("199"), 3, "2");
        m.extras[0].payload = bytes(r);
      },
      true);
  dimension.mutate("PDIR-envelope-flags", [](Model& m) {
    m.change_file = [](ByteBuffer& f) {
      auto off = le64(f.data() + f.size() - 112 + 16);
      f.at(off + 4) ^= 1;
    };
  });
  dimension.mutate(
      "optional-chunk-order",
      [](Model& m) {
        m.extras.insert(m.extras.begin() + 1, {"PROV", 0, bytes("{}")});
      },
      true);
  dimension.mutate("consistent-leaf-first", [](Model& m) {
    Uint64 id = m.elements.size() - 1;
    m.elements[id].leaves[0] = leaf(id, 1, 1, 0);
    auto payload = m.elements[id].leaves[0];
    m.change_plan = [=](auto& plan) {
      for (auto& p : plan)
        if (p.type == "BODY" && p.obj == id) p.first = 1;
    };
    m.change_ldir = [=](ByteBuffer& b) {
      size_t at = b.size() - 64;
      set64(b, at + 16, 1);
      ByteBuffer h = bytes(string("SICX1-LEAF\0", 11));
      append(h, bytes("BODY"));
      u64le(h, id);
      u64le(h, 1);
      u64le(h, 1);
      u64le(h, payload.size());
      append(h, payload);
      auto sum = digest(h);
      std::copy(sum.begin(), sum.end(), b.begin() + at + 32);
    };
  });
  dimension.mutate("rehashed-EXMT-singleton", [](Model& m) {
    auto payload = std::make_shared<ByteBuffer>();
    m.change_exmt = [=](ByteBuffer& b) { *payload = b; };
    m.change_ldir = [=](ByteBuffer& b) {
      set64(b, 8 + 24, 2);
      ByteBuffer h = bytes(string("SICX1-LEAF\0", 11));
      append(h, bytes("EXMT"));
      u64le(h, 0);
      u64le(h, 0);
      u64le(h, 2);
      u64le(h, payload->size());
      append(h, *payload);
      auto sum = digest(h);
      std::copy(sum.begin(), sum.end(), b.begin() + 8 + 32);
    };
  });
  {
    auto m = small(d, true);
    string json(m.extras[0].payload.begin(), m.extras[0].payload.end());
    string ray = "{\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c0\",\"word\":[\"0\"]}",
           other = ray;
    other.replace(other.find("c0"), 2, "c1");
    other.replace(other.find("[\"0\"]"), 5, "[\"1\"]");
    json.replace(json.find(ray), ray.size(), ray + "," + other);
    m.extras[0].payload = bytes(json);
    require(run(write(m), pre + "partial-deferred-ray-count", Opts{.partial_elements = {2}}) == 0,
            "unread RAYS-dependent count deferred");
    ++valid;
    bad(rejects, pre + "full-missing-BALL-for-second-ray", m);
  }

  // Partial verification must not silently inspect or bless unread payloads.
  auto broken = small(d, true);
  broken.change_file = [](ByteBuffer& f) { f[32] ^= 1; };
  require(run(write(broken), pre + "partial-bad-EXMT", Opts{.partial_elements = {2}}) == 2,
          "partial authenticates EXMT");
  ++rejects;
  Opts part;
  part.partial_elements = {2};
  part.expected_exact = hex(b.exact.data(), 32);
  require(run(b, pre + "partial-trusted", part) == 0, "partial trusted exact root");
  ++valid;
  part.expected_exact = string(64, '0');
  require(run(b, pre + "partial-root-mismatch", part) == 2, "trusted exact mismatch");
  ++rejects;
  Opts trust;
  trust.expected_file = string(64, '0');
  require(run(b, pre + "file-root-mismatch", trust) == 2, "trusted file mismatch");
  ++rejects;
  trust = {};
  trust.expected_sha = string(64, '0');
  require(run(b, pre + "file-sha-mismatch", trust) == 2, "trusted whole-file mismatch");
  ++rejects;
}

void multi_leaf_boundaries(size_t& valid, size_t& rejects) {
  // A real multi-leaf boundary needs >16384 active coordinates, even at d=4/8.
  for (unsigned d : {4u, 8u}) {
    Model m = small(d);
    m.elements.clear();
    vector<string> deg(16, "2"), zero(16, "0"), branches, levels;
    const int primes[] = {3, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61};
    for (int i = 0; i < 16; ++i) {
      // Round sqrt(prime)*2^128 with integer arithmetic; no parser branch helper.
      fmpz_t a, q, tmp;
      fmpz_init_set_ui(a, primes[i]);
      fmpz_init(q);
      fmpz_init(tmp);
      fmpz_mul_2exp(a, a, 256);
      fmpz_sqrt(q, a);
      fmpz_mul_ui(tmp, q, 2);
      fmpz_add_ui(tmp, tmp, 1);
      fmpz_mul(tmp, tmp, tmp);
      fmpz_mul_2exp(a, a, 2);
      if (fmpz_cmp(a, tmp) > 0) fmpz_add_ui(q, q, 1);
      Int64 E = -128;
      while (!fmpz_is_odd(q)) {
        fmpz_tdiv_q_2exp(q, q, 1);
        ++E;
      }
      fmpz_set_si(tmp, E);
      branches.push_back("[" + quote(hexz(q) + " " + hexz(tmp) + " 1 -80") + ",\"0 0 0 0\"]");
      fmpz_clear(a);
      fmpz_clear(q);
      fmpz_clear(tmp);
      element(m, "wsq", i, 1, Uint64(primes[i]) * (1u << i));
      element(m, "act", i, -1, Uint64(primes[i]) * (1u << i));
      m.elements.back().fields["axis"] = quote(std::to_string(i));
      levels.push_back(std::to_string(i));
    }
    m.meta["tower"] = "{\"branch_bits\":\"128\",\"branches\":" + array(branches, false) +
                      ",\"degrees\":" + array(deg) + "}";
    m.meta["galois"] = galois(m.d, deg);
    m.meta["generator_rescaling"] =
        "{\"rule\":\"sicx-2adic-greedy/1.0\",\"shifts\":" + array(zero) + "}";
    element(m, "rep", 0, 1, 1, levels, 65536);
    for (Uint64 r = 1; r <= d / 2; r *= 2) element(m, "rep", r, 0);
    auto decreasing = m;
    decreasing.elements[32].fields["levels"] = "[\"1\",\"0\"]";
    bad(rejects, "d" + std::to_string(d) + "-decreasing-levels", decreasing);
    auto split = write(m);
    m.grouped = true;
    auto grouped = write(m);
    require(split.exact == grouped.exact, "grouping exact root invariant");
    require(split.root != grouped.root, "grouping changes file root");
    Opts opt;
    opt.branch_degree_limit = 65536;
    require(run(split, "d" + std::to_string(d) + "-split", opt) == 0, "split leaves");
    require(run(grouped, "d" + std::to_string(d) + "-grouped", opt) == 0, "grouped leaves");
    valid += 2;
    opt.partial_elements = {32};
    require(run(grouped, "d" + std::to_string(d) + "-grouped-partial", opt) == 0,
            "partial grouped element");
    ++valid;
  }
}

void real_root_certification(size_t& /* valid */, size_t& /* rejects */) {
  {
    // X^3-1 has one real root and a nonreal conjugate pair. The same
    // completeness argument must preserve both nonzero imaginary parts.
    vector<ACB> f(4), roots(3);
    acb_set_si(f[0].v, -1);
    acb_one(f[3].v);
    auto isolate = [&] {
      acb_poly_t poly;
      acb_poly_init(poly);
      for (size_t k = 0; k < f.size(); ++k) acb_poly_set_coeff_acb(poly, k, f[k].v);
      acb_ptr result = _acb_vec_init(3);
      slong found = acb_poly_find_roots(result, poly, nullptr, 0, 2048);
      for (int j = 0; j < 3; ++j) acb_set(roots[j].v, result + j);
      _acb_vec_clear(result, 3);
      acb_poly_clear(poly);
      require(found == 3, "small conjugation fixture roots certified");
    };
    isolate();
    for (auto& root : roots) arb_add_error_2exp_si(acb_imagref(root.v), -512);
    auto original = roots;
    certify_real_roots(f, roots);
    size_t real = 0;
    for (size_t j = 0; j < roots.size(); ++j) {
      if (arb_is_positive(acb_realref(roots[j].v))) {
        ++real;
        require(arb_is_zero(acb_imagref(roots[j].v)), "real member of mixed roots proved");
      } else {
        require(!arb_contains_zero(acb_imagref(roots[j].v)) && acb_equal(roots[j].v, original[j].v),
                "nonreal conjugate pair retained");
      }
    }
    require(real == 1, "exactly one real cubic root");
    // A tiny but exact nonreal coefficient invalidates conjugation symmetry.
    arb_one(acb_imagref(f[0].v));
    arb_mul_2exp_si(acb_imagref(f[0].v), acb_imagref(f[0].v), -1000);
    isolate();
    original = roots;
    certify_real_roots(f, roots);
    for (size_t j = 0; j < roots.size(); ++j)
      require(acb_equal(roots[j].v, original[j].v), "nonreal coefficients forbid real proof");
    arb_zero(acb_imagref(f[0].v));
    arb_add_error_2exp_si(acb_imagref(f[0].v), -1000);
    isolate();
    original = roots;
    certify_real_roots(f, roots);
    for (size_t j = 0; j < roots.size(); ++j)
      require(acb_equal(roots[j].v, original[j].v),
              "zero-containing coefficient imaginary interval forbids real proof");
  }
}
}  // namespace sic_check::verifier::test
