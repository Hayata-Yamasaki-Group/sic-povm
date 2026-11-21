// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/container/rescaling_input.hpp"

#include "sicx/codec/rescale.hpp"
#include "sicx/codec/unrescaled.hpp"
namespace sic::sicx {
LeafSource stored_element_source(const Meta& meta, size_t id, StoredBlocks blocks) {
  return [&meta, id, blocks = std::move(blocks)](const LeafVisitor& visit) {
    const auto& element = meta.elements.at(id);
    IntCodec codec;
    PairTransform transform(meta.D0);
    FmpzPair value;
    sic::support::flint::Fmpz scale, x, y;
    if (fmpz_set_str(scale, element.scale.c_str(), 10))
      fail(sic::ErrorKind::invalid_format, "invalid stored element scale");
    blocks([&](std::span<const uint8_t> payload) {
      const auto header = decode_block(
          payload, meta,
          [&](uint64_t sub, const fmpz_t a, const fmpz_t b) {
            fmpz_mul(x, a, scale);
            fmpz_mul(y, b, scale);
            visit(element.layout.position(sub), x, y);
          },
          [](uint64_t, uint64_t) {}, codec, transform, value);
      if (header.element != id)
        fail(sic::ErrorKind::invalid_format, "stored block belongs to another element");
    });
  };
}

LeafSource stored_element_source(const Reader& reader, size_t id) {
  return stored_element_source(reader.meta(), id, [&reader, id](const auto& consume) {
    for (size_t block : reader.element_blocks(id)) {
      const auto payload = reader.leaf(block);
      consume(payload);
    }
  });
}

GeneratorRescaling rescaling_of_file(const Reader& reader, unsigned threads) {
  const Meta& meta = reader.meta();
  RescalingCollector collector(meta);
  for (size_t id = 0; id < meta.elements.size(); ++id)
    collector.add(element_id_of(meta, meta.elements[id]), stored_element_source(reader, id));
  return collector.compute(threads);
}
}  // namespace sic::sicx

namespace sic::sicx {
LeafSource unrescaled_source(const Reader& reader, size_t element) {
  const auto& meta = reader.meta();
  return unrescaled_source(meta, element_id_of(meta, meta.elements.at(element)),
                           stored_element_source(reader, element));
}
}  // namespace sic::sicx
