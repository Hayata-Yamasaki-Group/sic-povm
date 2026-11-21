// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/reconstruction/ghost.hpp"

namespace sic::exact {
template <class Rt>
Rt GhostRefiner::to_real(const arf_t x) {
  return selection_from_arf<Rt>(x);
}

template <class Rt>
Rt GhostRefiner::rsqrt(Rt x) {  // sqrt for long double / __float128 without libquadmath
  return selection_sqrt(x);
}

template <class Rt>
bool GhostRefiner::select_rows(const acb_mat_t J, long R, long n2, const std::vector<long>& rowsy) {
  typedef ComplexBall<Rt> C;
  auto nrm2 = [](const C& z) { return z.re * z.re + z.im * z.im; };
  const int selection_workers = n2 >= 64 ? kernel_threads : 1;
  const bool selection_progress = sic::support::kernel_setting("SIC_PROGRESS") != nullptr;
  const auto selection_started = std::chrono::steady_clock::now();
  if (selection_progress)
    sic::support::progressf(
        "Newton selector start: rows=%ld columns=%ld native_bytes=%zu workers=%d\n", R, n2,
        sizeof(Rt), selection_workers);
  std::unique_ptr<RowWorkerTeam> team_owner;
  try {
#ifdef SIC_ENABLE_TEST_HOOKS
    if (before_operation) before_operation(failure_context, "native_selection");
#endif
    team_owner = std::make_unique<RowWorkerTeam>(selection_workers);
  } catch (const std::system_error& error) {
    throw sic::support::resources::AdmissionError(std::string("row worker creation failed: ") +
                                                  error.what());
  }
  auto& team = *team_owner;
  // Every component is assigned by conversion before any read. Avoid a
  // serial zero pass over the large native matrix, allowing worker rows
  // to first-touch their own pages on NUMA hosts. Scalar accumulators
  // below are explicitly value-initialized.
  std::unique_ptr<C[]> M(new C[R * n2]);
  struct Range {
    long nonfinite_ball = 0, nonfinite_native = 0, underflow = 0;
    slong minimum = WORD_MAX, maximum = std::numeric_limits<slong>::min();
  };
  std::vector<Range> ranges(R);
  team.for_each(R, [&](long r) {
    for (long c = 0; c < n2; ++c) {
      const auto entry = acb_mat_entry(J, r, c);
      if (!acb_is_finite(entry)) {
        ++ranges[r].nonfinite_ball;
        M[r * n2 + c] = C{};
        continue;
      }
      M[r * n2 + c].re = to_real<Rt>(arb_midref(acb_realref(entry)));
      M[r * n2 + c].im = to_real<Rt>(arb_midref(acb_imagref(entry)));
      for (arb_srcptr component : {acb_realref(entry), acb_imagref(entry)})
        if (!arf_is_zero(arb_midref(component))) {
          const auto exponent = arf_abs_bound_lt_2exp_si(arb_midref(component));
          ranges[r].minimum = std::min(ranges[r].minimum, exponent);
          ranges[r].maximum = std::max(ranges[r].maximum, exponent);
        }
      for (const Rt& value : {M[r * n2 + c].re, M[r * n2 + c].im})
        ranges[r].nonfinite_native += !selection_finite(value);
      ranges[r].underflow +=
          (selection_finite(M[r * n2 + c].re) && !(M[r * n2 + c].re < (Rt)0) &&
           !(M[r * n2 + c].re > (Rt)0) && !arf_is_zero(arb_midref(acb_realref(entry))));
      ranges[r].underflow +=
          (selection_finite(M[r * n2 + c].im) && !(M[r * n2 + c].im < (Rt)0) &&
           !(M[r * n2 + c].im > (Rt)0) && !arf_is_zero(arb_midref(acb_imagref(entry))));
    }
  });
  Range range;
  for (const auto& row : ranges) {
    range.nonfinite_ball += row.nonfinite_ball;
    range.nonfinite_native += row.nonfinite_native;
    range.underflow += row.underflow;
    range.minimum = std::min(range.minimum, row.minimum);
    range.maximum = std::max(range.maximum, row.maximum);
  }
  if (selection_progress)
    sic::support::progressf(
        "Newton selector range: nonfinite_ball=%ld nonfinite_native=%ld underflow=%ld "
        "min_exp=%ld max_exp=%ld\n",
        range.nonfinite_ball, range.nonfinite_native, range.underflow, (long)range.minimum,
        (long)range.maximum);
  if (range.nonfinite_ball || range.nonfinite_native) {
    S.clear();
    return false;
  }
  auto equilibrate = [&](long index, bool column) {
    const long count = column ? R : n2;
    auto at = [&](long item) -> C& { return column ? M[item * n2 + index] : M[index * n2 + item]; };
    Rt largest = 0;
    for (long item = 0; item < count; ++item) {
      const auto& value = at(item);
      largest = std::max(largest, value.re < (Rt)0 ? -value.re : value.re);
      largest = std::max(largest, value.im < (Rt)0 ? -value.im : value.im);
    }
    if (!(largest > (Rt)0)) return;
    Rt mx = 0;
    for (long item = 0; item < count; ++item) {
      auto& value = at(item);
      value.re = value.re / largest;
      value.im = value.im / largest;
      mx = std::max(mx, nrm2(value));
    }
    const Rt factor = (Rt)1 / rsqrt(mx);
    for (long item = 0; item < count; ++item) {
      auto& value = at(item);
      value.re *= factor;
      value.im *= factor;
    }
  };
  for (int sweep = 0; sweep < 2; ++sweep) {
    team.for_each(n2, [&](long c) { equilibrate(c, true); });
    team.for_each(R, [&](long r) { equilibrate(r, false); });
  }
  // squared relative pivot threshold (double: pivots down to 1e-10, far above its rounding
  // errors)
  const Rt eps2 = std::is_same<Rt, double>::value ? (Rt)1e-20
                  : std::is_same<Rt, long double>::value
                      ? (Rt)(LDBL_MANT_DIG <= DBL_MANT_DIG ? 1e-20 : 1e-34)
                      : (Rt)1e-60;
  std::vector<char> used(R, 0);
  std::vector<Rt> nrm(R, 0);
  team.for_each(R, [&](long r) {
    for (long c = 0; c < n2; ++c) nrm[r] += nrm2(M[r * n2 + c]);
  });
  S.clear();
  Rt smax = 0, smin = 0;
  std::vector<C> qv(n2);
  for (long step = 0; step < n2; ++step) {
    long br = -1;
    Rt bn = -1;
    for (long r = 0; r < R; ++r)
      if (!used[r] && nrm[r] > bn) {
        bn = nrm[r];
        br = r;
      }
    if (br < 0 || !(bn > eps2 * smax * smax)) {
      if (selection_progress)
        sic::support::progressf(
            "Newton selector rank refusal: pivot=%ld/%ld best_norm=%g maximum_pivot=%g\n", step, n2,
            (double)bn, (double)smax);
      return false;
    }
    used[br] = 1;
    S.push_back(rowsy[br]);
    Rt nb = rsqrt(bn);
    smax = std::max(smax, nb);
    smin = step == 0 ? nb : std::min(smin, nb);
    for (long c = 0; c < n2; ++c) {
      qv[c].re = M[br * n2 + c].re / nb;
      qv[c].im = M[br * n2 + c].im / nb;
    }
    team.for_each(R, [&](long r) {
      if (!used[r]) {
        C ip{};
        for (long c = 0; c < n2; ++c) {
          const C& q = qv[c];
          const C& x = M[r * n2 + c];
          ip.re += q.re * x.re + q.im * x.im;
          ip.im += q.re * x.im - q.im * x.re;
        }
        Rt nr = 0;
        for (long c = 0; c < n2; ++c) {
          C& x = M[r * n2 + c];
          const C& q = qv[c];
          x.re -= ip.re * q.re - ip.im * q.im;
          x.im -= ip.re * q.im + ip.im * q.re;
          nr += nrm2(x);
        }
        nrm[r] = nr;
      }
    });
    if (selection_progress && ((step + 1) % 256 == 0 || step + 1 == n2))
      sic::support::progressf(
          "Newton selector progress: pivots=%ld/%ld elapsed=%.2fs\n", step + 1, n2,
          std::chrono::duration<double>(std::chrono::steady_clock::now() - selection_started)
              .count());
  }
  cond_est = (double)(smax / smin);
  return (long)S.size() == n2;
}

bool GhostRefiner::has_complete_selection() const {
  if (S.size() != (size_t)(2 * d - 2)) return false;
  return selection_kind == SelectionKind::modular_midpoint
             ? inverse_precision_proposal >= 128
             : std::isfinite(cond_est) && cond_est > 0;
}

}  // namespace sic::exact

namespace sic::exact {
template bool GhostRefiner::select_rows<long double>(const acb_mat_t, long, long,
                                                     const std::vector<long>&);
template bool GhostRefiner::select_rows<SelectionWide>(const acb_mat_t, long, long,
                                                       const std::vector<long>&);
}  // namespace sic::exact
namespace sic::exact {
template bool GhostRefiner::select_rows<double>(const acb_mat_t, long, long,
                                                const std::vector<long>&);
}
