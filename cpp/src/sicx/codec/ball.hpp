// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/sicx/format.hpp"
#include "sicx/codec/integer.hpp"

namespace sic::sicx {
int64_t radius_log2_bound(const BallPart& part);
BallPart get_ball_part(const uint8_t* data, unsigned limbs);
void put_ball_part(ByteBuffer& out, const BallPart& part);
std::string ball_part_text(const BallPart& part);
std::string hex_i64(int64_t value);
BallPart parse_ball_part(const std::string& text, unsigned limbs);
void parse_hex(fmpz_t out, const std::string& token);
std::string fmpz_hex(const fmpz_t v);
size_t ball_record_bytes(unsigned limbs);
}  // namespace sic::sicx
