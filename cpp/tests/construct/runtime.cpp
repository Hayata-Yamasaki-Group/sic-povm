// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/runtime/runtime.hpp"

#include <iostream>

#include "construct/components/components.hpp"
#include "construct/model/options.hpp"
#include "construct/model/run_services.hpp"
#include "construct/runtime/construction.hpp"
using namespace sic::construct;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
struct ForwardSelector : GeneratorSelector {
  std::string name() const override { return "sic-test-forward/1.0"; }
  std::vector<GeneratorChoice> select(const TowerShape&, TowerContext& tower, Context&,
                                      PrecisionManager&, CheckpointStore& checkpoint) override {
    auto choices = tower.select_candidates();
    for (auto& choice : choices) std::get<Candidate>(choice.form).kind = "sic-test-kind/1.0";
    checkpoint.choice_digest = generator_choice_digest(choices);
    return choices;
  }
};
GeneratorChoice native_choice(GeneratorChoice choice) {
  std::get<Candidate>(choice.form).kind = "sic-step-selection/1.0";
  return choice;
}
struct RefuseOnce : RescalingRule {
  bool refused = false;
  std::string name() const override { return sic::sicx::kRescalingRule; }
  sic::sicx::GeneratorRescaling choose(sic::sicx::RescalingCollector& collector,
                                       const ExactTowerOps& ops) override {
    if (!refused) {
      refused = true;
      throw ComponentRefusal(2048, "injected component precision refusal");
    }
    return collector.compute(ops.threads);
  }
};
struct RefuseAlways : RescalingRule {
  unsigned& calls;
  explicit RefuseAlways(unsigned& count) : calls(count) {}
  std::string name() const override { return sic::sicx::kRescalingRule; }
  sic::sicx::GeneratorRescaling choose(sic::sicx::RescalingCollector&,
                                       const ExactTowerOps&) override {
    // Small monotone requests exhaust the retry policy without a high-precision workload.
    throw ComponentRefusal(256L * (1 + ++calls), "injected persistent precision refusal");
  }
};
int main(int argc, char** argv) {
  require(argc == 2, "work argument");
  const std::filesystem::path work(argv[1]);
  std::filesystem::create_directories(work);
  setenv("SIC_TEST_ENV_SENTINEL", "preserve", 1);
  const int previous = flint_get_num_threads();
  for (int i = 0; i < 4; ++i) {
    auto output = work / ("run-" + std::to_string(i) + ".sicx");
    std::filesystem::remove(output);
    sic::support::KernelSettings settings{{"SIC_OUT", output.string()},
                                          {"SIC_THREADS", std::to_string(i + 1)},
                                          {"SIC_ENTRY_ARB", "1"},
                                          {"SIC_ORDER", "standard"},
                                          {"SIC_CLASS_THREADS", "2"}};
    AlignmentOptions alignment;
    alignment.selector = "step-selection";
    ProducerOptions options(2, alignment, {}, settings);
    Components components;
    components.selector = std::make_unique<ForwardSelector>();
    const auto owner = std::make_shared<ComponentRegistry>(components.registry);
    const std::weak_ptr<ComponentRegistry> shared = owner;
    components.registry.add_kind(
        "sic-test-kind/1.0", {{true, true, true},
                              [](unsigned, const auto&) {},
                              [shared](unsigned level, const auto& choice, slong bits) {
                                return shared.lock()->certify(level, native_choice(choice), bits);
                              }});
    components.evaluator_family = "sic-test-family/1.0";
    components.registry.add_family(
        "sic-test-family/1.0",
        {{"sic-test-kind/1.0"},
         [shared, i](const auto& choices, slong bits, bool bounded, bool verbose) {
           auto native = choices;
           for (auto& choice : native) choice = native_choice(choice);
           auto result =
               shared.lock()->evaluate("sic-dyadic-evaluator/1.0", native, bits, bounded, verbose);
           if (i == 2) result.g.back().character = false;
           return result;
         }});
    if (i == 1) components.rescaling = std::make_unique<RefuseOnce>();
    unsigned refusals = 0;
    if (i == 3) components.rescaling = std::make_unique<RefuseAlways>(refusals);
    sic::support::StopToken stop;
    unsigned messages = 0;
    if (i >= 2) {
      bool refused = false;
      try {
        (void)run(options, std::move(components), {&stop, {}});
      } catch (const sic::Error& error) {
        refused =
            error.kind() == (i == 2 ? sic::ErrorKind::construction : sic::ErrorKind::precision);
      }
      require(refused && !std::filesystem::exists(output),
              "structural or exhausted numerical refusal misclassified or published");
      if (i == 3)
        require(refusals == 4, "persistent precision refusal did not exhaust the retry loop");
      continue;
    }
    require(
        run(options, std::move(components), {&stop, [&](const std::string&) { ++messages; }}) == 0,
        "injected construction failed");
    require(messages > 0, "per-run progress sink was unused");
    sic::sicx::Reader reader(output);
    require(reader.meta().d == 4, "wrong output dimension");
    require(std::string(getenv("SIC_TEST_ENV_SENTINEL")) == "preserve",
            "library changed environment");
    require(flint_get_num_threads() == previous && !sic::support::current_kernel_settings &&
                !sic::support::current_stop_token,
            "library runtime was not restored");
  }
  // Cancel after physical-decoder construction and again after the forward
  // owners have been released, before the exact writer starts its elements.
  // A fresh in-process run afterwards must still have a clean FLINT/runtime lease.
  unsigned cancellation_index = 0;
  for (const std::string boundary :
       {"labelled conjugates of the step generators", "SICX generator rescaling ("}) {
    const auto directory = work / ("cancel-" + std::to_string(cancellation_index++));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto output = directory / "native.sicx";
    sic::support::KernelSettings settings{{"SIC_OUT", output.string()},
                                          {"SIC_THREADS", "4"},
                                          {"SIC_ORDER", "standard"},
                                          {"SIC_ENTRY_ARB", "1"}};
    AlignmentOptions alignment;
    alignment.selector = "step-selection";
    ProducerOptions options(2, alignment, {}, settings);
    sic::support::StopToken stop;
    bool observed = false, cancelled = false;
    try {
      (void)run(options, {}, {&stop, [&](const std::string& message) {
                                if (message.find(boundary) != std::string::npos) {
                                  observed = true;
                                  stop.request_stop();
                                }
                              }});
    } catch (const sic::Error& error) {
      cancelled = error.kind() == sic::ErrorKind::cancelled;
    }
    require(observed && cancelled && !std::filesystem::exists(output),
            "output-stage cancellation missed its boundary or published output");
    require(flint_get_num_threads() == previous && !sic::support::current_kernel_settings &&
                !sic::support::current_stop_token,
            "output-stage cancellation did not join and restore the runtime");
  }
  {
    const auto output = work / "d2.sicx";
    std::filesystem::remove(output);
    sic::support::KernelSettings settings{{"SIC_OUT", output.string()}, {"SIC_THREADS", "1"}};
    ProducerOptions options(1, {}, {}, settings);
    Components invalid;
    invalid.selector = std::make_unique<ForwardSelector>();
    bool rejected = false;
    try {
      (void)run(options, std::move(invalid), {});
    } catch (const sic::Error& error) {
      rejected = error.kind() == sic::ErrorKind::unsupported;
    }
    require(rejected && !std::filesystem::exists(output), "d2 silently ignored injected selector");
    unsigned messages = 0;
    require(
        run(options, {}, {nullptr, [&](const std::string&) { ++messages; }}) == 0 && messages >= 3,
        "d2 progress sink");
  }
  std::cout << "explicit isolated runs, injected evaluation, typed retry/refusal and d2: PASS\n";
}
