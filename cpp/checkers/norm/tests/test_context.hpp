// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/arb_fmpz_poly.h>
#include <flint/fmpz_poly.h>

#include <filesystem>
#include <fstream>
#include <tuple>

#include "../src/algebra/algebra.hpp"
#include "../src/format/canonical.hpp"
#include "../src/format/container.hpp"
#include "../src/format/integer_codec.hpp"
#include "../src/format/metadata.hpp"
#include "../src/format/quadratic.hpp"
#include "../src/runtime/diagnostics.hpp"
#include "test_writer.hpp"
namespace sic_check::norm::test {
using namespace sic_check::norm;
using fixture::Document;
using fixture::Node;
using fixture::num;
using fixture::overwrite;

void check(bool ok, const std::string& reason);

std::string hex(const Hash& h);

void write(const std::filesystem::path& path, const ByteBuffer& b);

Hash root(const ByteBuffer& b, size_t relative);

std::string integer_text(const fmpz_t value);

void big_integer(ByteBuffer& out, const fmpz_t value);

// Full integer trace coordinates, with independent scale extraction and run encoding.
void real_leaf(Document& d, size_t id, const std::vector<Integer>& coordinates);

// Independent fixture oracle: certified Arb polynomial roots, interval rounding,
// and exact rational substitution. No reader algebraic/branch routine is used.
std::string positive_real_branch(const std::vector<Integer>& coefficients, uint64_t bits,
                                 bool smallest = false);

std::string sqrt_branch(unsigned n, uint64_t bits, bool negative = false);

void rebuild(Document& d);
struct TestContext {
  const std::filesystem::path& scratch;
  std::ofstream& cases;
  size_t& accepted;
  size_t& rejected;
  size_t& refused;
  const fixture::Document* base_document = nullptr;
  const ByteBuffer* base_bytes = nullptr;

  void run(const ByteBuffer& b, const std::string& name, int expected = 0, ReadOptions options = {},
           const std::string& diagnostic = "");

  void expect_invalid(const std::function<void()>& f, const std::string& reason,
                      const std::string& diagnostic = "");

  void mutation(const std::string& name, const std::function<void(Document&)>& edit,
                int outcome = 1);

  void raw(const std::string& name, const std::function<void(ByteBuffer&)>& edit, int outcome = 1);

  void pdir(const std::string& name, size_t entry, size_t offset, uint64_t value,
            unsigned width = 8);

  void ldir(const std::string& name, size_t entry, size_t offset, uint64_t value,
            unsigned width = 8);

  void partial_bad(Document d, const std::string& name);

  void exmt(const std::string& name, const std::function<void(Node&)>& edit, int outcome = 1);

  void expect_resource(const std::function<void()>& f, const std::string& reason);
};
void codec_primitives(TestContext& context, std::filesystem::path& dir);
void corruption_fuzz_and_root_refusals(TestContext& context, std::filesystem::path& scratch,
                                       Document& original, ByteBuffer& valid, Document& odd,
                                       ReadOptions& large, Document& nonchain);
void unit_group_oracles(TestContext& context, std::filesystem::path& scratch);
void quadratic_branch_presentations(TestContext& context, uint64_t& base_dimension,
                                    Document& original, ByteBuffer& valid);
void cubic_input(TestContext& context, Document& odd);
void odd_action_presentations(TestContext& context, Document& original, Document& with_action,
                              Node& delta);
void quadratic_action_presentations(TestContext& context, Document& original, Document& qaction);
void multi_leaf_layout(Document& split, size_t& representative_id);
void multi_leaf_coordinates(Document& split, size_t& representative_id,
                            std::vector<std::pair<int64_t, int64_t>>& a,
                            std::vector<std::pair<int64_t, int64_t>>& b);
void grouped_leaf_layout(size_t& representative_id, Document& grouped);
void leaf_grouping_and_quadratic_bounds(TestContext& context, Document& split, Document& grouped,
                                        ReadOptions& large);
void pell_branch_bounds(TestContext& context, Document& split, ReadOptions& large,
                        Document& tiny_quadratic, Integer& fx, Integer& fy, Integer& trace,
                        Integer& norm, Integer& temp_integer);
void interval_branch_rounding(ComplexBall& value, ComplexBall& box);
void tower_arithmetic(std::filesystem::path& scratch, ByteBuffer& valid,
                      std::vector<std::pair<int64_t, int64_t>>& a,
                      std::vector<std::pair<int64_t, int64_t>>& b);
void container_envelopes(TestContext& context, Document& original, ByteBuffer& valid);
void partial_directory_refusals(TestContext& context, uint64_t& base_dimension, Document& original,
                                Document& empty_chunk, fixture::Physical& empty);
void whole_hash_mismatch(TestContext& context, ByteBuffer& valid, ReadOptions& wrong);
void authenticated_partial_input(TestContext& context, std::filesystem::path& scratch,
                                 Document& original, ByteBuffer& valid, fixture::Physical& empty,
                                 ReadOptions& partial);
void metadata_refusals(TestContext& context, Document& original);
void trusted_root_routes(TestContext& context, std::filesystem::path& scratch, ByteBuffer& valid,
                         ReadOptions& trust);
void trusted_root_mismatches(TestContext& context, ByteBuffer& valid, ReadOptions& trust,
                             ReadOptions& wrong);
void resource_and_input_refusals(TestContext& context, uint64_t& base_dimension,
                                 std::filesystem::path& dir, std::filesystem::path& scratch,
                                 Document& original, ByteBuffer& valid, ReadOptions& partial,
                                 Document& odd, Document& with_action, Node& delta,
                                 Document& qaction, Document& split, size_t& representative_id,
                                 std::vector<std::pair<int64_t, int64_t>>& a, Document& grouped,
                                 ReadOptions& large, Integer& fx, Integer& fy, ComplexBall& value,
                                 ComplexBall& box);
void nonchain_smith_presentation(std::vector<std::pair<int64_t, int64_t>>& a, Document& nonchain);
}  // namespace sic_check::norm::test
