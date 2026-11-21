// SPDX-License-Identifier: AGPL-3.0-or-later
#include <flint/acb_dft.h>
#include <flint/acb_poly.h>
#include <malloc.h>
#include <sys/resource.h>
#include <sys/statvfs.h>

#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string_view>

#include "construct/components/components.hpp"
#include "construct/model/session_state.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "construct/runtime/resources/resident_memory.hpp"
#include "construct/runtime/resources/table_memory.hpp"
#include "construct/runtime/session/field_values.inl.hpp"
#include "exact/analytic/cyclotomic_trace.hpp"
#include "exact/analytic/direct_eta.hpp"
#include "exact/analytic/product_candidate.hpp"
#include "exact/analytic/step_selection.hpp"
#include "exact/analytic/table.hpp"
#include "exact/arithmetic/maximal_order.hpp"
#include "exact/certification/bounds/height_bounds.hpp"
#include "exact/certification/reconstruction/ghost.hpp"
#include "exact/group/group.hpp"
#include "exact/group/group_checkpoint.hpp"
#include "exact/group/group_fourier.hpp"
#include "exact/runtime/newton_class_memory.hpp"
#include "exact/runtime/parallel.hpp"
#include "exact/storage/analytic_generator.hpp"
#include "exact/storage/completed_class_cache.hpp"
#include "exact/storage/ghost_class_backing.hpp"
#include "exact/storage/leaf_source.hpp"  // exact leaf spools (sic::exact::storage::Source, Private)
#include "exact/storage/leaf_store.hpp"
#include "exact/storage/table_checkpoint.hpp"
#include "exact/transform/fixed_field_tower_general.hpp"
#include "exact/transform/leaf_transform.hpp"
#include "exact/transform/quadratic_transform.hpp"
#include "exact/transform/relative_projection.hpp"
#include "exact/transform/representative_lattice.hpp"
#include "exact/transform/trace_band.hpp"
#include "sic/support/flint_raii.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
#include "support/resources.hpp"
namespace sic::construct {
int Construction::tower() {  // ---------------- composition series of A*^ and the coset tower
                             // ----------------

  {
    // The stabilizer-adapted tower; odd steps are allowed: they are adjoined first
    // (fixed_field_tower_general.hpp).
    if (Ssz != 1 || Dstar != N || N != D)
      throw sic::Error(sic::ErrorKind::construction, "adapted tower requires full support");
    scalar_fields = std::make_unique<ScalarFixedFieldsGeneral>((*group_), sig_odd, cc);
  }

  X = std::vector<char>(N, 0);
  X[0] = 1;
  {
    long cnt = 1;
    // cyclotomic characters (trivial on H_cyc = {g : g fixes zeta_{2d} and sqrt(eps)}): they come
    // first, because their steps have generators of height ~0 (powers of zeta_{2d}, times
    // sqrt(eps)). H_cyc is tested on a sample of its elements (a character trivial on the sample
    // but not on H_cyc only changes the order of the series).
    std::vector<char> cyc(N, 0);
    {
      std::vector<long> hs;
      for (long g = 0; g < N; ++g)
        if ((*group_).nn[g] == 1 && (*group_).chi[g] > 0) hs.push_back(g);
      std::vector<long> test;
      for (size_t q = 0; q < hs.size() && test.size() < 32;
           q += std::max<size_t>(1, hs.size() / 32))
        test.push_back(hs[q]);
      parallel_for(N, nth, [&](long c) {
        bool all = true;
        for (long h : test)
          if (chiexp(c, h) != 0) {
            all = false;
            break;
          }
        cyc[c] = all;
      });
    }
    size_t scalar_target = 0;
    auto relative_extension = [&](long degree, const auto& allowed) -> long {
      if (scalar_target >= scalar_fields->fields.size()) return -1;
      const auto& field = scalar_fields->fields[scalar_target];
      // Odd steps (adjoined first) are allowed: the annihilator below handles prime degrees.
      if (field.degree != 2 * degree || ps.size() != phis.size()) return -1;
      long character = -1;
      for (long c = 0; c < N; ++c)
        if (!X[c] && allowed(c) && X[(*group_).mul(c, c)]) {
          character = c;
          break;
        }
      if (character < 0) return -1;
      const size_t expected = N / degree;
      if (expected > (size_t)(8 * d))
        throw std::logic_error("relative target exceeds its scalar subgroup bound");
      budget.require_additional("relative extension subgroups",
                                sic::support::resources::mul(expected, 128));
      const auto kernel_generators = prime_annihilator_generators((*group_), phis, ps);
      const auto kernel = enumerate_exact_subgroup((*group_), kernel_generators, expected);
      std::vector<long> target = field.members;
      for (long h : field.members) target.push_back((*group_).mul(h, cc));
      std::sort(target.begin(), target.end());
      target.erase(std::unique(target.begin(), target.end()), target.end());
      if (kernel.size() != expected || target.size() * 2 != kernel.size() ||
          !std::includes(kernel.begin(), kernel.end(), target.begin(), target.end()))
        throw std::logic_error("relative target is not the verified index-two subgroup");
      for (long h : target)
        if (chiexp(character, h) != 0)
          throw std::logic_error("relative extension character does not annihilate the target");
      long tau = -1;
      for (long h : kernel)
        if (!std::binary_search(target.begin(), target.end(), h)) {
          tau = h;
          break;
        }
      if (tau < 0 || chiexp(character, tau) != Lc / 2 ||
          !std::binary_search(target.begin(), target.end(), (*group_).mul(tau, tau)))
        throw std::logic_error("invalid relative quadratic lift");
      const sic::support::Index point = (*group_).ghost_point(0, 1),
                                base = (*group_).rep_base[point];
      const long inverse = (*group_).pow((*group_).rep_elem[point], -1);
      std::vector<long> anchors;
      for (sic::support::Index c = 0; c < (*ar_).h(); ++c)
        for (long q = 1; q < d * d; ++q) {
          const sic::support::Index candidate = (*group_).orbit[c * d * d + q];
          if ((*group_).rep_base[candidate] == base) {
            anchors.push_back((*group_).mul((*group_).rep_elem[candidate], inverse));
            break;
          }
        }
      budget.require_additional(
          "relative projection roots, results and scratch",
          sic::support::resources::add(
              sic::support::resources::kMib,
              sic::support::resources::add(
                  sic::support::resources::mul(
                      2 * d + anchors.size(),
                      sic::support::resources::complex_bytes(first_pass_bits + 96)),
                  sic::support::resources::mul(
                      64 * (sic::support::resources::Count)nth,
                      sic::support::resources::complex_bytes(first_pass_bits + 192)))));
      sic::support::ComplexBallVector roots(2 * d), values(anchors.size());
      for (long k = 0; k < 2 * d; ++k) root_2d(k, first_pass_bits + 16, roots[k]);
      sic::support::flint::Arb sqe;
      to_arb(sqe, (*ar_).kc, (*ar_).eps, 1, first_pass_bits + 16);
      arb_sqrt(sqe, sqe, first_pass_bits + 16);
      parallel_for(anchors.size(), nth, [&](long q) {
        relative_projection_at(
            values[q], (*group_), target, tau, anchors[q], first_pass_bits + 16,
            [&](long g, acb_ptr out) { product_at(*T, g, first_pass_bits + 16, roots, sqe, out); });
      });
      long witness = -1;
      for (size_t q = 0; q < anchors.size(); ++q)
        if (acb_is_finite(values[q]) && arb_contains_zero(acb_imagref(values[q])) &&
            !arb_contains_zero(acb_realref(values[q]))) {
          witness = q;
          break;
        }
      sic::support::progressf(
          "relative extension probe: degree=%ld target=%ld HX=%zu HY=%zu anchors=%zu certified=%d "
          "[%.1fs]\n",
          degree, field.degree, kernel.size(), target.size(), anchors.size(), witness >= 0,
          stamp());
      if (witness < 0) return -1;
      auto target_generators = field.generators;
      target_generators.push_back(cc);
      sic::support::flint::String real_dump(arb_dump_str(acb_realref(values[witness])));
      sic::support::flint::String imaginary_dump(arb_dump_str(acb_imagref(values[witness])));
      relative_certificates.push_back({(int)phis.size(), character, tau, anchors[witness],
                                       first_pass_bits + 16, kernel_generators, target_generators,
                                       real_dump.get(), imaginary_dump.get()});
      relative_highest = phis.size();
      sic::support::progressf(
          "relative extension certificate: level=%zu character=%ld tau=%ld anchor=%ld "
          "precision=%ld; HX generators:",
          phis.size(), character, tau, anchors[witness], (long)first_pass_bits + 16);
      for (long h : kernel_generators) sic::support::progressf(" %ld", h);
      sic::support::progressf("; HY generators:");
      for (long h : target_generators) sic::support::progressf(" %ld", h);
      sic::support::progressf("\n");
      sic::support::progressf("  relative witness ball: %s | %s\n",
                              relative_certificates.back().real_ball.c_str(),
                              relative_certificates.back().imaginary_ball.c_str());
      for (size_t factor = 0; factor < (*product_).factors.size(); ++factor) {
        sic::support::progressf("  relative product factor %zu:", factor);
        for (long coefficient : (*product_).factors[factor])
          sic::support::progressf(" %ld", coefficient);
        sic::support::progressf("\n");
      }
      // This certifies the whole Y\\X extension band, not a literal
      // coefficient at character. Deliberately leave inSupp unchanged.
      return character;
    };
    while (cnt < Dstar) {
      while (scalar_target < scalar_fields->fields.size() &&
             cnt == scalar_fields->fields[scalar_target].degree) {
        scalar_fields->fields[scalar_target].level = phis.size();
        ++scalar_target;
      }
      long psi = -1;
      // With complex conjugation cc known, the series first exhausts the characters trivial on cc
      // (the real subfield F_{t-1} = L^{cc}); the last step is then the quadratic extension L /
      // F_{t-1}. All levels below the top are real, so the Galois FFT runs in real arithmetic
      // there.
      const bool real_phase = cc >= 0 && 2 * cnt < Dstar;
      auto allowed = [&](long c) {
        if (real_phase && chiexp(c, cc) != 0) return false;
        if (scalar_target < scalar_fields->fields.size())
          for (long h : scalar_fields->fields[scalar_target].generators)
            if (chiexp(c, h) != 0) return false;
        return true;
      };
      // prefer psi in the supports with psi^p in X for a prime p (then the step's coset meets
      // supp(z) already for kappa = 1): cyclotomic characters first, then p = 2, then odd primes p
      // dividing [A*^ : X] Adapted tower with an odd part: the odd prime steps come first (the
      // odd-degree subfield L^{G_2} lies in every E_m, since every scalar stabilizer is a 2-group),
      // so the quadratic chain above it passes through every R_m. Odd-order characters are trivial
      // on the 2-groups H_m and on cc, hence always allowed.
      const bool odd_first = cnt % scalar_fields->odd != 0;
      if (odd_first) {
        for (long p = 3, rest = scalar_fields->odd / std::gcd(cnt, scalar_fields->odd);
             p <= rest && psi < 0; p += 2) {
          if (rest % p) continue;
          bool pr = true;
          for (long q = 3; q * q <= p; q += 2)
            if (p % q == 0) pr = false;
          if (!pr) continue;
          for (long c = 0; c < N && psi < 0; ++c)
            if (inSupp[c] && !X[c] && allowed(c) && X[(*group_).pow(c, p)]) psi = c;
        }
        if (psi < 0) {
          sic::support::progressf(
              "adapted tower obstruction: no certified support witness of odd order; current "
              "degree=%ld odd part=%ld\n",
              cnt, scalar_fields->odd);
          throw sic::Error(sic::ErrorKind::construction,
                           "adapted tower has no certified support witness of odd order");
        }
      }
      if (psi < 0)
        psi = quadratic_support_witness(
            N, inSupp, X, cyc, [&](long a, long b) { return (*group_).mul(a, b); }, allowed);
      if (psi < 0) psi = relative_extension(cnt, allowed);
      if (psi < 0) {
        const long target_degree = scalar_target < scalar_fields->fields.size()
                                       ? scalar_fields->fields[scalar_target].degree
                                       : Dstar;
        sic::support::progressf(
            "adapted tower obstruction: no certified base-support witness for this constrained "
            "quadratic extension; current degree=%ld target degree=%ld support count=%zu\n",
            cnt, target_degree, (size_t)std::count(inSupp.begin(), inSupp.end(), 1));
        std::vector<long> two_torsion{0};
        for (size_t l = 0; l < (*group_).invf.size(); ++l)
          if ((*group_).invf[l] % 2 == 0) {
            const size_t count = two_torsion.size();
            for (size_t q = 0; q < count; ++q)
              two_torsion.push_back(two_torsion[q] + ((*group_).invf[l] / 2) * (*group_).stride[l]);
          }
        long missing_quadratic = 0;
        sic::support::progressf("  allowed order-two characters outside the current tower:");
        for (long character : two_torsion)
          if (!X[character] && allowed(character)) {
            ++missing_quadratic;
            sic::support::progressf(" %ld", character);
          }
        sic::support::progressf(" (%ld total)\n", missing_quadratic);
        {
          long candidates = 0, supported = 0;
          for (long c = 0; c < N; ++c)
            if (!X[c] && allowed(c) && X[(*group_).mul(c, c)]) {
              ++candidates;
              supported += inSupp[c] != 0;
            }
          sic::support::progressf(
              "  allowed characters c outside the tower with c^2 in it: %ld (%ld with a support "
              "witness)\n",
              candidates, supported);
        }
        throw sic::Error(
            sic::ErrorKind::construction,
            "adapted tower has no certified witness for its constrained quadratic extension");
      }
      for (long p = 3, rest = Dstar / cnt; p <= rest && psi < 0; p += 2) {
        if (rest % p) continue;
        bool pr = true;
        for (long q = 3; q * q <= p; q += 2)
          if (p % q == 0) pr = false;
        if (!pr) continue;
        for (long c = 0; c < N && psi < 0; ++c)
          if (inSupp[c] && !X[c] && allowed(c) && X[(*group_).pow(c, p)]) psi = c;
      }
      for (long c = 0; c < N && psi < 0; ++c)
        if (inA[c] && !X[c] && allowed(c)) psi = c;
      long mo = 1, cur = psi;
      while (!X[cur]) {
        cur = (*group_).mul(cur, psi);
        ++mo;
      }
      long p = 2;
      while (mo % p) ++p;
      long phi = (*group_).pow(psi, mo / p);

      std::vector<long> mem;
      for (long c = 0; c < N; ++c)
        if (X[c]) mem.push_back(c);
      long pw = phi;
      for (long e = 1; e < p; ++e) {
        for (long a : mem) X[(*group_).mul(a, pw)] = 1;
        pw = (*group_).mul(pw, phi);
      }
      cnt *= p;
      phis.push_back(phi);
      ps.push_back(p);
    }
  }
  t = (int)phis.size();
  (*model_).extra_resident = 0;
  sic::support::progressf("tower: %d steps, degrees:", t);
  for (long p : ps) sic::support::progressf(" %ld", p);
  sic::support::progressf("\n");
  // Ncos[i] = [F_i : K] = prod_{k<i} p_k: number of cosets of H_i = Gal(L/F_i) (times |S|)
  Ncos = std::vector<long>(t + 1, 1);
  for (int i = 0; i < t; ++i) Ncos[i + 1] = Ncos[i] * ps[i];
  // Nested labels obey cos_{i+1}(g) = p_i cos_i(g) + digit. Only
  // the final map is needed: cos_i(g) = cos_t(g) / (Ncos[t]/Ncos[i]).
  // Update each element in place while constructing the same labels and
  // first representatives as the full collection of level maps.
  sic::support::admit_index_count(N);
  sic::support::admit_index_count(Ncos.back());
  cos_top = std::vector<sic::support::Index>(N, 0);
  rep = std::vector<std::vector<long>>(t + 1);
  rep[0] = {0};
  for (int i = 0; i < t; ++i) {
    const long p = ps[i], q = Lc / p;
    if (Lc % p)
      throw sic::Error(sic::ErrorKind::construction, "step degree does not divide the exponent");
    std::vector<long> ker(Ncos[i]);
    for (long c = 0; c < Ncos[i]; ++c) ker[c] = chiexp(phis[i], rep[i][c]);
    parallel_for(N, nth, [&](long g) {
      const sic::support::Index previous = cos_top[g];
      const long df = ((chiexp(phis[i], g) - ker[previous]) % Lc + Lc) % Lc;
      if (df % q)
        throw sic::Error(sic::ErrorKind::construction, "coset labelling: phi^p not trivial on H_i");
      cos_top[g] = sic::support::checked_index(p * previous + df / q);
    });
    rep[i + 1].assign(Ncos[i + 1], -1);
    for (long g = 0; g < N; ++g)
      if (rep[i + 1][cos_top[g]] < 0) rep[i + 1][cos_top[g]] = g;
    for (long r0 : rep[i + 1])
      if (r0 < 0) throw sic::Error(sic::ErrorKind::construction, "empty coset");
    if (producer_options.progress)
      sic::support::progressf("coset labels complete: level=%d degree=%ld [%.1fs]\n", i + 1,
                              Ncos[i + 1], stamp());
  }
  cos_scale = std::vector<long>(t + 1);
  cos_shift = std::vector<sic::support::Index>(t + 1, -1);
  for (int i = 0; i <= t; ++i) {
    cos_scale[i] = Ncos[t] / Ncos[i];
    const auto scale = static_cast<unsigned long>(cos_scale[i]);
    if (std::has_single_bit(scale)) cos_shift[i] = std::countr_zero(scale);
  }

  for (const auto& field : scalar_fields->fields) {
    if (field.level < 0 || Ncos[field.level] != field.degree || coset(field.level, cc) != 0)
      throw sic::Error(sic::ErrorKind::construction,
                       "adapted fixed-field prefix degree/conjugation check failed");
    for (long h : field.members)
      if (coset(field.level, h) != 0)
        throw sic::Error(sic::ErrorKind::construction,
                         "adapted fixed-field prefix does not annihilate its scalar stabilizer");
    sic::support::progressf("adapted real prefix: level=%d degree=%ld stabilizer=%zu\n",
                            field.level, field.degree, field.members.size());
  }

  // Conjugation real tail (required): the last step is L / L^{cc}, and cc swaps the two cosets 2c,
  // 2c+1 of every coset c of H_{t-1}. Every F_i, i < t, is then real, and the Galois FFT runs in
  // real arithmetic below the top.
  {
    std::atomic<bool> okc(cc >= 0 && t >= 1 && ps[t - 1] == 2);
    if (okc)
      parallel_for(N, nth, [&](long g) {
        if (coset(t, (*group_).mul(g, cc)) != (coset(t, g) ^ 1)) okc = false;
      });
    if (!okc)
      throw sic::Error(sic::ErrorKind::construction,
                       "adapted tower requires the conjugation real tail");
  }
  sic::support::progressf(
      "Galois FFT: real arithmetic below the top level (the top step is complex conjugation)\n");
  phased_storage = phase_planned;
  if (phased_storage &&
      (!analytic_layout || !stream_actions || !stream_steps || !stream_reps || !compact_work))
    throw sic::Error(sic::ErrorKind::construction,
                     "phased storage requires the compact all-quadratic adapted path");

  model_->step_degrees.assign(ps.begin(), ps.end());
  model_->coset_counts.assign(Ncos.begin(), Ncos.end());
  model_->smith_rank = group_->invf.size();
  model_->kernel_workers = nth;
  // group_stage already charged the Smith-axis sum.
  table_parked = false;

  analytic_anchor = (*group_).ghost_point(0, 1);

  return 0;
}

}  // namespace sic::construct
