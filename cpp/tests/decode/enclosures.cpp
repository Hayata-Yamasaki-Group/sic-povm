// SPDX-License-Identifier: AGPL-3.0-or-later
// Validation harness only: compare authenticated reference and current text enclosures.
#include <flint/acb.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Ball {
  arb_t x;
  Ball() { arb_init(x); }
  ~Ball() { arb_clear(x); }
  Ball(const Ball&) = delete;
};
std::vector<std::string> read(const std::string& file, uint64_t d) {
  std::ifstream in(file);
  std::string line;
  auto require = [&](const std::string& expected) {
    if (!std::getline(in, line) || line != expected)
      throw std::runtime_error(file + ": expected " + expected);
  };
  require("sic-vector-balls/1.0");
  require("d " + std::to_string(d));
  std::vector<std::string> parts;
  for (uint64_t i = 0; i < d; ++i) {
    if (!std::getline(in, line)) throw std::runtime_error(file + ": truncated coordinates");
    const auto bar = line.find('|');
    if (bar == std::string::npos || line.find('|', bar + 1) != std::string::npos)
      throw std::runtime_error(file + ": invalid complex coordinate");
    for (auto text : {line.substr(0, bar), line.substr(bar + 1)}) {
      const auto first = text.find_first_not_of(" \t\r");
      if (first == std::string::npos) throw std::runtime_error(file + ": empty component");
      parts.push_back(text.substr(first, text.find_last_not_of(" \t\r") - first + 1));
    }
  }
  require("end");
  if (std::getline(in, line)) throw std::runtime_error(file + ": trailing data");
  return parts;
}
uint64_t certified(arb_srcptr x) {
  arf_t lo, hi;
  arf_init(lo);
  arf_init(hi);
  arb_get_lbound_arf(lo, x, ARF_PREC_EXACT);
  arb_get_ubound_arf(hi, x, ARF_PREC_EXACT);
  const double a = arf_get_d(lo, ARF_RND_NEAR), b = arf_get_d(hi, ARF_RND_NEAR);
  arf_clear(lo);
  arf_clear(hi);
  if (!std::isfinite(a) || a != b) throw std::runtime_error("uncertified FP64 rounding");
  return std::bit_cast<uint64_t>(a == 0 ? 0.0 : a);
}
void check(arb_srcptr old, arb_srcptr now, bool normalized, slong accuracy) {
  if (!arb_is_finite(old) || !arb_is_finite(now)) throw std::runtime_error("nonfinite ball");
  if (!arb_overlaps(old, now))
    throw std::runtime_error("reference and current enclosures are disjoint");
  if (normalized) {
    // Raw coordinates can enclose zero without a unique binary64 rounding.
    // Only the normalized output promises certified FP64 conversion.
    if (certified(old) != certified(now)) throw std::runtime_error("certified FP64 differs");
    // SICX 5.2: every nonzero radius is strictly below 2^-accuracy_bits.
    if (!mag_is_zero(arb_radref(now)) && mag_cmp_2exp_si(arb_radref(now), -accuracy) >= 0)
      throw std::runtime_error("normalized radius violates stored accuracy_bits");
  }
}
void check_raw(arb_srcptr real, arb_srcptr imag, slong accuracy) {
  acb_t value;
  acb_init(value);
  arb_set(acb_realref(value), real);
  arb_set(acb_imagref(value), imag);
  const bool pass = acb_is_finite(value) && (acb_rel_accuracy_bits(value) >= accuracy ||
                                             (mag_cmp_2exp_si(arb_radref(real), -accuracy) <= 0 &&
                                              mag_cmp_2exp_si(arb_radref(imag), -accuracy) <= 0));
  acb_clear(value);
  if (!pass) throw std::runtime_error("raw radius violates decoder absolute-or-relative target");
}
uint64_t integer(const std::string& text) {
  if (text.empty() ||
      !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
    throw std::runtime_error("invalid unsigned decimal policy");
  return std::stoull(text);
}
void self_test() {
  Ball a, b, zero;
  unsigned controls = 0;
  auto rejected = [&](auto operation) {
    bool failed = false;
    try {
      operation();
    } catch (const std::runtime_error&) {
      failed = true;
    }
    if (!failed) throw std::logic_error("negative enclosure control was accepted");
    ++controls;
  };
  arb_one(a.x);
  arb_add_error_2exp_si(a.x, -180);
  arb_one(b.x);
  arb_add_error_2exp_si(b.x, -170);
  check(a.x, b.x, true, 169);  // Different radii, same FP64 and overlap.
  ++controls;
  rejected([&] { check(a.x, b.x, true, 170); });  // Strict radius boundary.
  arb_one(b.x);
  arb_add_error_2exp_si(b.x, -40);
  rejected([&] { check(a.x, b.x, true, 39); });  // Overlap alone is insufficient.
  arb_set_ui(b.x, 2);
  rejected([&] { check(a.x, b.x, true, 128); });
  arb_one(b.x);
  arb_add_error_2exp_si(b.x, -100);
  rejected([&] { check_raw(b.x, zero.x, 128); });
  arb_one(b.x);
  arb_add_error_2exp_si(b.x, -180);
  check(a.x, b.x, false, 128);
  check_raw(b.x, zero.x, 128);
  ++controls;
  arb_one(b.x);
  arb_add_error_2exp_si(b.x, -128);
  check(a.x, b.x, false, 128);  // Inclusive absolute target boundary.
  check_raw(b.x, zero.x, 128);
  ++controls;
  arb_one(a.x);
  arb_mul_2exp_si(a.x, a.x, 256);
  arb_set(b.x, a.x);
  arb_add_error_2exp_si(a.x, 120);
  arb_add_error_2exp_si(b.x, 120);
  check(a.x, b.x, false, 128);  // Relative target with a large absolute radius.
  check_raw(b.x, zero.x, 128);
  ++controls;
  arb_one(a.x);
  arb_add_error_2exp_si(a.x, -100);
  check_raw(a.x, b.x, 128);  // Complex relative accuracy, despite tiny real component.
  ++controls;
  std::cout << "PASS " << controls << " enclosure gate controls\n";
}
}  // namespace
int main(int argc, char** argv) {
  int status = 0;
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test")
      self_test();
    else {
      if (argc != 6) throw std::runtime_error("usage: OLD NEW D normalized|raw|overlap ACCURACY");
      const auto d = integer(argv[3]);
      const std::string mode = argv[4];
      const auto accuracy = integer(argv[5]);
      if (!d || d > 2048 || (mode != "raw" && mode != "normalized" && mode != "overlap") ||
          accuracy < 64 || accuracy > 1048576)
        throw std::runtime_error("invalid comparison policy");
      const auto old = read(argv[1], d), now = read(argv[2], d);
      Ball a, b, real;
      for (size_t i = 0; i < old.size(); ++i) {
        if (arb_load_str(a.x, old[i].c_str()) || arb_load_str(b.x, now[i].c_str()))
          throw std::runtime_error("invalid Arb dump at component " + std::to_string(i));
        try {
          check(a.x, b.x, mode == "normalized", accuracy);
          if (mode == "raw") {
            if (i % 2 == 0)
              arb_set(real.x, b.x);
            else
              check_raw(real.x, b.x, accuracy);
          }
        } catch (const std::exception& e) {
          throw std::runtime_error("component " + std::to_string(i) + ": " + e.what());
        }
      }
      std::cout << "PASS " << d << " " << mode << " coordinates: finite enclosure overlap"
                << (mode == "normalized" ? ", identical certified FP64, normalized radius rule"
                    : mode == "raw"      ? ", raw absolute-or-relative radius rule"
                                         : "")
                << '\n';
    }
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    status = 1;
  }
  flint_cleanup_master();
  return status;
}
