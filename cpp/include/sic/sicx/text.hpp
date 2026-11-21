// SPDX-License-Identifier: AGPL-3.0-or-later
// SICX 1.0 canonical text form: JSON Lines, schema id sicx-text-json/1.0
// (docs/sicx.md section "Text form"; docs/schemas/sicx-text-json-1.schema.json).
//
// One canonical JSON object per line (sorted keys, no whitespace, every number
// a decimal string), in this order:
//   header   {"content_hash":"sha256","required_features":[],"schema":"sicx-text-json/1.0",
//             "sicx_version":["1","0"]}
//   EXMT     {"chunk":"EXMT","flags":"1","meta":{...}}
//   TOWR/BODY chunks in binary file order:
//            {"chunk":...,"codec":"0","flags":"1","leaves":[...]}
//            each leaf: {"count","element","first","runs"}
//            runs: ["zeros","n"] | ["pairs",[["x","y"],...]]
//            (stored coordinates, original basis)
//   RAYS     {"chunk":"RAYS","flags":"1","json":{...}} (finalized files only)
//   BALL     {"chunk":"BALL","flags":"1","label","ray","values":[[[M,E,m,e],
//             [M,E,m,e]],...]} (decimal Arb fields; one chunk per ray)
//   PREC/PROV {"chunk":...,"flags":"0","json":{...}} (when present)
//   final    {"content_sha256","exact_root","lines","merkle_root","sicx_bytes","sicx_sha256"}
// EXMT is first in both forms; undefined chunk types are rejected. LDIR, PDIR
// and the footer are recomputed. Text -> binary reproduces the .sicx byte for
// byte (checked against sicx_sha256); binary -> text is deterministic.
// Both directions stream with memory bounded by a window of blocks.
#pragma once

#include <fstream>
#include <functional>
#include <istream>
#include <ostream>

#include "sic/sicx/verify.hpp"

namespace sic::support::sha256 {
class Hasher;
}
namespace sic::sicx {

inline constexpr const char* kTextSchema = "sicx-text-json/1.0";

sic::support::Json dec(uint64_t v);
sic::support::Json dec_fmpz(const fmpz_t v);

sic::support::Json leaf_json(const Meta& meta, std::span<const uint8_t> payload);
std::string block_line(const Meta& meta, const IndexEntry& entry, const ByteBuffer& payload);

sic::support::Json ball_part_json(const BallPart& part);
BallPart ball_part_from_json(const sic::support::Json& j, unsigned limbs);

// The text-form line of chunk i (any type), verified against PDIR (and, at
// Reader construction, the Merkle root).
std::string chunk_line(const Reader& reader, size_t i);

class LineSink {
 public:
  virtual ~LineSink() = default;
  virtual void line(const std::string& text) = 0;  // without the newline
};

// Streams the text form; `threads` blocks are converted concurrently and written in order.
void dump_text_json(const Reader& reader, const std::string& sicx_sha256, LineSink& sink,
                    unsigned threads = 1);

class StreamSink : public LineSink {
  std::ostream& out_;

 public:
  explicit StreamSink(std::ostream& out);
  void line(const std::string& text) override;
};

// Part files NAME.part-NNNNN.jsonl split at line (block) boundaries once a part
// reaches part_bytes, plus NAME.parts.json listing name, bytes, lines and SHA-256.
class PartSink : public LineSink {
  std::filesystem::path base_;
  uint64_t limit_, bytes_ = 0, lines_ = 0;
  std::ofstream out_;
  std::unique_ptr<support::sha256::Hasher> hasher_;
  sic::support::Json parts_ = sic::support::Json::array_value();
  std::string name_;
  void close_part();

 public:
  PartSink(std::filesystem::path base, uint64_t limit);
  void line(const std::string& text) override;
  void finish();
  ~PartSink() override;
};

// Line source over a single .jsonl or a .parts.json index (parts are hash-checked).
class LineSource {
  std::vector<std::filesystem::path> files_;
  std::vector<std::string> hashes_;
  std::vector<uint64_t> expected_bytes_, expected_lines_;
  uint64_t part_bytes_ = 0, part_lines_ = 0;
  size_t file_ = 0;
  std::ifstream in_;
  std::unique_ptr<support::sha256::Hasher> part_hash_;
  bool open_next();

 public:
  explicit LineSource(const std::filesystem::path& path);
  bool next(std::string& text);
  ~LineSource();
};

// Text form -> .sicx (exclusive create). Checks the content hash and that the
// produced file has the recorded SHA-256; on any failure the output is removed.
std::string text_json_to_binary(const std::filesystem::path& in_path,
                                const std::filesystem::path& final_path);

}  // namespace sic::sicx
