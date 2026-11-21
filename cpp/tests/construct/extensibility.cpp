// SPDX-License-Identifier: AGPL-3.0-or-later
#include <iomanip>
#include <iostream>
#include <sstream>

#include "construct/components/components.hpp"
#include "construct/components/exact_services.hpp"
#include "construct/model/entry_order.hpp"
#include "construct/model/options.hpp"
#include "sic/sicx/finalize.hpp"
#include "sicx/codec/block.hpp"
#include "sicx/container/rescaling_input.hpp"
#include "sicx/model/branches.hpp"

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template <class F>
void refuses(sic::ErrorKind kind, F function) {
  try {
    function();
  } catch (const sic::Error& error) {
    require(error.kind() == kind, "unexpected refusal category");
    return;
  }
  throw std::runtime_error("expected policy refusal");
}
int main(int argc, char** argv) {
  require(argc == 2, "fixture argument");
  static_assert(sizeof(sic::support::Index) <= sizeof(int64_t));
  require(sic::support::checked_index(7) == 7 && sic::support::kNoIndex < 0,
          "index alias contract");
  refuses(sic::ErrorKind::resource, [] {
    sic::support::admit_index_count(
        static_cast<uint64_t>(std::numeric_limits<sic::support::Index>::max()) + 1);
  });
  sic::exact::DimensionPolicy dimension;
  refuses(sic::ErrorKind::invalid_argument, [&] { (void)dimension.checked(13); });
  dimension.maximum_exponent = 20;
  require(dimension.checked(13) == 8192, "raised dimension policy");
  require(sic::exact::CanonicalProfile::maximal_order(7) &&
              !sic::exact::CanonicalProfile::maximal_order(6),
          "canonical profile changed");
  sic::sicx::Meta general;
  general.d = 6;
  general.smith = {2};
  general.units = std::make_shared<const sic::sicx::UnitGroup>(6);
  general.unit_generator_words = {{1}};
  sic::construct::EntryOrder translations;
  translations.derive(general);
  require(translations.representatives == std::vector<long>{0, 1, 2, 3},
          "general scalar representatives");
  require(general.entry(4).representative == 2 && general.entry(4).word == std::vector<uint64_t>{1},
          "general entry translation");
  require(translations.index == std::vector<sic::support::Index>{0, 1, 2, 3, 2, 1},
          "general representative indices");
  sic::construct::ProductionPolicy policy;
  policy.validate();
  refuses(sic::ErrorKind::resource, [&] { (void)policy.valuation.admit(uint64_t{1} << 31, 0); });
  policy.valuation = {uint64_t{16} << 30, uint64_t{32} << 30};
  policy.precision.maximum_bits = 131072;
  policy.validate();
  require(policy.valuation.admit(uint64_t{1} << 31, 0) == (uint64_t{8} << 30),
          "raised valuation allowance");
  sic::sicx::Reader reader(argv[1], policy.resources);
  auto high = reader.meta();
  for (auto& e : high.elements)
    if (e.kind != "rep") e.max_bits = 65536;
  unsigned calls = 0;
  auto source = [&](size_t id, const sic::sicx::LeafVisitor& visit) {
    ++calls;
    sic::sicx::stored_element_source(reader, id)(visit);
  };
  refuses(sic::ErrorKind::precision, [&] { (void)sic::sicx::canonical_branches(high, source); });
  require(calls == 0, "default ceiling must refuse before reading leaves");
  const auto branches = sic::sicx::canonical_branches(high, source, policy.precision.maximum_bits);
  require(calls && branches.rectangles.size() == high.levels(),
          "raised branch ceiling was not used");
  auto invalid_json = policy;
  invalid_json.resources.json_depth = 1025;
  refuses(sic::ErrorKind::invalid_argument, [&] { invalid_json.validate(); });
  // A lowered spool allowance must refuse before reading/allocating a payload.
  auto low = policy.resources;
  low.block_bytes = 1;
  refuses(sic::ErrorKind::resource, [&] {
    (void)sic::sicx::StreamWriter::read_chunk_at(argv[1], reader.index().front(), low);
  });
  sic::construct::ProducerWriter component;
  sic::sicx::StreamOptions options;
  auto restrictive = policy;
  restrictive.resources.block_bytes = 1;
  // D must carry the caller's allowance into the concrete writer.
  std::string temporary = "/tmp/sic-policy-XXXXXX";
  require(::mkdtemp(temporary.data()), "temporary policy directory");
  struct Cleanup {
    std::filesystem::path p;
    ~Cleanup() { std::filesystem::remove_all(p); }
  } cleanup{temporary};
  auto output = component.open(std::filesystem::path(temporary) / "out", reader.meta(), options, 1,
                               restrictive);
  refuses(sic::ErrorKind::resource,
          [&] { output->writer().chunk("PROV", 0, sic::sicx::text_span("{}")); });
  auto bounded = policy;
  bounded.alignment_samples = 1;
  sic::construct::AlignmentOptions alignment;
  refuses(sic::ErrorKind::invalid_argument,
          [&] { sic::construct::ProducerOptions rejected(2, alignment, bounded); });
  auto read_limit = policy.resources;
  read_limit.block_bytes = 1;
  refuses(sic::ErrorKind::resource, [&] { sic::sicx::Reader rejected(argv[1], read_limit); });
  sic::construct::ComponentRegistry registry;
  unsigned evaluations = 0;
  registry.add_kind("sic-test-generator/1.0", {{true, false, false},
                                               [](unsigned, const auto&) {},
                                               [](unsigned, const auto&, slong) { return true; }});
  registry.add_family("sic-test-custom-family/1.0",
                      {{"sic-test-generator/1.0"}, [&](const auto& choices, slong, bool, bool) {
                         ++evaluations;
                         sic::construct::EvaluatedTower result;
                         result.sel = {std::get<sic::construct::Candidate>(choices[0].form).index};
                         result.g.resize(1);
                         result.fc.resize(1);
                         result.Cg.resize(1);
                         result.log2U.resize(1);
                         result.certified_Cg.resize(1);
                         result.certified_Cf.resize(1);
                         return result;
                       }});
  sic::construct::Candidate custom;
  custom.kind = "sic-test-generator/1.0";
  custom.index = 19;
  std::vector<sic::construct::GeneratorChoice> custom_choices{{custom, {}}};
  sic::construct::RegistryEvaluator dispatched(registry, "sic-test-custom-family/1.0",
                                               custom_choices);
  require(dispatched.evaluate_all(custom_choices, 256, false).sel[0] == 19 && evaluations == 1,
          "registry kind dispatch");
  refuses(sic::ErrorKind::unsupported,
          [&] { (void)dispatched.evaluate_all(custom_choices, 256, true); });
  std::get<sic::construct::Candidate>(custom_choices[0].form).kind =
      "unknown/1";  // identity-negative
  refuses(sic::ErrorKind::unsupported,
          [&] { (void)dispatched.evaluate_all(custom_choices, 256, false); });
  sic::sicx::Meta toy;
  toy.degrees = {2, 2};
  toy.width = {1, 2, 4};
  toy.D = 4;
  sic::sicx::FmpzPair pair;
  fmpz_set_ui(pair.x, 16);
  auto leaves = [&](const sic::sicx::LeafVisitor& visit) { visit(3, pair.x, pair.y); };
  sic::sicx::RescalingCollector unconstrained(toy), constrained(toy);
  unconstrained.add({"rep", 2, -1, -1, 1}, leaves);
  constrained.add({"rep", 2, -1, -1, 1}, leaves);
  unsigned valuation_visits = 0;
  constrained.valuations([&](int64_t, uint64_t, std::span<const uint32_t> cells) {
    require(cells.size() == 4, "valuation view");
    ++valuation_visits;
  });
  const std::vector<uint64_t> ceilings{0, 4};
  require(unconstrained.compute(1).shifts == std::vector<uint64_t>{4, 0}, "default greedy changed");
  require(constrained.compute(1, ceilings).shifts == std::vector<uint64_t>{0, 4} &&
              valuation_visits == 1,
          "constraint-aware greedy");
  sic::construct::ExactTower exact;
  exact.metadata = reader.meta();
  for (size_t i = 0; i < exact.metadata.elements.size(); ++i)
    exact.elements.push_back({sic::sicx::element_id_of(exact.metadata, exact.metadata.elements[i]),
                              sic::sicx::stored_element_source(reader, i)});
  auto exact_limit = sic::construct::ProductionPolicy{};
  exact_limit.exact_service_bytes = 1;
  refuses(sic::ErrorKind::resource,
          [&] { sic::construct::ExactServices refused(exact, exact_limit); });
  sic::construct::ExactServices services(exact, sic::construct::ProductionPolicy{});
  const auto denominators = services.integrality_denominators();
  require(denominators.size() == exact.metadata.levels(), "integrality witnesses");
  const auto value =
      services.from_trace(exact.elements.back().id.level, exact.elements.back().leaves);
  const auto product =
      services.multiply(value, services.inverse(value, exact.elements.back().id.level),
                        exact.elements.back().id.level);
  require(fmpq_is_one(product[0].a) && fmpq_is_zero(product[0].b), "exact inverse contract");
  require(services.isolate().rectangles.size() == exact.metadata.levels(), "exact branch service");
  services.verify_written(argv[1]);
  refuses(sic::ErrorKind::invalid_argument, [&] {
    (void)services.galois(
        value, exact.elements.back().id.level,
        std::vector<sic::construct::ExactServices::Value>(exact.elements.back().id.level));
  });
  sic::construct::ExactServices::Value oversized(1);
  fmpz_one(fmpq_numref(oversized[0].a));
  fmpz_mul_2exp(fmpq_numref(oversized[0].a), fmpq_numref(oversized[0].a), 1u << 20);
  refuses(sic::ErrorKind::resource, [&] { (void)services.multiply(oversized, oversized, 0); });
  refuses(sic::ErrorKind::resource, [&] { (void)services.inverse(oversized, 0); });
  refuses(sic::ErrorKind::resource, [&] { (void)services.galois(oversized, 0, {}); });
  refuses(sic::ErrorKind::resource, [&] {
    (void)services.from_trace(0, [&](const sic::sicx::LeafVisitor& visit) {
      visit(0, fmpq_numref(oversized[0].a), pair.y);
    });
  });
  sic::construct::PrecisionManager precision{128, 128};
  precision.working(256);
  require(precision.working_bits == 256, "working precision service");
  bool precision_requested = false;
  try {
    precision.request(512, "test precision request");
  } catch (const sic::construct::ComponentRefusal& error) {
    precision_requested = error.requested_bits == 512;
  }
  require(precision_requested, "typed precision refusal");
  std::cout << "production policy refusal and raised allowances: PASS\n";
}
