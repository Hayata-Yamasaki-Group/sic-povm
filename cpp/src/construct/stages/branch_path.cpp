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

#include "construct/components/branch_admission.hpp"
#include "construct/components/components.hpp"
#include "construct/model/session_state.hpp"
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
#include "sicx/codec/integer.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
namespace sic::construct {
int Construction::branch_path() {  // ---------------- branch path (from the exact step data only)
                                   // ----------------
  // iota(lambda) = x + y omega'. An element of F_n with leaves lambda_e is sum_e lambda_e prod_i
  // iota(b*_{i,e_i}), where b*_{i,k} = c_{i,k} / f_i'(g_i) with f_i(X) / (X - g_i) = sum_k c_{i,k}
  // X^k (b* = 1/2, 1/(2 w_i) for quadratic steps).
  ilv = std::vector<std::vector<Leaf>>(items.size());
  item_sources = std::vector<ExactSource>(items.size());
  item_constant = std::vector<char>(items.size(), 0);

  // candidate images of g_i given the choices below level i (pt.B[0..i-1]), with log |iota(g#_i)|
  // and the dual basis

  // greedy path with one step of look-ahead (maximize log|g#_i| + max log|g#_{i+1}|); with prev,
  // re-derive the same path at a higher precision (the candidate overlapping the previous choice)

  return 0;
}

}  // namespace sic::construct
