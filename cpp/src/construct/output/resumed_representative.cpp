// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/resumed_representative.hpp"

#include "sicx/codec/unrescaled.hpp"
#include "sicx/container/rescaling_input.hpp"

namespace sic::construct {
ResumedRepresentative::ResumedRepresentative(const std::filesystem::path& path,
                                             sic::sicx::ProducerWriter& output,
                                             size_t element_index, long position,
                                             long stored_prefix, long logical_half,
                                             const std::vector<uint64_t>& generator_shifts) {
  file.open(path, std::ios::binary);
  if (!file) throw sic::Error(sic::ErrorKind::io, "SICX resume: cannot open " + path.string());
  meta = &output.meta();
  element = element_index;
  const auto& info = meta->elements.at(element);
  if (info.kind != "rep" || info.position != position)
    throw sic::Error(sic::ErrorKind::invalid_format,
                     "SICX resume: adopted element is not the representative");
  for (const auto& entry : output.writer().index())
    if (entry.object == element) blocks.push_back(entry);
  if (blocks.size() != info.blocks())
    throw sic::Error(sic::ErrorKind::invalid_format,
                     "SICX resume: adopted representative blocks are incomplete");
  prefix = stored_prefix;
  half = logical_half;
  shifts = generator_shifts;
}
}  // namespace sic::construct

namespace sic::construct {
bool ResumedRepresentative::load() {
  if (next_block == blocks.size()) return false;
  const auto& entry = blocks[next_block++];
  sic::sicx::ByteBuffer payload(entry.length);
  file.seekg((std::streamoff)(entry.offset + sic::sicx::kChunkHeaderBytes));
  if (!file) throw sic::Error(sic::ErrorKind::io, "resume: cannot seek to an adopted block");
  file.read(reinterpret_cast<char*>(payload.data()), (std::streamsize)payload.size());
  if (file.bad()) throw sic::Error(sic::ErrorKind::io, "resume: cannot read an adopted block");
  if (!file) throw sic::Error(sic::ErrorKind::integrity, "resume: truncated adopted block");
  if (sic::sicx::sha256(payload.data(), payload.size()) != entry.sha256)
    throw sic::Error(sic::ErrorKind::integrity, "resume: an adopted block changed on disk");
  const sic::sicx::Meta& shape = *meta;
  const auto& info = shape.elements.at(element);
  auto stored = sic::sicx::stored_element_source(shape, element,
                                                 [&](const auto& consume) { consume(payload); });
  auto source = sic::sicx::unrescaled_source(shape, sic::sicx::element_id_of(shape, info),
                                             std::move(stored), shifts);
  pairs.clear();
  at = 0;
  source([&](uint64_t pos, const fmpz* x, const fmpz* y) {
    if ((long)(pos % (uint64_t)half) >= prefix || pos / (uint64_t)half > 1)
      throw sic::Error(sic::ErrorKind::invalid_format, "resume: leaf outside stored bands");
    const size_t index = (pos / half) * prefix + pos % half;
    if (any && index <= last)
      throw sic::Error(sic::ErrorKind::invalid_format, "resume: leaves out of order");
    any = true;
    last = index;
    auto value = std::make_unique<sic::sicx::FmpzPair>();
    fmpz_set(value->x, x);
    fmpz_set(value->y, y);
    pairs.emplace_back(index, std::move(value));
  });
  return true;
}

void ResumedRepresentative::fill(size_t first, std::span<Leaf> out) {
  for (size_t q = 0; q < out.size(); ++q) {
    while (at == pairs.size() && load()) {
    }
    if (at < pairs.size() && pairs[at].index == first + q) {
      fmpz_swap(out[q].x, pairs[at].value->x);
      fmpz_swap(out[q].y, pairs[at].value->y);
      ++at;
    } else {
      if (at < pairs.size() && pairs[at].index < first + q)
        throw sic::Error(sic::ErrorKind::invalid_format, "SICX resume: adopted leaf skipped");
      fmpz_zero(out[q].x);
      fmpz_zero(out[q].y);
    }
  }
}

void ResumedRepresentative::finish() {
  while (at == pairs.size() && load()) {
  }
  if (at != pairs.size() || next_block != blocks.size())
    throw sic::Error(sic::ErrorKind::invalid_format,
                     "SICX resume: adopted leaves beyond the representative");
}

}  // namespace sic::construct
