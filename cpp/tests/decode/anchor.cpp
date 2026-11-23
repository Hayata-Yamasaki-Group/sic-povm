// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/kernels/anchor.hpp"

#include <algorithm>

#include "assertions.hpp"
#include "decode/kernels/kernels.hpp"
#include "sic/decode/decoder.hpp"
#include "sicx/model/metadata.hpp"

namespace {
void compare_fixture(const char* path, const char* archive) {
  sic::sicx::Reader reader(path);
  const sic::sicx::Reader oracle(archive);
  const auto& meta = reader.meta();
  SIC_CHECK(oracle.finalized() && oracle.meta().d == meta.d);
  const auto archived_root = sic::sicx::hex(oracle.exact_root());
  const auto native_root = sic::sicx::hex(reader.exact_root());
  const auto& archived_rays = oracle.meta().rays;
  // The pinned translation (1,0,0,2) maps the archived c0 word into this native presentation.
  const bool pinned_archive =
      archived_root == "6eccc9014e805eed5c38d792d015a0059b48034913611a7b1ffaf40d792f427f";
  const bool pinned_native =
      native_root == "71e665d53173ce0a692dd13d5dc274a39741c7360895cd23dd28aad8f848674a";
  const bool d8_standard = pinned_archive || pinned_native;
  std::vector<uint64_t> translated_word;
  if (d8_standard) {
    const bool same_smith = meta.smith == oracle.meta().smith;
    const std::vector<uint64_t> zero_word(4);
    const bool archived_zero = archived_rays.size() == 1 && archived_rays.front().word == zero_word;
    const bool native_zero = meta.rays.size() == 1 && meta.rays.front().word == zero_word;
    SIC_CHECK(pinned_archive);
    SIC_CHECK(pinned_native);
    SIC_CHECK(same_smith);
    SIC_CHECK(archived_zero);
    SIC_CHECK(native_zero);
    if (!pinned_archive || !pinned_native || !same_smith || !archived_zero || !native_zero) return;
    translated_word = sic::sicx::words_add(archived_rays.front().word, {1, 0, 0, 2}, meta.smith);
  }
  sic::decode::Decoder::Options options;
  options.threads = 1;
  options.pin = false;
  std::vector<sic::decode::Decoder::Request> requests;
  const auto append_ray = [&](const std::vector<uint64_t>& word) {
    for (uint64_t j = 0; j < meta.d; ++j) {
      const auto entry = meta.entry(j);
      requests.push_back(
          {entry.representative, sic::sicx::words_add(word, entry.word, meta.smith)});
    }
  };
  for (const auto& ray : meta.rays) append_ray(ray.word);
  if (d8_standard) append_ray(translated_word);
  reader.plan_chunks(sic::decode::Decoder::input_chunks(reader, requests));
  sic::decode::Decoder decoder(reader, options, requests);
  const auto values = decoder.decode(requests);
  size_t offset = 0;
  const auto bits = options.target + 64;
  for (const auto& ray : meta.rays) {
    sic::support::ComplexBallVector raw(meta.d);
    for (size_t j = 0; j < meta.d; ++j) acb_set(raw[j], values[offset++]);
    const auto chosen = sic::decode::select_anchor(raw, bits);
    SIC_CHECK(chosen == ray.anchor);
    const auto match = std::ranges::find_if(archived_rays, [&](const auto& entry) {
      return native_root == archived_root && entry.word == ray.word;
    });
    std::cout << "ANCHOR " << path << ' ' << ray.label << " auto=" << chosen;
    if (d8_standard) {
      std::cout << " (same class, different representative)";
    } else if (match == archived_rays.end()) {
      std::cout << " (no comparable archived native word)";
    } else {
      const auto pinned = match->anchor;
      std::cout << " archived=" << match->label << ':' << pinned
                << (chosen == pinned ? " match" : " differs");
      sic::support::flint::Arb actual_modulus, archived_modulus;
      acb_abs(actual_modulus, raw[chosen], bits);
      acb_abs(archived_modulus, raw[pinned], bits);
      if (chosen != pinned && arb_gt(actual_modulus, archived_modulus))
        std::cout << " (the chosen coordinate is certified larger)";
      else if (chosen != pinned && arb_gt(archived_modulus, actual_modulus))
        std::cout << " (the scan earlier skipped an overlapping candidate)";
      else if (chosen != pinned)
        std::cout << " (the modulus enclosures do not establish a strict ordering)";
    }
    std::cout << '\n';
  }
  if (d8_standard) {
    const auto raw = values.span().subspan(offset, meta.d);
    const auto chosen = sic::decode::select_anchor(raw, bits);
    const auto& archived = archived_rays.front();
    SIC_CHECK(chosen == archived.anchor);
    const auto normalized = sic::decode::normalize(raw, chosen, bits);
    const auto parts = oracle.ball_parts(0);
    sic::support::flint::Acb archived_ball;
    size_t overlaps = 0;
    for (size_t j = 0; j < meta.d; ++j) {
      sic::sicx::ball_part_arb(acb_realref(archived_ball.get()), parts[2 * j]);
      sic::sicx::ball_part_arb(acb_imagref(archived_ball.get()), parts[2 * j + 1]);
      overlaps += acb_overlaps(normalized[j], archived_ball.get());
    }
    SIC_CHECK(overlaps == meta.d);
    std::cout << "ANCHOR " << path << " word=(1,0,0,2) auto=" << chosen
              << " archived=" << archived.label << ':' << archived.anchor
              << " normalized overlaps=" << overlaps << '/' << meta.d << '\n';
  }
}
}  // namespace

int main(int argc, char** argv) {
  flint_set_num_threads(1);
  if (argc > 1) {
    SIC_CHECK(argc % 2 == 1);
    for (int i = 1; i + 1 < argc; i += 2) compare_fixture(argv[i], argv[i + 1]);
    return sic_test::report();
  }
  sic::support::ComplexBallVector raw(5);
  constexpr slong bits = 192;
  // Coordinate 0 contains zero. Coordinate 1 is first certified nonzero.
  arb_add_error_2exp_si(acb_realref(raw[0]), 4);
  acb_set_si(raw[1], -3);
  arb_add_error_2exp_si(acb_realref(raw[1]), 0);
  acb_set_si(raw[2], 4);  // Equality with the incumbent upper bound keeps coordinate 1.
  acb_set_si(raw[3], 5);
  acb_set_si(raw[4], -5);
  SIC_CHECK(sic::decode::select_anchor(raw, bits) == 3);
  acb_zero(raw[3]);
  acb_zero(raw[4]);
  SIC_CHECK(sic::decode::select_anchor(raw, bits) == 1);
  arb_one(acb_imagref(raw[2]));
  SIC_CHECK(sic::decode::select_anchor(raw, bits) == 2);
  acb_zero(raw[1]);
  acb_zero(raw[2]);
  bool refused = false;
  try {
    (void)sic::decode::select_anchor(raw, bits);
  } catch (const sic::Error& error) {
    refused = error.kind() == sic::ErrorKind::precision &&
              std::string_view(error.what()).find("no certified nonzero") != std::string_view::npos;
  }
  SIC_CHECK(refused);
  return sic_test::report();
}
