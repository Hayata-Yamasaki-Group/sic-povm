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
#include "construct/output/quarantine.hpp"
#include "construct/runtime/observations/progress.hpp"
#include "construct/runtime/resources/resident_memory.hpp"
#include "construct/runtime/resources/table_memory.hpp"
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
#include "sic/bounds/overlap_bounds.hpp"
#include "sic/error.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
#include "support/resources.hpp"
#include "support/resources/platform_snapshot.hpp"
namespace sic::construct {
int Construction::sic_check() {
  const slong wpd = path.wpd;
  // normalize and verify the SIC property
  sic::support::RealBallVector check_scalars(5);
  arb_ptr nrm = check_scalars[0], a = check_scalars[1];
  arb_zero(nrm);
  for (long mm = 0; mm < d; ++mm) {
    acb_abs(a, vec[mm], wpd);
    arb_addmul(nrm, a, a, wpd);
  }
  arb_sqrt(nrm, nrm, wpd);
  for (long mm = 0; mm < d; ++mm) acb_div_arb(vec[mm], vec[mm], nrm, wpd);
  sic::support::progressf("fiducial (first entries):\n");
  for (long mm = 0; mm < std::min(d, 4L); ++mm) {
    sic::support::progressf("  psi[%ld] = ", mm);
    print_acb(vec[mm], 25, 0);
    sic::support::progressf("\n");
  }
  // overlaps |<psi| X^a Z^b |psi>|^2, (a,b) != (0,0). For fixed a, <psi|X^a Z^b|psi> = w^{-ab}
  // sum_j f_a(j) w^{bj} with f_a(j) = conj(psi_j) psi_{j-a}, so all b come from one DFT of length
  // d: O(d^2 log d). The check needs only 2^-100, so it runs at (at most) 512 bits; the entries are
  // balls, and rounding keeps them certified.
  const slong wpc = std::min<slong>(wpd, 512);
  sic::support::ComplexBallVector vr(d);
  for (long mm = 0; mm < d; ++mm) acb_set_round(vr[mm], vec[mm], wpc);
  arb_ptr maxdev = check_scalars[3];
  struct NativeBudget {
    int saved = flint_set_num_workers(0);
    ~NativeBudget() { flint_reset_num_workers(saved); }
  } native;
  sic::bounds::BallVerificationLimits limits;
  limits.working_bits = wpc;
  limits.minimum_absolute_accuracy_bits = std::min<slong>(100, wpc - 32);
  // The construction dimension and the configured process budget define this
  // caller's policy; the shared verifier still admits every allocation/work score.
  limits.maximum_dimension = std::max<std::size_t>(limits.maximum_dimension, d);
  limits.maximum_workspace_bytes = budget.remaining();
  const auto input_bits = sic::support::resources::mul(sic::support::resources::mul(2, d), wpc);
  limits.maximum_input_mantissa_bits = input_bits;
  limits.maximum_work_score =
      std::max(limits.maximum_work_score,
               sic::support::resources::add(
                   sic::support::resources::mul(sic::support::resources::mul(d, d),
                                                sic::support::resources::add(d, 8)),
                   sic::support::resources::add(input_bits, FLINT_BITS - 1) / FLINT_BITS));
  const auto checked = sic::bounds::bound_wh_overlap_deviations(vr.v.data(), d, limits);
  arb_set(maxdev, checked.maximum_squared_overlap_deviation());
  sic::support::progressf("max | |<psi|D_p|psi>|^2 - 1/(d+1) | = ");
  print_arb(maxdev, 5, 0);
  sic::support::progressf("\n");
  sic::support::progressf("time: exact representation and decoding %.1fs, total %.1fs\n", t_exact,
                          stamp());
  arb_ptr tol = check_scalars[4];
  arb_one(tol);
  arb_mul_2exp_si(tol, tol, -100);
  bool sic_ok = arb_lt(maxdev, tol);
#ifdef SIC_ENABLE_TEST_HOOKS
  if (const char* fault = producer_options.raw("SIC_TEST_FAIL_SIC_CHECK");
      fault && std::strcmp(fault, "1") == 0)
    sic_ok = false;
#endif
  sic::support::progressf("SIC check (deviation < 2^-100): %s\n", sic_ok ? "PASS" : "FAIL");
  struct rusage usage {};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    const double rss = sic::support::resources::peak_rss_bytes(usage) / (1024.0 * 1024.0);
    sic::support::progressf("resources: peak RSS %.1f MiB, user %.2fs, system %.2fs\n", rss,
                            usage.ru_utime.tv_sec + usage.ru_utime.tv_usec * 1e-6,
                            usage.ru_stime.tv_sec + usage.ru_stime.tv_usec * 1e-6);
  }
  if (sic_ok) {
    sic::sicx::Reader checked_reader(outp, producer_options.policy.resources);
    if (checked_reader.root() != completed_root)
      throw sic::Error(sic::ErrorKind::integrity, "completed quarantine root changed");
    sic::support::check_stop();
    // A published exact file commits the pair. Resume can compare a ray list
    // left by a stop before this publication against the regenerated list.
    if (!producer_options.rays_out.empty()) {
      selected_rays.exact_root = sic::sicx::hex(checked_reader.exact_root());
      const auto written =
          write_ray_list(producer_options.rays_out, selected_rays, budget, producer_options.resume);
      quarantine->record_ray_pair(checked_reader.size(), sic::sicx::hex(checked_reader.root()),
                                  selected_rays.exact_root, written.bytes, written.sha256);
    }
    quarantine->publish();
    sic::support::progressf("SICX checked output published to %s\n", producer_options.out);
  }
  if (!sic_ok) throw sic::Error(sic::ErrorKind::construction, "SIC acceptance check failed");
  return 0;
}

}  // namespace sic::construct
