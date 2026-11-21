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
namespace sic::construct {
int Construction::step_data() {  // ---------------- step data ----------------
  // Step data. For step i the generator comes from one candidate element x (sel[i]: one of the
  // generators x_j, with |iota(x_j)| <= 1 at the infinity_2 embeddings, or a cyclotomic or product
  // candidate): h_i = Tr_{L/F_{i+1}}(x) / |S| on G/H_{i+1}. Quadratic step: g_i = w_i = h_i -
  // sigma_i h_i (w_i(pc+1) = -w_i(pc)). Odd step: g_i = v_i = h_i, with minimal polynomial f_i =
  // X^p + sum_{k<p} a_k X^k over F_i; fc[i][k] is the coset vector of a_k on G/H_i (elementary
  // symmetric functions of the p conjugates). Cg[i] bounds |iota(g_i)| at the infinity_2
  // embeddings. certified_Cg and certified_Cf carry the proof bounds; Cg is only for heuristic
  // scoring. The leaves have height sum_i (p_i - 1) log max|g_i|, so each step takes the certified
  // candidate with the smallest log2 max|g_i| + log2 Cg[i] (the generators of the cyclotomic part,
  // for instance, give generators of small height).

  // A candidate element: zeta_{2d}^zpow (times sqrt(eps) if sq), whose conjugate at g
  // is zeta_{2d}^{zpow nn(g)} (times chi(g) sqrt(eps)); C bounds it at the infinity_2 embeddings.
  // (tb: the generator F(y), y = 2^{-(tj+1)}, read from the table tb: x_tj(g) = chi(g) T[g(0, 0, d
  // / 2^{tj+1})].)

  // cands: conjugate vectors of the candidates, Cc their infinity_2 bounds. With sel_in, step i
  // uses candidate sel_in[i]; otherwise each step takes the best certified candidate.

  // Step generators: each step takes the certified candidate of smallest height. The generic
  // element z is never needed by the adapted tower: an uncertified
  // step is refused.

  // candidates: the generators x_j (|iota| <= 1) and zeta_{2d}^{2^j}, sqrt(eps) zeta_{2d}^{2^j}
  // (heights ~0 at infinity_1: they give the steps of the cyclotomic part small generators)

  component_context.threads = (unsigned)nth;
  component_precision = sic::construct::PrecisionManager{
      first_pass_bits + 16, first_pass_bits + 16, producer_options.policy.precision.maximum_bits};

  tower_shape.dimension = d;
  tower_shape.order = (*ar_).order_index;
  tower_shape.degrees = ps;
  tower_shape.smith = (*group_).invf;

  tower_context.select_candidates = [&]() {
    const slong wp = first_pass_bits + 16;
    budget.require("coarse candidate selection", (*model_).support(wp));
    const auto candidates = cand_list();
    const StepSelectionKey selection_key{d, (*ar_).order_index, N, t};
    std::vector<sic::support::Index> proposed;
    if (const char* file = producer_options.step_selection) {
      const auto names = read_step_selection(file, selection_key);
      for (int i = 0; i < t; ++i) {
        auto found = std::find_if(candidates.begin(), candidates.end(),
                                  [&](const Cand& c) { return c.name == names[i]; });
        if (found == candidates.end() || (found->highest_needed >= 0 && i > found->highest_needed))
          throw sic::Error(sic::ErrorKind::invalid_argument,
                           "unavailable step-selection candidate at level " + std::to_string(i));
        proposed.push_back(found - candidates.begin());
      }
      sic::support::progressf(
          "step-selection proposal loaded: %d names; recomputing all selected generator "
          "certificates\n",
          t);
    }
    StepData s1 = step_data_multi(candidates, wp, producer_options.step_debug,
                                  proposed.empty() ? nullptr : &proposed);
    if (!s1.ok && !proposed.empty()) {
      sic::support::progressf(
          "step-selection proposal did not certify; searching all candidates\n");
      s1 = StepData{};
      s1 = step_data_multi(candidates, wp, producer_options.step_debug, nullptr);
    }
    if (!s1.ok)
      throw sic::Error(
          sic::ErrorKind::precision,
          "adapted witnessed tower generator certification needs more pass-1 precision");
    step_sel = s1.sel;
    selected_bounds = s1.certified_Cg;
    tower_context.galois_eligible.assign(t, 0);
    for (int i = 0; i < t - 1; ++i) {
      const auto& g = s1.g[i];
      tower_context.galois_eligible[i] =
          ps[i] == 2 && g.paired && !g.imaginary && !g.character && g.periodic.size() == 0;
    }
    if (phased_storage) {
      (*model_).generator_metadata = true;
      (*model_).phased_streamed_table = producer_options.stream_tables.value_or(true);
      for (int i = 0; i < t - 1; ++i) {
        if (s1.g[i].periodic.size())
          (*model_).descriptor_rows =
              sic::support::resources::add((*model_).descriptor_rows, s1.g[i].periodic.size());
        else {
          (*model_).generator_rows =
              std::max<sic::support::resources::Count>((*model_).generator_rows, 2 * Ncos[i]);
          (*model_).action_generator_rows =
              std::max<sic::support::resources::Count>((*model_).action_generator_rows, Ncos[i]);
        }
      }
    }
    if (!s1.g.back().character)
      throw sic::Error(sic::ErrorKind::construction,
                       "adapted tower requires the certified symbolic top generator");
    double h1 = 0;
    for (int i = 0; i < t; ++i) h1 += (ps[i] - 1) * s1.log2U[i];
    const auto cl = cand_list();
    if (const char* file = producer_options.save_step_selection) {
      std::vector<std::string> names;
      for (sic::support::Index index : step_sel) names.push_back(cl.at(index).name);
      write_step_selection(file, selection_key, names);
      sic::support::progressf(
          "step-selection search proposal saved; contains names only, no imported certificates\n");
    }
    sic::support::progressf("step generators:");
    for (int i = 0; i < t; ++i) sic::support::progressf(" %s", cl[step_sel[i]].name.c_str());
    sic::support::progressf("  (leaf height sum (p_i - 1) log2 max|g_i| = %.0f bits)   [%.1fs]\n",
                            h1, stamp());
    std::vector<sic::construct::GeneratorChoice> choices;
    for (int i = 0; i < t; ++i) {
      sic::construct::Candidate candidate;
      candidate.index = step_sel[i];
      candidate.name = cl.at(step_sel[i]).name;
      choices.push_back({std::move(candidate), {}});
    }
    return choices;
  };

  tower_context.coset_product = [&](unsigned level, long a, long b) {
    return coset(level, (*group_).mul(a, b));
  };
  tower_context.representatives = rep;
  auto native_certify = [&](unsigned level, const sic::construct::GeneratorChoice& choice,
                            slong wp) {
    const auto* candidate = std::get_if<sic::construct::Candidate>(&choice.form);
    if (const auto* combination = std::get_if<GaloisCombination>(&choice.form)) {
      if (level >= static_cast<unsigned>(t - 1) || ps[level] != 2 ||
          !tower_context.galois_eligible.at(level) || combination->terms.size() != 1 ||
          combination->terms[0].a0 != 1 || combination->terms[0].a1 != 0 ||
          combination->terms[0].word.size() != group_->invf.size())
        return false;
      for (size_t axis = 0; axis < group_->invf.size(); ++axis)
        if (combination->terms[0].word[axis] < 0 ||
            combination->terms[0].word[axis] >= group_->invf[axis])
          return false;
      candidate = &combination->base;
    }
    if (!candidate || level >= step_sel.size()) return false;
    if (wp == first_pass_bits + 16 && candidate->index == step_sel[level]) return true;
    auto proposed = step_sel;
    proposed[level] = candidate->index;
    return step_data_multi(cand_list(), wp, false, &proposed).ok;
  };
  auto validate_native = [this](unsigned level, const GeneratorChoice& choice) {
    const auto* candidate = std::get_if<Candidate>(&choice.form);
    if (!candidate) candidate = &std::get<GaloisCombination>(choice.form).base;
    const auto candidates = cand_list();
    if (level >= static_cast<unsigned>(t) || candidate->kind != kStepSelection ||
        candidate->index < 0 || static_cast<size_t>(candidate->index) >= candidates.size() ||
        !candidate->factors.empty() || candidate->twist != 0 ||
        candidate->name != candidates[candidate->index].name)
      throw sic::Error(sic::ErrorKind::unsupported, "unsupported native generator description");
    if (const auto* combination = std::get_if<GaloisCombination>(&choice.form)) {
      if (level + 1 >= static_cast<unsigned>(t) || ps[level] != 2 ||
          !tower_context.galois_eligible.at(level) || combination->terms.size() != 1 ||
          combination->terms[0].a0 != 1 || combination->terms[0].a1 != 0 ||
          combination->terms[0].word.size() != group_->invf.size())
        throw sic::Error(sic::ErrorKind::unsupported, "unsupported native Galois combination");
      for (size_t axis = 0; axis < group_->invf.size(); ++axis)
        if (combination->terms[0].word[axis] < 0 ||
            combination->terms[0].word[axis] >= group_->invf[axis])
          throw sic::Error(sic::ErrorKind::unsupported, "invalid native Galois word");
    }
  };
  for (const std::string kind : {kStepSelection, kGaloisChoice})
    if (!components.registry.has_kind(kind))
      components.registry.add_kind(kind, {{true, true, true}, validate_native, native_certify});
  tower_context.certify = [this](unsigned level, const GeneratorChoice& choice, slong bits) {
    return components.registry.certify(level, choice, bits);
  };
  generator_choices = components.selector->select(tower_shape, tower_context, component_context,
                                                  component_precision, component_checkpoints);

  if (!components.registry.has_family(kDyadicEvaluator))
    components.registry.add_family(
        kDyadicEvaluator,
        {{kStepSelection, kGaloisChoice},
         [&](const std::vector<sic::construct::GeneratorChoice>& choices, slong wp, bool backed,
             bool verbose) {
           std::vector<sic::support::Index> indices;
           for (const auto& choice : choices) {
             const auto* candidate = std::get_if<Candidate>(&choice.form);
             indices.push_back(candidate ? candidate->index
                                         : std::get<GaloisCombination>(choice.form).base.index);
           }
           struct Reset {
             const std::vector<GeneratorChoice>*& slot;
             const std::vector<GeneratorChoice>* previous;
             ~Reset() { slot = previous; }
           } reset{evaluation_choices, evaluation_choices};
           evaluation_choices = &choices;
           return step_data_multi(cand_list(), wp, verbose, &indices, backed);
         }});
  generator_evaluator = std::make_unique<RegistryEvaluator>(
      components.registry, components.evaluator_family, generator_choices);
  if (components.selector->name() != kStepSelection) {
    auto selected =
        generator_evaluator->evaluate_all(generator_choices, first_pass_bits + 16, false);
    if (!selected.ok || selected.certified_Cg.size() != static_cast<size_t>(t))
      throw sic::Error(sic::ErrorKind::construction, "injected selection metadata did not certify");
    if (selected.g.size() != static_cast<size_t>(t) || !selected.g.back().character)
      throw sic::Error(sic::ErrorKind::construction,
                       "native construction requires a symbolic top generator");
    selected_bounds = std::move(selected.certified_Cg);
  }
  // Only the empty transform composition is native in this step. A registered transform must
  // first provide its written-file verification in the output stage.
  components.require_native_supported();

  return 0;
}

}  // namespace sic::construct
