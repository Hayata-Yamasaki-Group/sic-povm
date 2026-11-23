// SPDX-License-Identifier: AGPL-3.0-or-later
#include <flint/acb_dft.h>
#include <flint/acb_poly.h>
#include <malloc.h>
#include <sys/resource.h>
#include <sys/statvfs.h>

#include <numeric>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
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
#include "exact/group/maximal_order_orbits.hpp"
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
#include "sic/error.hpp"
#include "sicx/codec/rescale.hpp"
#include "sicx/container/producer_writer.hpp"
#include "support/resources.hpp"
namespace sic::construct {
int Construction::group_stage() try {  // Construct the group G'.
  group_ = std::make_unique<Group>((*ar_));
  if (unit_power)
    sic::support::progressf("reduced order: %ld nonzero point orbits modulo u\n",
                            reduced_order::configure_group_orbits((*group_), cycle_unit));
  D = (long)(*model_).degree;
  group_cache = producer_options.group_cache;

  cached_group = group_cache && std::filesystem::exists(group_cache);
  if (require_completed_cache.enabled && !cached_group)
    throw sic::Error(
        sic::ErrorKind::io,
        "cache-only consumer lost its existing Group checkpoint; input construction is forbidden");
  if (cached_group) {
    if (pinned_inputs)
      verified_group_digest = cache_checksum_file(std::string(group_cache) + ".sha256");
    GroupCheckpoint::load((*group_), group_cache, D, [&](size_t bases) {
      (*model_).bases = bases;
      budget.require("group checkpoint load", (*model_).group_resident());
    });
    if (pinned_inputs &&
        verified_group_digest != cache_checksum_file(std::string(group_cache) + ".sha256"))
      throw sic::Error(sic::ErrorKind::integrity,
                       "group checkpoint identity changed while loading");
    sic::support::progressf(
        "exact group checkpoint loaded with source/order/SHA-256 checks: %s [%.1fs]\n", group_cache,
        stamp());
    if (unit_power && (*group_).N != D)
      throw sic::Error(sic::ErrorKind::construction, "reduced order: |G'| differs from h d^2/(2k)");
  } else {
    (*group_).generate_standard();
    sic::support::resources::Bytes closure_forecast = 0;
    (*group_).close(
        nth, D,
        [&](size_t bases, size_t generators) {
          closure_forecast = (*model_).group_closure(bases, generators);
          budget.require("group closure BFS", closure_forecast);
          sic::support::progressf(
              "group closure admission: %zu generators, %zu bases, forecast %llu bytes\n",
              generators, bases, (unsigned long long)closure_forecast);
        },
        [&](size_t) { budget.require("group closure progress", closure_forecast); });
    (*group_).gens.clear();
    (*group_).gens.shrink_to_fit();  // Release full permutations after generator construction
    if (unit_power && (*group_).N != D)
      throw sic::Error(sic::ErrorKind::construction, "reduced order: |G'| differs from h d^2/(2k)");
    if (group_cache) {
      sic::support::progressf("group closure complete [%.1fs]\n", stamp());
      GroupCheckpoint::save((*group_), group_cache);
      if (pinned_inputs)
        verified_group_digest = cache_checksum_file(std::string(group_cache) + ".sha256");
      sic::support::progressf("validated exact group checkpoint saved: %s [%.1fs]\n", group_cache,
                              stamp());
    }
  }
  if (!producer_options.rays_out.empty()) {
    sic::support::progressf("ray selection started [%.6fs]\n", stamp());
    selected_rays = select_rays(*group_, unit_power ? cycle_unit.k : 1, nth, budget);
    sic::support::progressf("ray selection complete: %zu rays [%.6fs]\n",
                            selected_rays.words.size(), stamp());
  }
  // sigma_(a) for odd a mod 2d (Artin symbols of positive rational integers): (Z/2d)^* = <-1> x
  // <5>, so sigma_(a) = sigma_(5)^k sigma_(-1)^e.
  sig_odd = std::vector<long>(2 * d, -1);
  {
    auto artin_int = [&](long a) {  // positive a = a mod 2d, prime to Delta
      while (std::gcd(a, (*ar_).Delta) != 1) a += 2 * d;
      return (*group_).element_of((*group_).principal_gen(a, 0));
    };
    long s5 = artin_int(5), sm1 = artin_int(2 * d - 1);
    long a = 1, g = 0;
    for (long k = 0; k < d / 2; ++k) {
      sig_odd[a] = g;
      sig_odd[2 * d - a] = (*group_).mul(g, sm1);
      a = (a * 5) % (2 * d);
      g = (*group_).mul(g, s5);
    }
    for (long r = 1; r < 2 * d; r += 2)
      if (sig_odd[r] < 0)
        throw sic::Error(sic::ErrorKind::construction, "sigma_(a) table incomplete");
  }
  N = (*group_).N;
  entry_metadata.d = d;
  entry_metadata.smith.assign(group_->invf.begin(), group_->invf.end());
  entry_metadata.units = std::make_shared<const sic::sicx::UnitGroup>(d);
  for (const auto unit : entry_metadata.units->generators) {
    const auto coordinates = group_->coord(sig_odd.at(unit));
    entry_metadata.unit_generator_words.emplace_back(coordinates.begin(), coordinates.end());
  }
  entry_translations.derive(entry_metadata);
  entry_actions.resize(d);
  for (long coordinate = 0; coordinate < d; ++coordinate) {
    const auto entry = entry_metadata.entry(coordinate);
    long action = 0;
    for (size_t axis = 0; axis < entry.word.size(); ++axis)
      action += entry.word[axis] * group_->stride[axis];
    entry_actions[coordinate] = action;
  }
  (*model_).bases = (*group_).nb();
  (*model_).kernel_workers = nth;
  for (long v : (*group_).invf)
    (*model_).smith_sum = sic::support::resources::add((*model_).smith_sum, v);
  sic::support::progressf("|G'| = %ld (D = %ld), invariants:", N, D);
  for (long v : (*group_).invf) sic::support::progressf(" %ld", v);
  sic::support::progressf(", base points %zu   [%.1fs]\n", (*group_).nb(), stamp());
  if (N != D) sic::support::progressf("WARNING: |G'| != D\n");

  if ((*model_).compact_tables) {
    budget.require("analytic table index layout", (*model_).group_resident());
    analytic_layout = std::make_shared<OrbitTableLayout>(d, (*ar_).h(), (*group_).orbit);
  }

  // characters: chi_m(g) = e(sum_l m_l g_l / n_l), indexed like the elements
  Lc = 1;
  for (long v : (*group_).invf) Lc = std::lcm(Lc, v);

  m = s + 2;  // generators: F(y_j) (j = 1..s), zeta_{2d}, sqrt(eps)

  product_ = std::make_unique<ProductCandidate>(m, kProductFactors, seed);
  relative_highest = -1;

  return 0;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::construct
