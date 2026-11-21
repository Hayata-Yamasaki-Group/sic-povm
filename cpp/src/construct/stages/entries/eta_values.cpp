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
#include "construct/model/failure_test_hooks.hpp"
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
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
#include "support/resources.hpp"
namespace sic::construct {
void Construction::make_entry_ctx(const Table& Tb, EntryCtx& E) {
  if (Tb.compact()) {
    E.direct = &Tb;
    return;
  }
  const long nbp = (long)(*group_).nb();
  std::vector<sic::support::Index> posx((size_t)(*ar_).h() * d * d, -1), pt;
  E.oi.assign((size_t)N * nbp, 0);
  // positions by base point, then by g: a term of eta_entry has a fixed base point, so walking g
  // reads the table sequentially (the interleaved order touched a new page for every value)
  for (long b = 0; b < nbp; ++b)
    for (long g = 0; g < N; ++g) {
      const long q = g * nbp + b;
      const sic::support::Index x = (*group_).img[q];
      if (posx[x] < 0) {
        posx[x] = sic::support::checked_index(pt.size());
        pt.push_back(x);
      }
      E.oi[q] = posx[x];
    }
  const long np = (long)pt.size();
  const double bytes = (double)np * (2.0 * (Tb.prec / 8.0 + 64));
  if (bytes > 0.25 * static_cast<double>(sic::support::resources::Budget().remaining())) {
    E.Tptr.resize(np);
    for (long q = 0; q < np; ++q) E.Tptr[q] = tval(Tb, pt[q]);
    return;
  }
  E.Tord = sic::support::ComplexBallVector(np);
  const long nchunk = std::min<long>(np, 16L * nth);
  parallel_for(nchunk, nth, [&](long ch) {
    for (long q = ch * np / nchunk; q < (ch + 1) * np / nchunk; ++q)
      acb_set(E.Tord[q], tval(Tb, pt[q]));
  });
}

void Construction::eta_entry(const EntryCtx& E, long mm, slong wp,
                             const sic::support::ComplexBallVector& z2d, const arb_t sqd1,
                             sic::support::ComplexBallVector& out, int nthr,
                             const std::vector<long>* requested,
                             const std::function<void(long, long, acb_srcptr)>* consumer,
                             long requested_group) {
  const long nc = Ncos[t], h4 = d / 2, nbp = (long)(*group_).nb();
  std::vector<long> owner;
  std::vector<char> owner_conj;
  if (!requested && !consumer) {
    const long M = d / std::gcd(d, mm);
    std::vector<long> gens;
    if (M <= 2) {
      gens = {sig_odd[5 % (2 * d)], sig_odd[2 * d - 1]};
    } else {
      gens = {(*group_).pow(sig_odd[5], M / 4)};
    }
    std::set<long> H{0};
    std::vector<long> pending{0};
    for (size_t i = 0; i < pending.size(); ++i) {
      for (long gen : gens) {
        const long h = (*group_).mul(pending[i], gen);
        if (H.insert(h).second) pending.push_back(h);
      }
    }
    if (H.size() > 1) {
      const long nout = nc / 2;
      owner.assign(nout, -1);
      owner_conj.assign(nout, 0);
      for (long c0 = 0; c0 < nout; ++c0) {
        if (owner[c0] >= 0) continue;
        const long leader = rep[t][2 * c0];
        for (long h : H) {
          const long c2 = coset(t, (*group_).mul(leader, h));
          const long outidx = c2 / 2;
          const char conj = (char)(c2 & 1);
          if (owner[outidx] >= 0 && (owner[outidx] != c0 || owner_conj[outidx] != conj))
            throw sic::Error(sic::ErrorKind::construction,
                             "inconsistent eta fixed-field orbit assignment");
          owner[outidx] = c0;
          owner_conj[outidx] = conj;
        }
      }
      for (long c = 0; c < nout; ++c)
        if (owner[c] < 0)
          throw sic::Error(sic::ErrorKind::construction,
                           "incomplete eta fixed-field orbit assignment");
      long leaders = 0;
      for (long c = 0; c < nout; ++c) leaders += owner[c] == c;
      if (d >= 128)
        sic::support::progressf("  eta m=%ld: H size %zu, independent %ld of %ld real entries\n",
                                mm, H.size(), leaders, nout);
    }
  }
  if (requested)
    for (long g : *requested)
      if (g < 0 || g >= N) throw std::invalid_argument("invalid requested entry element");
  out = sic::support::ComplexBallVector(consumer ? 0 : (requested ? requested->size() : nc / 2));
  auto expand_fixed_field = [&]() {
    if (owner.empty()) return;
    for (long c = 0; c < (long)owner.size(); ++c) {
      const long src = owner[c];
      if (src == c) continue;
      if (owner_conj[c])
        acb_conj(out[c], out[src]);
      else
        acb_set(out[c], out[src]);
    }
  };
  const size_t nl = (*group_).invf.size();
  std::vector<long> sqv, sgn, rbv;
  std::vector<std::vector<long>> tab;  // tab[q nl + l][v] = ((v + digit_l(r_q)) mod n_l) stride_l
  for (long i = 0; i < d; ++i) {
    if (i == 0 && ((-mm) % d + d) % d == 0) continue;
    const sic::support::Index x0 = (*group_).ghost_point(i, -mm);
    const long r0 = (*group_).rep_elem[x0];
    rbv.push_back((*group_).rep_base[x0]);
    sqv.push_back(((*ar_).order_index * (i + mm) * (i + mm)) % (2 * d));
    sgn.push_back(((i - mm) % 2 == 0) ? 1 : -1);
    for (size_t l = 0; l < nl; ++l) {
      const long n = (*group_).invf[l], rl = (*group_).digit(r0, l);
      std::vector<long> tb(n);
      for (long v = 0; v < n; ++v) tb[v] = ((v + rl) % n) * (*group_).stride[l];
      tab.push_back(std::move(tb));
    }
  }
  const long nt = (long)sqv.size();
  // blocks of BS cosets: for each term the BS table reads are close together (Tord follows the g
  // order)
  const long BS = 16;
  const long iterations = requested ? requested->size() : N;
  if (requested_group <= 0 || iterations % requested_group || (!requested && requested_group != 1))
    throw std::invalid_argument("invalid requested entry ownership groups");
  const long units = iterations / requested_group;
  const long nchunk = std::min<long>(units, 32L * std::max(1, nthr));
  parallel_for(nchunk, nthr, [&](long ch) {
    sic::support::ComplexBallVector R(BS * h4);
    sic::support::ComplexBallVector table_value(1), output_value(consumer ? 1 : 0);
    std::vector<char> used(BS * h4, 0);
    std::vector<long> dg(BS * nl), nnb(BS), gb(BS), output_index(BS);
    std::vector<std::vector<long>> touched(BS);
    const long end = ((ch + 1) * units / nchunk) * requested_group;
    long position = (ch * units / nchunk) * requested_group;
    while (position < end) {
      long nbk = 0;
      for (; position < end && nbk < BS; ++position) {
        const long g = requested ? (*requested)[position] : position;
        const long c = coset(t, g);
        if (!requested && (rep[t][c] != g || (c & 1) || (!owner.empty() && owner[c / 2] != c / 2)))
          continue;
        output_index[nbk] = requested ? position : c >> 1;
        gb[nbk] = g;
        nnb[nbk] = (*group_).nn[g];
        touched[nbk].clear();
        for (size_t l = 0; l < nl; ++l) dg[nbk * nl + l] = (*group_).digit(g, l);
        ++nbk;
      }
      for (long q = 0; q < nt; ++q) {
        const long rq = rbv[q], sq = sqv[q];
        const bool neg = sgn[q] < 0;
        for (long bb = 0; bb < nbk; ++bb) {
          long idx = 0;
          for (size_t l = 0; l < nl; ++l) idx += tab[q * nl + l][dg[bb * nl + l]];
          const auto source = E.direct ? E.direct->reference((*group_).img[idx * nbp + rq])
                                       : Table::Reference{E.at(E.oi[idx * nbp + rq]), false};
          acb_srcptr f = source.value;
          if (source.conjugate) {
            acb_conj(table_value[0], f);
            f = table_value[0];
          }
          const long k = (sq * nnb[bb]) % (2 * d), r = k % h4;
          long qq = k / h4;
          if (neg) qq ^= 2;
          const long ri = bb * h4 + r;
          acb_ptr Rr = R[ri];
          if (!used[ri]) {
            used[ri] = 1;
            touched[bb].push_back(r);
            acb_zero(Rr);
          }
          switch (qq) {
            case 0:
              arb_add(acb_realref(Rr), acb_realref(Rr), acb_realref(f), wp);
              arb_add(acb_imagref(Rr), acb_imagref(Rr), acb_imagref(f), wp);
              break;
            case 1:
              arb_sub(acb_realref(Rr), acb_realref(Rr), acb_imagref(f), wp);
              arb_add(acb_imagref(Rr), acb_imagref(Rr), acb_realref(f), wp);
              break;
            case 2:
              arb_sub(acb_realref(Rr), acb_realref(Rr), acb_realref(f), wp);
              arb_sub(acb_imagref(Rr), acb_imagref(Rr), acb_imagref(f), wp);
              break;
            default:
              arb_add(acb_realref(Rr), acb_realref(Rr), acb_imagref(f), wp);
              arb_sub(acb_imagref(Rr), acb_imagref(Rr), acb_realref(f), wp);
              break;
          }
        }
      }
      for (long bb = 0; bb < nbk; ++bb) {
        const long gg = gb[bb];
        acb_ptr o = consumer ? output_value[0] : out[output_index[bb]];
        acb_zero(o);
        for (long r : touched[bb]) {
          if (r == 0)
            acb_add(o, o, R[bb * h4], wp);
          else
            acb_addmul(o, R[bb * h4 + r], z2d[r], wp);
          used[bb * h4 + r] = 0;
        }
        acb_neg(o, o);
        if (mm == 0) arb_add(acb_realref(o), acb_realref(o), sqd1, wp);
        if ((*group_).chi[gg] < 0) acb_neg(o, o);
        if (consumer) (*consumer)(gg, output_index[bb], o);
      }
    }
  });
  expand_fixed_field();
}

bool Construction::direct_eta_class(const EntryCtx& context, sic::support::Index source, long mm,
                                    slong wp, const sic::support::ComplexBallVector& roots,
                                    const arb_t sqrt_d1, const std::vector<long>& requested,
                                    const std::function<void(long, long, acb_srcptr)>& consume,
                                    long group) {
  auto refuse = [&](const std::string& why) {
    if (direct_eta_rows) direct_eta_rows->clear();
    sic::support::progressf(
        "direct eta refused: class=%ld eta=%ld reason=%s; literal table path used\n", (long)source,
        mm, why.c_str());
    return false;
  };
  if (!context.direct || !context.load_class || !T || !T->compact() || source < 0 ||
      (size_t)source >= table_backing.size() || !table_backing[source])
    return refuse("no streamed compact class backing");
  if (!table_backing[source].ghost) return false;  // full quadrature class: literal path by design
  const auto& ghost = *table_backing[source].ghost;
  const auto& md = ghost.metadata;
  const long e = ghost.group_element, o = (*ar_).order_index, dd = d * d;
  if (!ghost.state || md.d != d || e < 0 || e >= (*group_).N || md.chi != (*group_).chi[e] ||
      md.n != (o * (*group_).nn[e]) % (2 * d) || ghost.packing_precision != T->prec || md.k < 0 ||
      md.k >= d || md.constants_precision < md.precision)
    return refuse("inconsistent certified root backing");
  if (!(o & 1)) return refuse("even order index");
  if (group <= 0 || requested.size() % (size_t)group) return refuse("invalid ownership groups");
  if (mm < 0 || mm >= d) return refuse("representative index");
  const double started = stamp();
  const slong precision = md.precision;
#ifdef SIC_ENABLE_TEST_HOOKS
  const bool inject = !direct_eta_fault.empty() && source % 2 == 0;
#endif
  try {
#ifdef SIC_ENABLE_TEST_HOOKS
    if (inject && direct_eta_fault == "admission")
      budget.require_additional("direct representative entries (injected)",
                                std::numeric_limits<sic::support::resources::Bytes>::max() / 4);
#endif
    prepare_direct_rows(
        sic::support::resources::add(
            sic::support::resources::mul(requested.size(),
                                         sic::support::resources::complex_bytes(wp)),
            DirectEtaRoot::workspace_bytes(d, std::max(precision, md.constants_precision), nth,
                                           requested.size())),
        precision);
  } catch (const sic::support::resources::AdmissionError& error) {
    return refuse(error.what());
  }
  // Ghost coordinates of a canonical class-0 point: inverse of Group::ghost_point.
  long oinv = 1;
  for (int it = 0; it < 7; ++it) oinv = (((oinv * (2 - (o % d) * oinv)) % d) + d) % d;
  if (((o % d) * oinv) % d != 1 % d) return refuse("order index inverse");
  const long shift = ((((*ar_).t + (*ar_).order_index * (*ar_).cls[0].cycle[0].B) / 2) % d + d) % d;
  auto ghost_coordinates = [&](sic::support::Index point, long& i, long& j) {
    if (point < 0 || point >= dd) return false;
    const long I = (point % dd) / d, J = point % d;
    i = (I * oinv) % d;
    j = ((J - shift * i) % d + d) % d;
    return (*group_).ghost_point(i, j) == point;
  };
  const sic::support::Index anchor = (*group_).ghost_point(0, 1),
                            basis = (*group_).ghost_point(1, 0);
  const long einv = (*group_).pow(e, -1);
  const auto& layout = *context.direct->layout;
  std::vector<DirectEtaPlan> plans(requested.size());
  std::mutex failure_mutex;
  std::string failure;
  std::atomic<bool> failed(false);
  std::atomic<long> conjugated(0);
  parallel_for((long)requested.size(), nth, [&](long position) {
    if (failed) return;
    auto fail = [&](const std::string& why) {
      std::lock_guard<std::mutex> lock(failure_mutex);
      if (failure.empty()) failure = why + " at element " + std::to_string(requested[position]);
      failed = true;
    };
#ifdef SIC_ENABLE_TEST_HOOKS
    if (inject && direct_eta_fault == "plan" && position == (long)requested.size() / 2)
      return fail("injected planning fault");
#endif
    const long g = requested[position];
    const sic::support::Index code =
        layout.encoded((*group_).act(g, anchor), context.direct->paired_values);
    if (!code) return fail("zero anchor");
    if (((code < 0 ? -(long)code : code) - 1) / layout.slots != source)
      return fail("class grouping");
    const bool conjugate = code < 0;
    long physical = g;
    if (conjugate) {
      if (cc_pre < 0) return fail("no conjugation element");
      physical = (*group_).mul(cc_pre, g);
      const sic::support::Index moved =
          layout.encoded((*group_).act(physical, anchor), context.direct->paired_values);
      if (moved <= 0 || (moved - 1) / layout.slots != source)
        return fail("conjugate does not reach the stored class");
      if ((o * ((*group_).nn[physical] + (*group_).nn[g])) % (2 * d) ||
          (*group_).chi[physical] != (*group_).chi[g])
        return fail("conjugation is not zeta -> zeta^-1 with chi = 1");
    }
    const long h = (*group_).mul(einv, physical);
    long B, matrix_d, A, C, i10, j10;
    if (!ghost_coordinates((*group_).act(h, anchor), B, matrix_d))
      return fail("relative map leaves class 0");
    A = ((d - 1) * B + matrix_d) % d;
    C = (d - B) % d;
    if (!ghost_coordinates((*group_).act(h, basis), i10, j10) ||
        (*group_).ghost_point(A, C) != (*group_).act(h, basis))
      return fail("relative map is not the epsilon-linear multiplication fixed by its (0,1) image");
    // Group elements act K-linearly by construction (fill_map); the two
    // basis images then determine the map. As an independent guard, compare
    // the whole requested line with the Group action for every 64th entry
    // (every entry in check mode).
    if (direct_eta_check || position % 64 == 0)
      for (long i = 0; i < d; ++i) {
        const long ip = ((A * i - B * mm) % d + d) % d, jp = ((C * i - matrix_d * mm) % d + d) % d;
        if ((*group_).act(h, (*group_).ghost_point(i, -mm)) != (*group_).ghost_point(ip, jp))
          return fail("relative map differs from the Group action on the requested line");
      }
    const long ng = (o * (*group_).nn[physical]) % (2 * d);
    const auto why = plan_direct_eta(d, md.n, ng, mm, A, B, C, matrix_d, (*group_).chi[physical],
                                     conjugate, plans[position]);
    if (!why.empty()) return fail(why);
    plans[position].position = position;
    if (conjugate) ++conjugated;
  });
  if (failed) {
    plans = std::vector<DirectEtaPlan>();
    trim_heap();
    return refuse(failure);
  }
  sic::support::ComplexBallVector values(requested.size());
  DirectEtaRoot::Census census;
  std::string unwind;
  {
    sic::support::ComplexBallVector packed = ghost.state->complex(), v(d), u(d);
    if (packed.size() != (size_t)(2 * d - 2))
      unwind = "root state size";
    else {
      for (long a = 0, q = 0; a < d; ++a) {
        if (a == md.k) {
          acb_one(v[a]);
          acb_one(u[a]);
        } else {
          acb_set(v[a], packed[q]);
          acb_set(u[a], packed[d - 1 + q]);
          ++q;
        }
      }
      // Roots of unity: one shared context at the largest constants
      // precision, rounded per class (rounding a ball is rigorous).
      if (!direct_eta_roots || direct_eta_roots->p < md.constants_precision) {
        slong top = md.constants_precision;
        for (const auto& other : table_backing)
          if (other.ghost) top = std::max(top, other.ghost->metadata.constants_precision);
        direct_eta_roots.reset();
        direct_eta_roots = std::make_unique<GhostRefiner::Ctx>(d, 1, top);
      }
      GhostRefiner::Ctx constants(*direct_eta_roots, precision);
      try {
#ifdef SIC_ENABLE_TEST_HOOKS
        if (failure_test_hooks::direct_eta) failure_test_hooks::direct_eta();
#endif
        DirectEtaRoot root(d, precision, std::move(v), std::move(u), constants.w2d);
#ifdef SIC_ENABLE_TEST_HOOKS
        root.test_fault_mid_rows = inject && direct_eta_fault == "evaluate";
        if (inject && direct_eta_fault == "stop")
          std::ofstream(producer_options.stop_file) << "injected\n";
#endif
        // The checksum authenticates every root ball; normalization placement
        // and the constants context complete the arithmetic generation. Root
        // dimension and working precision are independently part of each key.
        const std::string domain = ghost.state->digest() + ":" + std::to_string(md.k) + ":" +
                                   std::to_string(direct_eta_roots->p);
        census = root.evaluate(plans, values, wp, nth, direct_eta_rows.get(), domain);
#ifdef SIC_ENABLE_TEST_HOOKS
        if (inject && direct_eta_fault == "nonfinite" && values.size())
          acb_indeterminate(values[values.size() / 2]);
#endif
      } catch (const sic::Error& error) {
        if (error.kind() != sic::ErrorKind::precision && error.kind() != sic::ErrorKind::resource &&
            error.kind() != sic::ErrorKind::construction)
          throw;
        unwind = error.what();
      } catch (const std::runtime_error& error) {
        unwind = error.what();
      }
    }
  }
  plans = std::vector<DirectEtaPlan>();
  if (unwind.empty())
    for (size_t q = 0; q < values.size(); ++q)
      if (!acb_is_finite(values[q])) {
        unwind = "nonfinite direct representative entry";
        break;
      }
  if (!unwind.empty()) {
    values = sic::support::ComplexBallVector();
    trim_heap();
    return refuse(unwind);
  }
  const double evaluated = stamp();
  if (direct_eta_check) {
    sic::support::ComplexBallVector literal(requested.size());
    std::function<void(long, long, acb_srcptr)> capture =
        [&](long, long position, acb_srcptr value) { acb_set(literal[position], value); };
    sic::support::ComplexBallVector unused;
    context.load_class(source);
    eta_entry(context, mm, wp, roots, sqrt_d1, unused, nth, &requested, &capture, group);
    if (context.drop_class) context.drop_class(source);
    auto log2_radius = [](acb_srcptr z) {
      return std::max(mag_get_d_log2_approx(arb_radref(acb_realref(z))),
                      mag_get_d_log2_approx(arb_radref(acb_imagref(z))));
    };
    double worst_direct = -1e300, worst_literal = -1e300;
    for (size_t q = 0; q < values.size(); ++q) {
      if (!acb_overlaps(values[q], literal[q]))
        throw sic::Error(sic::ErrorKind::construction,
                         "direct representative entry does not overlap the literal entry");
      worst_direct = std::max(worst_direct, log2_radius(values[q]));
      worst_literal = std::max(worst_literal, log2_radius(literal[q]));
    }
    sic::support::progressf(
        "direct eta literal check passed: class=%ld eta=%ld entries=%zu log2_radius direct=%.1f "
        "literal=%.1f\n",
        (long)source, mm, values.size(), worst_direct, worst_literal);
  }
  const long units = (long)requested.size() / group;
  parallel_for(units, nth, [&](long unit) {
    for (long position = unit * group; position < (unit + 1) * group; ++position)
      consume(requested[position], position, values[position]);
  });
  if (producer_options.progress)
    sic::support::progressf(
        "direct eta class: class=%ld eta=%ld entries=%zu conjugate=%ld rows=%zu row_cells=%llu "
        "scalar=%zu "
        "precision=%ld hits=%zu evictions=%zu cache_bytes=%llu evaluate=%.2fs total=%.2fs "
        "[%.1fs]\n",
        (long)source, mm, values.size(), (long)conjugated, census.rows, census.row_cells,
        census.scalar, (long)precision, census.hits, census.evictions,
        (unsigned long long)(direct_eta_rows ? direct_eta_rows->bytes() : 0), evaluated - started,
        stamp() - started, stamp());
  values = sic::support::ComplexBallVector();
  trim_heap();  // Budget admission is RSS based; return the freed class workspace.
  return true;
}

void Construction::class_entries(const EntryCtx& context, sic::support::Index source, long mm,
                                 slong wp, const sic::support::ComplexBallVector& roots,
                                 const arb_t sqrt_d1, const std::vector<long>& requested,
                                 const std::function<void(long, long, acb_srcptr)>& consume,
                                 long group) {
  if (context.load_class &&
      direct_eta_class(context, source, mm, wp, roots, sqrt_d1, requested, consume, group))
    return;
  sic::support::ComplexBallVector unused;
  if (context.load_class) context.load_class(source);
  eta_entry(context, mm, wp, roots, sqrt_d1, unused, nth, &requested, &consume, group);
  if (context.drop_class) context.drop_class(source);
}
}  // namespace sic::construct
