// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/contraction.hpp"
#include "decode/decoder.hpp"
#include "decode/input/element_source.hpp"
namespace sic::decode {
uint64_t Decoder::Impl::contract_body(const BodyJob& job, const Reader::ElementChunk& unit,
                                      const BodyPlan& body, const Tree& out_tree,
                                      std::vector<Sparse>& parts) {
  const auto& e = meta_.elements[body.rep_ids[job.r]];
  LeafConverter convert(meta_.D0, out_tree.prec, e.max_bits);
  Carry carry(out_tree, 0, e.block_level);
  ComplexBallVector value(1);
  uint64_t base = 0;
  const auto bytes = stream_verified_leaf(meta_, reader_.leaf_entry(job.chunk), unit, job.chunk,
                                          base, [&](uint64_t pos, const fmpz_t x, const fmpz_t y) {
                                            carry.zeros_until(pos);
                                            convert(value[0], x, y);
                                            carry.value(pos, value[0]);
                                          });
  if (base / out_tree.width[e.block_level] != job.b)
    fail(sic::ErrorKind::invalid_format, "BODY leaf position differs from its directory entry");
  parts[job.r].val[job.slot] = carry.release();
  return bytes;
}
}  // namespace sic::decode
