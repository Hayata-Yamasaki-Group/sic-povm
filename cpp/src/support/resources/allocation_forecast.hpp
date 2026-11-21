// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <filesystem>
#include <optional>

#include "support/resources/snapshot.hpp"
namespace sic::support::resources {
Bytes real_bytes(BitCount precision);
Bytes complex_bytes(BitCount precision);
Bytes ghost_reconstruction_scratch(Count dimension, BitCount precision, int workers);
BitCount newton_matrix_bits(BitCount precision);
Bytes inverse_column_scratch(Count columns, int workers, BitCount precision);
Bytes ball_product_scratch(Count columns, int workers, BitCount precision);
Bytes linear_correction_vectors(Count count, BitCount precision, int workers);
Bytes linear_correction_workspace(Count dimension, BitCount precision, int workers);
Bytes modular_selection_bytes(Count rows, Count columns, int workers, BitCount precision);
Bytes leaf_block_bytes(Count count, BitCount coordinate_bits);
}  // namespace sic::support::resources
