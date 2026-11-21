// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/resources/allocation_forecast.hpp"

#include <flint/acb.h>
#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "sic/error.hpp"
#include "sic/support/admission.hpp"
#include "sic/support/kernel_settings.hpp"
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace sic::support::resources {
Bytes real_bytes(BitCount precision) {
  return add(sizeof(arb_struct), add(mul(add(precision, 63) / 64, sizeof(ulong)), 32));
}

Bytes complex_bytes(BitCount precision) { return mul(2, real_bytes(precision)); }

Bytes ghost_reconstruction_scratch(Count dimension, BitCount precision, int workers) {
  if (dimension < 2 || workers <= 0)
    throw std::invalid_argument("invalid reconstruction workspace");
  const Count active = std::min<Count>(workers, dimension);
  const Count entries = add(add(mul(3, dimension), 64), mul(active, add(mul(2, dimension), 64)));
  return add(64 * kMib, add(mul(entries, complex_bytes(add(precision, 128))), mul(active, kMib)));
}

BitCount newton_matrix_bits(BitCount precision) {
  return add(precision, add(add(precision, 3) / 4, 256));
}

Bytes inverse_column_scratch(Count columns, int workers, BitCount precision) {
  if (workers <= 0) throw std::invalid_argument("inverse worker count must be positive");
  const Count active = std::min<Count>(workers, add(columns, 7) / 8);
  const Bytes per_worker =
      add(mul(columns, sizeof(acb_struct) + 1),
          add(2 * kMib, mul(32, complex_bytes(newton_matrix_bits(precision)))));
  return add(mul(columns, sizeof(slong)), mul(active, per_worker));
}

Bytes ball_product_scratch(Count columns, int workers, BitCount precision) {
  if (workers <= 0) throw std::invalid_argument("ball product worker count must be positive");
  const Count active = std::min<Count>(workers, columns);
  // Handles/arguments, dot accumulators, optional length-byte Gauss mask and
  // scalar multiplication scratch. No copied or transposed input matrix.
  // This is an engineering allowance for the inspected FLINT path.
  return mul(active, add(2 * kMib, add(columns, mul(32, complex_bytes(add(precision, 64))))));
}

Bytes linear_correction_vectors(Count count, BitCount precision, int workers) {
  if (!count || workers <= 0) throw std::invalid_argument("invalid correction workspace");
  const int active = static_cast<int>(std::min<Count>(workers, count));
  return add(64 * kMib,
             add(mul(mul(5, count), complex_bytes(add(precision, 128))),
                 add(mul(mul(2, count), sizeof(mag_struct)),
                     add(ball_product_scratch(count, active, precision), mul(active, kMib)))));
}

Bytes linear_correction_workspace(Count dimension, BitCount precision, int workers) {
  if (dimension < 2) throw std::invalid_argument("invalid correction dimension");
  const Count count = mul(2, dimension) - 2;
  const Count values =
      add(mul(count, count), add(mul(2, mul(dimension, dimension)), mul(128, dimension)));
  return add(linear_correction_vectors(count, precision, workers),
             add(64 * kMib, mul(values, complex_bytes(add(precision, 128)))));
}

Bytes modular_selection_bytes(Count rows, Count columns, int workers, BitCount precision) {
  if (workers <= 0) throw std::invalid_argument("modular worker count must be positive");
  const auto matrix = mul(mul(rows, columns), sizeof(ulong));
  const auto indexing = mul(rows, sizeof(long) + sizeof(unsigned char));
  const auto scratch = mul(workers, add(2 * kMib, mul(4, real_bytes(precision))));
  return add(64 * kMib, add(matrix, add(indexing, scratch)));
}

Bytes leaf_block_bytes(Count count, BitCount coordinate_bits) {
  // Two fmpz objects, independent limb payloads and allocator allowances.
  return mul(count, add(2 * sizeof(fmpz),
                        mul(2, add(mul(add(coordinate_bits, 63) / 64, sizeof(ulong)), 32))));
}
}  // namespace sic::support::resources
