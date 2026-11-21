// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/group/group.hpp"

#include "exact/runtime/parallel.hpp"
#include "exact/runtime/row_worker_team.hpp"
#include "sic/error.hpp"
#include "sic/support/flint_raii.hpp"
#include "support/resources.hpp"

namespace sic::exact {
long kronecker_si(long a, long n) {
  fmpz_t A, N;
  fmpz_init_set_si(A, a);
  fmpz_init_set_si(N, n);
  int r = fmpz_kronecker(A, N);
  fmpz_clear(A);
  fmpz_clear(N);
  return r;
}

Group::Group(const Arithmetic& a)
    : ar(a),
      d(a.d),
      npts(sic::support::resources::mul(a.h(), sic::support::resources::mul(a.d, a.d))) {
  sic::support::admit_index_count(npts);
  long m = d + 1;
  for (long p = 2; p * p <= m; ++p)
    while (m % (p * p) == 0) m /= p * p;
  D1 = (m == 1) ? 1 : ((m % 4 == 1) ? m : 4 * m);
  // E_{J, eps^3}(eps x) = E_{J, eps^3}(x): points are taken modulo multiplication by eps
  GElem e;
  e.perm.assign(npts, -1);
  orbit.resize(npts);
  std::iota(orbit.begin(), orbit.end(), sic::support::Index{0});
  for (sic::support::Index c = 0; c < ar.h(); ++c) fill_map(e, c, c, ar.eps);
  for (long x = 0; x < npts; ++x) {
    long y = e.perm[x], z = e.perm[y];
    if (e.perm[z] != x)
      throw sic::Error(sic::ErrorKind::construction, "eps does not have order 3 on points");
    orbit[x] = sic::support::checked_index(std::min({x, y, z}));
  }
  orbit_ready = true;
}

int Group::chi_of_norm(long Nm) const {
  if (D1 == 1) return 1;
  long k = kronecker_si(D1, Nm);
  if (k == 0) throw std::runtime_error("chi_of_norm: norm not prime to d+1");
  return (int)k;
}

void Group::generate_standard() {
  gens.clear();
  long candidates[][2] = {{3, 0}, {5, 0}, {7, 0}, {1, 2}, {3, 2},
                          {1, 4}, {5, 2}, {3, 4}, {7, 2}, {1, 6}};
  for (auto& candidate : candidates) {
    QuadraticNumber xi = add(QuadraticNumber(candidate[0]), scal(Rational(candidate[1]), ar.eps));
    if (!totally_positive(ar.kc, xi)) continue;
    Rational value = norm(ar.kc, xi);
    const long integer_norm = fmpz_get_si(fmpq_numref(value.v));
    if (std::gcd(integer_norm, 2 * ar.Delta) != 1) continue;
    gens.push_back(principal_gen(candidate[0], candidate[1]));
  }
  sic::support::Index count = 0;
  for (long p = 3; p < 400 && count < 4; p += 2) {
    bool prime = true;
    for (long q = 2; q * q <= p; ++q)
      if (p % q == 0) prime = false;
    if (!prime || ar.original_Delta % p == 0 || kronecker_si(ar.Delta, p) != 1) continue;
    gens.push_back(prime_gen(p));
    ++count;
  }
}

void Group::fill_map(GElem& g, sic::support::Index c, sic::support::Index c2,
                     const QuadraticNumber& m) const {
  const QuadraticContext& kc = ar.kc;
  auto [ut, vt] = ar.coords(c2, sic::exact::mul(kc, m, scal(Rational(1, d), ar.cls[c].tau[0])));
  auto [u1, v1] =
      ar.coords(c2, sic::exact::mul(kc, m, QuadraticNumber(Rational(1, d), Rational(0))));
  const long Ut = resid(ut, d), Vt = resid(vt, d), U1 = resid(u1, d), V1 = resid(v1, d);
  for (long i = 0; i < d; ++i)
    for (long j = 0; j < d; ++j) {
      long im = pidx(c2, (i * Ut + j * U1) % d, (i * Vt + j * V1) % d);
      g.perm[pidx(c, i, j)] = orbit_ready ? orbit[im] : sic::support::checked_index(im);
    }
}

GElem Group::principal_gen(long a, long b) const {  // xi = a + b eps, totally positive
  const QuadraticContext& kc = ar.kc;
  QuadraticNumber xi = add(QuadraticNumber(a), scal(Rational(b), ar.eps));
  if (!totally_positive(kc, xi)) throw std::runtime_error("xi not totally positive");
  Rational Nq = norm(kc, xi);
  long Nl = fmpz_get_si(fmpq_numref(Nq.v));
  GElem g;
  g.perm.assign(npts, -1);
  for (sic::support::Index c = 0; c < ar.h(); ++c) fill_map(g, c, c, xi);
  g.n = ((Nl % (2 * d)) + 2 * d) % (2 * d);
  g.chi = chi_of_norm(Nl);
  return g;
}

GElem Group::prime_gen(long p) const {  // split prime p, P = (p, tau - r); sigma_P: J -> P^{-1} J
  const QuadraticContext& kc = ar.kc;
  const Form& f = ar.cls[0].cycle[0];
  long tr = -f.B, r = -1;
  for (long x = 0; x < p; ++x)
    if (((x * x - tr * x + f.C) % p + p) % p == 0) {
      r = x;
      break;
    }
  if (r < 0) throw std::runtime_error("prime_gen: p not split");
  long rc = ((tr - r) % p + p) % p;                                            // conjugate root
  Lattice Pc{QuadraticNumber(p), sub(ar.cls[0].tau[0], QuadraticNumber(rc))};  // conjugate ideal P'
  GElem g;
  g.perm.assign(npts, -1);
  for (sic::support::Index c = 0; c < ar.h(); ++c) {
    Lattice L = lattice_scale(kc, QuadraticNumber(Rational(1, p), Rational(0)),
                              lattice_mul(kc, Pc, ar.class_involution(c)));
    auto [c2, mu] = ar.identify(L);
    fill_map(g, c, c2, inv(kc, mu));
  }
  g.n = p % (2 * d);
  g.chi = chi_of_norm(p);
  return g;
}

std::vector<long> Group::coord(long g) const {
  std::vector<long> c(invf.size());
  for (size_t l = 0; l < c.size(); ++l) c[l] = digit(g, l);
  return c;
}

long Group::index_of(const std::vector<long>& c) const {
  long g = 0;
  for (size_t l = 0; l < invf.size(); ++l)
    g += (((c[l] % invf[l]) + invf[l]) % invf[l]) * stride[l];
  return g;
}

long Group::mul(long a, long b) const {
  long g = 0;
  for (size_t l = 0; l < invf.size(); ++l) g += ((digit(a, l) + digit(b, l)) % invf[l]) * stride[l];
  return g;
}

long Group::pow(long a, long k) const {
  long g = 0;
  for (size_t l = 0; l < invf.size(); ++l)
    g += ((((digit(a, l) * k) % invf[l]) + invf[l]) % invf[l]) * stride[l];
  return g;
}

long Group::order(long a) const {
  long o = 1;
  for (size_t l = 0; l < invf.size(); ++l) {
    long n = invf[l];
    o = std::lcm(o, n / std::gcd(digit(a, l), n));
  }
  return o;
}

long Group::element_of(const GElem& g) const {
  const size_t B = nb();
  for (long e = 0; e < N; ++e) {
    if (nn[e] != g.n || chi[e] != g.chi) continue;
    bool same = true;
    for (size_t b = 0; b < B && same; ++b) same = img[e * B + b] == g.perm[base[b]];
    if (same) return e;
  }
  throw std::runtime_error("element_of: not in the group");
}

void Group::close(int requested_workers, long element_limit,
                  const std::function<void(size_t, size_t)>& before_bfs,
                  const std::function<void(size_t)>& progress) {
  if (requested_workers <= 0) throw std::invalid_argument("positive group worker count required");
  sic::support::admit_index_count(element_limit > 0 ? element_limit : 0);
  if (element_limit <= 0) throw std::invalid_argument("positive group element cap required");
  const size_t k = gens.size();
  // 1. base points: one canonical point per orbit of <gens>
  std::vector<sic::support::Index> par(npts);
  std::iota(par.begin(), par.end(), sic::support::Index{0});
  auto find = [&](sic::support::Index x) {
    while (par[x] != x) {
      par[x] = par[par[x]];
      x = par[x];
    }
    return x;
  };
  for (long x = 0; x < npts; ++x) {
    if (orbit[x] != x) continue;
    for (auto& g : gens) {
      sic::support::Index a = find((sic::support::Index)x), b = find(g.perm[x]);
      if (a != b) par[std::max(a, b)] = std::min(a, b);
    }
  }
  base.clear();
  for (long x = 0; x < npts; ++x)
    if (orbit[x] == x && find((sic::support::Index)x) == x) base.push_back((sic::support::Index)x);
  const size_t B = base.size();
  std::vector<sic::support::Index>().swap(par);
  if (before_bfs) before_bfs(B, k);
  // 2. BFS over elements, keyed by (images of the base points, n, chi). Elements are stored flat
  // (images, n, chi,
  //    exponent vector over the generators) and found by open addressing on a 64-bit hash. The
  //    neighbour index of every (element, generator) is kept; relations are fed to the Hermite
  //    form only after the BFS, in batches, until the relation lattice has index |G'| (then it is
  //    the full relation lattice: it is contained in it).
  std::vector<sic::support::Index> eim(base.begin(), base.end());
  std::vector<long> en{1};
  std::vector<int> echi{1};
  // BFS words have at most |G|-1 letters; nonnegative exponents fit the
  // same sic::support::Index cap as element indices. Relations still use long.
  std::vector<sic::support::Index> expo(k, 0);
  std::vector<sic::support::Index> nbr;
  const bool bounded = element_limit != std::numeric_limits<sic::support::Index>::max();
  using sic::support::resources::add;
  using sic::support::resources::mul;
  // Uncapped callers retain the serial grow-on-demand path. Production supplies
  // its admitted exact degree, so every parallel owner can be forecast first.
  int workers = 1;
  size_t tile_rows = 0;
  size_t hash_size = bounded ? 16 : 1 << 16;
  if (bounded)
    while (hash_size < mul(element_limit, 2)) hash_size = mul(hash_size, 2);
  if (bounded && k) {
    const sic::support::resources::Budget budget;
    workers = static_cast<int>(
        std::min<long>(requested_workers, std::min<long>(nested_loop_budget, element_limit)));
    // Fixed ordered slots are disjoint worker candidate lists; no frontier-sized
    // allocation and no sorting/atomic insertion are needed. Hashes are expanded
    // with the images so the owner only performs the ordered hash-table probe.
    const auto candidate_bytes = add(mul(B, sizeof(sic::support::Index)), sizeof(uint64_t));
    const auto row_bytes = mul(k, candidate_bytes);
    if (workers > 1 && row_bytes <= 4 * sic::support::resources::kMib) {
      tile_rows = std::min<sic::support::resources::Count>(
          element_limit, 4 * sic::support::resources::kMib / row_bytes);
      const auto arrays = add(mul(element_limit, add(mul(B + 2 * k, sizeof(sic::support::Index)),
                                                     sizeof(long) + sizeof(int))),
                              mul(hash_size, sizeof(sic::support::Index)));
      const auto scratch = add(mul(tile_rows, row_bytes), mul(B + k, sizeof(sic::support::Index)));
      budget.require_additional(
          "parallel group closure arrays/candidates/workers",
          add(arrays, add(scratch, mul(workers, sic::support::resources::kMib))));
    } else {
      workers = 1;
    }
  }
  if (bounded) {
    eim.reserve(sic::support::resources::mul(element_limit, B));
    en.reserve(element_limit);
    echi.reserve(element_limit);
    expo.reserve(sic::support::resources::mul(element_limit, k));
    nbr.reserve(sic::support::resources::mul(element_limit, k));
  }
  auto hkey = [&](const sic::support::Index* im, long n, int ch) {
    uint64_t h = 1469598103934665603ULL ^ (uint64_t)(n * 3 + (ch > 0));
    for (size_t b = 0; b < B; ++b) {
      h ^= static_cast<uint64_t>(static_cast<std::make_unsigned_t<sic::support::Index>>(im[b]));
      h *= 1099511628211ULL;
      h ^= h >> 29;
    }
    return h;
  };
  std::vector<sic::support::Index> slot(hash_size, -1);
  auto find_or_add = [&](const sic::support::Index* im, long n, int ch, uint64_t hash,
                         bool& added) -> sic::support::Index {
    const size_t mask = slot.size() - 1;
    size_t pos = (size_t)hash & mask;
    for (;; pos = (pos + 1) & mask) {
      const sic::support::Index e = slot[pos];
      if (e < 0) break;
      if (en[e] == n && echi[e] == ch && std::equal(im, im + B, &eim[(size_t)e * B])) {
        added = false;
        return e;
      }
    }
    // Point and BFS indices are signed ints; reject only their actual
    // representation limit.
    if (en.size() >=
        (size_t)std::min<long>(element_limit, std::numeric_limits<sic::support::Index>::max()))
      throw sic::Error(sic::ErrorKind::resource,
                       "group exceeds its admitted element/index capacity");
    const sic::support::Index e = sic::support::checked_index(en.size());
    eim.insert(eim.end(), im, im + B);
    en.push_back(n);
    echi.push_back(ch);
    slot[pos] = e;
    added = true;
    if (2 * en.size() > slot.size()) {  // rehash
      std::vector<sic::support::Index> ns(slot.size() * 2, -1);
      const size_t m2 = ns.size() - 1;
      for (sic::support::Index q = 0; q < (sic::support::Index)en.size(); ++q) {
        size_t ps = (size_t)hkey(&eim[(size_t)q * B], en[q], echi[q]) & m2;
        while (ns[ps] >= 0) ps = (ps + 1) & m2;
        ns[ps] = q;
      }
      slot.swap(ns);
    }
    return e;
  };
  {
    bool a;
    en.clear();
    echi.clear();
    eim.clear();
    find_or_add(base.data(), 1, 1, hkey(base.data(), 1, 1), a);
  }
  std::vector<sic::support::Index> nk(B);
  std::vector<sic::support::Index> ex(k);
  const size_t progress_stride = std::max<size_t>(1, bounded ? element_limit / 16 : 1000000);
  auto insert = [&](size_t q, size_t gi, const sic::support::Index* images, uint64_t hash) {
    const long n2 = (gens[gi].n * en[q]) % (2 * d);
    const int c2 = gens[gi].chi * echi[q];
    bool added;
    const auto e = find_or_add(images, n2, c2, hash, added);
    if (added) {
      std::copy_n(expo.data() + q * k, k, ex.data());
      if (ex[gi] == std::numeric_limits<sic::support::Index>::max())
        throw std::overflow_error("group BFS exponent capacity");
      ex[gi] += 1;
      expo.insert(expo.end(), ex.begin(), ex.end());
    }
    nbr.push_back(e);
  };
  auto report = [&](size_t q) {
    if (q % progress_stride != 0) return;
    sic::support::check_stop();
    if (progress) progress(q);
    sic::support::check_stop();
  };
  if (workers == 1) {
    for (size_t q = 0; q < en.size(); ++q) {
      report(q);
      for (size_t gi = 0; gi < k; ++gi) {
        const GElem& g = gens[gi];
        for (size_t b = 0; b < B; ++b) nk[b] = g.perm[eim[q * B + b]];
        insert(q, gi, nk.data(), hkey(nk.data(), (g.n * en[q]) % (2 * d), g.chi * echi[q]));
      }
    }
  } else {
    // These owners die before relation/Smith workspace is allocated. The team
    // inherits scoped settings/cancellation, joins on failure, and is reused.
    std::vector<sic::support::Index> candidates(mul(mul(tile_rows, k), B));
    std::vector<uint64_t> hashes(mul(tile_rows, k));
    RowWorkerTeam team(workers);
    for (size_t first = 0; first < en.size();) {
      const size_t level_end = en.size();
#ifdef SIC_ENABLE_TEST_HOOKS
      if (closure_trace)
        closure_trace->max_level_tiles = std::max(closure_trace->max_level_tiles,
                                                  (level_end - first + tile_rows - 1) / tile_rows);
#endif
      for (size_t begin = first; begin < level_end;) {
        sic::support::check_stop();
        const size_t rows = std::min(tile_rows, level_end - begin);
        team.for_each_indexed(rows, [&](long row, int worker) {
          const size_t q = begin + row;
          if (q % progress_stride == 0) {
#ifdef SIC_ENABLE_TEST_HOOKS
            if (closure_trace && closure_trace->worker_stop_check)
              closure_trace->worker_stop_check(q, worker, false);
#endif
            sic::support::check_stop();
#ifdef SIC_ENABLE_TEST_HOOKS
            if (closure_trace && closure_trace->worker_stop_check)
              closure_trace->worker_stop_check(q, worker, true);
#endif
          }
          (void)worker;
          for (size_t gi = 0; gi < k; ++gi) {
            const GElem& g = gens[gi];
            auto* images = candidates.data() + (row * k + gi) * B;
            for (size_t b = 0; b < B; ++b) images[b] = g.perm[eim[q * B + b]];
            hashes[row * k + gi] = hkey(images, (g.n * en[q]) % (2 * d), g.chi * echi[q]);
          }
        });
        // No worker retains an array view here. Insertions may grow the source
        // arrays, but never alter any source row or candidate already expanded.
        for (size_t row = 0; row < rows; ++row) {
          const size_t q = begin + row;
          report(q);
          for (size_t gi = 0; gi < k; ++gi)
            insert(q, gi, candidates.data() + (row * k + gi) * B, hashes[row * k + gi]);
        }
        begin += rows;
      }
      first = level_end;
    }
  }
#ifdef SIC_ENABLE_TEST_HOOKS
  if (closure_trace) {
    closure_trace->images = eim;
    closure_trace->norms = en;
    closure_trace->signs = echi;
    closure_trace->exponents = expo;
    closure_trace->neighbours = nbr;
  }
#endif
  slot.clear();
  slot.shrink_to_fit();
  const long Nel = (long)en.size();
  // relation lattice in Hermite form (k x k), relations added in batches until its determinant is
  // |G'|
  std::vector<std::vector<long>> M;
  {
    sic::support::flint::FmpzMat H(k, k);
    std::vector<std::vector<long>> batch;
    auto flush = [&]() {
      if (batch.empty()) return;
      {
        sic::support::flint::FmpzMat M(k + batch.size(), k), Hn(k + batch.size(), k);
        for (size_t i = 0; i < k; ++i)
          for (size_t j = 0; j < k; ++j) fmpz_set(fmpz_mat_entry(M, i, j), fmpz_mat_entry(H, i, j));
        for (size_t r = 0; r < batch.size(); ++r)
          for (size_t j = 0; j < k; ++j) fmpz_set_si(fmpz_mat_entry(M, k + r, j), batch[r][j]);
        fmpz_mat_hnf(Hn, M);
        for (size_t i = 0; i < k; ++i)
          for (size_t j = 0; j < k; ++j)
            fmpz_set(fmpz_mat_entry(H, i, j), fmpz_mat_entry(Hn, i, j));
      }
      batch.clear();
    };
    auto full_index = [&]() {  // det H == |G'| (H upper triangular after hnf)
      fmpz_t det;
      fmpz_init_set_ui(det, 1);
      for (size_t i = 0; i < k; ++i) fmpz_mul(det, det, fmpz_mat_entry(H, i, i));
      fmpz_abs(det, det);
      const bool ok = fmpz_cmp_si(det, Nel) == 0;
      fmpz_clear(det);
      return ok;
    };
    size_t bsz = 256;
    bool done_rel = false;
    for (size_t q = 0; q < (size_t)Nel && !done_rel; ++q)
      for (size_t gi = 0; gi < k; ++gi) {
        const sic::support::Index e = nbr[q * k + gi];
        std::vector<long> r(k);
        bool zero = true;
        for (size_t i = 0; i < k; ++i) {
          r[i] = (long)expo[q * k + i] + (i == gi) - expo[(size_t)e * k + i];
          if (r[i]) zero = false;
        }
        if (zero) continue;
        batch.push_back(std::move(r));
        if (batch.size() >= bsz) {
          flush();
          bsz = std::min<size_t>(4096, bsz * 2);
          if (full_index()) {
            done_rel = true;
            break;
          }
        }
      }
    flush();
    if (!full_index())
      throw sic::Error(sic::ErrorKind::construction,
                       "close: relation lattice does not have index |G'|");
    std::vector<sic::support::Index>().swap(nbr);
    M = std::vector<std::vector<long>>(k, std::vector<long>(k, 0));
    for (size_t i = 0; i < k; ++i)
      for (size_t j = 0; j < k; ++j) M[i][j] = fmpz_get_si(fmpz_mat_entry(H, i, j));
  }
#ifdef SIC_ENABLE_TEST_HOOKS
  if (closure_trace) closure_trace->relations = M;
#endif
  std::vector<std::vector<long>> V;
  smith(M, V);
#ifdef SIC_ENABLE_TEST_HOOKS
  if (closure_trace) {
    closure_trace->smith = M;
    closure_trace->transform = V;
  }
#endif
  // 3. coordinates and mixed-radix reindexing
  std::vector<size_t> keep;
  invf.clear();
  for (size_t p = 0; p < k; ++p) {
    long v = std::labs(M[p][p]);
    if (v != 1) {
      invf.push_back(v);
      keep.push_back(p);
    }
  }
  stride.assign(invf.size(), 1);
  for (size_t l = 1; l < invf.size(); ++l) stride[l] = stride[l - 1] * invf[l - 1];
  N = 1;
  for (long v : invf) N *= v;
  if (N != Nel)
    throw sic::Error(sic::ErrorKind::construction, "close: |G| does not match the Smith form");
  std::vector<sic::support::Index> destination(N);
  std::vector<char> seen(N, 0);
  for (size_t e = 0; e < (size_t)Nel; ++e) {
    long g = 0;
    for (size_t l = 0; l < keep.size(); ++l) {
      long s = 0;
      for (size_t i = 0; i < k; ++i) s += expo[e * k + i] * V[i][keep[l]];
      g += (((s % invf[l]) + invf[l]) % invf[l]) * stride[l];
    }
    if (seen[g]) throw sic::Error(sic::ErrorKind::construction, "close: coordinates not injective");
    seen[g] = 1;
    destination[e] = (sic::support::Index)g;
  }
#ifdef SIC_ENABLE_TEST_HOOKS
  if (closure_trace) closure_trace->destination = destination;
#endif
  std::vector<sic::support::Index>().swap(expo);
  std::fill(seen.begin(), seen.end(), 0);
  // Permute complete rows and scalar actions together using the checked
  // bijection, without a second image array or norm/sign copies.
  std::vector<sic::support::Index> row(B);
  for (long start = 0; start < N; ++start) {
    if (seen[start]) continue;
    std::copy_n(eim.data() + start * B, B, row.data());
    long norm_value = en[start];
    int sign_value = echi[start];
    long current = start;
    do {
      const long next = destination[current];
      for (size_t b = 0; b < B; ++b) std::swap(row[b], eim[next * B + b]);
      std::swap(norm_value, en[next]);
      std::swap(sign_value, echi[next]);
      seen[current] = 1;
      current = next;
    } while (current != start);
  }
  img = std::move(eim);
  nn = std::move(en);
  chi = std::move(echi);
  std::vector<sic::support::Index>().swap(destination);
  if (nn[0] != 1 || chi[0] != 1 || img[0] != base[0])
    throw sic::Error(sic::ErrorKind::construction, "close: index 0 is not the identity");
  // 4. representatives for the action on arbitrary canonical points
  rep_elem.assign(npts, -1);
  rep_base.assign(npts, -1);
  for (long g = 0; g < N; ++g)
    for (size_t b = 0; b < B; ++b) {
      sic::support::Index x = img[g * B + b];
      if (rep_elem[x] < 0) {
        rep_elem[x] = (sic::support::Index)g;
        rep_base[x] = (sic::support::Index)b;
      }
    }
  for (long x = 0; x < npts; ++x)
    if (orbit[x] == x && rep_elem[x] < 0)
      throw sic::Error(sic::ErrorKind::construction, "close: point not reached");
  // consistency check of the representation against the generator permutations on all points
  for (size_t gi = 0; gi < k; ++gi) {
    long g = index_of_gen(gi, V, keep);
    for (long x = 0; x < npts; ++x)
      if (orbit[x] == x && act(g, (sic::support::Index)x) != gens[gi].perm[x])
        throw sic::Error(sic::ErrorKind::construction, "close: action check failed");
  }
}

long Group::index_of_gen(size_t gi, const std::vector<std::vector<long>>& V,
                         const std::vector<size_t>& keep) const {
  long g = 0;
  for (size_t l = 0; l < keep.size(); ++l)
    g += (((V[gi][keep[l]] % invf[l]) + invf[l]) % invf[l]) * stride[l];
  return g;
}

void Group::smith(std::vector<std::vector<long>>& M, std::vector<std::vector<long>>& V) {
  size_t k = M.size();
  V.assign(k, std::vector<long>(k, 0));
  for (size_t i = 0; i < k; ++i) V[i][i] = 1;
  auto colop = [&](size_t a, size_t b, long q) {  // col a -= q col b
    for (size_t i = 0; i < k; ++i) {
      M[i][a] -= q * M[i][b];
      V[i][a] -= q * V[i][b];
    }
  };
  auto colswap = [&](size_t a, size_t b) {
    for (size_t i = 0; i < k; ++i) {
      std::swap(M[i][a], M[i][b]);
      std::swap(V[i][a], V[i][b]);
    }
  };
  auto rowop = [&](size_t a, size_t b, long q) {
    for (size_t j = 0; j < k; ++j) M[a][j] -= q * M[b][j];
  };
  auto rowswap = [&](size_t a, size_t b) { std::swap(M[a], M[b]); };
  for (size_t p = 0; p < k; ++p) {
    for (;;) {
      long best = 0;
      size_t bi = p, bj = p;
      for (size_t i = p; i < k; ++i)
        for (size_t j = p; j < k; ++j)
          if (M[i][j] != 0 && (best == 0 || std::labs(M[i][j]) < best)) {
            best = std::labs(M[i][j]);
            bi = i;
            bj = j;
          }
      if (best == 0) break;
      rowswap(p, bi);
      colswap(p, bj);
      bool clean = true;
      for (size_t i = p + 1; i < k; ++i)
        if (M[i][p]) {
          long q = M[i][p] / M[p][p];
          rowop(i, p, q);
          if (M[i][p]) clean = false;
        }
      for (size_t j = p + 1; j < k; ++j)
        if (M[p][j]) {
          long q = M[p][j] / M[p][p];
          colop(j, p, q);
          if (M[p][j]) clean = false;
        }
      if (!clean) continue;
      bool div = true;
      for (size_t i = p + 1; i < k && div; ++i)
        for (size_t j = p + 1; j < k; ++j)
          if (M[i][j] % M[p][p]) {
            rowop(p, i, -1);
            div = false;
            break;
          }
      if (div) break;
    }
  }
}

}  // namespace sic::exact
