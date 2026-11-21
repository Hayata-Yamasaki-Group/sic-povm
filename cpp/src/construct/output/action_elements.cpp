// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/action_elements.hpp"

#include "construct/model/session_state.hpp"
#include "construct/output/output_plan.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "construct/runtime/resources/resident_memory.hpp"
#include "construct/runtime/session/field_values.inl.hpp"
#include "exact/certification/newton/recognition.hpp"
#include "exact/transform/representative_lattice.hpp"
#include "sic/support/flint_raii.hpp"
namespace sic::construct {
void Construction::initialize_actions(size_t nl,
                                      std::vector<std::vector<sic::support::Index>>& gens,
                                      std::vector<std::unique_ptr<ActionElement>>& el,
                                      std::vector<std::vector<long>>& el_of) {
  using Elem = ActionElement;
  for (int i = 0; i < t; ++i) {
    for (size_t l = 0; l < nl; ++l)
      if (coset(i + 1, (*group_).stride[l]) != 0) gens[i].push_back((int)l);
    const HeightBound& Cg = sdt.certified_Cg[i];
    const HeightBound twice_Cg = HeightBound(2) * Cg;
    const long p = ps[i];
    auto pairs = [](ulong n) {
      const ulong a = n % 2 ? n : n / 2;
      const ulong b = n % 2 ? (n - 1) / 2 : n - 1;
      if (b && a > std::numeric_limits<ulong>::max() / b)
        throw sic::Error(sic::ErrorKind::resource, "action height exponent overflow");
      return a * b;
    };
    auto add = [&](int l, long k, const HeightBound& b) {
      el_of[i].push_back((long)el.size());
      el.push_back(std::make_unique<Elem>());
      *el.back() = Elem{i, l, k, b, {}, {}};
    };
    if (p == 2) {
      for (int l : gens[i]) add(l, -1, Cg.pow(2));
      continue;
    }
    add(-1, -2, twice_Cg.pow(pairs(p)));
    for (int l : gens[i])
      for (long k = 0; k < p; ++k)
        add(l, k,
            HeightBound(p) * Cg * HeightBound::binomial(p - 1, k) * Cg.pow(p - 1 - k) *
                twice_Cg.pow(pairs(p - 1)));
  }
}
int Construction::recognize_actions(slong wp, arb_srcptr sq0, PrivateLeafStore& spool,
                                    std::vector<std::vector<sic::support::Index>>& gens,
                                    std::vector<std::unique_ptr<ActionElement>>& el,
                                    std::vector<std::vector<long>>& el_of, long& n_act) {
  using Elem = ActionElement;
  auto* action_spool = &spool;
  auto finish_elem = [&](Elem& E, sic::support::ComplexBallVector& a) -> bool {
    const int i = E.i;
    const long nc = Ncos[i];
    sic::support::RealBallVector y(nc);
    for (long c = 0; c < nc; ++c) arb_swap(y[c], acb_realref(a[c]));
    a = sic::support::ComplexBallVector();
    leaves_fft_real(y, i, sdt, wp, nth);
    RecognitionLattice RL(log2_bp_of(sdt, i, E.height), sq0, wp);
    std::atomic<long> bad(-1);
    const bool spill_action =
        memory.decide_reserved(
            "action/" + std::to_string(E.i) + "/" + std::to_string(E.l) + "/" + std::to_string(E.k),
            {sic::support::resources::leaf_block_bytes(nc, recognition_bits(y, (slong)RL.log2Bp)),
             true, false}) == Retention::spill;
    if (spill_action) {
      admit_recognition_block(y, (slong)RL.log2Bp);
      struct RecognitionFailure {};
      try {
        E.source =
            action_spool->append_generated(nc, [&](size_t first_leaf, std::span<Leaf> block) {
              const long chunks = std::min<long>(block.size(), 16L * nth);
              parallel_for(chunks, nth, [&](long chunk) {
                RecognitionLattice::Work work;
                for (size_t j = chunk * block.size() / chunks;
                     j < (chunk + 1) * block.size() / chunks && bad < 0; ++j) {
                  const size_t e = first_leaf + j;
                  if (!RL.recognize(y[e], block[j].x, block[j].y, work)) {
                    bad = e;
                    break;
                  }
                  arb_clear(y[e]);
                  arb_init(y[e]);
                }
              });
              if (bad >= 0) throw RecognitionFailure{};
            });
      } catch (const RecognitionFailure&) {
      }
    } else {
      E.lv = std::vector<Leaf>(nc);
      const long nchunk = std::min<long>(nc, 16L * nth);
      parallel_for(nchunk, nth, [&](long ch) {
        RecognitionLattice::Work rw;
        for (long e = ch * nc / nchunk; e < (ch + 1) * nc / nchunk && bad < 0; ++e)
          if (!RL.recognize(y[e], E.lv[e].x, E.lv[e].y, rw)) bad = e;
      });
    }
    if (bad >= 0) {
      sic::support::progressf(
          "  action data (step %d, generator %d, k = %ld): leaf %ld not recognized: log2 rad = "
          "%.1f, needed < %.1f\n",
          i, E.l, E.k, (long)bad, mag_get_d_log2_approx(arb_radref(y[bad])),
          -std::ceil(RL.log2Bp) - 7);
      return false;
    }
    n_act += nc;
    return true;
  };
  for (int i = 0; i < t; ++i) {
    const long p = ps[i], nc = Ncos[i];
    const auto& generator_values = sdt.g[i];
    if (p == 2) {
      if (generator_values.character &&
          std::all_of(ps.begin(), ps.begin() + i, [](long degree) { return degree == 2; })) {
        for (long q : el_of[i]) {
          Elem& E = *el[q];
          E.constant = true;
          E.lv = std::vector<Leaf>(1);
          fmpz_set_si(E.lv[0].x, -2 * eighth_character((*group_).nn[(*group_).stride[E.l]]) * nc);
          n_act += nc;
        }
        continue;
      }
      for (long q : el_of[i]) {
        Elem& E = *el[q];
        reserve_final("actions", 2, wp, model_->decoder_hint ? model_->decoder_hint : wp);
        if (generator_values.backing)
          budget.require_additional(
              "action generator reload",
              sic::support::resources::mul(nc, sic::support::resources::real_bytes(wp)));
        generator_values.prepare();
        sic::support::ComplexBallVector a(nc);
        const long chunks = std::min<long>(nc, 16L * nth);
        parallel_for(chunks, nth, [&](long chunk) {
          sic::support::ComplexBallVector value(2);
          for (long c = chunk * nc / chunks; c < (chunk + 1) * nc / chunks; ++c) {
            generator_values.get(value[0],
                                 coset(i + 1, (*group_).mul(rep[i][c], (*group_).stride[E.l])));
            generator_values.get(value[1], coset(i + 1, rep[i][c]));
            acb_mul(a[c], value[0], value[1], wp);
          }
        });
        generator_values.park();
        if (!finish_elem(E, a)) return 1;
      }
      continue;
    }
    const sic::support::ComplexBallVector& gv = generator_values.full;
    // odd step, per coset c with v_j = gv[p c + j]: F(X) = prod_j (X - v_j), F / (X - v_j) = sum_k
    // c^(j)_k X^k, F'(v_j) = prod_{j' != j} (v_j - v_j'), delta = prod_{j < j'} (v_j' - v_j); for
    // tau = tau_l with u_j = tau(v) at sub-coset j: delta a_k = sum_j u_j c^(j)_k delta / F'(v_j).
    // All delta a_k of one generator in one pass.
    auto lagrange = [&](long c, sic::support::ComplexBallVector& L,
                        acb_t dl) {  // L[j p + k] = c^(j)_k delta / F'(v_j)
      sic::support::flint::Acb df, fp;
      acb_one(dl);
      for (long j = 0; j < p; ++j)
        for (long j2 = j + 1; j2 < p; ++j2) {
          acb_sub(df, gv[p * c + j2], gv[p * c + j], wp);
          acb_mul(dl, dl, df, wp);
        }
      sic::support::ComplexBallVector Fc(p + 1);
      acb_poly_t F;
      acb_poly_init(F);
      acb_poly_product_roots(F, gv[p * c], p, wp);
      for (long k = 0; k <= p; ++k) acb_poly_get_coeff_acb(Fc[k], F, k);
      acb_poly_clear(F);
      for (long j = 0; j < p; ++j) {
        acb_srcptr vj = gv[p * c + j];
        acb_one(L[j * p + p - 1]);
        for (long k = p - 1; k >= 1; --k) {
          acb_mul(L[j * p + k - 1], vj, L[j * p + k], wp);
          acb_add(L[j * p + k - 1], L[j * p + k - 1], Fc[k], wp);
        }
        acb_zero(fp);
        for (long k = p - 1; k >= 0; --k) {
          acb_mul(fp, fp, vj, wp);
          acb_add(fp, fp, L[j * p + k], wp);
        }
        acb_div(fp, dl, fp, wp);
        for (long k = 0; k < p; ++k) acb_mul(L[j * p + k], L[j * p + k], fp, wp);
      }
    };
    for (long q : el_of[i])
      if (el[q]->k == -2) {
        reserve_final("actions", 2, wp, model_->decoder_hint ? model_->decoder_hint : wp);
        sic::support::ComplexBallVector a(nc);
        parallel_for(nc, nth, [&](long c) {
          sic::support::flint::Acb df;
          acb_one(a[c]);
          for (long j = 0; j < p; ++j)
            for (long j2 = j + 1; j2 < p; ++j2) {
              acb_sub(df, gv[p * c + j2], gv[p * c + j], wp);
              acb_mul(a[c], a[c], df, wp);
            }
        });
        if (!finish_elem(*el[q], a)) return 1;
      }
    for (int l : gens[i]) {
      reserve_final("actions", 2, wp, model_->decoder_hint ? model_->decoder_hint : wp);
      std::vector<sic::support::ComplexBallVector> av(p, sic::support::ComplexBallVector(nc));
      const long nchunk = std::min<long>(nc, 16L * nth);
      parallel_for(nchunk, nth, [&](long ch) {
        sic::support::ComplexBallVector L(p * p);
        sic::support::flint::Acb dl;
        for (long c = ch * nc / nchunk; c < (ch + 1) * nc / nchunk; ++c) {
          lagrange(c, L, dl);
          for (long k = 0; k < p; ++k) acb_zero(av[k][c]);
          for (long j = 0; j < p; ++j) {
            acb_srcptr u =
                gv[coset(i + 1, (*group_).mul(rep[i + 1][p * c + j], (*group_).stride[l]))];
            for (long k = 0; k < p; ++k) acb_addmul(av[k][c], u, L[j * p + k], wp);
          }
        }
      });
      for (long q : el_of[i])
        if (el[q]->l == l && el[q]->k >= 0)
          if (!finish_elem(*el[q], av[el[q]->k])) return 1;
    }
  }
  return 0;
}

}  // namespace sic::construct
