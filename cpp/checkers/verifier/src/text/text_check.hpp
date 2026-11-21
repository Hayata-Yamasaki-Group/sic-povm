// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::verifier {

// ------------------------------------------------------------------------------------------------
// text form (JSON Lines)
string dec(const fmpz_t x);

string qs(const string& s);

string render_block_line(Container& C, const Chunk& c, vector<Uint8>& buf);

string render_ball_line(Container& C, size_t ci, vector<Uint8>& buf);

bool bounded_text_line(FILE* fp, string& out, bool& had_nl, size_t limit = 256u << 20);

void text_check(Container& C, const string& tpath);
}  // namespace sic_check::verifier
