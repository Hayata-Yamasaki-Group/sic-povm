// SPDX-License-Identifier: AGPL-3.0-or-later
#include <flint/nmod_mat.h>
#include <unistd.h>

#include "assertions.hpp"
#include "exact/analytic/table.hpp"
#include "exact/certification/reconstruction/ghost.hpp"
#include "exact/group/group.hpp"
#include "support/resources.hpp"

namespace {
ulong rational_residue(arf_srcptr value, nmod_t mod) {
  sic::exact::Rational exact;
  arf_get_fmpq(exact.v, value);
  return nmod_mul(fmpz_fdiv_ui(fmpq_numref(exact.v), mod.n),
                  nmod_inv(fmpz_fdiv_ui(fmpq_denref(exact.v), mod.n), mod), mod);
}

void reductions() {
  for (ulong prime : {UWORD(5), UWORD(13), UWORD(2013265921), UWORD(1811939329)}) {
    nmod_t mod;
    nmod_init(&mod, prime);
    sic::exact::ModularDyadicReduction reduce(prime);
    arf_t value;
    arf_init(value);
    for (long numerator : {-1234567L, -1L, 0L, 1L, 9876543L})
      for (long exponent : {-400L, -17L, 0L, 17L, 400L}) {
        arf_set_si(value, numerator);
        arf_mul_2exp_si(value, value, exponent);
        SIC_CHECK(reduce(value) == rational_residue(value, mod));
      }
    fmpz_t huge;
    fmpz_init(huge);
    fmpz_one(huge);
    fmpz_mul_2exp(huge, huge, 1000);
    ulong expected = 2;
    for (int q = 0; q < 1000; ++q) expected = nmod_mul(expected, expected, mod);
    arf_one(value);
    arf_mul_2exp_fmpz(value, value, huge);
    SIC_CHECK(reduce(value) == expected);
    fmpz_neg(huge, huge);
    arf_one(value);
    arf_mul_2exp_fmpz(value, value, huge);
    SIC_CHECK(reduce(value) == nmod_inv(expected, mod));
    arf_pos_inf(value);
    bool refused = false;
    try {
      reduce(value);
    } catch (const std::invalid_argument&) {
      refused = true;
    }
    SIC_CHECK(refused);
    fmpz_clear(huge);
    arf_clear(value);
  }
}

void matrix_case(int kind, bool opposite) {
  constexpr long kRows = 24, columns = 8;
  const ulong prime = UWORD(2013265921);
  sic::support::flint::AcbMat input(kRows, columns);
  for (long r = 0; r < kRows; ++r)
    for (long c = 0; c < columns; ++c) {
      auto value = acb_mat_entry(input, r, c);
      if (kind == 0) {
        if (r < columns) continue;  // Force original-row permutations.
        acb_one(value);
        if ((r - columns) % columns == c) {
          arf_t tiny;
          arf_init(tiny);
          arf_one(tiny);
          arf_mul_2exp_si(tiny, tiny, -200);
          arb_add_arf(acb_realref(value), acb_realref(value), tiny, 512);
          arf_clear(tiny);
        }
      } else if (kind == 1 || kind == 2) {
        acb_set_si(value, (r * 17 + c * 19) % 23 - 11);
        arb_set_si(acb_imagref(value), (r * 7 + c) % 13 - 6);
        acb_mul_2exp_si(value, value, 3 * (r % 5) - 2 * (c % 7));
        if (kind == 2 && c == 1) acb_zero(value);  // A skipped column.
      }
    }
  std::vector<long> labels(kRows);
  std::iota(labels.begin(), labels.end(), 100);
  const auto serial = sic::exact::select_modular_rows(input, labels, 1, 512, prime, opposite);
  const auto parallel = sic::exact::select_modular_rows(input, labels, 4, 512, prime, opposite);
  SIC_CHECK(serial.rank == parallel.rank);
  SIC_CHECK(serial.rows == parallel.rows);
  SIC_CHECK(serial.finite && parallel.finite);
  nmod_t mod;
  nmod_init(&mod, prime);
  nmod_mat_t original, minor;
  nmod_mat_init(original, kRows, columns, prime);
  nmod_mat_init(minor, columns, columns, prime);
  for (long r = 0; r < kRows; ++r)
    for (long c = 0; c < columns; ++c) {
      const auto value = acb_mat_entry(input, r, c);
      nmod_mat_entry(original, r, c) =
          nmod_add(rational_residue(arb_midref(acb_realref(value)), mod),
                   nmod_mul(parallel.imaginary_unit,
                            rational_residue(arb_midref(acb_imagref(value)), mod), mod),
                   mod);
    }
  SIC_CHECK(parallel.rank == nmod_mat_rank(original));
  if (parallel.rank == columns) {
    SIC_CHECK(parallel.rows.size() == columns);
    for (long r = 0; r < columns; ++r)
      for (long c = 0; c < columns; ++c)
        nmod_mat_entry(minor, r, c) = nmod_mat_entry(original, parallel.rows[r] - 100, c);
    SIC_CHECK(nmod_mat_rank(minor) == columns);
  } else
    SIC_CHECK(parallel.rows.empty());
  if (kind == 0) {
    SIC_CHECK(parallel.rank == columns);
    sic::exact::GhostRefiner native(8, 1, 1);
    SIC_CHECK(!native.select_rows<sic::exact::SelectionWide>(input, kRows, columns, labels));
  }
  nmod_mat_clear(original);
  nmod_mat_clear(minor);
}

void cancellation_and_guards() {
  sic::support::flint::AcbMat input(1, 1);
  const ulong prime = 13, imaginary = n_sqrtmod(prime - 1, prime);
  acb_set_si(acb_mat_entry(input, 0, 0), -(long)imaginary);
  arb_one(acb_imagref(acb_mat_entry(input, 0, 0)));
  SIC_CHECK(sic::exact::select_modular_rows(input, {7}, 1, 64, prime, false).rank == 0);
  SIC_CHECK(sic::exact::select_modular_rows(input, {7}, 1, 64, prime, true).rank == 1);
  bool refused = false;
  try {
    sic::exact::select_modular_rows(input, {7}, 1, 64, prime, false, 1);
  } catch (const sic::support::resources::AdmissionError&) {
    refused = true;
  }
  SIC_CHECK(refused);
  acb_one(acb_mat_entry(input, 0, 0));
  arb_const_pi(acb_realref(acb_mat_entry(input, 0, 0)), 1024);
  refused = false;
  try {
    sic::exact::select_modular_rows(input, {7}, 1, 64);
  } catch (const sic::support::resources::AdmissionError&) {
    refused = true;
  }
  SIC_CHECK(refused);
  arf_one(arb_midref(acb_realref(acb_mat_entry(input, 0, 0))));
  mag_zero(arb_radref(acb_realref(acb_mat_entry(input, 0, 0))));
  fmpz_t exponent;
  fmpz_init(exponent);
  fmpz_one(exponent);
  fmpz_mul_2exp(exponent, exponent, 1000);
  arf_mul_2exp_fmpz(arb_midref(acb_realref(acb_mat_entry(input, 0, 0))),
                    arb_midref(acb_realref(acb_mat_entry(input, 0, 0))), exponent);
  refused = false;
  try {
    sic::exact::select_modular_rows(input, {7}, 1, 64);
  } catch (const sic::support::resources::AdmissionError&) {
    refused = true;
  }
  SIC_CHECK(refused);
  SIC_CHECK(sic::exact::select_modular_rows(input, {7}, 1, 2048).rank == 1);
  fmpz_clear(exponent);
  for (ulong bad : {UWORD(0), UWORD(1), UWORD(2), UWORD(7), UWORD(9)}) {
    refused = false;
    try {
      sic::exact::select_modular_rows(input, {7}, 1, 64, bad);
    } catch (const std::invalid_argument&) {
      refused = true;
    }
    SIC_CHECK(refused);
  }
  acb_indeterminate(acb_mat_entry(input, 0, 0));
  const auto invalid = sic::exact::select_modular_rows(input, {7}, 1, 64);
  SIC_CHECK(!invalid.finite);
  SIC_CHECK(invalid.rows.empty());
  char stop_path[] = "/tmp/sic-modular-stop-XXXXXX";
  const int descriptor = mkstemp(stop_path);
  SIC_CHECK(descriptor >= 0);
  if (descriptor >= 0) {
    close(descriptor);
    sic::support::ScopedStopFile stop_scope(stop_path);
    refused = false;
    try {
      sic::exact::select_modular_rows(input, {7}, 4, 64);
    } catch (const std::runtime_error& error) {
      refused = std::string(error.what()).find("requested stop") != std::string::npos;
    }
    SIC_CHECK(refused);
    unlink(stop_path);
  }
  refused = false;
  try {
    sic::exact::modular_row_workspace(UINT64_MAX, 2, 1, 128);
  } catch (const std::overflow_error&) {
    refused = true;
  }
  SIC_CHECK(refused);
}

void refinement() {
  sic::exact::Arithmetic arithmetic(3);
  sic::exact::Group group(arithmetic);
  group.generate_standard();
  group.close(
      sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")), 64);
  sic::exact::Table table(arithmetic, 256);
  table.build_class(0);
  sic::support::ComplexBallVector seed(64), output;
  for (long y = 1; y < 64; ++y) table.read(seed[y], group.ghost_point(y / 8, y % 8));
  sic::exact::GhostRefiner modular(8, 1, 1);
  modular.kernel_threads = 4;
  modular.retain_certified_root = true;
  std::string why;
  SIC_CHECK(modular.refine(seed.v, 400, output.v, why));
  SIC_CHECK(modular.has_complete_selection());
  SIC_CHECK(bool(modular.certified_root));
  SIC_CHECK(modular.selection_kind == sic::exact::GhostRefiner::SelectionKind::modular_midpoint);
  SIC_CHECK(std::isnan(modular.cond_est));
  sic::exact::GhostRefiner incomplete(8, 1, 1);
  incomplete.S_hint.assign(3, 1);
  incomplete.hint_kind = modular.selection_kind;
  incomplete.inverse_precision_hint = WORD_MAX;
  SIC_CHECK(incomplete.refine(seed.v, 400, output.v, why));
  SIC_CHECK(incomplete.has_complete_selection());
  sic::exact::GhostRefiner hinted(8, 1, 1);
  hinted.S_hint = modular.S;
  hinted.hint_kind = modular.selection_kind;
  hinted.inverse_precision_hint = modular.inverse_precision_proposal;
  hinted.cond_hint = std::numeric_limits<double>::quiet_NaN();
  SIC_CHECK(hinted.refine(seed.v, 400, output.v, why));
  SIC_CHECK(hinted.has_complete_selection());
  SIC_CHECK(std::isnan(hinted.cond_est));
  hinted.workspace_limit = 1;
  SIC_CHECK(!hinted.refine(seed.v, 400, output.v, why));
  SIC_CHECK(!hinted.has_complete_selection());
  SIC_CHECK(hinted.inverse_precision_proposal == 0);
}
}  // namespace

int main() {
  reductions();
  for (int kind = 0; kind < 4; ++kind)
    for (bool opposite : {false, true}) matrix_case(kind, opposite);
  cancellation_and_guards();
  refinement();
  return sic_test::report();
}
