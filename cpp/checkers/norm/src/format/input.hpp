// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
#include "canonical_json.hpp"
namespace sic_check::norm {

class Reader {
  Meta m;
  std::vector<Chunk> chunks;
  std::vector<std::vector<Pair>> leaves;
  std::vector<std::vector<BallPart>> balls;

  static std::vector<uint64_t> word(const Json& j, const std::vector<uint64_t>& mod);

  void metadata(const Json& j);

  void raydata(const Json& j);

  ReadOptions options;
  Verification verification;
  std::vector<Logical> logical;
  std::vector<uint64_t> loaded_coordinates;
  std::vector<bool> complete;
  uint64_t dense_coordinates = 0;

  static uint64_t leaf_width(const Meta& meta, const Element& e);

  void layout();

  void decode_element_chunk(const Chunk& c);

  static void undo_pair(Pair& v, int64_t D0);

  static void varput(ByteBuffer& out, uint64_t v);

  static void intput(ByteBuffer& out, const Integer& x);

  ByteBuffer reencode_leaf(const Logical& l, const Integer& scale) const;

  void finish_elements();

  void rescaling();

  void decode_ball(const Chunk& c);

  static std::string type_at(std::span<const uint8_t> b, size_t at = 0);

  static bool element_type(const std::string& t);

  static bool json_type(const std::string& t);

  static void chunk_rules(const Chunk& c);

  void coverage();

  static Json json(const Chunk& c);

 public:
  explicit Reader(const std::string& path, ReadOptions requested = {});

  const Verification& report() const;

  std::string summary() const;

  const Meta& meta() const;

  const std::vector<Pair>& element_leaves(size_t id) const;

  const std::vector<BallPart>& ball_parts(size_t ray) const;
};
}  // namespace sic_check::norm
