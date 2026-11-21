// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/fmpz_poly.h>
#include <openssl/sha.h>
#include <sys/mman.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "../src/certificate/branches.hpp"
#include "../src/certificate/branches_numeric.hpp"
#include "../src/decode/check.hpp"
#include "../src/decode/decoder.hpp"
#include "../src/format/ball_codec.hpp"
#include "../src/format/canonical_json.hpp"
#include "../src/format/container.hpp"
#include "../src/format/integer_codec.hpp"
#include "../src/format/metadata.hpp"
#include "../src/format/sha256.hpp"
#include "../src/format/units.hpp"
#include "../src/model/input_model.hpp"
#include "../src/runtime/diagnostics.hpp"
#include "../src/text/text_check.hpp"
#include "fault_seams.hpp"
#include "test_writer.hpp"
namespace sic_check::verifier::test {
using namespace sic_check::verifier;
using namespace fixture;
extern size_t checks;
extern string fixtures_dir;

void require(bool ok, const string& what);

int run(const fixture::Output& out, const string& name, Opts opt = {}, bool numerical = false);

void bad(size_t& rejects, const string& name, fixture::Model m);

void setup_inverse(Model& m);
struct DimensionContext {
  unsigned& d;
  const string& pre;
  size_t& rejects;

  void mutate(string name, std::function<void(Model&)> change, bool final = false);

  void meta(string name, string key, string value);

  void descriptor_reject(string name, Model m);

  void raysbad(string name, string from, string to);

  void resource(string name, Model m);

  void partial_bad(string name, Model m);
};
void container_envelopes(size_t& valid, DimensionContext& dimension, string& pre, Output& a,
                         Output& b);
void partial_payload_authentication(size_t& valid, size_t& rejects, DimensionContext& dimension,
                                    string& pre, Output& b, ByteBuffer& encoded, Model& unread);
void directory_bootstrap_refusals(unsigned& d, size_t& valid, size_t& rejects,
                                  DimensionContext& dimension, string& pre, Output& b,
                                  string& json);
void multi_leaf_boundaries(size_t& valid, size_t& rejects);
void real_root_certification(size_t& valid, size_t& rejects);
void exact_metadata_refusals(unsigned& d, DimensionContext& dimension);
void ray_metadata_refusals(DimensionContext& dimension);
void json_sibling_depth(size_t& valid, string& pre, Model& siblings, string& json);
void ray_parser_contracts(size_t& valid, size_t& rejects);
void scalar_parser_contracts(size_t& valid, size_t& rejects);
void integer_run_canonicality(DimensionContext& dimension);
void rescaling_enumeration_oracle(size_t& valid, size_t& rejects);
void text_container_identity(size_t& valid, size_t& rejects);
void ball_exact_consistency(size_t& valid, size_t& rejects);
void json_depth_refusals(unsigned& d, DimensionContext& dimension, Model& deep, string& nested);
void payload_size_refusals(unsigned& d, DimensionContext& dimension, Model& bigInt,
                           ByteBuffer& encoded);
void exact_decode_refusals(size_t& valid, size_t& rejects);
void field_dimension_refusals(size_t& valid, size_t& rejects);
void directory_storage_refusals(size_t& valid, size_t& rejects);
void hash_and_text_bounds(size_t& valid, size_t& rejects);
void rescaling_and_entry_contracts(unsigned& d, size_t& valid, size_t& rejects,
                                   DimensionContext& dimension, string& pre, Output& a, Output& b,
                                   string& json, string& nested);
void wide_rescaling_weights(size_t& valid, size_t& rejects);
void quadratic_square_branches(size_t& valid, size_t& rejects);
void cubic_zero_branches(size_t& valid, size_t& rejects);
void signed_base_coordinates(size_t& valid, size_t& rejects);
void odd_branch_and_action_contracts(size_t& valid, size_t& rejects);
void exact_nearest_even_ties(size_t& valid, size_t& rejects);
void interval_ties_and_source_snapshot(size_t& valid, size_t& rejects);
void uncertain_rounding_and_short_reads(size_t& valid, size_t& rejects);
void binary64_and_ball_storage_bounds(size_t& valid, size_t& rejects);
}  // namespace sic_check::verifier::test
