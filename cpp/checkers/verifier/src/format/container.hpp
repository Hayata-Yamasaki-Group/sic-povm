// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::verifier {

// Check declared sizes without materializing the requested buffers or tables.
void admit_physical_payload(Uint64 size);

void admit_rescaling_key(size_t count);

Hash merkle(const vector<Hash>& leaves, size_t lo, size_t hi);

int type_rank(const string& t);

void hash_check(const Hash& got, const Hash& want, const string& what);

void put64(vector<Uint8>& b, Uint64 x);

Hash logical_hash(const Logical& l, const Uint8* b, size_t n);

Hash tree_entries(const vector<Uint8>& bytes, size_t width);

void expected_hash(const string& expected, const Hash& actual, const char* what);

void check_header(File& f, const Chunk& c);

void read_payload(File& f, const Chunk& c, vector<Uint8>& buf);

void open_container(Container& C, const Opts& O);

// ------------------------------------------------------------------------------------------------
// full verification pass
struct ElemAcc {
  vector<char> found;
  int nfound = 0;
  bool zero = true;
  fmpz_t g;
  Uint64 maxbits = 0;

  ElemAcc();

  ~ElemAcc();

  ElemAcc(const ElemAcc&) = delete;
};

void verify_chunks(Container& C, const Opts& O);

// ------------------------------------------------------------------------------------------------
// generator rescaling (spec 3.7, 5.3) The rule of spec 3.7 on the stored leaves: J admissible iff
// a.J <= v for every nonzero leaf; levels with gain_s > 0 in order of decreasing gain_s (ties:
// increasing s) get the largest J_s keeping admissibility given the values already fixed and 0 for
// levels not yet reached; levels with gain_s = 0 get J_s = 0.
vector<Uint64> rescaling_rule(const Container& C);

void rescaling_check(Container& C, const Opts&);

void report_trust(const Container& C, const Opts& O);
}  // namespace sic_check::verifier
