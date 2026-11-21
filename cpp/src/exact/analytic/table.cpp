// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/analytic/table.hpp"

namespace sic::exact {
Table::Table(const Arithmetic& a, slong p, int nth) : ar(a), prec(p), d(a.d), nthreads(nth) {
  uint64_t state_count = 0;
  for (const auto& cls : ar.cls)
    state_count = sic::support::resources::add(state_count, cls.digit.size());
  sic::support::admit_index_count(state_count);
  sic::support::Index ns = 0;
  for (sic::support::Index c = 0; c < ar.h(); ++c) {
    soff.push_back(ns);
    for (size_t l = 0; l < ar.cls[c].digit.size(); ++l) {
      s_c.push_back(c);
      s_l.push_back(sic::support::checked_index(l));
      ++ns;
    }
  }
}

Table::~Table() {
  for (auto& v : val)
    for (auto& z : v) acb_clear(&z);
  for (auto& s : sd) {
    for (auto& z : s.L) acb_clear(&z);
    for (auto& z : s.S) acb_clear(&z);
  }
  for (auto& kv : gl_cache)
    for (auto& pr : kv.second) {
      arb_clear(&pr.abscissa);
      arb_clear(&pr.weight);
    }
}

void Table::enable_compact(const std::shared_ptr<OrbitTableLayout>& labels) {
  if (!val.empty()) throw std::logic_error("compact layout must precede table allocation");
  layout = labels;
  packed.resize(ar.h());
  val.resize(ar.h());
}

Table::Reference Table::reference(long point) const {
  if (!layout) return {&val.at(point / (d * d)).at(point % (d * d)), false};
  const sic::support::Index encoded = layout->encoded(point, paired_values);
  if (!encoded) return {zero_value[0], false};
  const long index = (encoded < 0 ? -(long)encoded : encoded) - 1;
  const long c = index / layout->slots, slot = index % layout->slots;
  if (packed[c].size() != (size_t)layout->slots)
    throw std::out_of_range("read of an unstored analytic class");
  return {packed[c][slot], encoded < 0};
}

void Table::read(acb_ptr out, long point) const {
  const auto source = reference(point);
  if (source.conjugate)
    acb_conj(out, source.value);
  else
    acb_set(out, source.value);
}

acb_ptr Table::writable(long point) {
  if (!layout) return &val.at(point / (d * d)).at(point % (d * d));
  const sic::support::Index encoded = layout->encoded(point, false);
  if (!encoded) throw std::out_of_range("zero point is not a stored analytic value");
  const long index = encoded - 1, c = index / layout->slots;
  if (!packed[c].size()) packed[c] = sic::support::ComplexBallVector(layout->slots);
  return packed[c][index % layout->slots];
}

void Table::pack_class(sic::support::Index c) {
  if (!layout) return;
  if (!packed[c].size()) packed[c] = sic::support::ComplexBallVector(layout->slots);
  for (long q = 1; q < d * d; ++q) {
    const long point = c * d * d + q;
    if (layout->canonical[point] == point) acb_swap(writable(point), &val[c][q]);
  }
  for (auto& value : val[c]) acb_clear(&value);
  std::vector<acb_struct>().swap(val[c]);
}

void Table::reduced_value(acb_t res, sic::support::Index c, long i, long j) const {
  const ClassRep& cr = ar.cls[c];
  const sic::support::Index r = sic::support::checked_index(cr.digit.size());
  const slong wp = prec + 20;
  long x = ((i % d) + d) % d;
  if (x == 0) x = d;           // x in (0, 1]  -> numerator in [1, d]
  long y = ((j % d) + d) % d;  // y in [0, 1)  -> numerator in [0, d)
  sic::support::flint::Acb Lsum, Sprod;
  acb_one(Sprod);
  long bsum = 0;
  for (sic::support::Index l = 0; l < 3 * r; ++l) {
    const sic::support::Index lm = l % r;
    const sic::support::Index sid = soff[c] + lm;
    const long id = (x - 1) * d + y;
    acb_add(Lsum, Lsum, &sd[sid].L[id], wp);
    acb_mul(Sprod, Sprod, &sd[sid].S[id], wp);
    const long bb = cr.digit[lm];
    if (l < r) bsum += bb - 3;
    long xn = bb * x + y, yn = -x;
    xn = ((xn % d) + d) % d;
    if (xn == 0) xn = d;
    yn = ((yn % d) + d) % d;
    x = xn;
    y = yn;
  }
  acb_exp(res, Lsum, wp);
  acb_mul(res, res, Sprod, wp);
  sic::support::flint::Acb mu;
  eighth_root(mu, bsum, wp);
  acb_mul(res, res, mu, prec);
}

void Table::build_batched() {
  const sic::support::Index ns = nstates();
  sd.assign(ns, SData());
  alloc_values();
  n_nodes_total = 0;
  for (sic::support::Index c = 0; c < ar.h(); ++c) build_one_class(c);
  n_integrals = (long)ns * d * d;
}

void Table::build_selected(const std::vector<char>& want) {
  sd.assign(nstates(), SData());
  alloc_values();
  n_nodes_total = 0;
  for (sic::support::Index c = 0; c < ar.h(); ++c)
    if (want[c]) build_one_class(c);
  n_integrals = 0;
  for (sic::support::Index c = 0; c < ar.h(); ++c)
    if (want[c]) n_integrals += (long)ar.cls[c].digit.size() * d * d;
}

void Table::build_class(sic::support::Index c) {
  if (c < 0 || c >= ar.h()) throw std::out_of_range("table class index");
  if (sd.empty()) sd.assign(nstates(), SData());
  if (val.empty()) val.resize(ar.h());
  if (val[c].empty()) {
    val[c].resize(d * d);
    for (auto& z : val[c]) acb_init(&z);
  }
  build_one_class(c);
}

void Table::build_one_class(sic::support::Index c) {
  if (val[c].empty()) {
    val[c].resize(d * d);
    for (auto& value : val[c]) acb_init(&value);
  }
  if (stream_states) {
    build_one_class_streamed(c);
    pack_class(c);
    if (class_completed) class_completed(c);
    return;
  }
  {
    const sic::support::Index s0 = soff[c], r = sic::support::checked_index(ar.cls[c].digit.size());
    parallel_for(r, nthreads, [&](long l) { batch_state(s0 + (sic::support::Index)l); });
    parallel_for(d, nthreads, [&](long i) {
      for (long j = 0; j < d; ++j)
        if (i != 0 || j != 0) reduced_value(&val[c][i * d + j], c, i, j);
    });
    for (sic::support::Index l = 0; l < r; ++l) {
      SData& st = sd[s0 + l];
      n_nodes_total += st.nodes;
      for (auto& z : st.L) acb_clear(&z);
      for (auto& z : st.S) acb_clear(&z);
      st.L.clear();
      st.S.clear();
      st.L.shrink_to_fit();
      st.S.shrink_to_fit();
    }
  }
  pack_class(c);
  if (class_completed) class_completed(c);
}

void Table::build_one_class_streamed(sic::support::Index c) {
  const auto& digits = ar.cls[c].digit;
  const sic::support::Index s0 = soff[c];
  const slong wp = prec + 20;
  const auto period = cycle_matrix(digits, d);
  GridMatrix prefix;
  std::vector<acb_struct> product(d * d);
  struct Clear {
    std::vector<acb_struct>& values;
    ~Clear() {
      for (auto& z : values) acb_clear(&z);
    }
  } clear{product};
  for (long q = 0; q < d * d; ++q) {
    acb_init(&product[q]);
    acb_one(&product[q]);
    acb_zero(&val[c][q]);
  }
  long multiplier = 0;
  for (size_t l = 0; l < digits.size(); ++l) {
    const sic::support::Index sid = s0 + (sic::support::Index)l;
    batch_state(sid);
    const auto visits = cycle_visits(prefix, period, d);
    parallel_for(d * d - 1, nthreads, [&](long p) {
      const long q = p + 1, i = q / d, j = q % d;
      for (const auto& visit : visits) {
        const auto [xr, y] = visit.apply(i, j, d);
        const long x = xr ? xr : d;
        const long index = (x - 1) * d + y;
        acb_add(&val[c][q], &val[c][q], &sd[sid].L[index], wp);
        acb_mul(&product[q], &product[q], &sd[sid].S[index], wp);
      }
    });
    multiplier = GridMatrix::residue(multiplier + digits[l] - 3, 8);
    prefix = GridMatrix::multiply(GridMatrix::step(digits[l], d), prefix, d);
    n_nodes_total += sd[sid].nodes;
    for (auto& z : sd[sid].L) acb_clear(&z);
    for (auto& z : sd[sid].S) acb_clear(&z);
    std::vector<acb_struct>().swap(sd[sid].L);
    std::vector<acb_struct>().swap(sd[sid].S);
    // No rule references survive batch_state in the streamed path.
    // Bound retained quadrature-rule caches across a very long cycle.
    sic::support::resources::Bytes cache_bytes = 0;
    for (const auto& entry : gl_cache)
      cache_bytes = sic::support::resources::add(
          cache_bytes, sic::support::resources::mul(
                           2 * entry.second.size(),
                           sic::support::resources::real_bytes(entry.first.precision_bits)));
    if (cache_bytes > 64 * sic::support::resources::kMib) {
      for (auto& entry : gl_cache)
        for (auto& node : entry.second) {
          arb_clear(&node.abscissa);
          arb_clear(&node.weight);
        }
      gl_cache.clear();
    }
  }
  sic::support::flint::Acb mu;

  eighth_root(mu, multiplier, wp);
  parallel_for(d * d - 1, nthreads, [&](long p) {
    const long q = p + 1;
    acb_exp(&val[c][q], &val[c][q], wp);
    acb_mul(&val[c][q], &val[c][q], &product[q], wp);
    acb_mul(&val[c][q], &val[c][q], mu, prec);
  });
}

void Table::alloc_values() {
  if (layout) {
    val.resize(ar.h());
    return;
  }
  val.assign(ar.h(), std::vector<acb_struct>(d * d));
  for (auto& v : val)
    for (auto& z : v) acb_init(&z);
}

void Table::release_states() {
  for (auto& s : sd) {
    for (auto& z : s.L) acb_clear(&z);
    for (auto& z : s.S) acb_clear(&z);
    s.L.clear();
    s.S.clear();
    s.L.shrink_to_fit();
    s.S.shrink_to_fit();
  }
}

}  // namespace sic::exact

namespace sic::exact {
void Table::block_value(acb_t res, sic::support::Index c, long i, long j, slong p) {
  view().block_value(res, c, i, j, p);
}
void Table::batch_state(sic::support::Index sid) { view().batch_state(sid); }
void Table::eighth_root(acb_t r, long k, slong p) { TableView::eighth_root(r, k, p); }
sic::support::resources::Bytes Table::kernel_bytes(size_t nodes, long rows, size_t differences,
                                                   slong wp) const {
  return table_kernel_bytes(d, node_slab, gl_mutex, gl_cache, nodes, rows, differences, wp);
}
double Table::gl_nodes(std::vector<QuadratureNode>& nodes, const arb_t b, const arb_t eta, slong wp,
                       double umax, mag_t error, const std::function<void(size_t)>& admit_nodes) {
  return view().gl_nodes(nodes, b, eta, wp, umax, error, admit_nodes);
}
const std::vector<QuadratureNode>& Table::gl_rule(long n, slong wp) {
  return view().gl_rule(n, wp);
}
}  // namespace sic::exact
