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
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
#include "support/resources.hpp"
#include "support/resources/platform_snapshot.hpp"
namespace sic::construct {
sic::support::resources::BitCount Construction::recognition_bits(
    const sic::support::RealBallVector& values, slong conjugate_height) {
  // For omega=(1+sqrt(D0))/2 and D0>=5, solving the two real
  // embeddings gives |x|,|y| <= |beta|+|beta'|. Bound coordinate
  // payloads from the actual input balls and the certified height.
  if (D0 < 5) throw std::logic_error("unsupported recognition field bound");
  slong exponent = std::max<slong>(0, conjugate_height);
  mag_t magnitude;
  mag_init(magnitude);
  for (const auto& value : values.v)
    if (arb_is_finite(&value)) {
      arb_get_mag(magnitude, &value);
      if (!mag_is_zero(magnitude)) {
        if (!fmpz_fits_si(MAG_EXPREF(magnitude))) {
          mag_clear(magnitude);
          throw sic::support::resources::AdmissionError(
              "recognition coordinate exponent is outside the supported range");
        }
        exponent = std::max(exponent, fmpz_get_si(MAG_EXPREF(magnitude)));
      }
    }
  mag_clear(magnitude);
  return sic::support::resources::add(exponent, 3);
}

void Construction::admit_recognition_block(const sic::support::RealBallVector& values,
                                           slong conjugate_height) {
  const auto bits = recognition_bits(values, conjugate_height);
  budget.require_additional(
      "exact recognition block",
      sic::support::resources::leaf_block_bytes(std::min<size_t>(262144, values.size()), bits));
  if (producer_options.progress)
    sic::support::progressf("recognition block admission: %zu rows, coordinate bound %llu bits\n",
                            values.size(), (unsigned long long)bits);
}

void Construction::trim_heap() {
#if defined(__GLIBC__)
  sic::support::resources::release_free_heap();
#endif
}

void Construction::release_band_heap(const char* where, long eta, long band, long bands) {
  const auto before = sic::support::resources::inspect().rss;
  trim_heap();
  const auto after = sic::support::resources::inspect().rss;
  sic::support::progressf(
      "band heap release: where=%s eta=%ld band=%ld/%ld rss_before_trim=%llu rss_after_trim=%llu\n",
      where, eta, band, bands, (unsigned long long)before, (unsigned long long)after);
  fflush(stdout);
}
}  // namespace sic::construct
