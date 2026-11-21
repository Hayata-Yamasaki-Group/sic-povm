// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <openssl/sha.h>

#include <filesystem>
#include <fstream>

#include "../src/model/input_model.hpp"
namespace fixture {
using namespace sic_check::verifier;

using ByteBuffer = vector<Uint8>;

void u32(ByteBuffer& b, uint32_t x);

void u64le(ByteBuffer& b, Uint64 x);

void set64(ByteBuffer& b, size_t off, Uint64 x);

void set32(ByteBuffer& b, size_t off, uint32_t x);

void append(ByteBuffer& a, const ByteBuffer& b);

ByteBuffer bytes(const string& x);

ByteBuffer digest(const ByteBuffer& b);

ByteBuffer tree(const vector<ByteBuffer>& list, size_t start, size_t n);

void var(ByteBuffer& b, Uint64 n);

void integer(ByteBuffer& b, Int64 n);

string quote(const string& v);

string array(const vector<string>& v, bool quoted = true);

string object(const std::map<string, string>& m);

struct Element {
  std::map<string, string> fields;
  vector<ByteBuffer> leaves;
  Uint64 coords = 1;
  Int64 value = 0;
};

struct Physical {
  string type;
  uint32_t flags;
  ByteBuffer payload;
  Uint64 first_leaf = 0, leaf_count = 0, obj = 0, first = 0, count = 0;
  uint32_t codec = 0, reserved = 0;
  Int64 decoded_delta = 0;
};

struct Model {
  unsigned d;
  std::map<string, string> meta;
  vector<Element> elements;
  vector<Physical> extras;
  std::function<void(ByteBuffer&)> change_ldir, change_pdir, change_file, change_exmt;
  std::function<void(vector<Physical>&)> change_plan;
  bool grouped = false;
};

ByteBuffer leaf(Uint64 id, Uint64 first, Uint64 count, Int64 x);

void element(Model& m, string kind, Uint64 position, Int64 value, Uint64 scale = 1,
             vector<string> levels = {}, Uint64 coords = 1);

// The writer derives only generator counts here; tests below derive generators independently.
size_t generator_count(unsigned d);

string galois(unsigned d, vector<string> smith, vector<vector<string>> words = {});

Model small(unsigned d, bool final = false);

struct Output {
  ByteBuffer file, exact, root;
  size_t ld = 0, pd = 0;
  vector<size_t> offsets;
};

Output write(Model m);
}  // namespace fixture
