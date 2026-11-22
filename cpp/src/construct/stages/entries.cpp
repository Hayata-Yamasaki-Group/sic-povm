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
#include "sic/error.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
#include "support/resources.hpp"
namespace sic::construct {
int Construction::entries() {  // ---------------- entries of the cheap column and leaves
  D0 = (*ar_).Delta;
  for (long p = 2; p * p <= D0; ++p)
    while (D0 % (p * p) == 0) D0 /= p * p;
  arb_t eta_bound;
  arb_init(eta_bound);
  arb_sqrt_ui(eta_bound, d + 1, 128);
  arb_add_ui(eta_bound, eta_bound, d, 128);
  Ceta = HeightBound::absolute_ball(eta_bound);
  arb_clear(eta_bound);
  // |leaf'| <= 2 [F_n:K] C_a prod_{j<n} C_j^{p_j-1}.
  // Every factor rounds upwards; the integer exponent is checked in mag_t.

  // Requested representative entries of one streamed physical class.
  // Root-backed classes use the certified chirp/Gauss
  // contraction of direct_eta.hpp instead of reconstructing the class grid and
  // summing d table values per entry. Full (quadrature) classes, unsupported
  // geometry and failed admission keep the literal path. SIC_DIRECT_ETA_CHECK=1
  // additionally evaluates every entry by the literal path and requires ball
  // overlap (control mode; the consumed values are the direct ones).
  direct_eta_check = producer_options.direct_eta_check.get();
  // Test-only fault injection. Applies to
  // root-backed classes with an even class index only, so one run mixes
  // direct classes with refused (literal) classes. Production environments
  // must not set it; the production option parser refuses the key.
#ifdef SIC_ENABLE_TEST_HOOKS

  if (producer_options.raw("SIC_DIRECT_ETA_TEST_FAULT")) {
    direct_eta_fault = producer_options.direct_eta_fault.get();
    if (direct_eta_fault == "stop" && !producer_options.stop_file)
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       "SIC_DIRECT_ETA_TEST_FAULT=stop requires SIC_STOP_FILE");
  }
#endif

  // A band's channel limbs are freed leaf by leaf
  // during recognition, from glibc arenas filled by every worker. Return them
  // to the kernel before the next band is admitted against the measured RSS.

  requested_band_bits = 0;
  if (const auto& configured_bits = producer_options.rep_band_bits.get(); configured_bits) {
    const auto bits = *configured_bits;
    if (bits > 8 || (bits && !phased_storage))
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       "SIC_REP_BAND_BITS requires phased channels and a value in 0..8");
    requested_band_bits = bits;
  }

  if (requested_band_bits)
    for (int r = 0; r <= s; ++r) {
      const auto& field = scalar_fields->fields[scalar_fields->representative_field[r]];
      (*model_).phased_representative_rows = std::max<sic::support::resources::Count>(
          (*model_).phased_representative_rows, (2 * field.degree) >> representative_band_bits(r));
    }
  if (phased_storage)
    sic::support::progressf(
        "phased generator metadata: trace_rows=%llu action_reload_rows=%llu descriptor_rows=%llu "
        "representative_rows=%llu\n",
        (unsigned long long)(*model_).generator_rows,
        (unsigned long long)(*model_).action_generator_rows,
        (unsigned long long)(*model_).descriptor_rows,
        (unsigned long long)((*model_).phased_representative_rows
                                 ? (*model_).phased_representative_rows
                                 : (*model_).degree));

  // the exact step data: w_i^2 for quadratic steps, the coefficients a_k of f_i for odd steps, all
  // in F_i k < 0: w_i^2

  item_idx = std::vector<std::vector<sic::support::Index>>(t);
  for (int i = 0; i < t; ++i) {
    if (ps[i] == 2) {
      item_idx[i].push_back(sic::support::checked_index(items.size()));
      items.push_back({i, -1});
    } else
      for (long k = 0; k < ps[i]; ++k) {
        item_idx[i].push_back(sic::support::checked_index(items.size()));
        items.push_back({i, k});
      }
  }

  // leaves of a step-data item (real balls); F_i (i < t) lies in the real subfield (conjugation
  // real tail)

  // log2 of the largest leaf radius

  return 0;
}

}  // namespace sic::construct
