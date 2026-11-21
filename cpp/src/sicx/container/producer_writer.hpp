// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/block.hpp"
#include "sicx/codec/integer.hpp"
#include "sicx/container/verify.hpp"
#include "sicx/container/verify.tpp"
#include "sicx/model/branches.hpp"
// Element-level streaming writer for producers (docs/sicx.md section 5.4).
//
// A producer hands over each element (tower, action or representative) as a
// re-iterable stream of its nonzero leaves in increasing full position. Per element:
//   pass 1: level set (minimal), scale (gcd of all integers), max_bits, logical digest;
//   pass 2: leaves divided by the scale are cut into blocks by the block rule, encoded
//           in admitted parallel batches, round-tripped, and committed in order (fsync each).
// Memory: an admitted bounded batch plus codec scratch; the element is never held whole.
// Resume: after an element's blocks are durable its ElementInfo is journaled as a note; a
// restarted producer may skip that element (done() adopts its recovered blocks).
// EXMT (with every element's info) is written after the last element; finish_exact()
// closes the exact part. Elements must arrive in canonical order (checked).
#pragma once
#include <fstream>
#include <functional>

#include "sic/sicx/branches.hpp"
#include "sic/sicx/verify.hpp"
#include "sic/support/kernel_settings.hpp"

namespace sic::sicx {

sic::support::Json element_info_json(const ElementInfo& e);

struct ElementStats {
  ElementInfo info;
  Digest digest{};
  uint64_t nonzero = 0;
  double bits_sum = 0;  // sum of bit lengths of all full integers (block-size forecasts)
};

// Pass 1 over one element.
ElementStats element_stats(const Meta& header, const ElementId& id, const LeafSource& source);

// One owner thread serializes public calls. add()/write_meta() synchronously borrow
// sources and run visitors synchronously; callbacks cannot reenter this
// writer. After an exception discard the writer; durable recovery uses a new instance.
class ProducerWriter {
 public:
  struct Stats {
    uint64_t tower_bytes = 0, body_bytes = 0, blocks = 0;
    uint64_t peak_buffer_count = 0, peak_workspace_bytes = 0;
    double commit_seconds = 0, encode_seconds = 0;
    double pass1_seconds = 0, collection_seconds = 0, parallel_seconds = 0;
  };

  // `header`: d, D0, degrees/width/D, smith, words, branches, field labels, catalogue,
  // block_coordinates. Elements are added later.
  // workers counts the owner too. K is bounded by workers, element blocks,
  // buffer_limit and fresh memory headroom; K full workspaces are admitted first.
  ProducerWriter(const std::filesystem::path& path, const Meta& header,
                 const StreamOptions& options, uint64_t physical_target = kPhysicalChunkTargetBytes,
                 sic::PrecisionPolicy branch_policy = {}, unsigned workers = 1,
                 uint64_t buffer_limit = UINT64_MAX);

  ~ProducerWriter();
  void preserve_spool_for_resume() { preserve_spool_ = true; }
  StreamWriter& writer() { return published_ ? *published_ : writer_; }
  void set_rescaling_shifts(std::vector<uint64_t> shifts);
  const Meta& meta() const { return header_; }
  // Branch rectangles enter EXMT only, so a producer that opens the writer before they are
  // final (a resume that adopts elements before the generator rescaling is known) sets them later.
  void set_branches(std::vector<std::array<std::string, 2>> branches);
  const Stats& stats() const { return stats_; }
  const std::vector<Digest>& digests() const { return digests_; }

  // True when a resumed run already holds this element completely; its blocks are adopted
  // and its info restored, so the caller may skip computing it.
  bool done(const ElementId& id);

  // One source block, its encoded payload and round-trip/codec temporaries.
  static uint64_t writer_workspace(const ElementInfo& e);

  // Returns an owned descriptor snapshot, stable across later add()/done() calls.
  // Adds one element: two passes over `source` (it must yield the same leaves both times).
  // `admit` (optional) runs after pass 1, before any block of the element is written: a
  // disk admission can use the exact level set and bit bound of the element.
  ElementInfo add(const ElementId& id, const LeafSource& source,
                  const std::function<void(const ElementInfo&)>& admit = {});

  // EXMT after the last element; validated by the reader rules first. Returns its text.
  std::string write_meta();

  // ------------------------------------------------ multi-node part files
  // A node writes the contiguous element range [first, first + n) as a part file
  // (StreamOptions::part): its blocks carry global element ids. finish_part() adds
  // NAME.elements.json (EXMT header without elements, the range and the ElementInfo of each
  // element); merge_element_parts() concatenates the parts in order and writes EXMT, giving
  // the same bytes as one node writing every element (the exact part; finalize afterwards).
  void set_first_element(uint64_t first);
  // `source` identifies the input of the run (e.g. the source export SHA-256 or the producer
  // plan); parts merge only if their sources are equal.
  void finish_part(const std::filesystem::path& path, const std::string& source);

 private:
  static std::string note_key(uint64_t element);
  void check_order(const ElementInfo& e) const;

  Meta header_;
  static StreamOptions spool_options(StreamOptions o);
  bool preserve_spool_ = false;
  StreamWriter writer_;
  std::filesystem::path path_;
  StreamOptions options_;
  std::unique_ptr<StreamWriter> published_;
  uint64_t physical_target_;
  sic::PrecisionPolicy branch_policy_;
  unsigned workers_;
  uint64_t buffer_limit_;
  Stats stats_;
  std::vector<Digest> digests_;
  uint64_t offset_ = 0;
};

// Merges element-range part files (ProducerWriter::finish_part) in order into the exact part
// of one .sicx: chunks are re-hashed and copied (append_part), the ranges must be contiguous
// from element 0, the headers identical, and EXMT is rebuilt and validated by the reader rules.
Digest merge_element_parts(const std::filesystem::path& out,
                           const std::vector<std::filesystem::path>& parts,
                           const StreamOptions& options = StreamOptions{false, false, {}, false});

}  // namespace sic::sicx
