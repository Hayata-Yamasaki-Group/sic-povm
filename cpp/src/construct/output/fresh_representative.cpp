// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/representative_leaves.hpp"
#include "construct/runtime/observations/progress.hpp"
#include "construct/runtime/session/field_values.inl.hpp"
#include "exact/certification/newton/recognition.hpp"
#include "exact/transform/representative_lattice.hpp"

namespace sic::construct {
void Construction::collect_representative_block(FreshRepresentative& operation, size_t first,
                                                std::span<const Leaf> block) {
  const size_t prefix = operation.ne / 2, half = Ncos[t] / 2;
  operation.collector.add({"rep", (unsigned)t, -1, -1, rep_entry(operation.r)},
                          [&](const sic::sicx::LeafVisitor& visit) {
                            for (size_t q = 0; q < block.size(); ++q) {
                              const size_t index = first + q;
                              visit((index / prefix) * half + index % prefix, block[q].x,
                                    block[q].y);
                            }
                          });
}

void Construction::spool_representative_block(FreshRepresentative& operation, size_t first,
                                              std::span<Leaf> block) {
  collect_representative_block(operation, first, block);
  long block_bits = 0;
  for (const auto& leaf : block)
    block_bits = std::max(block_bits, (long)std::max(fmpz_bits(leaf.x), fmpz_bits(leaf.y)));
  operation.bits = std::max(operation.bits, block_bits);
  if (operation.lattice) {
    budget.require_additional("representative lattice spool encode",
                              sic::exact::representative_lattice::scratch_bytes(block_bits, nth));
    sic::exact::representative_lattice::transform(block, D0, nth, false);
  }
}

void Construction::release_representative_forward(int r) {
  if (!phased_storage) return;
  if (r + 1 == entry_translations.representatives.size()) table_backing.clear();
  int remaining_level = 0;
  for (int next = r + 1; next < entry_translations.representatives.size(); ++next)
    remaining_level = std::max(
        remaining_level, scalar_fields->fields[scalar_fields->representative_field[next]].level);
  // Only analytic forward transforms use these payloads. Later
  // physical-decoder retries use exact leaves and character flags.
  for (int i = remaining_level; i < t - 1; ++i) {
    sdt.g[i].park();
    sdt.g[i].backing.reset();
    sdt.g[i].pieces.clear();
  }
}

int Construction::recognize_fresh_representative(FreshRepresentative& operation) {
  const int r = operation.r, band_bits = operation.band_bits;
  const slong wp = operation.wp;
  auto sq0 = operation.sq0;
  auto& Ectx = operation.context;
  auto sqd1 = operation.sqrt_d1;
  auto& z2d = operation.roots;
  auto& y = operation.channels;
  auto& yb = operation.scratch;
  auto& lv = operation.leaves;
  auto& rep_spool = operation.spool;
  auto& rep_source = operation.source;
  auto& rep_bits = operation.bits;
  const bool spill_rep = bool(rep_spool);
  double band_values_time = 0, band_fft_time = 0;
  const double c0 = stamp();
  if (band_bits) {
    y = sic::support::RealBallVector();
    yb = sic::support::RealBallVector();
  }
  if (!band_bits) representative_channels(Ectx, r, wp, z2d, sqd1, sdt, y);
  double c1 = stamp();
#ifdef SIC_ENABLE_TEST_HOOKS
  trace_representative_owners("channels", r, *operation.census);
#endif
  if (!band_bits)
    transform_channels(y, r, sdt, wp, &yb);  // a band is transformed just before its recognition
#ifdef SIC_ENABLE_TEST_HOOKS
  trace_representative_owners("transform-live", r, *operation.census);
#endif
  // The forward transform has joined every reader of its old channel buffer.
  // Recognition only reads y; retaining yb would overlap it with the exact leaves.
  yb = sic::support::RealBallVector();
  if (!band_bits) release_representative_forward(r);
#ifdef SIC_ENABLE_TEST_HOOKS
  trace_representative_owners("transformed", r, *operation.census);
#endif
  double c2 = stamp();
  std::atomic<long> bad(-1);
  const long ne = operation.ne = band_bits ? operation.ne : (long)y.size();
  const long nchunk = std::min<long>(ne, 64L * nth);
  RecognitionLattice RLe(representative_height(sdt, r), sq0, wp);
  std::vector<long> mbc(nchunk, 0);
  if (band_bits) {
    struct RecognitionFailure {};
    const long size = (ne / 2) >> band_bits, bands = 1L << band_bits;
    std::vector<ExactSource> real_parts, imaginary_parts;
    try {
      for (long band = 0; band < bands; ++band) {
        y = sic::support::RealBallVector();
        release_band_heap("export", rep_entry(r), band, bands);
        double started = stamp();
        representative_band_channels(Ectx, r, wp, z2d, sqd1, sdt, band_bits, band, y);
        band_values_time += stamp() - started;
        started = stamp();
        transform_channels(y, r, sdt, wp, &yb, band_bits);
        yb = sic::support::RealBallVector();
        band_fft_time += stamp() - started;
        if (producer_options.progress)
          sic::support::progressf("representative band: eta=%ld band=%ld/%ld rows=%ld [%.1fs]\n",
                                  rep_entry(r), band, bands, 2 * size, stamp());
        admit_recognition_block(y, (slong)RLe.log2Bp);
        for (int channel = 0; channel < 2; ++channel) {
          auto recognize = [&](size_t first_leaf, std::span<Leaf> block) {
            const long chunks = std::min<long>(block.size(), 16L * nth);
            parallel_for(chunks, nth, [&](long chunk) {
              RecognitionLattice::Work work;
              for (size_t q = chunk * block.size() / chunks;
                   q < (chunk + 1) * block.size() / chunks && bad < 0; ++q) {
                const size_t local = channel * size + first_leaf + q;
                if (!RLe.recognize(y[local], block[q].x, block[q].y, work)) {
                  bad = channel * (ne / 2) + band * size + first_leaf + q;
                  break;
                }
                arb_clear(y[local]);
                arb_init(y[local]);
              }
            });
            if (bad >= 0) throw RecognitionFailure{};
            spool_representative_block(operation, channel * (ne / 2) + band * size + first_leaf,
                                       block);
          };
          auto source = rep_spool->append_generated(size, recognize);
          (channel ? imaginary_parts : real_parts).push_back(std::move(source));
        }
      }
      real_parts.insert(real_parts.end(), imaginary_parts.begin(), imaginary_parts.end());
      rep_source = rep_spool->concatenate(std::move(real_parts));
      maxbits = std::max(maxbits, rep_bits);
      release_representative_forward(r);
    } catch (const RecognitionFailure&) { /* numerical retry below */
    }
  } else if (spill_rep) {
    admit_recognition_block(y, (slong)RLe.log2Bp);
    struct RecognitionFailure {};
    try {
      rep_source = rep_spool->append_generated(ne, [&](size_t first_leaf, std::span<Leaf> block) {
        const long chunks = std::min<long>(block.size(), 16L * nth);
        parallel_for(chunks, nth, [&](long chunk) {
          RecognitionLattice::Work work;
          for (size_t q = chunk * block.size() / chunks;
               q < (chunk + 1) * block.size() / chunks && bad < 0; ++q) {
            const size_t e = first_leaf + q;
            if (!RLe.recognize(y[e], block[q].x, block[q].y, work)) {
              bad = e;
              break;
            }
            arb_clear(y[e]);
            arb_init(y[e]);
          }
        });
        if (bad >= 0) throw RecognitionFailure{};
        spool_representative_block(operation, first_leaf, block);
      });
      maxbits = std::max(maxbits, rep_bits);
    } catch (const RecognitionFailure&) { /* numerical retry below */
    }
  } else {
    lv = std::vector<Leaf>(ne);
    parallel_for(nchunk, nth, [&](long ch) {
      RecognitionLattice::Work rw;
      for (long e = ch * ne / nchunk; e < (ch + 1) * ne / nchunk && bad < 0; ++e) {
        if (!RLe.recognize(y[e], lv[e].x, lv[e].y, rw)) {
          bad = e;
          break;
        }
        mbc[ch] = std::max(mbc[ch], (long)std::max(fmpz_bits(lv[e].x), fmpz_bits(lv[e].y)));
        // This input has one consumer. Keep an unsuccessful row intact for the
        // radius diagnostic, as in the spooled recognition path above.
        arb_clear(y[e]);
        arb_init(y[e]);
      }
    });
    if (bad < 0) collect_representative_block(operation, 0, lv);
  }
  if (bad >= 0) {
    const long bad_row = band_bits ? ((long)bad / (ne / 2)) * ((ne / 2) >> band_bits) +
                                         (long)bad % ((ne / 2) >> band_bits)
                                   : (long)bad;
    sic::support::progressf("  entry %ld leaf %ld not recognized: log2 rad = %.1f, needed < %.1f\n",
                            rep_entry(r), (long)bad, mag_get_d_log2_approx(arb_radref(y[bad_row])),
                            -std::ceil(RLe.log2Bp) - 7);
    return 1;
  }
  for (long x : mbc) maxbits = std::max(maxbits, x);
  if (rep_spool && producer_options.progress)
    sic::support::progressf("representative spool: eta=%ld logical_rows=%ld bytes=%llu\n",
                            rep_entry(r), ne, (unsigned long long)rep_spool->bytes());
  const double c3 = stamp();
  operation.values_time = c1 - c0 + band_values_time;
  operation.fft_time = c2 - c1 + band_fft_time;
  operation.recognition_time = c3 - c2 - band_values_time - band_fft_time;
  return 0;
}
}  // namespace sic::construct
