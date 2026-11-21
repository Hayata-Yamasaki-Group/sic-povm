// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fixtures/fixture.hpp"
namespace sicx_tests {
void finalization_cases(Fixture& fixture) {
  auto& source = fixture.source;
  auto& good = fixture.good;
  auto& tests = fixture.tests;
  if (source.finalized()) {
    const auto ri = *source.rays_chunk();
    auto bad_rays = [&](const std::string& name, auto mutate) {
      fixture.rejected(name, [&](auto& rows) {
        auto j = json(rows[ri]);
        mutate(j);
        set_json(rows[ri], j);
      });
    };
    bad_rays("unknown RAYS key",
             [](auto& j) { j["extra"] = sic::support::Json::string_value("0"); });
    bad_rays("empty RAYS", [](auto& j) { j["rays"].array.clear(); });
    bad_rays("ray spelling", [](auto& j) {
      j["rays"].array[0]["label"] = sic::support::Json::string_value("class0");
    });
    bad_rays("ray leading zero", [](auto& j) {
      j["rays"].array[0]["label"] = sic::support::Json::string_value("c00");
    });
    bad_rays("duplicate ray", [](auto& j) { j["rays"].array.push_back(j["rays"].array[0]); });
    bad_rays("ray suffix too long", [](auto& j) {
      j["rays"].array[0]["label"] = sic::support::Json::string_value("c1000000000000000000");
    });
    bad_rays("anchor range", [&](auto& j) {
      j["rays"].array[0]["anchor"] = sic::support::Json::integer_value(source.meta().d);
    });
    bad_rays("ball limbs zero",
             [](auto& j) { j["balls"]["limbs"] = sic::support::Json::string_value("0"); });
    bad_rays("ball limbs too large",
             [](auto& j) { j["balls"]["limbs"] = sic::support::Json::string_value("1025"); });
    bad_rays("working bits zero",
             [](auto& j) { j["balls"]["working_bits"] = sic::support::Json::string_value("0"); });
    bad_rays("accuracy tight", [](auto& j) {
      j["rays"].array[0]["accuracy_bits"] = sic::support::Json::string_value("1");
    });
    for (const char* name :
         {"sic-decode/2 (contraction: acb_dot per class value)",       // identity-negative
          "sic_decode_sicx/2 (contraction: acb_dot per class value)",  // identity-negative
          "sic-decode/1", "sic-decode/1.0.0", "sic-decode/1.0.1",      // identity-negative
          "other/1.0.0"})                                              // identity-negative
      bad_rays(std::string("decoder identifier ") + name,
               [=](auto& j) { j["decoder"]["name"] = sic::support::Json::string_value(name); });
    bad_rays("decoder binary identity", [](auto& j) {
      j["decoder"]["sha256"] = sic::support::Json::string_value(std::string(64, '0'));
    });
    bad_rays("decoder source commit", [](auto& j) {
      j["decoder"]["source_commit"] = sic::support::Json::string_value("invalid");
    });
    bad_rays("ray word width", [](auto& j) { j["rays"].array[0]["word"].array.clear(); });
    bad_rays("ray word residue", [&](auto& j) {
      j["rays"].array[0]["label"] = sic::support::Json::string_value("c1");
      j["rays"].array[0]["word"].array[0] =
          sic::support::Json::integer_value(source.meta().smith[0]);
    });
    {
      auto j = json(good[ri]);
      j["rays"].array[0]["word"].array[0] = sic::support::Json::string_value("1");
      sic::sicx::Meta accepted = source.meta();
      accepted.rays.clear();
      sic::sicx::rays_from_json(j, accepted);
      if (accepted.rays[0].label != "c0" || accepted.rays[0].word[0] != 1)
        throw std::runtime_error("nonzero canonical word refused for c0");
      ++tests;
      std::cout << "PASS accept nonzero canonical word for c0\n";
    }
    bad_rays("nonconsecutive labels",
             [](auto& j) { j["rays"].array[0]["label"] = sic::support::Json::string_value("c1"); });
    bad_rays("duplicate canonical words", [](auto& j) {
      auto second = j["rays"].array[0];
      second["label"] = sic::support::Json::string_value("c1");
      j["rays"].array.push_back(second);
    });
    bad_rays("reversed canonical words", [](auto& j) {
      auto second = j["rays"].array[0];
      second["label"] = sic::support::Json::string_value("c1");
      j["rays"].array[0]["word"].array[0] = sic::support::Json::string_value("1");
      j["rays"].array.push_back(second);
    });
    const size_t ball = source.ball_chunk(0);
    fixture.rejected("missing BALL", [&](auto& rows) { rows.erase(rows.begin() + ball); });
    fixture.rejected("extra BALL",
                     [&](auto& rows) { rows.insert(rows.begin() + ball, rows[ball]); });
    fixture.rejected("BALL without RAYS", [&](auto& rows) { rows.erase(rows.begin() + ri); });
    fixture.rejected("BALL flags", [&](auto& rows) { rows[ball].bytes[0] = 2; });
    fixture.rejected("BALL even midpoint", [&](auto& rows) { rows[ball].bytes[32] &= 0xfe; });
    fixture.rejected("BALL even radius", [&](auto& rows) {
      rows[ball].bytes[16] = 2;
      for (unsigned k = 17; k < 24; ++k) rows[ball].bytes[k] = 0;
    });
    fixture.rejected("BALL large radius", [&](auto& rows) { rows[ball].bytes[19] = 0x40; });
    fixture.rejected("BALL exponent bound", [&](auto& rows) { rows[ball].bytes[13] = 2; });
    fixture.rejected("BALL length", [&](auto& rows) { rows[ball].bytes.pop_back(); });
    fixture.rejected("BALL zero midpoint sign", [&](auto& rows) {
      rows[ball].bytes[0] = 1;
      for (size_t k = 32; k < 32 + 8 * source.meta().ball_limbs; ++k) rows[ball].bytes[k] = 0;
    });
    fixture.rejected("BALL zero radius exponent", [&](auto& rows) {
      for (unsigned k = 16; k < 24; ++k) rows[ball].bytes[k] = 0;
      rows[ball].bytes[24] = 1;
    });
    if (source.prec()) {
      const auto pi = *source.prec();
      fixture.rejected("PREC unknown key", [&](auto& rows) {
        auto j = json(rows[pi]);
        j["unknown"] = sic::support::Json::string_value("0");
        set_json(rows[pi], j);
      });
      fixture.rejected("PREC negative", [&](auto& rows) {
        auto j = json(rows[pi]);
        j["delta_bits"] = sic::support::Json::string_value("-1");
        set_json(rows[pi], j);
      });
    }
  }
}
}  // namespace sicx_tests
int main(int argc, char** argv) {
  return sicx_tests::run_fixture_case(argc, argv, &sicx_tests::finalization_cases);
}
