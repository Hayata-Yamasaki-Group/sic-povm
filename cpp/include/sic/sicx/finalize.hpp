// SPDX-License-Identifier: AGPL-3.0-or-later
// SICX two-stage completion (docs/sicx.md section 5.8).
//
// Stage 1 (producer): EXMT, TOWR*, BODY*, PROV? -> LDIR, PDIR, footer.
// The exact root covers EXMT and logical leaves; the file root binds the layout.
// Stage 2 (sic-decode --finalize): RAYS, BALL (one per
// ray), PREC replace the tail and PDIR/footer are rewritten. The exact chunks, their PDIR entries
// and the exact-part root never change; the full root and the file SHA-256 do.
//
// The decoder constructs BALL by outward rounding its own certified normalized
// enclosures and checks containment and unique identical binary64 rounding.
#pragma once
#include <fstream>

#include "sic/sicx/json.hpp"
#include "sic/sicx/verify.hpp"
#include "sic/support/flint_raii.hpp"

namespace sic::sicx {

struct FinalizeRay {
  std::string label;
  std::vector<uint64_t> word;
  int64_t anchor = -1;  // required; certified nonzero by the decoder
};

// Round the certified source enclosure outward, then verify endpoint containment
// after the canonical wire conversion. arb_contains uses exact dyadic comparisons.
BallPart outward_ball_part(arb_srcptr source, unsigned limbs);

struct Finalization {
  std::string rays_text, prec_text;
  std::vector<ByteBuffer> balls;
};

// Builds RAYS, the BALL payloads and PREC; fills meta.rays / ball_limbs / ball_working_bits.
// decoder / decoder_source_commit: the certified decoder that checked these balls (recorded in
// RAYS; omitted only for non-release files that were not checked by a decoder).
Finalization build_finalization(Meta& meta, const std::vector<FinalizeRay>& rays,
                                const std::vector<std::vector<BallPart>>& parts,
                                uint64_t working_bits, const std::string& decoder = "",
                                const std::string& decoder_source_commit = "");

std::span<const uint8_t> text_span(const std::string& s);

// Appends the finalization chunks in canonical order.
void write_finalization(StreamWriter& writer, const Meta& meta, const Finalization& f);

// Digest of the finalization content: the plan of the finalization journal, so a resumed
// finalization can only continue with exactly the content that was checked before it began.
Digest finalization_digest(const Finalization& f, const Digest& exact_root);

// True when an interrupted finalization of `path` can be resumed.
bool finalization_in_progress(const std::filesystem::path& path);
// EXMT of a file whose finalization was interrupted (read through the journal; hash-checked).
Meta meta_of_interrupted(const std::filesystem::path& path);

Digest interrupted_exact_root(const std::filesystem::path& path);

// Stage 2 on an existing file, in place. `resume` continues an interrupted finalization.
// Returns {exact root, full root}; the exact part is checked to be unchanged.
struct FinalizedRoots {
  Digest exact_root;
  Digest file_root;
};
FinalizedRoots finalize_in_place(const std::filesystem::path& path, const Meta& meta,
                                 const Finalization& f, bool resume,
                                 const Digest& decoded_exact_root, VerificationPolicy policy = {});

}  // namespace sic::sicx
