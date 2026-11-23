// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sic/sicx/finalize.hpp"
#include "support/resources.hpp"

namespace sic::sicx {

// Without a sink this builds the in-memory library result. The CLI supplies
// an ordered spool sink so the complete set of encoded BALLs need not be retained.
using BallSink = std::function<void(std::span<const uint8_t>)>;
using BallSource = std::function<std::span<const uint8_t>(size_t)>;
class FinalizationBuilder {
 public:
  FinalizationBuilder(Meta& meta, size_t count, unsigned limbs, uint64_t working_bits,
                      const std::string& decoder, const std::string& decoder_source_commit,
                      BallSink sink = {});
  void append(Ray ray, std::span<const BallPart> parts);
  struct PackedRay {
    Ray ray;
    ByteBuffer payload;
  };
  PackedRay pack(Ray ray, std::span<const BallPart> parts) const;
  void append(PackedRay packed);
  Finalization finish() &&;

 private:
  Meta& meta_;
  size_t count_;
  std::vector<Ray> rays_;
  Finalization out_;
  BallSink sink_;
};

struct FinalizedArtifact {
  FinalizedRoots roots;
  std::string sha256;
};
ResourceLimits finalization_reader_limits(const sic::support::resources::Budget& budget);
uint64_t finalization_hash_buffer(uint64_t file_bytes);
Digest interrupted_exact_root(const std::filesystem::path& path,
                              const std::vector<IndexEntry>& entries,
                              const WorkspaceAdmission& admit);
FinalizedArtifact finalize_and_hash(const std::filesystem::path& path, const Meta& meta,
                                    const Finalization& f, const BallSource& balls, bool resume,
                                    const Digest& decoded_exact_root, VerificationPolicy policy,
                                    const sic::support::resources::Budget& budget,
                                    const std::function<void()>& check_peak);

}  // namespace sic::sicx
