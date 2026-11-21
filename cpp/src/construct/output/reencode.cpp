// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/reencode.hpp"

namespace sic::construct {
void reencode(const sic::sicx::Reader& reader, const std::filesystem::path& output,
              unsigned threads, const sic::sicx::VerificationPolicy& policy) {
  if (!threads)
    throw sic::Error(sic::ErrorKind::invalid_argument, "reencode requires positive workers");
  (void)sic::checked_precision(256, 0, policy.precision.maximum_bits);
  QuarantineOutput quarantine(output, false);
  const auto& meta = reader.meta();

  uint64_t largest = 0, max_bits = 0, inverse_bits = 0, valuation_cells = 0;
  uint64_t cache_growth = 0, text_bytes = 0;
  for (const auto& entry : reader.index()) {
    largest = std::max(largest, entry.length);
    if (entry.type_name() == "TOWR" || entry.type_name() == "BODY")
      cache_growth = std::min<uint64_t>(
          reader.resource_limits().cache_bytes,
          sic::support::resources::add(
              cache_growth,
              sic::support::resources::add(
                  entry.length, sic::support::resources::add(
                                    256, sic::support::resources::mul(
                                             entry.leaf_count, 2 * sizeof(sic::sicx::LeafSpan))))));
    else if (entry.type_name() == "EXMT" || entry.type_name() == "RAYS" ||
             entry.type_name() == "PROV" || entry.type_name() == "PREC")
      text_bytes = sic::support::resources::add(text_bytes, entry.length);
  }
  // Reader caches grow across owners. Writer/Reader directories, Merkle temporaries and
  // parsed JSON also remain live independently of the largest active payload.
  const auto metadata_growth = sic::support::resources::add(
      sic::support::resources::add(sic::support::resources::mul(8, largest),
                                   sic::support::resources::mul(128, text_bytes)),
      sic::support::resources::add(
          sic::support::resources::mul(
              4096, sic::support::resources::add(
                        meta.elements.size(), sic::support::resources::add(
                                                  reader.index().size(), reader.logical().size()))),
          sic::support::resources::mul(64, meta.d)));
  std::set<std::pair<unsigned, uint64_t>> weights;
  bool representative = false;
  for (const auto& element : meta.elements) {
    max_bits = std::max(max_bits, element.max_bits);
    const auto id = sic::sicx::element_id_of(meta, element);
    const auto own = sic::sicx::own_weight(id, meta.degrees);
    uint64_t shift =
        own ? sic::support::resources::mul(own, meta.rescaling_shifts.at(id.level)) : 0;
    for (unsigned i = 0; i < id.level; ++i)
      shift = sic::support::resources::add(
          shift, sic::support::resources::mul(meta.degrees[i] - 1, meta.rescaling_shifts.at(i)));
    if (shift > 8 * sic::sicx::kMaxIntegerBytes)
      sic::sicx::fail(sic::ErrorKind::resource,
                      "reencode: inverse reconstruction resource refusal");
    inverse_bits = std::max(inverse_bits, sic::support::resources::add(element.max_bits, shift));
    if (own)
      weights.emplace(id.level, own);
    else
      representative = true;
  }
  for (const auto& [level, own] : weights) {
    (void)own;
    valuation_cells = sic::support::resources::add(valuation_cells, meta.width.at(level));
  }
  if (representative) valuation_cells = sic::support::resources::add(valuation_cells, meta.D);
  const auto leaf_bytes = sic::support::resources::add((max_bits + 7) / 8, 64);
  const auto per_worker = sic::support::resources::add(
      sic::support::resources::mul(4, largest),
      sic::support::resources::mul(
          uint64_t{1} << sic::sicx::kLogicalLeafLog2Limit,
          sic::support::resources::mul(
              4, sic::support::resources::add(sic::support::resources::mul(2, leaf_bytes), 128))));
  const std::vector<long> degrees(meta.degrees.begin(), meta.degrees.end());
  sic::support::resources::Budget budget;
  budget.require_additional(
      "file-route verification, codec, inverse and valuation buffers",
      sic::support::resources::add(
          sic::support::resources::add(sic::support::resources::mul(2, cache_growth),
                                       metadata_growth),
          sic::support::resources::add(
              sic::support::resources::mul(8, valuation_cells),
              sic::support::resources::add(
                  sic::support::resources::mul(std::max(1u, threads), per_worker),
                  branch_scratch_bytes(degrees, policy.precision.maximum_bits, inverse_bits, 0)))));
  (void)sic::sicx::verify_all(reader, threads, policy);
  sic::sicx::GeneratorRescaling shifts;
  {
    sic::sicx::RescalingCollector collector(meta, policy.valuation);
    for (size_t i = 0; i < meta.elements.size(); ++i)
      collector.add(sic::sicx::element_id_of(meta, meta.elements[i]),
                    sic::sicx::unrescaled_source(reader, i));
    Greedy2adicRule rule;
    ExactTowerOps ops;
    ops.threads = threads;
    shifts = rule.choose(collector, ops);
  }
  if (shifts.shifts != meta.rescaling_shifts)
    sic::sicx::fail(sic::ErrorKind::integrity, "reencode: recorded rescaling differs");
  for (size_t i = 0; i < meta.elements.size(); ++i) {
    const auto id = sic::sicx::element_id_of(meta, meta.elements[i]);
    const auto before =
        sic::sicx::element_stats(meta, id, sic::sicx::stored_element_source(reader, i));
    const auto after = sic::sicx::element_stats(
        meta, id,
        sic::sicx::rescaled_source(meta, id, sic::sicx::unrescaled_source(reader, i), shifts));
    if (before.digest != after.digest || before.info.scale != after.info.scale ||
        before.info.max_bits != after.info.max_bits || before.info.levels != after.info.levels)
      sic::sicx::fail(sic::ErrorKind::integrity,
                      "reencode: inverse/default-rule leaf identity failed");
  }
  // Empty B and the verified default C reproduce the same exact presentation. Serialize those
  // checked chunks through StreamWriter, preserving the input's physical grouping and optional
  // records. This identity route needs no second full private leaf spool.
  sic::sicx::StreamOptions options;
  options.durable = true;
  options.limits = reader.resource_limits();
  {
    sic::sicx::StreamWriter writer(quarantine.path(), options);
    for (size_t i = 0; i < reader.index().size(); ++i) {
      const auto& entry = reader.index()[i];
      const auto payload = reader.chunk(i);
      writer.chunk(entry.type_name(), entry.flags, payload, entry.object, entry.first, entry.count);
    }
    writer.finish();
  }
  quarantine.completed();
  sic::sicx::Reader checked(quarantine.path(), reader.resource_limits());
  (void)sic::sicx::verify_all(checked, threads, policy);
  if (checked.exact_root() != reader.exact_root() || checked.root() != reader.root() ||
      checked.size() != reader.size())
    sic::sicx::fail(sic::ErrorKind::integrity, "reencode: route differs from the input snapshot");
  quarantine.publish();
}

}  // namespace sic::construct
