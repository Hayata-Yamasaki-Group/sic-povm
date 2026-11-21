// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/transform/representative_lattice.hpp"

namespace sic::exact::representative_lattice {
void transform(std::span<sic::exact::Leaf> block, long D0, int threads, bool decode) {
  const long n = static_cast<long>(block.size());
  if (!n) return;
  const int workers = static_cast<int>(std::max<long>(1, std::min<long>(threads, n / 1024)));
  const long chunks = workers > 1 ? 4L * workers : 1;
  sic::exact::parallel_for(chunks, workers, [&](long k) {
    sic::support::LatticeCoordinates coordinates(D0);
    for (long q = k * n / chunks; q < (k + 1) * n / chunks; ++q) {
      if (decode)
        coordinates.decode(block[q].x, block[q].y);
      else
        coordinates.encode(block[q].x, block[q].y);
    }
  });
}

}  // namespace sic::exact::representative_lattice
