// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
template <class Context>
void ghost_coefficient(acb_ptr out, const Context& context, long d, long n, long original) {
  const long i = original / d, j = original % d;
  const long exponent = (2 * d - (n * ((i * i + j * j) % (2 * d))) % (2 * d)) % (2 * d);
  acb_mul(out, context.cst, &context.w2d[exponent], context.p);
  if ((i + j) % 2) acb_neg(out, out);
}

template <class Context, class Emit>
void reconstruct_ghost_state(const GhostRootMetadata& metadata,
                             const std::vector<acb_struct>& state, const Context& context,
                             const Emit& emit) {
  const long d = metadata.d, n = metadata.n, k = metadata.k;
  if (d < 2 || n <= 0 || n >= 2 * d || std::gcd(n, d) != 1 || k < 0 || k >= d ||
      state.size() != (size_t)(2 * d - 2) || context.p != metadata.precision)
    throw std::invalid_argument("invalid certified ghost reconstruction metadata");
  const long inverse = n_invmod(n % d, d);
  sic::support::ComplexBallVector v(d), u(d), work(3), products(d), transformed(d);
  for (long a = 0, q = 0; a < d; ++a) {
    if (a == k) {
      acb_one(v[a]);
      acb_one(u[a]);
    } else {
      acb_set(v[a], &state[q]);
      acb_set(u[a], &state[d - 1 + q]);
      ++q;
    }
  }
  acb_dot(work[0], nullptr, 0, u.v.data(), 1, v.v.data(), 1, d, context.p);
  acb_inv(work[0], work[0], context.p);
  struct Plan {
    acb_dft_pre_t value;
    Plan(long dimension, slong precision) { acb_dft_precomp_init(value, dimension, precision); }
    ~Plan() { acb_dft_precomp_clear(value); }
  } plan(d, context.p);
  for (long j = 0; j < d; ++j) {
    for (long a = 0; a < d; ++a) acb_mul(products[a], v[a], u[(a + j) % d], context.p);
    acb_dft_precomp(transformed.v.data(), products.v.data(), plan.value, context.p);
    for (long i = 0; i < d; ++i) {
      if (!i && !j) continue;
      const long original = ((inverse * i) % d) * d + j;
      acb_mul(work[1], transformed[i], work[0], context.p);
      ghost_coefficient(work[2], context, d, n, original);
      acb_mul(work[1], work[1], work[2], context.p);
      emit(original, work[1]);
    }
  }
}

template <class Context, class Emit>
bool try_parallel_ghost_reconstruction(const GhostRootMetadata& metadata,
                                       const std::vector<acb_struct>& state, const Context& context,
                                       const Emit& emit, int owned_workers,
                                       sic::support::resources::Bytes scratch_limit, bool verbose,
                                       const GhostReconstructionTeamFactory& factory) {
  const long d = metadata.d, n = metadata.n, k = metadata.k;
  if (d < 2 || n <= 0 || n >= 2 * d || std::gcd(n, d) != 1 || k < 0 || k >= d ||
      state.size() != (size_t)(2 * d - 2) || context.p != metadata.precision || owned_workers <= 0)
    throw std::invalid_argument("invalid parallel ghost reconstruction metadata");
  if (owned_workers == 1 || d < 4 || (d & (d - 1)) || context.p < 2 || context.p > WORD_MAX / 16 ||
      context.w2d.size() != (size_t)(2 * d))
    return false;
  auto bounded = [&](acb_srcptr value) {
    return acb_is_finite(value) && acb_bits(value) <= context.p + 64 &&
           ARB_IS_LAGOM(acb_realref(value)) && ARB_IS_LAGOM(acb_imagref(value));
  };
  for (const auto& value : state)
    if (!bounded(&value)) return false;
  if (!bounded(context.cst)) return false;
  for (const auto& value : context.w2d)
    if (!bounded(&value)) return false;
  auto check_stop = [] { sic::support::check_stop(); };
  check_stop();
  const sic::support::resources::Budget budget;
  const int active = static_cast<int>(std::min<long>(owned_workers, d));
  if (active <= 1) return false;
  sic::support::resources::Bytes bytes;
  try {
    bytes = sic::support::resources::ghost_reconstruction_scratch(d, context.p, active);
  } catch (const std::overflow_error&) {
    return false;
  }
  if (bytes > scratch_limit) return false;
  struct Plan {
    acb_dft_pre_t value;
    Plan(long dimension, slong precision) { acb_dft_precomp_init(value, dimension, precision); }
    ~Plan() { acb_dft_precomp_clear(value); }
  };
  struct Row {
    sic::support::ComplexBallVector products, transformed, work;
    explicit Row(long dimension) : products(dimension), transformed(dimension), work(2) {}
  };
  struct Shared {
    sic::support::ComplexBallVector v, u, inverse;
    std::unique_ptr<Plan> plan;
    std::vector<std::unique_ptr<Row>> rows;
    Shared(long dimension, long normalization, slong precision, int workers,
           const std::vector<acb_struct>& root)
        : v(dimension), u(dimension), inverse(1) {
      for (long a = 0, q = 0; a < dimension; ++a) {
        if (a == normalization) {
          acb_one(v[a]);
          acb_one(u[a]);
        } else {
          acb_set(v[a], &root[q]);
          acb_set(u[a], &root[dimension - 1 + q]);
          ++q;
        }
      }
      acb_dot(inverse[0], nullptr, 0, u.v.data(), 1, v.v.data(), 1, dimension, precision);
      acb_inv(inverse[0], inverse[0], precision);
      plan = std::make_unique<Plan>(dimension, precision);
      for (int worker = 0; worker < workers; ++worker)
        rows.push_back(std::make_unique<Row>(dimension));
    }
  };
  std::unique_ptr<Shared> shared;
  std::unique_ptr<RowWorkerTeam> team;
  try {
    budget.require_additional("parallel certified ghost reconstruction", bytes);
    shared = std::make_unique<Shared>(d, k, context.p, active, state);
    if (!bounded(shared->inverse[0]) || shared->plan->value->type != DFT_RAD2) return false;
    team = factory ? factory(active) : std::make_unique<RowWorkerTeam>(active);
  } catch (const sic::support::resources::AdmissionError&) {
    check_stop();
    return false;
  } catch (const std::system_error&) {
    check_stop();
    return false;
  } catch (const std::bad_alloc&) {
    check_stop();
    return false;
  }
  if (!team) throw std::logic_error("reconstruction worker factory returned no team");
  check_stop();
  const long inverse = n_invmod(n % d, d);
  if (verbose)
    sic::support::progressf(
        "parallel ghost reconstruction start: dimension=%ld bits=%ld workers=%d scratch=%llu\n", d,
        (long)context.p, active, (unsigned long long)bytes);
  // One lane owns each row buffer. The shared plan is read-only and each
  // RowWorkerTeam callback has native workers suppressed, including caller.
  team->for_each(active, [&](long lane) {
    auto& row = *shared->rows[lane];
    for (long j = lane; j < d; j += active) {
      check_stop();
      for (long a = 0; a < d; ++a)
        acb_mul(row.products[a], shared->v[a], shared->u[(a + j) % d], context.p);
      acb_dft_precomp(row.transformed.v.data(), row.products.v.data(), shared->plan->value,
                      context.p);
      for (long i = 0; i < d; ++i) {
        if (!i && !j) continue;
        const long original = ((inverse * i) % d) * d + j;
        acb_mul(row.work[0], row.transformed[i], shared->inverse[0], context.p);
        ghost_coefficient(row.work[1], context, d, n, original);
        acb_mul(row.work[0], row.work[0], row.work[1], context.p);
        emit(original, row.work[0]);
      }
    }
  });
  team.reset();
  if (verbose)
    sic::support::progressf(
        "parallel ghost reconstruction done: dimension=%ld bits=%ld workers=%d\n", d,
        (long)context.p, active);
  return true;
}

}  // namespace sic::exact
