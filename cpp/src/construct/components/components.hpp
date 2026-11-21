// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/components/production_policy.hpp"
#include "sicx/container/producer_writer.hpp"
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "construct/components/component_registry.hpp"
#include "construct/components/generator_choice.hpp"
#include "exact/certification/bounds/height_bounds.hpp"
#include "exact/storage/analytic_generator.hpp"
#include "sicx/codec/rescale.hpp"
#include "support/index.hpp"
#include "support/resources.hpp"
namespace sic::construct {
// Component instances are serialized on their construction owner thread. Distinct
// runs own distinct instances. Callbacks, Context/TowerContext spans and services
// borrow the session, cannot be retained past a call, and cannot reenter the core.
// Exceptions invalidate temporary outputs; discard them before retrying.

struct Context {
  const sic::support::resources::Budget& memory;
  unsigned threads;
  std::function<void(const std::string&)> progress;
  const sic::construct::ProductionPolicy* policy = nullptr;
  sic::support::StopToken* stop = nullptr;
  void check_stop() const;
};
class ComponentRefusal : public sic::Error {
 public:
  explicit ComponentRefusal(slong bits, const std::string& message);
  slong requested_bits;
};
struct PrecisionManager {
  slong selection_bits = 0;
  slong working_bits = 0;
  slong maximum_bits = sic::kNativeMaximumPrecision;
  void working(slong bits);
  [[noreturn]] void request(slong bits, const std::string& why) const;
};
// Per-run immutable choice identity and rescaling journal key. Spool/cache I/O
// stays with the core writer; components must not retain or mutate its handles.
struct CheckpointStore {
  std::string choice_digest;
  std::string rescaling_rule = sic::sicx::kRescalingRule;
};
struct TowerShape {
  long dimension = 0, order = 0, D0 = 0;
  std::vector<long> degrees, smith;
};
struct TowerContext {
  std::vector<char> galois_eligible;
  std::vector<long> coset_counts, step_degrees;
  std::vector<sic::support::Index> representative_levels;
  std::vector<std::string> representative_names;
  std::function<void(int, long, std::vector<sic::support::Index>&)> permutation;
  std::function<std::vector<long>(unsigned, long)> conjugate_word;
  std::function<uint64_t(slong, unsigned)> selection_live_bytes;
  std::function<void(slong, unsigned, std::vector<std::vector<float>>&, std::vector<char>&,
                     std::vector<std::vector<float>>&)>
      logarithms;
  // Services retain ownership in the core; a selector freezes descriptions, never rounded values.
  std::function<std::vector<GeneratorChoice>()> select_candidates;
  std::function<bool(unsigned, const GeneratorChoice&, slong)> certify;
  std::function<long(unsigned, long, long)> coset_product;
  std::span<const std::vector<long>> representatives;
};
struct GeneratorSelector {
  virtual ~GeneratorSelector() = default;
  virtual std::string name() const = 0;
  virtual sic::support::Json provenance() const;
  virtual std::vector<GeneratorChoice> select(const TowerShape&, TowerContext&, Context&,
                                              PrecisionManager&, CheckpointStore&) = 0;
};
std::string generator_choice_digest(const std::vector<GeneratorChoice>& choices);
class StepSelectionSelector final : public GeneratorSelector {
 public:
  std::string name() const override;
  std::vector<GeneratorChoice> select(const TowerShape&, TowerContext& tower, Context& context,
                                      PrecisionManager& precision,
                                      CheckpointStore& checkpoints) override;
};
struct ProvenanceRecord {
  std::string component, name;
  unsigned version = 1;
  sic::support::Json parameters = sic::support::Json::object_value();
};
struct ExactTower {
  sic::sicx::Meta metadata;
  struct Element {
    sic::sicx::ElementId id;
    sic::sicx::LeafSource leaves;
  };
  std::vector<Element> elements;
  std::vector<ProvenanceRecord> provenance;
};
class ExactServices;
struct ExactTowerOps {
  std::vector<long> degrees;
  unsigned threads = 1;
  // Lazily acquires owned exact arithmetic, denominator witnesses, branch isolation
  // and written-file verification; empty means explicitly unavailable.
  std::function<std::shared_ptr<ExactServices>()> exact_services;
  std::function<sic::sicx::LeafSource(const sic::sicx::ElementId&, sic::sicx::LeafSource,
                                      sic::sicx::LeafSource)>
      multiply;
  std::function<sic::sicx::LeafSource(const sic::sicx::ElementId&, sic::sicx::LeafSource,
                                      const std::vector<long>&)>
      galois;
};
struct RepresentationTransform {
  virtual ~RepresentationTransform() = default;
  virtual std::string name() const = 0;
  virtual ExactTower apply(ExactTower, ExactTowerOps&, Context&, PrecisionManager&,
                           CheckpointStore&) = 0;
  virtual bool invertible() const = 0;
  virtual ExactTower inverse(ExactTower, ExactTowerOps&, Context&);
  virtual void verify_written(const std::filesystem::path&, ExactTowerOps&, Context&);
  virtual bool adopt_verified(const sic::sicx::Reader&, CheckpointStore&) { return false; }
};
struct RescalingRule {
  virtual ~RescalingRule() = default;
  virtual std::string name() const = 0;
  virtual sic::sicx::GeneratorRescaling choose(sic::sicx::RescalingCollector&,
                                               const ExactTowerOps&) = 0;
};
class Greedy2adicRule final : public RescalingRule {
 public:
  std::string name() const override;
  sic::sicx::GeneratorRescaling choose(sic::sicx::RescalingCollector& collector,
                                       const ExactTowerOps& ops) override;
};
class NoRescalingRule final : public RescalingRule {
 public:
  std::string name() const override;
  sic::sicx::GeneratorRescaling choose(sic::sicx::RescalingCollector&,
                                       const ExactTowerOps& ops) override;
};
struct SicxWriter {
  virtual ~SicxWriter() = default;
  virtual std::unique_ptr<sic::sicx::ProducerWriter> open(
      const std::filesystem::path&, const sic::sicx::Meta&, const sic::sicx::StreamOptions&,
      unsigned threads, const sic::construct::ProductionPolicy& policy) = 0;
};
class ProducerWriter final : public SicxWriter {
 public:
  std::unique_ptr<sic::sicx::ProducerWriter> open(
      const std::filesystem::path& path, const sic::sicx::Meta& header,
      const sic::sicx::StreamOptions& options, unsigned,
      const sic::construct::ProductionPolicy& policy) override;
};
}  // namespace sic::construct

namespace sic::construct {
struct Components {
  Components();
  ComponentRegistry registry;
  std::string evaluator_family = kDyadicEvaluator;
  std::unique_ptr<GeneratorSelector> selector;
  std::vector<std::unique_ptr<RepresentationTransform>> transforms;
  std::unique_ptr<RescalingRule> rescaling;
  std::unique_ptr<SicxWriter> writer;
  void require_closed_form_supported() const;
  void require_native_supported() const;
};

std::string component_record(const Components& components, const CheckpointStore& checkpoints);
}  // namespace sic::construct
