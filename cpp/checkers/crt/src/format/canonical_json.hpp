// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::crt::sicx {

// ------------------------------------------------------------------------------------------------
// canonical JSON
struct Jv {
  char t = 's';  // 's' string, 'a' array, 'o' object
  string s;
  vector<Jv> a;
  vector<std::pair<string, Jv>> o;

  const Jv* get(const string& k) const;
};

struct JParser {
  const Uint8 *p = nullptr, *e = nullptr;
  int depth = 0;

  Jv parse_top(const Uint8* b, size_t n);

  Jv value();

  string str();
};

void ser_str(string& o, const string& s);

void ser(string& o, const Jv& v);

Jv parse_canonical(const Uint8* b, size_t n, const char* what);

bool is_uint(const string& s);

Uint64 to_u64(const string& s, const char* what);

const Jv& j_get(const Jv& o, const char* k, char t);

void j_keys(const Jv& o, std::initializer_list<const char*> keys, const char* what);

vector<Uint64> j_uvec(const Jv& v, const char* what);

Int64 signed_decimal(const string& s, const char* what);
}  // namespace sic_check::crt::sicx
