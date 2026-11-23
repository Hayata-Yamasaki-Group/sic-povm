// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/runtime/construction.hpp"

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
#include "construct/model/options.hpp"
#include "construct/model/session_state.hpp"
#include "construct/output/quarantine.hpp"
#include "construct/runtime/resources/resident_memory.hpp"
#include "construct/runtime/resources/table_memory.hpp"
#include "construct/runtime/runtime.hpp"
#include "construct/stages/dimension_two.hpp"
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
#include "exact/group/ray_selection.hpp"
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

namespace sic::construct {
int Construction::run() {
  int status = 0;
  if ((status = initialize()) != 0) return status;
  if ((status = group_stage()) != 0) return status;
  if ((status = structure()) != 0) return status;
  if ((status = tower()) != 0) return status;
  if ((status = step_data()) != 0) return status;
  if ((status = entries()) != 0) return status;
  if ((status = alignment_selection()) != 0) return status;
  if ((status = precision()) != 0) return status;
  if ((status = branch_path()) != 0) return status;
  if ((status = final_pass()) != 0) return status;
  if ((status = sic_check()) != 0) return status;
  return 0;
}

int run(const ProducerOptions& producer_options, Components components,
        const RunServices& runtime) {
  producer_options.policy.validate();
  producer_options.validate_settings();
  sic::support::KernelSettingsScope settings(&producer_options.kernel_settings());
  sic::support::ProgressSink progress{runtime.progress, {}};
  sic::support::ProgressScope progress_scope(&progress);
  NativeRuntimeLease native;
  sic::support::StopToken owned_stop(producer_options.stop_file ? producer_options.stop_file : "");
  sic::support::StopScope stop_scope(runtime.stop ? runtime.stop : &owned_stop);
  sic::support::check_stop();
  if (producer_options.exponent == 1) {
    const auto start = std::chrono::steady_clock::now();
    components.require_closed_form_supported();
    // d = 2 (s = 1) lies outside the real-quadratic Stark-unit construction (K = Q(sqrt(-3))): it
    // is produced from its explicit closed form (the tetrahedral fiducial),
    // construct/stages/dimension_two.hpp.
    const char* out = producer_options.out;
    const char* radicals = producer_options.d2_radicals;
    if (!out)
      throw sic::Error(sic::ErrorKind::invalid_argument, "d = 2 (s = 1) needs an output path");
    QuarantineOutput closed_form(out, false);
    const int status = sic::construct::dimension_two::make(
        closed_form.path(), radicals ? radicals : "", "",
        [&](acb_srcptr first, acb_srcptr second) {
          sic::support::ComplexBallVector vector(2);
          acb_set(vector[0], first);
          acb_set(vector[1], second);
          const auto checked = sic::bounds::bound_wh_overlap_deviations(vector.v.data(), 2);
          sic::support::RealBallVector tolerance(1);
          arb_one(tolerance[0]);
          arb_mul_2exp_si(tolerance[0], tolerance[0], -100);
          if (!arb_lt(checked.maximum_squared_overlap_deviation(), tolerance[0]))
            throw sic::Error(sic::ErrorKind::construction, "closed-form SIC check failed");
#ifdef SIC_ENABLE_TEST_HOOKS
          if (producer_options.raw("SIC_TEST_FAIL_SIC_CHECK"))
            throw sic::Error(sic::ErrorKind::construction, "injected SIC refusal");
#endif
        },
        producer_options.policy);
    if (status) return status;
    closed_form.completed();
    sic::sicx::Reader checked(closed_form.path(), producer_options.policy.resources);
    (void)sic::sicx::verify_all(
        checked, 1, {producer_options.policy.precision, producer_options.policy.valuation});
    if (!producer_options.rays_out.empty()) {
      const auto stamp = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      };
      sic::support::progressf("ray selection started [%.6fs]\n", stamp());
      auto budget = sic::support::resources::Budget{};
      auto rays = select_rays(dimension_two_projection(), 1, budget);
      rays.exact_root = sic::sicx::hex(checked.exact_root());
      sic::support::progressf("ray selection complete: %zu rays [%.6fs]\n", rays.words.size(),
                              stamp());
      const auto written = write_ray_list(producer_options.rays_out, rays, budget);
      closed_form.record_ray_pair(checked.size(), sic::sicx::hex(checked.root()), rays.exact_root,
                                  written.bytes, written.sha256);
    }
    closed_form.publish();
    return 0;
  }
  return Construction(producer_options, std::move(components), runtime).run();
}

}  // namespace sic::construct
