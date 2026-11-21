// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_writer.hpp"

namespace fixture {
using namespace sic_check::verifier;
void u32(ByteBuffer& b, uint32_t x) {
  for (int i = 0; i < 4; ++i) b.push_back(x >> (8 * i));
}

void u64le(ByteBuffer& b, Uint64 x) {
  for (int i = 0; i < 8; ++i) b.push_back(x >> (8 * i));
}

void set64(ByteBuffer& b, size_t off, Uint64 x) {
  for (int i = 0; i < 8; ++i) b.at(off + i) = x >> (8 * i);
}

void set32(ByteBuffer& b, size_t off, uint32_t x) {
  for (int i = 0; i < 4; ++i) b.at(off + i) = x >> (8 * i);
}

void append(ByteBuffer& a, const ByteBuffer& b) { a.insert(a.end(), b.begin(), b.end()); }

ByteBuffer bytes(const string& x) { return ByteBuffer(x.begin(), x.end()); }

ByteBuffer digest(const ByteBuffer& b) {
  ByteBuffer h(32);
  SHA256(b.data(), b.size(), h.data());
  return h;
}

ByteBuffer tree(const vector<ByteBuffer>& list, size_t start, size_t n) {
  if (!n) return digest({});
  if (n == 1) {
    ByteBuffer b{0};
    append(b, list[start]);
    return digest(b);
  }
  size_t k = 1;
  while (k < n - k) k *= 2;
  ByteBuffer b{1};
  append(b, tree(list, start, k));
  append(b, tree(list, start + k, n - k));
  return digest(b);
}

void var(ByteBuffer& b, Uint64 n) {
  do {
    Uint8 v = n & 127;
    n >>= 7;
    b.push_back(v | (n ? 128 : 0));
  } while (n);
}

void integer(ByteBuffer& b, Int64 n) {
  if (!n) {
    b.push_back(0);
    return;
  }
  Uint64 a = n < 0 ? Uint64(-(n + 1)) + 1 : Uint64(n), t = a;
  size_t len = 0;
  while (t) {
    ++len;
    t >>= 8;
  }
  var(b, 2 * len + (n < 0));
  for (size_t i = 0; i < len; ++i) b.push_back(a >> (8 * i));
}

string quote(const string& v) {  // fixture strings contain no control bytes
  string r = "\"";
  for (char c : v) {
    if (c == '"' || c == '\\') r += '\\';
    r += c;
  }
  return r + '"';
}

string array(const vector<string>& v, bool quoted) {
  string s = "[";
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) s += ',';
    s += quoted ? quote(v[i]) : v[i];
  }
  return s + "]";
}

string object(const std::map<string, string>& m) {
  string s = "{";
  for (const auto& [k, v] : m) {
    if (s.size() > 1) s += ',';
    s += quote(k) + ":" + v;
  }
  return s + "}";
}

ByteBuffer leaf(Uint64 id, Uint64 first, Uint64 count, Int64 x) {
  ByteBuffer b;
  var(b, id);
  var(b, first);
  var(b, count);
  var(b, count * 2 + (x != 0));
  if (x)
    for (Uint64 j = 0; j < count; ++j) {
      integer(b, x);
      integer(b, 0);
    }
  return b;
}

void element(Model& m, string kind, Uint64 position, Int64 value, Uint64 scale,
             vector<string> levels, Uint64 coords) {
  Element e;
  e.coords = coords;
  e.value = value;
  e.fields = {
      {"kind", quote(kind)},
      {"levels", array(levels)},
      {"max_bits",
       quote(std::to_string(value ? 64 - __builtin_clzll(Uint64(std::abs(value)) * scale) : 0))},
      {"scale", quote(std::to_string(scale))}};
  e.fields[kind == "rep" ? "position" : "level"] = quote(std::to_string(position));
  Uint64 width = std::min<Uint64>(coords, 16384), id = m.elements.size();
  for (Uint64 first = 0; first < coords; first += width)
    e.leaves.push_back(leaf(id, first, width, value));
  m.elements.push_back(e);
}

size_t generator_count(unsigned d) {
  size_t h = 0;
  for (unsigned p = 2; p <= d; ++p) {
    if (d % p) continue;
    unsigned q = 1;
    do {
      q *= p;
      d /= p;
    } while (d % p == 0);
    h += p != 2 ? 1 : q == 2 ? 0 : q == 4 ? 1 : 2;
  }
  return h;
}

string galois(unsigned d, vector<string> smith, vector<vector<string>> words) {
  if (words.empty()) words.assign(generator_count(d), vector<string>(smith.size(), "0"));
  vector<string> rows;
  for (const auto& word : words) rows.push_back(array(word));
  return "{\"smith\":" + array(smith) + ",\"unit_generator_words\":" + array(rows, false) + "}";
}

Model small(unsigned d, bool final) {
  Model m;
  m.d = d;
  m.meta = {
      {"d", quote(std::to_string(d))},
      {"format", quote("SICX")},
      {"version", "[\"1\",\"0\"]"},
      {"required_features", "[]"},
      {"tower",
       "{\"branch_bits\":\"128\",\"branches\":[[\"0 0 0 0\",\"1 0 0 0\"]],\"degrees\":[\"2\"]}"},
      {"galois", galois(d, {"2"})},
      {"generator_rescaling", "{\"rule\":\"sicx-2adic-greedy/1.0\",\"shifts\":[\"0\"]}"}};
  vector<string> reps{"0"};
  for (unsigned r = 1; r < d; ++r)
    if (d % r == 0) reps.push_back(std::to_string(r));
  m.meta["entry_rule"] = "{\"name\":\"sicx-divisors/1.0\",\"representatives\":" + array(reps) + "}";
  element(m, "wsq", 0, -1);
  element(m, "act", 0, 1);
  m.elements.back().fields["axis"] = "\"0\"";
  for (const auto& r : reps) element(m, "rep", std::stoull(r), r == "0" ? 1 : 0, r == "0" ? 2 : 1);
  if (final) {
    string rays =
        "{\"balls\":{\"limbs\":\"1\",\"working_bits\":\"256\"},\"decoder\":{\"name\":\"sic-decode/"
        "1.0\",\"source_commit\":\"0000000000000000000000000000000000000000\"},\"rays\":[{"
        "\"accuracy_bits\":\"199\",\"anchor\":\"0\",\"label\":\"c0\",\"word\":[\"0\"]}]}";
    m.extras.push_back({"RAYS", 1, bytes(rays)});
    ByteBuffer balls;
    for (unsigned j = 0; j < 2 * d; ++j) {
      u64le(balls, 0);
      u64le(balls, 0);
      u64le(balls, j == 0 ? 1 : 0);
      u64le(balls, j == 0 ? Uint64(-200) : 0);
      u64le(balls, j == 0 ? 1 : 0);
    }
    Physical b{"BALL", 1, balls};
    b.count = d;
    m.extras.push_back(b);
  }
  return m;
}

Output write(Model m) {
  vector<string> desc;
  for (auto& e : m.elements) desc.push_back(object(e.fields));
  m.meta["elements"] = array(desc, false);
  vector<Physical> chunks{{"EXMT", 1, bytes(object(m.meta)), 0, 1}};
  if (m.change_exmt) m.change_exmt(chunks[0].payload);
  vector<ByteBuffer> logical, entries;
  auto addlogical = [&](string kind, Uint64 el, Uint64 first, Uint64 count,
                        const ByteBuffer& data) {
    ByteBuffer e = bytes(kind);
    u32(e, 0);
    u64le(e, el);
    u64le(e, first);
    u64le(e, count);
    ByteBuffer h = bytes(string("SICX1-LEAF\0", 11));
    append(h, bytes(kind));
    u64le(h, el);
    u64le(h, first);
    u64le(h, count);
    u64le(h, data.size());
    append(h, data);
    append(e, digest(h));
    logical.push_back(e);
  };
  addlogical("EXMT", 0, 0, 1, chunks[0].payload);
  for (size_t i = 0; i < m.elements.size(); ++i) {
    const auto& e = m.elements[i];
    string type = e.fields.at("kind") == "\"rep\"" ? "BODY" : "TOWR";
    size_t num = e.leaves.size();
    Uint64 width = std::min<Uint64>(e.coords, 16384);
    size_t start = logical.size();
    for (size_t j = 0; j < num; ++j) {
      addlogical(type, i, j * width, width, e.leaves[j]);
      if (!m.grouped || j == 0)
        chunks.push_back({type, 1, e.leaves[j], start + j, 1, i, j * width, width});
      else {
        append(chunks.back().payload, e.leaves[j]);
        ++chunks.back().leaf_count;
        chunks.back().count += width;
      }
    }
  }
  for (const auto& p : m.extras) chunks.push_back(p);
  if (m.change_plan) m.change_plan(chunks);
  Output out;
  out.file = bytes(string("SICX\r\n\x1a\n", 8));
  out.file.insert(out.file.end(), {1, 0, 0, 0, 0, 0, 0, 0});
  for (const auto& c : chunks) {
    out.offsets.push_back(out.file.size());
    ByteBuffer p = bytes(c.type);
    u32(p, c.flags);
    u64le(p, out.file.size());
    u64le(p, c.payload.size());
    u32(p, c.codec);
    u32(p, c.reserved);
    u64le(p, c.payload.size() + c.decoded_delta);
    u64le(p, c.first_leaf);
    u64le(p, c.leaf_count);
    u64le(p, c.obj);
    u64le(p, c.first);
    u64le(p, c.count);
    append(p, digest(c.payload));
    entries.push_back(p);
    append(out.file, bytes(c.type));
    u32(out.file, c.flags);
    u64le(out.file, c.payload.size());
    append(out.file, c.payload);
  }
  ByteBuffer ld;
  u64le(ld, logical.size());
  for (const auto& e : logical) append(ld, e);
  if (m.change_ldir) m.change_ldir(ld);
  logical.clear();
  for (size_t i = 8; i < ld.size(); i += 64)
    logical.emplace_back(ld.begin() + i, ld.begin() + i + 64);
  out.exact = tree(logical, 0, logical.size());
  out.ld = out.file.size();
  ByteBuffer lchunk = bytes("LDIR");
  u32(lchunk, 1);
  u64le(lchunk, ld.size());
  append(lchunk, ld);
  append(out.file, lchunk);
  ByteBuffer pd;
  u64le(pd, entries.size());
  for (const auto& e : entries) append(pd, e);
  if (m.change_pdir) m.change_pdir(pd);
  entries.clear();
  for (size_t i = 8; i < pd.size(); i += 112)
    entries.emplace_back(pd.begin() + i, pd.begin() + i + 112);
  ByteBuffer D = bytes(string("SICX1-LDIR\0", 11));
  append(D, digest(lchunk));
  entries.push_back(D);
  out.root = tree(entries, 0, entries.size());
  out.pd = out.file.size();
  append(out.file, bytes("PDIR"));
  u32(out.file, 1);
  u64le(out.file, pd.size());
  append(out.file, pd);
  u64le(out.file, out.ld);
  u64le(out.file, ld.size());
  u64le(out.file, out.pd);
  u64le(out.file, pd.size());
  append(out.file, out.exact);
  append(out.file, out.root);
  u64le(out.file, out.file.size() + 16);
  append(out.file, bytes(string("SICXEND\x1a", 8)));
  if (m.change_file) m.change_file(out.file);
  return out;
}
}  // namespace fixture
