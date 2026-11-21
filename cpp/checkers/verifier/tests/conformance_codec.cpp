// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
namespace sic_check::verifier::test {
using namespace sic_check::verifier;
using namespace fixture;
void integer_run_canonicality(DimensionContext& dimension) {
  dimension.mutate("empty-prefix-run",
                   [](Model& m) { m.elements[0].leaves[0] = {0, 0, 1, 0, 3, 3, 1, 0}; });
  dimension.mutate("adjacent-data-runs", [](Model& m) {
    m.elements[2].fields["levels"] = "[\"0\"]";
    m.elements[2].coords = 2;
    m.elements[2].leaves[0] = {2, 0, 2, 3, 2, 1, 0, 3, 2, 1, 0};
  });
  dimension.mutate("nonzero-run-overrun",
                   [](Model& m) { m.elements[2].leaves[0] = {2, 0, 1, 5, 2, 1, 0, 2, 1, 0}; });
  dimension.mutate("nonprimitive", [](Model& m) {
    m.elements[0].leaves[0] = leaf(0, 0, 1, -2);
    m.elements[0].fields["max_bits"] = "\"2\"";
  });
  dimension.mutate("maxbits-unattained",
                   [](Model& m) { m.elements[0].fields["max_bits"] = "\"2\""; });
  dimension.mutate("maxbits-exceeded",
                   [](Model& m) { m.elements[0].fields["max_bits"] = "\"0\""; });
  dimension.mutate("zero-scale", [](Model& m) { m.elements.back().fields["scale"] = "\"2\""; });
  dimension.mutate("rescaling-noncanonical", [](Model& m) {
    m.meta["tower"] =
        "{\"branch_bits\":\"128\",\"branches\":[[\"0 0 0 0\",\"1 1 0 "
        "0\"]],\"degrees\":[\"2\"]}";
    m.elements[0].fields["scale"] = "\"4\"";
    m.elements[0].fields["max_bits"] = "\"3\"";
    m.elements[1].fields["scale"] = "\"4\"";
    m.elements[1].fields["max_bits"] = "\"3\"";
  });
  dimension.mutate("PROV-rescaling", [](Model& m) {
    m.extras.push_back({"PROV", 0, bytes("{\"generator_rescaling\":{}}")});
  });
  dimension.mutate("PROV-array", [](Model& m) { m.extras.push_back({"PROV", 0, bytes("[]")}); });
  for (string json : {"{\"bad\":\"0\"}", "{\"branch_bits\":[]}", "{\"branch_bits\":\"-1\"}"})
    dimension.mutate(
        "PREC-" + hex((const Uint8*)json.data(), json.size()),
        [=](Model& m) {
          m.extras.push_back({"PREC", 0, bytes(json)});
        },
        true);
  dimension.mutate("PREC-without-RAYS", [](Model& m) {
    m.extras.push_back({"PREC", 0, bytes("{}")});
  });
  dimension.mutate("BALL-without-RAYS", [](Model& m) {
    m.extras.push_back({"BALL", 1, ByteBuffer(320)});
  });
  dimension.mutate(
      "missing-BALL", [](Model& m) { m.extras.pop_back(); }, true);
}

void rescaling_enumeration_oracle(size_t& /* valid */, size_t& /* rejects */) {
  // Independent bounded reference: enumerate feasible shifts one at a time,
  // rather than using the reader's minimum-quotient or suffix-tightness rules.
  {
    Uint64 seed = 0x513ad874cc22;
    auto random = [&]() {
      seed ^= seed << 13;
      seed ^= seed >> 7;
      seed ^= seed << 17;
      return seed;
    };
    for (unsigned trial = 0; trial < 512; ++trial) {
      Container c;
      c.M.deg = {3, 3, 3};
      c.M.shifts = {random() % 5, random() % 5, random() % 5};
      c.rs_N = {1, 3, 9, 27};
      c.rs_gain.assign(3, 0);
      struct Constraint {
        vector<Uint64> a;
        Uint64 v;
      };
      vector<Constraint> constraints;
      auto add = [&](Uint64 e, int own, Uint64 weight, Uint64 valuation) {
        vector<Uint64> a(3);
        for (int i = 0; i < 3; ++i) {
          a[i] = i == own ? weight : e / c.rs_N[i] % 3;
          c.rs_gain[i] += a[i];
        }
        constraints.push_back({a, valuation});
        Uint64 key = e | (Uint64(own + 1) << 40) | (weight << 46);
        auto [it, inserted] = c.rs_minv.emplace(key, valuation);
        if (!inserted) it->second = std::min(it->second, valuation);
      };
      for (int i = 0; i < 3; ++i)
        if (trial % 7 != 0) add(0, i, 1 + random() % 7, random() % 6);
      for (int j = 0, n = 1 + random() % 8; j < n; ++j) add(random() % 27, -1, 0, random() % 6);
      if (trial < 3) {
        c.rs_minv.clear();
        constraints.clear();
        c.rs_gain.assign(3, 0);
        c.M.shifts = {1, trial == 0 ? 1u : 0u, 0};
        // Coupled order/tie witnesses and a strict 1<2 singleton witness.
        if (trial < 2)
          add(4, -1, 0, 0);
        else
          add(0, 0, 2, 1);
      }
      auto greedy = [&](bool source) {
        vector<int> order{0, 1, 2};
        std::stable_sort(order.begin(), order.end(),
                         [&](int a, int b) { return c.rs_gain[a] > c.rs_gain[b]; });
        vector<Uint64> j(3, 0);
        for (int s : order) {
          if (!c.rs_gain[s]) continue;
          for (;;) {
            ++j[s];
            bool feasible = true;
            for (const auto& k : constraints) {
              Uint64 dot = 0, v = k.v;
              for (int i = 0; i < 3; ++i) {
                dot += k.a[i] * j[i];
                if (source) v += k.a[i] * c.M.shifts[i];
              }
              feasible &= dot <= v;
            }
            if (!feasible) {
              --j[s];
              break;
            }
            if (j[s] > 128) throw std::runtime_error("reference enumeration bound");
          }
        }
        return j;
      };
      bool expected = greedy(false) == vector<Uint64>(3, 0) && greedy(true) == c.M.shifts;
      const auto before = c.rs_minv;
      bool accepted = true;
      try {
        rescaling_check(c, {});
      } catch (const Reject&) {
        accepted = false;
      }
      require(accepted == expected, "rescaling independent enumerated reference");
      require(c.rs_minv == before, "rescaling verification preserves stored valuations");
    }
  }
}

void text_container_identity(size_t& valid, size_t& rejects) {
  for (unsigned d : {4u, 8u}) {
    auto m = small(d, true);
    auto b = write(m);
    vector<string> desc;
    for (const auto& e : m.elements) desc.push_back(object(e.fields));
    m.meta["elements"] = array(desc, false);
    vector<string> lines{
        "{\"content_hash\":\"sha256\",\"required_features\":[],\"schema\":\"sicx-text-json/"
        "1.0\",\"sicx_version\":[\"1\",\"0\"]}",
        "{\"chunk\":\"EXMT\",\"flags\":\"1\",\"meta\":" + object(m.meta) + "}"};
    for (size_t i = 0; i < m.elements.size(); ++i) {
      const auto& e = m.elements[i];
      string type = e.fields.at("kind") == "\"rep\"" ? "BODY" : "TOWR";
      string runs = e.value ? "[[\"pairs\",[[" + quote(std::to_string(e.value)) + ",\"0\"]]]]"
                            : "[[\"zeros\",\"1\"]]";
      lines.push_back("{\"chunk\":" + quote(type) +
                      ",\"codec\":\"0\",\"flags\":\"1\",\"leaves\":[{\"count\":\"1\",\"element\":" +
                      quote(std::to_string(i)) + ",\"first\":\"0\",\"runs\":" + runs + "}]}");
    }
    lines.push_back("{\"chunk\":\"RAYS\",\"flags\":\"1\",\"json\":" +
                    string(m.extras[0].payload.begin(), m.extras[0].payload.end()) + "}");
    string ball = "{\"chunk\":\"BALL\",\"flags\":\"1\",\"label\":\"c0\",\"ray\":\"0\",\"values\":[";
    for (unsigned i = 0; i < d; ++i) {
      if (i) ball += ",";
      ball += i ? "[[\"0\",\"0\",\"0\",\"0\"],[\"0\",\"0\",\"0\",\"0\"]]"
                : "[[\"1\",\"0\",\"1\",\"-200\"],[\"0\",\"0\",\"0\",\"0\"]]";
    }
    lines.push_back(ball + "]}");
    string text;
    for (const auto& line : lines) text += line + "\n";
    string final = "{\"content_sha256\":" + quote(hex(digest(bytes(text)).data(), 32)) +
                   ",\"exact_root\":" + quote(hex(b.exact.data(), 32)) +
                   ",\"lines\":" + quote(std::to_string(lines.size())) +
                   ",\"merkle_root\":" + quote(hex(b.root.data(), 32)) +
                   ",\"sicx_bytes\":" + quote(std::to_string(b.file.size())) +
                   ",\"sicx_sha256\":" + quote(hex(digest(b.file).data(), 32)) + "}\n";
    string path = fixtures_dir + "/d" + std::to_string(d) + "-valid.jsonl";
    {
      std::ofstream f(path);
      f << text << final;
    }
    Opts opt;
    opt.text_path = path;
    require(run(b, "d" + std::to_string(d) + "-text", opt) == 0, "text rendering identity");
    ++valid;
    {
      string changed = text;
      changed.replace(changed.find("sicx-text-json/1.0"), 18,
                      "sicx-text-json/1.1");  // identity-negative
      string changed_final = final;
      auto old_hash = hex(digest(bytes(text)).data(), 32),
           new_hash = hex(digest(bytes(changed)).data(), 32);
      changed_final.replace(changed_final.find(old_hash), old_hash.size(), new_hash);
      {
        std::ofstream f(path);
        f << changed << changed_final;
      }
      require(run(b, "d" + std::to_string(d) + "-text-wrong-header-rehashed", opt) == 2,
              "text lines bind binary meaning even with recomputed content hash");
      ++rejects;
      string wrong = final;
      wrong.replace(wrong.find(hex(b.exact.data(), 32)), 64, string(64, '0'));
      {
        std::ofstream f(path);
        f << text << wrong;
      }
      require(run(b, "d" + std::to_string(d) + "-text-wrong-final-identity", opt) == 2,
              "text final line binds roots and identities");
      ++rejects;
    }
    {
      std::ofstream f(path);
      f << text << final.substr(0, final.size() - 1);
    }
    require(run(b, "d" + std::to_string(d) + "-text-missing-LF", opt) == 2, "text final LF");
    ++rejects;
    {
      std::ofstream f(path);
      f << text << final << "{}\n";
    }
    require(run(b, "d" + std::to_string(d) + "-text-extra-line", opt) == 2, "text appended data");
    ++rejects;
    {
      std::ofstream f(path);
      f << text << final;
    }
  }
}

void ball_exact_consistency(size_t& valid, size_t& rejects) {
  // Mandatory decode rejection takes precedence over a separate rounding refusal.
  for (unsigned d : {4u, 8u}) {
    auto m = small(d, true);
    auto& e = m.elements.back();
    e.fields["scale"] = "\"2\"";
    e.fields["max_bits"] = "\"2\"";
    e.value = 1;
    e.leaves[0] = leaf(m.elements.size() - 1, 0, 1, 1);
    string rays(m.extras[0].payload.begin(), m.extras[0].payload.end());
    rays.replace(rays.find("199"), 3, "3");
    m.extras[0].payload = bytes(rays);
    auto& ball = m.extras[1].payload;
    set64(ball, 8, Uint64(-3));
    set64(ball, 24, Uint64(-4));
    set64(ball, 32, 7);
    size_t k = (d / 2) * 80;
    set64(ball, k + 8, Uint64(-1));
    set64(ball, k + 16, 1);
    set64(ball, k + 24, Uint64(-4));
    set64(ball, k + 32, 1);
    Opts opt;
    opt.decode = "off";
    require(run(write(m), "d" + std::to_string(d) + "-BALL-inconsistent-container", opt) == 0,
            "BALL-only checks do not prove exact consistency");
    ++valid;
    opt.decode = "full";
    require(run(write(m), "d" + std::to_string(d) + "-BALL-inconsistent-full", opt, true) == 2,
            "full decode must reject proved inconsistency");
    ++rejects;
    opt.decode = "ratio";
    require(run(write(m), "d" + std::to_string(d) + "-BALL-inconsistent-ratio", opt, true) == 2,
            "optional ratio must reject proved inconsistency");
    ++rejects;
  }
}
}  // namespace sic_check::verifier::test
