// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <filesystem>

#include "sic/sicx/format.hpp"
#include "sic/support/ball_vectors.hpp"
namespace sic::data {
void require_new_output(const std::filesystem::path& path);
std::string dump_ball(const acb_t z);
void write_balls(const std::filesystem::path& path, const sic::support::ComplexBallVector& v);
void write_decode_provenance(const std::filesystem::path& path, const std::string& text);
std::string json_escape(const std::string& text);
struct DecodedComparison {
  uint64_t overlap, same64;
  int64_t worst;
};
DecodedComparison write_decoded_fp64(const std::filesystem::path& fp_path,
                                     const sic::support::ComplexBallVector& w,
                                     const std::vector<sic::sicx::BallPart>& parts,
                                     uint64_t dimension);

}  // namespace sic::data
