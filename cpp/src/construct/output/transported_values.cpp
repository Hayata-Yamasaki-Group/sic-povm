// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/transported_values.hpp"

#include "construct/model/session_state.hpp"
#include "construct/runtime/session/field_values.inl.hpp"
#include "exact/transform/leaf_transform.hpp"
#include "exact/transform/quadratic_transform.hpp"
#include "sic/support/flint_raii.hpp"
#include "support/resources/platform_snapshot.hpp"
namespace sic::construct {
void Construction::TransportedValues::dual_at(acb_ptr out, int i, long c, const Dec& decoder) {
  if (decoder.symbolic_top && i == session.t - 1) {
    acb_set(out, decoder.top_dual[0]);
    if (eighth_character((*session.group_).nn[session.rep[session.t][2 * c]]) < 0)
      acb_neg(out, out);
  } else {
    size_t index = c;
    if (decoder.pruned && decoder.labels[i].size() != (size_t)session.Ncos[i]) {
      const auto& labels = decoder.labels[i];
      const auto found = std::lower_bound(labels.begin(), labels.end(), c);
      if (found == labels.end() || *found != c)
        throw std::logic_error("missing pruned physical dual label");
      index = found - labels.begin();
    }
    acb_set(out, decoder.h2w[i][index]);
  }
}

void Construction::TransportedValues::lam_vals(const std::vector<Leaf>& lv, slong wd,
                                               sic::support::ComplexBallVector& v) {
  long mb = 0;
  for (auto& l : lv) mb = std::max(mb, (long)std::max(fmpz_bits(l.x), fmpz_bits(l.y)));
  const slong pr = mb + wd + 64;
  if (pr > omh_prec) {
    omh_prec = pr;
    arb_sqrt_ui(omh, session.D0, pr);
    arb_sub_ui(omh, omh, 1, pr);
    arb_neg(omh, omh);
    arb_mul_2exp_si(omh, omh, -1);
  }
  const long n = (long)lv.size();
  if ((long)v.size() != n) v = sic::support::ComplexBallVector(n);
  const long nchunk = std::min<long>(n, 16L * session.nth);
  parallel_for(nchunk, session.nth, [&](long ch) {
    arb_t x;
    arb_init(x);
    for (long e = ch * n / nchunk; e < (ch + 1) * n / nchunk; ++e) {
      const slong pe = std::max(fmpz_bits(lv[e].x), fmpz_bits(lv[e].y)) + wd + 64;
      arb_set_fmpz(x, lv[e].y);
      arb_mul(x, x, omh, pe);
      arb_add_fmpz(x, x, lv[e].x, pe);
      arb_set_round(acb_realref(v[e]), x, wd);
      arb_zero(acb_imagref(v[e]));
    }
    arb_clear(x);
  });
}
void Construction::TransportedValues::inv_fft(sic::support::ComplexBallVector& a, int n,
                                              const Dec& Dc, sic::support::ComplexBallVector& b) {
  if (std::all_of(session.ps.begin(), session.ps.begin() + n, [](long p) { return p == 2; })) {
    b = sic::support::ComplexBallVector();
    quadratic_complex_inverse(a.v, n, Dc.wd, session.nth,
                              [&](acb_ptr value, int i, long c) { dual_at(value, i, c, Dc); });
    return;
  }
  if (Dc.pruned) throw std::logic_error("full inverse after decoder pruning");
  if (b.size() != a.size()) b = sic::support::ComplexBallVector(a.size());
  const slong wd = Dc.wd;
  for (int i = 0; i < n; ++i) {
    const long p = session.ps[i], Ni = session.Ncos[i], block = session.Ncos[i + 1],
               nblk = (long)a.size() / block, tot = nblk * Ni;
    const long nchunk = std::min<long>(tot, 16L * session.nth);
    parallel_for(nchunk, session.nth, [&](long ch) {
      sic::support::flint::Acb h, tq, dual;
      for (long u = ch * tot / nchunk; u < (ch + 1) * tot / nchunk; ++u) {
        const long b0 = (u / Ni) * block, c = u % Ni;
        if (p == 2) {
          acb_mul_2exp_si(h, a[b0 + c], -1);
          dual_at(dual, i, c, Dc);
          acb_mul(tq, a[b0 + Ni + c], dual, wd);
          acb_add(b[b0 + 2 * c], h, tq, wd);
          acb_sub(b[b0 + 2 * c + 1], h, tq, wd);
        } else {
          for (long j = 0; j < p; ++j) {
            acb_ptr o = b[b0 + p * c + j];
            acb_mul(o, a[b0 + c], Dc.Bo[i][0][p * c + j], wd);
            for (long k = 1; k < p; ++k)
              acb_addmul(o, a[b0 + k * Ni + c], Dc.Bo[i][k][p * c + j], wd);
          }
        }
      }
    });
    std::swap(a.v, b.v);
  }
}
void Construction::TransportedValues::inv_fft_pruned(
    const std::vector<Leaf>& leaves, int n, const Dec& Dc, sic::support::ComplexBallVector& out,
    const std::vector<long>& lab, long offset, ExactSource source, bool lattice, long known_bits) {
  if (lattice && (!source || known_bits <= 0))
    throw std::logic_error("lattice representative source without its bit bound");
  long bits = source ? (lattice ? known_bits : (long)source.max_bits()) : 0;
  if (!source)
    for (const auto& leaf : leaves)
      bits = std::max(bits, (long)std::max(fmpz_bits(leaf.x), fmpz_bits(leaf.y)));
  const slong pr = bits + Dc.wd + 64;
  if (session.phased_storage) {
    const auto nodes =
        sic::support::resources::add(2048, sic::support::resources::mul(4 * (n + 1), lab.size()));
    session.budget.require_additional(
        "pruned inverse leaf scratch",
        sic::support::resources::add(
            sic::support::resources::mul(nodes, sic::support::resources::complex_bytes(Dc.wd)),
            sic::support::resources::add(
                source ? source.reader_forecast()
                       : sic::support::resources::leaf_block_bytes(65536, bits),
                sic::support::resources::mul(8, sic::support::resources::real_bytes(pr)))));
  }
  if (pr > omh_prec) {
    omh_prec = pr;
    arb_sqrt_ui(omh, session.D0, pr);
    arb_sub_ui(omh, omh, 1, pr);
    arb_neg(omh, omh);
    arb_mul_2exp_si(omh, omh, -1);
  }
  sic::support::ComplexBallVector work(2);
  std::unique_ptr<ExactSource::Cursor> cursor;
  if (source) cursor = std::make_unique<ExactSource::Cursor>(source, offset);
  std::optional<sic::support::LatticeCoordinates> coordinates;
  if (lattice) coordinates.emplace(session.D0);
  Leaf decoded;
  out = inverse_leaf_tree(
      session.ps, n, lab,
      [&](long e, acb_ptr value) {
        const Leaf* stored = cursor ? &cursor->next() : &leaves[offset + e];
        if (coordinates) {
          fmpz_set(decoded.x, stored->x);
          fmpz_set(decoded.y, stored->y);
          coordinates->decode(decoded.x, decoded.y);
          stored = &decoded;
        }
        const auto& leaf = *stored;
        const slong pe = std::max(fmpz_bits(leaf.x), fmpz_bits(leaf.y)) + Dc.wd + 64;
        arb_ptr real = acb_realref(work[0]);
        arb_set_fmpz(real, leaf.y);
        arb_mul(real, real, omh, pe);
        arb_add_fmpz(real, real, leaf.x, pe);
        arb_set_round(acb_realref(value), real, Dc.wd);
      },
      [&](acb_ptr value, acb_srcptr child, int i, long c1, long k) {
        const long p = session.ps[i], c = c1 / p, j = c1 % p;
        if (p == 2) {
          if (k == 0)
            acb_mul_2exp_si(value, child, -1);
          else {
            dual_at(work[1], i, c, Dc);
            acb_mul(work[1], child, work[1], Dc.wd);
            if (j == 0)
              acb_add(value, value, work[1], Dc.wd);
            else
              acb_sub(value, value, work[1], Dc.wd);
          }
        } else {
          size_t index = c1;
          const auto& labels = Dc.odd_labels[i];
          if (Dc.pruned) {
            const auto found = std::lower_bound(labels.begin(), labels.end(), c1);
            if (found == labels.end() || *found != c1)
              throw std::logic_error("missing pruned odd dual label");
            index = found - labels.begin();
          }
          if (k == 0)
            acb_mul(value, child, Dc.Bo[i][0][index], Dc.wd);
          else
            acb_addmul(value, child, Dc.Bo[i][k][index], Dc.wd);
        }
      });
}
void Construction::TransportedValues::conj_of(const std::vector<Leaf>& lv, int n, const Dec& Dc,
                                              sic::support::ComplexBallVector& out,
                                              ExactSource source) {
  if (!source) {
    sic::support::ComplexBallVector b;
    lam_vals(lv, Dc.wd, out);
    inv_fft(out, n, Dc, b);
  } else {
    const slong pr = source.max_bits() + Dc.wd + 64;
    if (session.phased_storage)
      session.budget.require_additional(
          "physical inverse leaf input",
          sic::support::resources::add(
              sic::support::resources::mul(source.count(),
                                           sic::support::resources::complex_bytes(Dc.wd)),
              sic::support::resources::add(
                  source.reader_forecast(4096),
                  sic::support::resources::mul(8 * session.nth,
                                               sic::support::resources::real_bytes(pr)))));
    if (pr > omh_prec) {
      omh_prec = pr;
      arb_sqrt_ui(omh, session.D0, pr);
      arb_sub_ui(omh, omh, 1, pr);
      arb_neg(omh, omh);
      arb_mul_2exp_si(omh, omh, -1);
    }
    out = sic::support::ComplexBallVector(source.count());
    source.read_blocks([&](size_t offset, std::span<Leaf> block) {
      parallel_for(block.size(), session.nth, [&](long q) {
        arb_t value;
        arb_init(value);
        const auto& leaf = block[q];
        const slong pe = std::max(fmpz_bits(leaf.x), fmpz_bits(leaf.y)) + Dc.wd + 64;
        arb_set_fmpz(value, leaf.y);
        arb_mul(value, value, omh, pe);
        arb_add_fmpz(value, value, leaf.x, pe);
        arb_set_round(acb_realref(out[offset + q]), value, Dc.wd);
        arb_clear(value);
      });
    });
    sic::support::ComplexBallVector scratch;
    inv_fft(out, n, Dc, scratch);
  }
}
void Construction::TransportedValues::conj_of_action(const Elem& E, int n, const Dec& Dc,
                                                     sic::support::ComplexBallVector& out) {
  if (E.constant) throw std::logic_error("symbolic top action should not be numerically decoded");
  conj_of(E.lv, n, Dc, out, E.source);
}
slong Construction::TransportedValues::min_acc(const sic::support::ComplexBallVector& v) {
  slong ma = WORD_MAX;
  for (size_t q = 0; q < v.size(); ++q) ma = std::min(ma, acb_rel_accuracy_bits(v[q]));
  return ma;
}
bool Construction::TransportedValues::build_dec(slong wd, Dec& Dc, std::string& why) {
  session.reserve_final("physical decoder", 3, wp, wd);
  // the path roots iota_path(g_i) start the labelled conjugates: they are needed to ~wd bits
  for (int round = 0;; ++round) {
    slong acc = WORD_MAX;
    for (int i = 0; i < session.t; ++i)
      acc = std::min(acc, acb_rel_accuracy_bits(session.path.root[i]));
    if (acc >= wd + 32) break;
    Path p2;
    if (round >= 4 || !session.compute_path(2 * session.path.wpd, &session.path, p2)) {
      why = "branch path at a higher precision failed";
      return false;
    }
    session.path = std::move(p2);
  }
  Dc.wd = wd;
  Dc.h2w.assign(session.t, sic::support::ComplexBallVector());
  Dc.Bo.assign(session.t, {});
  Dc.symbolic_top = false;
  Dc.top_dual = sic::support::ComplexBallVector();
  Dc.pruned = false;
  Dc.labels.assign(session.t, {});
  Dc.odd_labels.assign(session.t, {});
  for (int i = 0; i < session.t; ++i) {
    const long p = session.ps[i], nn = session.Ncos[i + 1];
    if (p == 2 && session.sdt.g[i].character) {
      Dc.top_dual = sic::support::ComplexBallVector(1);
      sic::support::ComplexBallVector check(1);
      acb_sqr(check[0], session.path.root[i], wd);
      acb_add_si(check[0], check[0], 2, wd);
      if (!acb_contains_zero(check[0])) {
        why = "symbolic top root does not enclose a root of X^2+2";
        return false;
      }
      acb_mul_2exp_si(Dc.top_dual[0], session.path.root[i], 1);
      acb_inv(Dc.top_dual[0], Dc.top_dual[0], wd);
      if (!acb_is_finite(Dc.top_dual[0]) || acb_rel_accuracy_bits(Dc.top_dual[0]) < 96) {
        why = "symbolic top dual has insufficient accuracy";
        return false;
      }
      Dc.symbolic_top = true;
      continue;
    }
    if (p == 2) {
      const long count = session.Ncos[i];
      for (long c = 0; c < count; ++c)
        if (session.rep[i][c] != session.rep[i + 1][2 * c])
          throw sic::Error(sic::ErrorKind::construction,
                           "quadratic parent representative is not its even child");
      // Unseen slots hold w_i^2; seen slots hold its physical even
      // conjugate. The odd conjugate is exactly its negative. Each
      // square is consumed only after a unique certified root reset.
      const size_t item = session.item_idx[i][0];
      sic::support::ComplexBallVector W;
      conj_of(session.ilv[item], i, Dc, W, session.item_sources[item]);
      std::vector<char> seen(count, 0);
      std::vector<long> queue;
      queue.reserve(count);
      sic::support::flint::Acb sq, rt;
      bool okc = true;
      sic::support::ComplexBallVector transported_root(1);
      auto reset = [&](long c2, acb_srcptr transported_value) {
        acb_srcptr s2 = W[c2];
        if (arb_is_negative(acb_realref(s2))) {
          acb_neg(sq, s2);
          acb_sqrt(rt, sq, wd);
          acb_mul_onei(rt, rt);
        } else
          acb_sqrt(rt, s2, wd);
        if (!acb_is_finite(rt) || acb_contains_zero(rt)) {
          okc = false;
          return;
        }
        acb_neg(sq, rt);
        const bool o1 = acb_overlaps(rt, transported_value),
                   o2 = acb_overlaps(sq, transported_value);
        if (o1 == o2) {
          okc = false;
          return;
        }
        acb_set(W[c2], o1 ? rt : sq);
      };
      reset(0, session.path.root[i]);
      if (okc) {
        seen[0] = 1;
        queue.push_back(0);
      }
      // Each completed Smith axis closes the reached subgroup. The
      // group is abelian, so forward closure under the next axis also
      // closes the old axes on new labels. Keep one decoded Gamma.
      for (long q : el_of[i]) {
        if (!okc) break;
        const int l = el[q]->l;
        if (seen[session.coset(i, (*session.group_).stride[l])]) continue;
        sic::support::ComplexBallVector Gamma;
        conj_of_action(*el[q], i, Dc, Gamma);
        for (size_t h = 0; h < queue.size() && okc; ++h) {
          const long c1 = queue[h], g = session.rep[i][c1];
          const long gp = (*session.group_).mul(g, (*session.group_).stride[l]),
                     full = session.coset(i + 1, gp), c2 = full / 2;
          if (!seen[c2]) {
            acb_div(transported_root[0], Gamma[c1], W[c1], wd);
            if (full & 1) acb_neg(transported_root[0], transported_root[0]);
            reset(c2, transported_root[0]);
            if (!okc) break;
            seen[c2] = 1;
            queue.push_back(c2);
          }
          // Include closing edges, rather than checking only a tree.
          acb_mul(sq, W[c2], W[c1], wd);
          if (full & 1) acb_neg(sq, sq);
          if (!acb_overlaps(sq, Gamma[c1])) okc = false;
        }
        if (!okc) break;
      }
      if (!okc) {
        why = "transported conjugate of w_" + std::to_string(i) + " is not a square root of w^2";
        return false;
      }
      if ((long)queue.size() != count) {
        why = "BFS did not reach every lower coset (step " + std::to_string(i) + ")";
        return false;
      }
      for (long c = 0; c < count; ++c) {
        acb_mul_2exp_si(sq, W[c], 1);
        acb_inv(W[c], sq, wd);
      }
      Dc.h2w[i] = std::move(W);
      if (min_acc(Dc.h2w[i]) < 96) {
        why = "accuracy lost at step " + std::to_string(i);
        return false;
      }
    } else {
      std::vector<char> seen(nn, 0);
      std::vector<long> queue;
      queue.reserve(nn);
      sic::support::ComplexBallVector W(nn);
      acb_set_round(W[0], session.path.root[i], wd);
      seen[0] = 1;
      queue.push_back(0);
      // conjugates of delta, of delta a_{l,k} and of the coefficients of f_i on G/H_i
      sic::support::ComplexBallVector Dl;
      std::vector<std::vector<sic::support::ComplexBallVector>> Al(
          nl, std::vector<sic::support::ComplexBallVector>(p));
      for (long q : el_of[i]) {
        if (el[q]->k == -2)
          conj_of_action(*el[q], i, Dc, Dl);
        else
          conj_of_action(*el[q], i, Dc, Al[el[q]->l][el[q]->k]);
      }
      for (int l : gens[i])
        for (long k = 0; k < p; ++k)
          for (long c = 0; c < session.Ncos[i]; ++c) acb_div(Al[l][k][c], Al[l][k][c], Dl[c], wd);
      std::vector<sic::support::ComplexBallVector> Af(p);
      for (long k = 0; k < p; ++k) {
        const size_t item = session.item_idx[i][k];
        conj_of(session.ilv[item], i, Dc, Af[k], session.item_sources[item]);
      }
      sic::support::flint::Acb pw, sm;
      // Root reset: the transported ball T of V(c2) is replaced by the interval Newton image N(B)
      // = z - f(z) / f'(B) of a box B around T (z: a few Newton steps from mid T). N(B) in B and
      // 0 not in f'(B) prove a unique root of f = iota_path(sigma f_i) in B, which is the true
      // V(c2) since T lies in B.
      sic::support::flint::Acb zr, fz, dz, fb, db, Bx, Nb;
      auto horner = [&](acb_t f, acb_t df, const acb_t x,
                        long c) {  // f(x), f'(x) for X^p + sum_k Af[k][c] X^k
        acb_one(f);
        acb_zero(df);
        for (long k = p - 1; k >= 0; --k) {
          acb_mul(df, df, x, wd);
          acb_add(df, df, f, wd);
          acb_mul(f, f, x, wd);
          acb_add(f, f, Af[k][c], wd);
        }
      };
      auto reset_odd = [&](long c2) {
        const long c = c2 / p;
        acb_get_mid(zr, W[c2]);
        for (int it = 0; it < 4; ++it) {
          horner(fz, dz, zr, c);
          acb_div(fz, fz, dz, wd);
          acb_sub(zr, zr, fz, wd);
          acb_get_mid(zr, zr);
        }
        mag_t rr, m2;
        mag_init(rr);
        mag_init(m2);
        acb_sub(fz, zr, W[c2], wd);
        acb_get_mag(rr, fz);
        mag_mul_2exp_si(rr, rr, 1);
        acb_get_mag(m2, zr);
        mag_mul_2exp_si(m2, m2, 16 - wd);
        mag_add(rr, rr, m2);
        acb_set(Bx, zr);
        arb_add_error_mag(acb_realref(Bx), rr);
        arb_add_error_mag(acb_imagref(Bx), rr);
        mag_clear(rr);
        mag_clear(m2);
        horner(fz, dz, zr, c);
        horner(fb, db, Bx, c);
        if (acb_contains_zero(db)) return;
        acb_div(Nb, fz, db, wd);
        acb_sub(Nb, zr, Nb, wd);
        if (acb_contains(Bx, Nb)) acb_set(W[c2], Nb);
      };
      for (size_t h = 0; h < queue.size(); ++h) {
        const long c1 = queue[h], g = session.rep[i + 1][c1];
        for (int l : gens[i]) {
          const long c2 =
              session.coset(i + 1, (*session.group_).mul(g, (*session.group_).stride[l]));
          if (seen[c2]) continue;
          acb_set(sm, Al[l][0][c1 / p]);
          acb_one(pw);
          for (long k = 1; k < p; ++k) {
            acb_mul(pw, pw, W[c1], wd);
            acb_addmul(sm, Al[l][k][c1 / p], pw, wd);
          }
          acb_swap(W[c2], sm);
          reset_odd(c2);
          seen[c2] = 1;
          queue.push_back(c2);
        }
      }
      if ((long)queue.size() != nn) {
        why = "BFS did not reach every coset (step " + std::to_string(i) + ")";
        return false;
      }
      // dual basis c_k(V) / f'(V), and the check f(V) = 0
      Dc.Bo[i].assign(p, sic::support::ComplexBallVector(nn));
      bool okc = true;
      sic::support::ComplexBallVector cf(p);
      for (long c1 = 0; c1 < nn && okc; ++c1) {
        const long c = c1 / p;
        acb_one(cf[p - 1]);
        for (long k = p - 1; k >= 1; --k) {
          acb_mul(cf[k - 1], W[c1], cf[k], wd);
          acb_add(cf[k - 1], cf[k - 1], Af[k][c], wd);
        }
        acb_mul(sm, W[c1], cf[0], wd);
        acb_add(sm, sm, Af[0][c], wd);  // f(V)
        if (!acb_contains_zero(sm)) okc = false;
        acb_zero(pw);
        for (long k = p - 1; k >= 0; --k) {
          acb_mul(pw, pw, W[c1], wd);
          acb_add(pw, pw, cf[k], wd);
        }  // f'(V)
        for (long k = 0; k < p; ++k) acb_div(Dc.Bo[i][k][c1], cf[k], pw, wd);
      }
      if (!okc) {
        why = "inconsistent conjugates of v_" + std::to_string(i);
        return false;
      }
      for (long k = 0; k < p; ++k)
        if (min_acc(Dc.Bo[i][k]) < 96) {
          why = "accuracy lost at step " + std::to_string(i);
          return false;
        }
    }
  }
  // All action inverses are complete. Representatives only need
  // prefixes of their scalar transport labels, not every field label.
  for (int i = 0; i < session.t; ++i)
    if (session.ps[i] == 2 && !(Dc.symbolic_top && i == session.t - 1)) {
      auto& labels = Dc.labels[i];
      for (long mm = 0; mm < session.d; ++mm) {
        long sg;
        session.rep_of(mm, sg);
        labels.push_back(session.coset(i, sg));
      }
      std::sort(labels.begin(), labels.end());
      labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
      sic::support::ComplexBallVector needed(labels.size());
      for (size_t q = 0; q < labels.size(); ++q) acb_swap(needed[q], Dc.h2w[i][labels[q]]);
      Dc.h2w[i] = std::move(needed);
    }
  for (int i = 0; i < session.t; ++i)
    if (session.ps[i] != 2) {
      auto& labels = Dc.odd_labels[i];
      for (long mm = 0; mm < session.d; ++mm) {
        long sg;
        session.rep_of(mm, sg);
        labels.push_back(session.coset(i + 1, sg));
      }
      std::sort(labels.begin(), labels.end());
      labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
      for (long k = 0; k < session.ps[i]; ++k) {
        sic::support::ComplexBallVector needed(labels.size());
        for (size_t q = 0; q < labels.size(); ++q) acb_swap(needed[q], Dc.Bo[i][k][labels[q]]);
        Dc.Bo[i][k] = std::move(needed);
      }
      sic::support::progressf("odd decoder step %d pruned to %zu of %ld labels\n", i, labels.size(),
                              session.Ncos[i + 1]);
    }
  Dc.pruned = true;
#if defined(__GLIBC__)
  sic::support::resources::release_free_heap();
#endif
  return true;
}

}  // namespace sic::construct
