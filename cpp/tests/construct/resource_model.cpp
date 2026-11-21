// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>

#include "assertions.hpp"
#include "construct/runtime/resources/construction_model.hpp"
#include "exact/analytic/table.hpp"
#include "exact/certification/reconstruction/ghost.hpp"
#include "exact/group/group.hpp"
#include "support/resources.hpp"

static bool throws(const std::function<void()>& action) {
  try {
    action();
    return false;
  } catch (const std::exception&) {
    return true;
  }
}

int main() {
  sic::construct::resources::Model model(2048, 128, 2045, 5462, 12);
  SIC_CHECK(model.degree == 268435456);
  SIC_CHECK(model.points == 536870912);
  SIC_CHECK(model.representative_rows() == 3221225472ULL);
  const auto streamed_bytes = model.quadrature(144, 2);
  SIC_CHECK(model.quadrature(144, 64) == streamed_bytes);  // one shared state, not 64 state copies
  model.streamed_states = false;
  SIC_CHECK(model.quadrature(144, 2) > 1500 * sic::support::resources::kGib);
  SIC_CHECK(model.quadrature(144, 2) > 10 * streamed_bytes);
  model.streamed_states = true;
  SIC_CHECK(model.group_closure(12, 14) > model.group_closure(1, 14));
  const auto group_bytes = model.group_resident();
  model.extra_resident = model.degree;
  SIC_CHECK(model.group_resident() == group_bytes + model.degree);
  model.extra_resident = 0;
  SIC_CHECK(model.newton_worker(1905, true) > model.newton_worker(1905));
  SIC_CHECK(model.newton_worker(4000, false, 512) < model.newton_worker(4000));
  SIC_CHECK(model.newton_worker(4000, false, 512, true, 64) >
            model.newton_worker(4000, false, 512));
  SIC_CHECK(model.newton_worker(4000, false, 512, true, 64, true) >
            model.newton_worker(4000, false, 512, true, 64));
  SIC_CHECK(model.newton_worker(4000, false, 512, false, 64, true) >
            model.newton_worker(4000, false, 512));
  SIC_CHECK(model.newton_worker(4000, false, 512, false, 64) ==
            model.newton_worker(4000, false, 512));
  SIC_CHECK(model.newton_worker(4000, false, 512, false, 64, false, true) >
            model.newton_worker(4000, false, 512));
  SIC_CHECK(model.newton_worker(4000, false, 512, false, 64, true, true) >
            model.newton_worker(4000, false, 512, false, 64, true));
  SIC_CHECK(model.newton_worker(4000, false, 512, false, 64, true, true, true) >
            model.newton_worker(4000, false, 512, false, 64, true, true));
  SIC_CHECK(model.newton_worker(4000, false, 512, false, 1, false, false, true) ==
            model.newton_worker(4000, false, 512));
  SIC_CHECK(model.newton_worker(4000, false, 512, false, 64, true, true, true, true) >
            model.newton_worker(4000, false, 512, false, 64, true, true, true));
  SIC_CHECK(model.final_buffers(1905, 2000) > model.final_buffers(1905, 500));
  const auto dense_final = model.final_buffers(4016, 900);
  model.compact_tables = true;
  model.computed_classes = 64;
  model.bases = 12;
  model.kernel_workers = 64;
  model.smith_sum = 5134;
  model.phased_final = true;
  const auto phased_final = model.final_buffers(4016, 900);
  SIC_CHECK(phased_final < dense_final);
  SIC_CHECK(phased_final >=
            sic::support::resources::mul(model.degree + model.generator_window,
                                         sic::support::resources::real_bytes(4016)));
  SIC_CHECK(model.final_buffers(4016, 5000) > phased_final);
  SIC_CHECK(model.final_buffers(8016, 900) > phased_final);
  const auto unbanded_high = model.final_buffers(17016, 709);
  model.generator_metadata = true;
  model.phased_streamed_table = true;
  model.generator_rows = 262144;
  model.action_generator_rows = 131072;
  model.descriptor_rows = 8192;
  model.phased_representative_rows = model.degree / 4;
  const auto banded_high = model.final_buffers(17016, 709);
  SIC_CHECK(banded_high < unbanded_high);
  SIC_CHECK(banded_high >= sic::support::resources::mul(
                               model.degree / 4, sic::support::resources::real_bytes(17016)));
  // This is a conditional model boundary, not a measured target precision.
  SIC_CHECK(unbanded_high > 503 * 1024 * sic::support::resources::kMib);
  SIC_CHECK(banded_high < 192 * 1024 * sic::support::resources::kMib);
  SIC_CHECK(model.quadrature(4016, 64, 2) - model.quadrature(4016, 64, 1) == model.table(4016));
  SIC_CHECK(
      model.quadrature(4016, 64, 1) ==
      sic::support::resources::add(
          model.group_resident(),
          sic::support::resources::add(model.table(4016), model.quadrature_workspace(4016, 64))));
  SIC_CHECK(throws([] { sic::construct::resources::Model too_large(4096, 1000, 1, 1, 13); }));

  // The closure cap is checked before inserting an unplanned BFS element.
  sic::exact::Arithmetic ar(3);
  sic::exact::Group group(ar);
  group.gens.push_back(group.principal_gen(3, 0));
  bool checked = false;
  SIC_CHECK(throws([&] {
    group.close(
        sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")), 1,
        [&](size_t bases, size_t generators) { checked = bases > 0 && generators == 1; });
  }));
  SIC_CHECK(checked);
  SIC_CHECK(group.N == 0);
  // An accelerator that exceeds its class slot must fail before reading seed
  // data, leaving the caller able to use the certified quadrature fallback.
  sic::exact::GhostRefiner ghost(8, 1, 1);
  ghost.workspace_limit = 1;
  std::vector<acb_struct> seed, output;
  output.resize(1);
  acb_init(&output[0]);
  arb_const_pi(acb_realref(&output[0]), 1024);
  std::string why;
  SIC_CHECK(!ghost.refine(seed, 400, output, why));
  SIC_CHECK(output.empty());  // stale late-retry payload is released before admission
  SIC_CHECK(why.find("admitted class slot") != std::string::npos);
  ghost.workspace_limit = UINT64_MAX;
  SIC_CHECK(!ghost.refine(seed, WORD_MAX, output, why));
  SIC_CHECK(why.find("admitted class slot") != std::string::npos);
  SIC_CHECK(!ghost.refine(seed, 400, seed, why));
  SIC_CHECK(why.find("distinct") != std::string::npos);
  seed.resize(64);
  for (auto& value : seed) acb_init(&value);
  acb_indeterminate(&seed[1]);
  SIC_CHECK(!ghost.refine(seed, 400, output, why));
  SIC_CHECK(why.find("nonfinite") != std::string::npos);
  for (auto& value : seed) acb_clear(&value);
  seed.clear();
  // A valid small ghost reaches the conditioning proposal, where a failed
  // reused estimate must refuse before conversion/allocation at huge bits.
  sic::exact::Group standard(ar);
  standard.generate_standard();
  standard.close(
      sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")), 64);
  sic::exact::Table table(ar, 256);
  table.build_class(0);
  seed.resize(64);
  for (auto& value : seed) acb_init(&value);
  for (long y = 1; y < 64; ++y) table.read(&seed[y], standard.ghost_point(y / 8, y % 8));
  sic::exact::GhostRefiner invalid_condition(8, 1, 1);
  invalid_condition.S_hint.resize(14);
  std::iota(invalid_condition.S_hint.begin(), invalid_condition.S_hint.end(), 1);
  invalid_condition.cond_hint = std::numeric_limits<double>::infinity();
  SIC_CHECK(!invalid_condition.refine(seed, 400, output, why));
  SIC_CHECK(why.find("precision") != std::string::npos);
  SIC_CHECK(output.empty());
  // An incomplete hint is not a square subsystem. Ignore it and obtain a
  // fresh modular midpoint selection followed by full root certification.
  sic::exact::GhostRefiner incomplete_hint(8, 1, 1);
  incomplete_hint.S_hint.assign(3, 1);
  incomplete_hint.cond_hint = std::numeric_limits<double>::infinity();
  incomplete_hint.retain_certified_root = true;
  SIC_CHECK(incomplete_hint.refine(seed, 400, output, why));
  SIC_CHECK(incomplete_hint.S.size() == 14);
  SIC_CHECK(incomplete_hint.selection_kind ==
            sic::exact::GhostRefiner::SelectionKind::modular_midpoint);
  SIC_CHECK(std::isnan(incomplete_hint.cond_est));
  SIC_CHECK(bool(incomplete_hint.certified_root));
  SIC_CHECK(!output.empty());
  sic::exact::GhostRefiner underestimated_matrix(8, 1, 1);
  underestimated_matrix.S_hint = invalid_condition.S_hint;
  underestimated_matrix.cond_hint = std::exp2(300.0);
  underestimated_matrix.matrix_precision_hint = 128;
  underestimated_matrix.workspace_limit = 600000;
  SIC_CHECK(!underestimated_matrix.refine(seed, 400, output, why));
  SIC_CHECK(why.find("admitted class slot") != std::string::npos);
  SIC_CHECK(underestimated_matrix.newton_iters == 0);
  for (auto& value : seed) acb_clear(&value);
  return sic_test::report();
}
