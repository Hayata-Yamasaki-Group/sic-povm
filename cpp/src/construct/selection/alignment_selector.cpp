// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/selection/alignment_selector.hpp"

#include "construct/model/memory_plan.hpp"
#include "construct/selection/alignment_search.hpp"
#include "sic/error.hpp"

namespace sic::construct {
std::vector<GeneratorChoice> ConjugateAlignmentSelector::select(const TowerShape&,
                                                                TowerContext& tower,
                                                                Context& context,
                                                                PrecisionManager& precision,
                                                                CheckpointStore& checkpoints) {
  context.check_stop();
  alignment::AlignInput input;
  input.t = static_cast<int>(base_.size());
  input.ps = tower.step_degrees;
  input.Ncos = tower.coset_counts;
  input.threads = context.threads;
  input.seed = options_.seed;
  input.train = options_.train;
  input.cand = options_.candidates;
  input.sweeps = options_.sweeps;
  input.cache_bytes = options_.cache_bytes.value_or(options_.memory_bytes);
  input.batch_bytes = options_.batch_bytes;
  input.worker_bytes = options_.worker_bytes;
  input.lg.resize(input.t);
  input.searchable.assign(input.t, 0);
  for (int i = 0; i < input.t - 1; ++i) input.searchable[i] = input.ps[i] == 2;
  for (size_t i = 0; i < tower.representative_levels.size(); ++i)
    input.reps.push_back({tower.representative_levels[i], {}, tower.representative_names.at(i)});
  input.perm = tower.permutation;
  MemoryPlanner planner(context.memory);
  input.threads =
      planner.adjust("alignment preparation", input.threads, 8 * sic::support::resources::kMib);
  auto replay_probe = input;
  replay_probe.cache_bytes = 0;
  auto core = std::max(sic::support::resources::inspect().rss,
                       tower.selection_live_bytes(precision.selection_bits, input.threads));
  while (input.threads > 1) {
    replay_probe.threads = input.threads;
    const auto probe = alignment::align_memory(replay_probe);
    if (sic::support::resources::add(core,
                                     sic::support::resources::add(probe.inputs, probe.workspace)) <=
            context.memory.limit() &&
        sic::support::resources::add(probe.inputs, probe.workspace) <= options_.memory_bytes)
      break;
    input.threads = std::max(1, input.threads / 2);
    core = std::max(sic::support::resources::inspect().rss,
                    tower.selection_live_bytes(precision.selection_bits, input.threads));
  }
  const auto requested_cache = input.cache_bytes;
  auto select_cache = [&](uint64_t resident, bool include_inputs) {
    input.cache_bytes = requested_cache;
    auto cached = alignment::align_memory(input);
    input.cache_bytes = 0;
    auto replay = alignment::align_memory(input);
    if (cached.cache) {
      planner.reset();
      const auto mandatory = sic::support::resources::add(
          resident,
          sic::support::resources::add(include_inputs ? replay.inputs : 0, replay.workspace));
      const auto extra = cached.workspace - replay.workspace;
      const auto choice = planner.decide(
          "alignment/F", {extra, false, true, 1}, mandatory,
          sic::support::resources::add(
              resident, options_.memory_bytes > replay.inputs
                            ? options_.memory_bytes - (include_inputs ? 0 : replay.inputs)
                            : 0));
      if (choice == Retention::keep) {
        input.cache_bytes = requested_cache;
        return cached;
      }
    }
    return replay;
  };
  auto memory = select_cache(core, true);
  const auto alignment_bytes = sic::support::resources::add(memory.inputs, memory.workspace);
  if (alignment_bytes > options_.memory_bytes)
    throw sic::support::resources::AdmissionError(
        "alignment memory budget is below the complete replay live set");
  const auto initial_ceiling = sic::support::resources::add(core, alignment_bytes);
  context.memory.require("alignment input/evaluation/search complete live set", initial_ceiling);
  if (context.progress)
    context.progress(
        "alignment admission: provisional_mode=" + std::string(memory.cache ? "cache" : "replay") +
        " core=" + std::to_string(core) + " inputs=" + std::to_string(memory.inputs) +
        " workspace=" + std::to_string(memory.workspace) +
        " total=" + std::to_string(initial_ceiling) +
        " alignment_budget=" + std::to_string(options_.memory_bytes));
  std::vector<std::vector<float>> representative_logs(input.reps.size());
  tower.logarithms(precision.selection_bits, input.threads, input.lg, input.searchable,
                   representative_logs);
  for (size_t i = 0; i < input.reps.size(); ++i)
    input.reps[i].lx = std::move(representative_logs[i]);
  representative_logs.clear();
  representative_logs.shrink_to_fit();
  const auto search_resident = sic::support::resources::inspect().rss;
  memory = select_cache(search_resident, false);
  const auto search_ceiling = sic::support::resources::add(search_resident, memory.workspace);
  if (sic::support::resources::add(memory.inputs, memory.workspace) > options_.memory_bytes)
    throw sic::support::resources::AdmissionError("alignment budget below actual replay live set");
  if (context.progress)
    context.progress("alignment search: mode=" + std::string(memory.cache ? "cache" : "replay") +
                     " inputs=" + std::to_string(memory.inputs) + " workspace=" +
                     std::to_string(memory.workspace) + " rss=" + std::to_string(search_resident) +
                     " total=" + std::to_string(search_ceiling));
  context.memory.require("alignment complete search workspace", search_ceiling);
  if (options_.trace)
    input.trace = [&](const char* stage) {
      context.check_stop();
      const auto resident = sic::support::resources::inspect().rss;
      if (resident > search_ceiling)
        throw sic::support::resources::AdmissionError("alignment exceeded admitted live set");
      context.memory.require("alignment memory trace", resident);
      if (context.progress)
        context.progress(std::string("alignment trace: ") + stage + " rss=" +
                         std::to_string(resident) + " admitted=" + std::to_string(search_ceiling));
    };
  auto sampled = std::chrono::steady_clock::now();
  input.rebalance = [&](int& workers, const std::function<void()>& evict) {
    const auto now = std::chrono::steady_clock::now();
    if (now - sampled < std::chrono::seconds(2)) return;
    sampled = now;
    workers = planner.adjust(
        "alignment search", workers, 8 * sic::support::resources::kMib,
        planner.control() == MemoryControl::keep_all ? std::function<void()>{} : evict);
  };
  const auto result = alignment::align_search(input);
  if (context.progress) context.progress(result.log);
  auto choices = base_;
  provenance_["name"] = sic::support::Json::string_value(name());
  provenance_["base"] = sic::support::Json::string_value(kStepSelection);
  provenance_["base_candidates"] = sic::support::Json::array_value();
  provenance_["levels"] = sic::support::Json::array_value();
  provenance_["model"] =
      sic::support::Json::string_value("full max-product, computed K-embedding, bare conjugates");
  provenance_["train"] = sic::support::Json::integer_value(options_.train);
  provenance_["cand"] = sic::support::Json::integer_value(options_.candidates);
  provenance_["sweeps"] = sic::support::Json::integer_value(options_.sweeps);
  provenance_["seed"] = sic::support::Json::integer_value(options_.seed);
  for (unsigned level = 0; level < choices.size(); ++level) {
    const auto& base = std::get<Candidate>(base_.at(level).form);
    provenance_["base_candidates"].array.push_back(sic::support::Json::string_value(base.name));
    if (!result.choice[level]) continue;
    auto word = tower.conjugate_word(level, result.choice[level]);
    choices[level].form = GaloisCombination{base, {{word, 1, 0}}};
    choices[level].forecast_at = [cells = input.Ncos.at(level)](slong bits) {
      return ResourceForecast{
          0, sic::support::resources::mul(cells, sic::support::resources::real_bytes(bits)), 0};
    };
    choices[level].forecast = choices[level].forecast_at(precision.selection_bits);
    if (!tower.certify(level, choices[level], precision.selection_bits))
      throw sic::Error(sic::ErrorKind::precision, "aligned generator choice did not certify");
    sic::support::Json record = sic::support::Json::object_value(),
                       term = sic::support::Json::object_value();
    record["level"] = sic::support::Json::integer_value(level);
    term["word"] = sic::support::Json::integer_array(word);
    term["a0"] = sic::support::Json::integer_value(1);
    term["a1"] = sic::support::Json::integer_value(0);
    record["terms"] = sic::support::Json::array_value();
    record["terms"].array.push_back(term);
    provenance_["levels"].array.push_back(record);
  }
  const auto canonical = sic::support::dump_json(provenance_);
  checkpoints.choice_digest = sic::sicx::hex(
      sic::sicx::sha256(reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size()));
  if (context.progress)
    context.progress("generator selection: " + canonical +
                     "; search_seconds=" + std::to_string(result.seconds));
  return choices;
}
}  // namespace sic::construct

namespace sic::construct {
ConjugateAlignmentSelector::ConjugateAlignmentSelector(std::vector<GeneratorChoice> base,
                                                       AlignmentOptions options)
    : base_(std::move(base)), options_(std::move(options)) {}
}  // namespace sic::construct

namespace sic::construct {
std::string ConjugateAlignmentSelector::name() const { return kConjugateAlignment; }
}  // namespace sic::construct

namespace sic::construct {
sic::support::Json ConjugateAlignmentSelector::provenance() const { return provenance_; }
}  // namespace sic::construct
