// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/components/components.hpp"

namespace sic::construct {
void Context::check_stop() const {
  if (stop)
    stop->check();
  else
    sic::support::check_stop();
}

[[noreturn]] void PrecisionManager::request(slong bits, const std::string& why) const {
  (void)sic::checked_precision(bits, 0, maximum_bits);
  throw ComponentRefusal(bits, why);
}

std::string generator_choice_digest(const std::vector<GeneratorChoice>& choices) {
  auto description = sic::support::Json::array_value();
  for (const auto& choice : choices) {
    auto record = sic::support::Json::object_value();
    const auto* candidate = std::get_if<Candidate>(&choice.form);
    const auto* combination = std::get_if<GaloisCombination>(&choice.form);
    const auto& base = candidate ? *candidate : combination->base;
    record["kind"] = sic::support::Json::string_value(base.kind);
    record["name"] = sic::support::Json::string_value(base.name);
    record["index"] = sic::support::Json::integer_value(base.index);
    record["factors"] = sic::support::Json::integer_array(base.factors);
    record["twist"] = sic::support::Json::integer_value(base.twist);
    record["terms"] = sic::support::Json::array_value();
    if (combination)
      for (const auto& term : combination->terms) {
        auto t = sic::support::Json::object_value();
        t["word"] = sic::support::Json::integer_array(term.word);
        t["a0"] = sic::support::Json::integer_value(term.a0);
        t["a1"] = sic::support::Json::integer_value(term.a1);
        record["terms"].array.push_back(t);
      }
    description.array.push_back(record);
  }
  const auto text = sic::support::dump_json(description);
  return sic::sicx::hex(
      sic::sicx::sha256(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

std::vector<GeneratorChoice> StepSelectionSelector::select(const TowerShape&, TowerContext& tower,
                                                           Context& context,
                                                           PrecisionManager& precision,
                                                           CheckpointStore& checkpoints) {
  context.check_stop();
  // The core callback retains certification and proposal fallback in their original order.
  auto result = tower.select_candidates();
  for (unsigned i = 0; i < result.size(); ++i)
    if (!tower.certify(i, result[i], precision.selection_bits))
      throw sic::Error(sic::ErrorKind::construction,
                       "selected generator lost its core certificate");
  checkpoints.choice_digest = generator_choice_digest(result);
  return result;
}

ExactTower RepresentationTransform::inverse(ExactTower, ExactTowerOps&, Context&) {
  throw sic::Error(sic::ErrorKind::unsupported, "transform has no verified inverse");
}

void RepresentationTransform::verify_written(const std::filesystem::path&, ExactTowerOps&,
                                             Context&) {
  throw sic::Error(sic::ErrorKind::unsupported, "transform has no written-file verifier");
}

sic::sicx::GeneratorRescaling NoRescalingRule::choose(sic::sicx::RescalingCollector&,
                                                      const ExactTowerOps& ops) {
  sic::sicx::GeneratorRescaling result;
  result.shifts.assign(ops.degrees.size(), 0);
  return result;
}

std::unique_ptr<sic::sicx::ProducerWriter> ProducerWriter::open(
    const std::filesystem::path& path, const sic::sicx::Meta& header,
    const sic::sicx::StreamOptions& options, unsigned threads,
    const sic::construct::ProductionPolicy& policy) {
  auto configured = options;
  configured.limits = policy.resources;
  return std::make_unique<sic::sicx::ProducerWriter>(
      path, header, configured, sic::sicx::kPhysicalChunkTargetBytes, policy.precision, threads);
}

void Components::require_closed_form_supported() const {
  require_native_supported();
  if (!dynamic_cast<const StepSelectionSelector*>(selector.get()) ||
      !dynamic_cast<const Greedy2adicRule*>(rescaling.get()) ||
      !dynamic_cast<const ProducerWriter*>(writer.get()) || evaluator_family != kDyadicEvaluator ||
      !registry.empty())
    throw sic::Error(sic::ErrorKind::unsupported,
                     "closed-form construction does not support injected components");
}

void Components::require_native_supported() const {
  if (!selector || !rescaling || !writer)
    throw sic::Error(sic::ErrorKind::invalid_argument, "missing production component");
  if (!transforms.empty())
    throw sic::Error(sic::ErrorKind::unsupported,
                     "native transform needs written-file verification");
  if (rescaling->name() != sic::sicx::kRescalingRule)
    throw sic::Error(sic::ErrorKind::unsupported,
                     "noncanonical native rescaling rule is unsupported");
}

std::string component_record(const Components& components, const CheckpointStore& checkpoints) {
  std::string record = components.selector->name() + "\n" + checkpoints.choice_digest + "\n" +
                       components.rescaling->name();
  if (components.evaluator_family != kDyadicEvaluator) record += "\n" + components.evaluator_family;
  return record;
}

}  // namespace sic::construct

namespace sic::construct {
ComponentRefusal::ComponentRefusal(slong bits, const std::string& message)
    : sic::Error(sic::ErrorKind::precision, message), requested_bits(bits) {}
}  // namespace sic::construct

namespace sic::construct {
void PrecisionManager::working(slong bits) {
  working_bits = sic::checked_precision(bits, 0, maximum_bits);
}
}  // namespace sic::construct

namespace sic::construct {
sic::support::Json GeneratorSelector::provenance() const {
  return sic::support::Json::object_value();
}
}  // namespace sic::construct

namespace sic::construct {
std::string StepSelectionSelector::name() const { return kStepSelection; }
}  // namespace sic::construct

namespace sic::construct {
std::string Greedy2adicRule::name() const { return sic::sicx::kRescalingRule; }
}  // namespace sic::construct

namespace sic::construct {
sic::sicx::GeneratorRescaling Greedy2adicRule::choose(sic::sicx::RescalingCollector& collector,
                                                      const ExactTowerOps& ops) {
  return collector.compute(ops.threads);
}
}  // namespace sic::construct

namespace sic::construct {
std::string NoRescalingRule::name() const { return "none"; }
}  // namespace sic::construct

namespace sic::construct {
Components::Components()
    : selector(std::make_unique<StepSelectionSelector>()),
      rescaling(std::make_unique<Greedy2adicRule>()),
      writer(std::make_unique<ProducerWriter>()) {}
}  // namespace sic::construct
