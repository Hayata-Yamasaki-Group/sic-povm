// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/decode/decoder.hpp"
#include "sicx/codec/block.hpp"
// Certified decoding of SICX files (docs/sicx.md section 11).
//
// Internal decoder interface; implementations are split by responsibility.
#pragma once

#include <flint/acb_poly.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>

#include "decode/input/leaf_values.hpp"
#include "decode/kernels/kernels.hpp"
#include "decode/model.hpp"
#include "decode/pool.hpp"
#include "decode/prefix/prefix_walk.hpp"
#include "sic/sicx/json.hpp"
#include "sic/sicx/verify.hpp"

namespace sic::decode {
using sic::sicx::Reader;

class Decoder::Impl {
 public:
  using Options = Decoder::Options;
  using Request = Decoder::Request;
  static Options validate_options(Options options, const Reader& reader);
  Impl(const Reader& reader, Options options);
  Impl(const Reader& reader, Options options, const std::vector<Request>& requests);
  static std::vector<size_t> input_chunks(const Reader& reader,
                                          const std::vector<Request>& requests);
  const Stats& stats() const;
  const Placement& placement() const;
  slong base_precision() const;
  unsigned pool_groups() const;
  ComplexBallVector decode(const std::vector<Request>& requests);
  friend class Decoder;

 private:
  const Reader& reader_;
  const Meta& meta_;
  Options options_;
  const std::thread::id owner_ = std::this_thread::get_id();
  void check_owner() const;
  std::ostream& progress() const;
  Placement place_;
  MemoryBudget budget_;
  std::unique_ptr<Pool> pool_;
  uint64_t tree_serial_ = 0;
  mutable Stats stats_;
  slong base_prec_ = 0;
  std::vector<ComplexBallVector> base_roots_;  // per level, at base_prec_
  std::vector<ElementLeaves> tower_;           // per element id (tower and action elements)
  struct ElementKey {
    std::string kind;
    unsigned level;
    int64_t axis, coefficient;
    auto operator<=>(const ElementKey&) const = default;
  };
  std::map<ElementKey, size_t> element_id_;
  std::vector<std::vector<uint64_t>> generations_;  // graph nodes by depth (depth >= 1)
  std::set<int64_t> planned_representatives_;
  std::set<int64_t> consumed_representatives_;
  bool exact_checked_ = false;
  void plan_input(const std::vector<Request>& requests);

  bool accurate(acb_srcptr v) const;
  static double seconds(std::chrono::steady_clock::time_point s);
  // Resident baseline for the budget: RSS after returning free heap pages.
  // Free heap pages are returned (malloc_trim, which costs milliseconds on a large
  // multi-arena heap) only when the resident set uses more than half the budget.
  void rebase();
  // Parsed-leaf bytes are at most this multiple of the payload bytes plus 160 B
  // per leaf (FmpzPair, two mpz with allocator overhead, index arrays); the
  // decision uses the payload bound before parsing.
  static constexpr uint64_t kParsedPerPayload = 8;

  void load_elements(const std::vector<size_t>& ids);
  uint64_t payload_of(size_t id) const;

  void load_tower();
  bool has_element(const std::string& kind, unsigned level, int64_t axis = -1,
                   int64_t k = -1) const;
  size_t element_id(const std::string& kind, unsigned level, int64_t axis = -1,
                    int64_t k = -1) const;

  // ---------------------------------------------------------- level-parallel contraction
  // Sparse level vector of one element: the occupied positions (increasing, in
  // units of width(level)) and the count(level) class values of each.
  struct Sparse {
    std::vector<uint64_t> pos;
    std::vector<ComplexBallVector> val;
  };
  // Lift sparse vectors from their level to `height`. Each class value is
  // combine_class over the present children (a missing child is a structural
  // zero and is omitted), exactly as in the Carry, so the values do not depend
  // on where the contraction was cut.
  void lift(const Tree& tree, std::vector<Sparse>& v, std::vector<unsigned>& level,
            unsigned height);
  // The element's value at `height` (zeros when it has no leaves); refuses nonfinite values.
  static ComplexBallVector top_value(Sparse& v, uint64_t classes);

  // Bytes of the level-parallel arrays for `elements` elements cut at level L,
  // `occupied` of whose subtrees at L hold leaves (UINT64_MAX: unknown, dense):
  // the slot arrays plus two consecutive levels of class vectors.
  uint64_t lift_bytes(const Tree& tree, uint64_t occupied, size_t elements, unsigned L,
                      unsigned height) const;
  // Frees the heap limbs of large ball vectors in parallel (their destructors are then cheap).
  void release_parallel(const std::vector<ComplexBallVector*>& vectors);
  void release_parallel(std::vector<ComplexBallVector>& vectors);
  void release_tree(Tree& tree);
  uint64_t carry_bytes(const Tree& tree, unsigned L) const;

  uint64_t nonempty_blocks(const std::vector<size_t>& ids, size_t first, size_t group,
                           uint64_t width, uint64_t cap) const;
  // Occupied subtrees of width `width` (UINT64_MAX when more than 2^20: treated as dense).
  uint64_t occupied(const std::vector<size_t>& ids, size_t first, size_t group,
                    uint64_t width) const;
  // Values of every element of `ids` (all lying in F_height) at the classes of
  // level `height`, contracted together.
  std::vector<ComplexBallVector> contract_many(const Tree& tree, const std::vector<size_t>& ids,
                                               unsigned height);

  // ---------------------------------------------------------- DAG
  struct Graph {
    std::unique_ptr<ReserveGuard> reservation;
    struct Node {
      uint64_t source;
      unsigned axis;
    };
    std::vector<Node> nodes{{0, 0}};
    std::vector<uint64_t> endpoints;
  };
  Graph plan(const std::vector<Request>& requests);

  struct Family {
    sic::decode::kernels::Polynomial polynomial;
    ComplexBallVector roots;
    Family();
  };

  static void quadratic_roots(ComplexBallVector& roots, acb_srcptr square, slong bits);
  // Unique overlap selection among certified isolated roots (sic::decode::kernels::select); with
  // `authoritative`, the selected root must lie inside the target rectangle.
  static unsigned member(const Family& family, acb_srcptr target, bool authoritative, slong bits,
                         acb_ptr selected, acb_ptr rounded);

  // Build the restriction tree of every graph node. With `authoritative`, node 0
  // is selected by the stored branch rectangles (base path); otherwise by
  // overlap with the certified base-path roots.
  Tree prepare(const Graph& graph, slong bits, bool authoritative,
               std::vector<std::vector<uint64_t>>* node_classes_out);

  void base_path();

  // Keep only the classes on paths to the endpoints (the engine's prune step),
  // relabelled in increasing old-label order at every level.
  static Tree prune(Tree& full, const std::vector<std::vector<uint64_t>>& node_classes,
                    const Graph& graph, std::vector<uint64_t>& endpoint_class);

  struct BodyJob {
    size_t r, chunk, slot;
    uint64_t b, bytes;
  };
  struct PhysicalTask {
    size_t owner;
    std::vector<BodyJob> leaves;
    uint64_t bytes = 0;
  };
  struct BodyPlan {
    std::vector<int64_t> reps;
    std::vector<size_t> rep_ids;
    std::vector<Sparse> parts;
    std::vector<unsigned> level;
    std::vector<std::vector<BodyJob>> jobs;
    std::vector<uint64_t> reserve;
    uint64_t largest = 0;
    size_t chunks = 0;
  };
  struct BodyAdmission {
    size_t last;
    uint64_t reserve, scheduling, carry;
  };
  BodyAdmission plan_body_admission(const BodyPlan& body, const Tree& tree, size_t first) const;
  BodyPlan plan_body(const std::vector<Request>& requests, const Tree& tree, slong wp);
  std::vector<PhysicalTask> physical_tasks(const std::vector<BodyJob>& jobs);
  uint64_t contract_body(const BodyJob& job, const Reader::ElementChunk& unit, const BodyPlan& body,
                         const Tree& tree, std::vector<Sparse>& parts);
  ComplexBallVector decode_pass(const std::vector<Request>& requests, slong wp);
};

}  // namespace sic::decode
