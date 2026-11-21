// SPDX-License-Identifier: AGPL-3.0-or-later
#include <unistd.h>

#include <atomic>
#include <fstream>

#include "assertions.hpp"
#include "exact/certification/reconstruction/ghost.hpp"

static void check_parallel_edges() {
  const sic::exact::GhostRootMetadata metadata{16, 3, 0, 1, 144, 256};
  sic::exact::GhostRefiner::Ctx high(16, 1, 256), context(high, 144);
  sic::support::ComplexBallVector state(30);
  for (size_t q = 0; q < state.size(); ++q) acb_one(state[q]);
  std::atomic<long> emitted{0};
  auto emit = [&](long, acb_srcptr) { ++emitted; };
  auto run = [&](sic::support::resources::Bytes limit,
                 const sic::exact::GhostReconstructionTeamFactory& factory = {}) {
    return sic::exact::try_parallel_ghost_reconstruction(metadata, state.v, context, emit, 4, limit,
                                                         false, factory);
  };
  const auto limit = sic::support::resources::ghost_reconstruction_scratch(16, 144, 4);
  SIC_CHECK(!run(1));
  SIC_CHECK(emitted == 0);
  bool factory_reached = false;
  SIC_CHECK(!run(limit, [&](int workers) -> std::unique_ptr<sic::exact::RowWorkerTeam> {
    factory_reached = true;
    SIC_CHECK(workers == 4);
    throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
  }));
  SIC_CHECK(factory_reached);
  SIC_CHECK(emitted == 0);
  acb_indeterminate(state[0]);
  SIC_CHECK(!run(limit));
  SIC_CHECK(emitted == 0);
  acb_one(state[0]);
  bool caught = false;
  try {
    sic::exact::try_parallel_ghost_reconstruction(
        metadata, state.v, context,
        [&](long, acb_srcptr) {
          ++emitted;
          throw std::runtime_error("partial output");
        },
        4, limit);
  } catch (const std::runtime_error& error) {
    caught = std::string(error.what()) == "partial output";
  }
  SIC_CHECK(caught);
  SIC_CHECK(emitted > 0);
  SIC_CHECK(emitted < 255);
  SIC_CHECK(flint_get_num_threads() == 4);

  char path[] = "/tmp/sic-reconstruction-stop-XXXXXX";
  const int descriptor = mkstemp(path);
  SIC_CHECK(descriptor >= 0);
  if (descriptor < 0) return;
  close(descriptor);
  sic::support::ScopedStopFile stop_scope(path);
  emitted = 0;
  caught = false;
  try {
    run(limit);
  } catch (const std::runtime_error&) {
    caught = true;
  }
  SIC_CHECK(caught);
  SIC_CHECK(emitted == 0);
  unlink(path);
  caught = false;
  try {
    sic::exact::try_parallel_ghost_reconstruction(
        metadata, state.v, context,
        [&](long, acb_srcptr) {
          if (emitted.fetch_add(1) == 0) {
            std::ofstream marker(path);
          }
        },
        4, limit);
  } catch (const std::runtime_error&) {
    caught = true;
  }
  SIC_CHECK(caught);
  SIC_CHECK(emitted > 0);
  SIC_CHECK(emitted < 255);
  SIC_CHECK(flint_get_num_threads() == 4);
  unlink(path);
}

static void check_native_threaded_reference() {
  // d > 512 exercises FLINT's threaded radix-two reference path.
  constexpr long kD = 1024;
  const sic::exact::GhostRootMetadata metadata{kD, 2 * kD - 1, 17, -1, 96, 128};
  sic::exact::GhostRefiner::Ctx high(kD, -1, 128), context(high, 96);
  sic::support::ComplexBallVector state(2 * kD - 2), serial(kD * kD);
  for (size_t q = 0; q < state.size(); ++q) {
    acb_set_si(state[q], (q % 31) + 1);
    acb_mul_2exp_si(state[q], state[q], -5);
    arb_add_error_2exp_si(acb_realref(state[q]), -80);
  }
  sic::exact::reconstruct_ghost_state(metadata, state.v, context,
                                      [&](long y, acb_srcptr value) { acb_set(serial[y], value); });
  std::atomic<long> equal{0};
  SIC_CHECK(sic::exact::try_parallel_ghost_reconstruction(
      metadata, state.v, context,
      [&](long y, acb_srcptr value) {
        if (acb_equal(serial[y], value)) ++equal;
      },
      4, sic::support::resources::ghost_reconstruction_scratch(kD, 96, 4)));
  SIC_CHECK(equal == kD * kD - 1);
  SIC_CHECK(flint_get_num_threads() == 4);
}

int main() {
  flint_set_num_threads(4);
  for (long d : {8, 16})
    for (int chi : {-1, 1})
      for (long n : {1L, 3L, 2 * d - 1})
        for (long k : {0L, d - 1}) {
          sic::exact::GhostRefiner::Ctx high(d, chi, 256), context(high, 144);
          sic::support::ComplexBallVector state(2 * d - 2), v(d), u(d), actual(d * d),
              replay(d * d), work(5);
          for (long q = 0; q < 2 * d - 2; ++q) {
            acb_set_si(state[q], q + 11);
            acb_div_si(state[q], state[q], 13, 192);
            arb_set_si(acb_imagref(state[q]), q % 3 - 1);
            arb_mul_2exp_si(acb_imagref(state[q]), acb_imagref(state[q]), -8);
          }
          for (long a = 0, q = 0; a < d; ++a) {
            if (a == k) {
              acb_one(v[a]);
              acb_one(u[a]);
            } else {
              acb_set(v[a], state[q]);
              acb_set(u[a], state[d - 1 + q]);
              ++q;
            }
          }
          const sic::exact::GhostRootMetadata metadata{d, n, k, chi, 144, 256};
          sic::exact::reconstruct_ghost_state(
              metadata, state.v, context,
              [&](long y, acb_srcptr value) { acb_set(actual[y], value); });
          sic::exact::GhostRefiner::Ctx restored_high(d, chi, 256), restored(restored_high, 144);
          sic::exact::reconstruct_ghost_state(
              metadata, state.v, restored,
              [&](long y, acb_srcptr value) { acb_set(replay[y], value); });
          sic::support::ComplexBallVector parallel(d * d);
          SIC_CHECK(sic::exact::try_parallel_ghost_reconstruction(
              metadata, state.v, context,
              [&](long y, acb_srcptr value) { acb_set(parallel[y], value); }, 4,
              sic::support::resources::ghost_reconstruction_scratch(d, 144, 4)));
          acb_dot(work[0], nullptr, 0, u.v.data(), 1, v.v.data(), 1, d, 144);
          acb_inv(work[0], work[0], 144);
          for (long i = 0; i < d; ++i)
            for (long j = 0; j < d; ++j) {
              const long y = i * d + j;
              if (!y) continue;
              SIC_CHECK(acb_equal(actual[y], replay[y]));
              SIC_CHECK(acb_equal(actual[y], parallel[y]));
              acb_zero(work[1]);
              for (long a = 0; a < d; ++a) {
                acb_mul(work[2], v[a], u[(a + j) % d], 144);
                acb_mul(work[2], work[2], &context.wd[(d - (n * i * a) % d) % d], 144);
                acb_add(work[1], work[1], work[2], 144);
              }
              acb_mul(work[1], work[1], work[0], 144);
              const long exponent = n * (i * i + j * j) % (2 * d);
              acb_div(work[3], context.cst, &context.w2d[exponent], 144);
              if ((i + j) % 2) acb_neg(work[3], work[3]);
              acb_mul(work[1], work[1], work[3], 144);
              SIC_CHECK(acb_overlaps(work[1], actual[y]));
            }
        }
  sic::exact::GhostRefiner failed(8, 1, 1);
  failed.workspace_limit = 1;
  failed.retain_certified_root = true;
  failed.certified_root = std::make_unique<sic::exact::CertifiedGhostRoot>();
  failed.S.assign(14, 1);
  failed.cond_est = 3;
  sic::support::ComplexBallVector seed(64), output;
  for (size_t q = 0; q < seed.size(); ++q) acb_one(seed[q]);
  std::string reason;
  SIC_CHECK(!failed.refine(seed.v, 256, output.v, reason));
  SIC_CHECK(!failed.certified_root);
  SIC_CHECK(failed.S.empty());
  SIC_CHECK(failed.cond_est == 0);
  check_parallel_edges();
  check_native_threaded_reference();
  flint_cleanup_master();
  return sic_test::report();
}
