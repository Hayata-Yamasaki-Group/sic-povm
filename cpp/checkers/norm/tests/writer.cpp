// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_writer.hpp"

namespace fixture {
void put(ByteBuffer& b, uint64_t x, unsigned n) {
  for (unsigned j = 0; j < n; ++j) b.push_back(uint8_t(x >> (8 * j)));
}

void overwrite(ByteBuffer& b, size_t at, uint64_t x, unsigned n) {
  for (unsigned j = 0; j < n; ++j) b.at(at + j) = uint8_t(x >> (8 * j));
}

ByteBuffer bytes(const std::string& s) { return ByteBuffer(s.begin(), s.end()); }

void append(ByteBuffer& a, const ByteBuffer& b) { a.insert(a.end(), b.begin(), b.end()); }

Hash hash(const ByteBuffer& b) { return sic_check::norm::digest(b); }

Hash tree(std::vector<ByteBuffer> items) {
  if (items.empty()) return hash({});
  if (items.size() == 1) {
    ByteBuffer b{0};
    append(b, items[0]);
    return hash(b);
  }
  size_t k = 1;
  while (k * 2 < items.size()) k *= 2;
  auto left = tree({items.begin(), items.begin() + k}),
       right = tree({items.begin() + k, items.end()});
  ByteBuffer b{1};
  b.insert(b.end(), left.begin(), left.end());
  b.insert(b.end(), right.begin(), right.end());
  return hash(b);
}

Node::Node(const char* s) : value(s) {}

Node::Node(std::string s) : value(std::move(s)) {}

auto Node::a(std::vector<Node> values) -> Node {
  Node n;
  n.type = list;
  n.array = std::move(values);
  return n;
}

auto Node::o(std::map<std::string, Node> values) -> Node {
  Node n;
  n.type = map;
  n.object = std::move(values);
  return n;
}

Node& Node::operator[](const std::string& key) { return object.at(key); }

auto Node::dump() const -> std::string {
  auto quote = [](const std::string& s) {
    std::string out = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char c : s) {
      if (c == '"' || c == '\\') out += '\\';
      if (c < 32) {
        out += "\\u00";
        out += hex[c >> 4];
        out += hex[c & 15];
      } else
        out += char(c);
    }
    return out + '"';
  };
  if (type == text) return quote(value);
  std::string out = type == list ? "[" : "{";
  bool first = true;
  if (type == list)
    for (const auto& n : array) {
      if (!first) out += ',';
      first = false;
      out += n.dump();
    }
  else
    for (const auto& [k, n] : object) {
      if (!first) out += ',';
      first = false;
      out += quote(k) + ':' + n.dump();
    }
  return out + (type == list ? ']' : '}');
}

Node num(uint64_t n) { return Node(std::to_string(n)); }

void var(ByteBuffer& b, uint64_t x) {
  while (x > 127) {
    b.push_back(uint8_t(x) | 128);
    x >>= 7;
  }
  b.push_back(uint8_t(x));
}

void integer(ByteBuffer& b, int64_t x) {
  uint64_t v = x < 0 ? uint64_t(-(x + 1)) + 1 : uint64_t(x), n = 0, t = v;
  while (t) {
    ++n;
    t >>= 8;
  }
  var(b, 2 * n + (x < 0));
  put(b, v, unsigned(n));
}

ByteBuffer leaf_bytes(uint64_t id, uint64_t first,
                      const std::vector<std::pair<int64_t, int64_t>>& pairs) {
  ByteBuffer b;
  var(b, id);
  var(b, first);
  var(b, pairs.size());
  for (size_t i = 0; i < pairs.size();) {
    bool data = pairs[i].first || pairs[i].second;
    size_t end = i + 1;
    while (end < pairs.size() && bool(pairs[end].first || pairs[end].second) == data) ++end;
    var(b, 2 * (end - i) + data);
    for (; i < end; ++i)
      if (data) {
        integer(b, pairs[i].first);
        integer(b, pairs[i].second);
      }
  }
  return b;
}

Document::Document(uint64_t dimension, bool finalized, unsigned levels, unsigned degree)
    : d(dimension) {
  Node degrees = Node::a(), branches = Node::a(), shifts = Node::a(), descriptors = Node::a(),
       reps = Node::a();
  uint64_t D = 1;
  for (unsigned i = 0; i < levels; ++i) {
    degrees.array.push_back(num(degree));
    shifts.array.push_back("0");
    D *= degree;
    branches.array.push_back(Node::a({"1 0 0 0", "0 0 0 0"}));
    for (unsigned k = 0; k < (degree == 2 ? 1 : degree); ++k) {
      Node e = Node::o({{"kind", degree == 2 ? "wsq" : "fcoef"},
                        {"level", num(i)},
                        {"levels", Node::a()},
                        {"max_bits", num(k ? 0 : 1)},
                        {"scale", "1"}});
      if (degree != 2) e.object["k"] = num(k);
      if (degree == 2) {
        e["scale"] = num(uint64_t(1) << i);
        e["max_bits"] = num(i + 1);
      }
      descriptors.array.push_back(e);
      size_t id = leaves.size();
      // X^2-1 or X^p-1. These are container fixtures, not claimed field/SIC certificates.
      leaves.push_back(
          {"TOWR", id, 0, 1, leaf_bytes(id, 0, {{k ? 0 : (degree == 2 ? 1 : -1), 0}})});
    }
  }
  // Deliberately enumerate divisors directly, independent of the reader.
  for (uint64_t r = 0; r < d; ++r) {
    if (r && d % r) continue;
    reps.array.push_back(num(r));
    Node e = Node::o({{"kind", "rep"},
                      {"levels", Node::a()},
                      {"max_bits", "1"},
                      {"position", num(r)},
                      {"scale", "1"}});
    descriptors.array.push_back(e);
    size_t id = leaves.size();
    leaves.push_back({"BODY", id, 0, 1, leaf_bytes(id, 0, {{1, 0}})});
  }
  Node unit_words = Node::a();
  uint64_t remaining = d;
  for (uint64_t p = 2; p <= remaining; ++p) {
    if (remaining % p) continue;
    unsigned exponent = 0;
    do {
      remaining /= p;
      ++exponent;
    } while (remaining % p == 0);
    unsigned count = p != 2 ? 1 : exponent < 2 ? 0 : exponent == 2 ? 1 : 2;
    for (unsigned j = 0; j < count; ++j) unit_words.array.push_back(Node::a({"0"}));
  }
  exmt = Node::o(
      {{"d", num(d)},
       {"elements", descriptors},
       {"entry_rule", Node::o({{"name", "sicx-divisors/1.0"}, {"representatives", reps}})},
       {"format", "SICX"},
       {"galois", Node::o({{"smith", Node::a({num(D)})}, {"unit_generator_words", unit_words}})},
       {"generator_rescaling", Node::o({{"rule", "sicx-2adic-greedy/1.0"}, {"shifts", shifts}})},
       {"required_features", Node::a()},
       {"tower", Node::o({{"branch_bits", "128"}, {"branches", branches}, {"degrees", degrees}})},
       {"version", Node::a({"1", "0"})}});
  physical.push_back({"EXMT", 1, 0, 0, 0, 1, 0, 0, 0, {}});
  for (size_t i = 0; i < leaves.size(); ++i) {
    const auto& l = leaves[i];
    physical.push_back({l.kind, 1, 0, 0, i + 1, 1, l.id, l.first, l.count, l.payload});
  }
  physical.push_back({"PROV", 0});
  if (finalized) {
    rays =
        Node::o({{"balls", Node::o({{"limbs", "1"}, {"working_bits", "128"}})},
                 {"decoder",
                  Node::o({{"name", "sic-decode/1.0"}, {"source_commit", std::string(40, 'a')}})},
                 {"rays", Node::a({Node::o({{"accuracy_bits", "127"},
                                            {"anchor", "0"},
                                            {"label", "c0"},
                                            {"word", Node::a({"0"})}})})}});
    physical.push_back({"RAYS"});
    Physical ball{"BALL", 1, 0, 0, 0, 0, 0, 0, d, {}};
    for (uint64_t i = 0; i < 2 * d; ++i) {
      // The unit vector e_0 with a small real radius. Not a SIC claim.
      put(ball.payload, 0);
      put(ball.payload, 0);
      put(ball.payload, i == 0 ? 1 : 0);
      put(ball.payload, i == 0 ? uint64_t(-128) : 0);
      put(ball.payload, i == 0 ? 1 : 0);
    }
    physical.push_back(ball);
    physical.push_back({"PREC", 0});
  }
}

auto Document::get(const std::string& type) -> Physical& {
  for (auto& c : physical)
    if (c.type == type) return c;
  throw std::runtime_error("test writer chunk missing: " + type);
}

auto Document::sync_leaves() -> void {
  for (auto& p : physical)
    if (p.type == "TOWR" || p.type == "BODY") {
      p.payload.clear();
      for (uint64_t i = p.first_leaf - 1; i < p.first_leaf - 1 + p.leaf_count; ++i)
        append(p.payload, leaves.at(size_t(i)).payload);
    }
}

auto Document::encode() const -> ByteBuffer {
  auto chunks = physical;
  ByteBuffer meta = bytes(exmt.dump());
  if (edit_exmt) edit_exmt(meta);
  for (auto& c : chunks) {
    if (c.type == "EXMT") c.payload = meta;
    if (c.type == "PROV") {
      c.payload = bytes(prov.dump());
      if (edit_prov) edit_prov(c.payload);
    }
    if (c.type == "PREC") c.payload = bytes(prec.dump());
    if (c.type == "RAYS") {
      c.payload = bytes(rays.dump());
      if (edit_rays) edit_rays(c.payload);
    }
  }
  std::vector<Leaf> all{{"EXMT", 0, 0, 1, meta}};
  all.insert(all.end(), leaves.begin(), leaves.end());
  std::vector<ByteBuffer> ld, pd;
  for (const auto& l : all) {
    ByteBuffer entry = bytes(l.kind);
    put(entry, 0, 4);
    put(entry, l.id);
    put(entry, l.first);
    put(entry, l.count);
    ByteBuffer message = bytes(std::string("SICX1-LEAF\0", 11));
    append(message, bytes(l.kind));
    put(message, l.id);
    put(message, l.first);
    put(message, l.count);
    put(message, l.payload.size());
    append(message, l.payload);
    auto h = hash(message);
    entry.insert(entry.end(), h.begin(), h.end());
    ld.push_back(entry);
  }
  if (edit_ldir) edit_ldir(ld);
  ByteBuffer out = header;
  for (const auto& c : chunks) {
    ByteBuffer e = bytes(c.type);
    put(e, c.flags, 4);
    put(e, out.size());
    put(e, c.payload.size());
    put(e, c.codec, 4);
    put(e, c.reserved, 4);
    put(e, c.payload.size());
    put(e, c.first_leaf);
    put(e, c.leaf_count);
    put(e, c.object);
    put(e, c.first);
    put(e, c.count);
    auto h = hash(c.payload);
    e.insert(e.end(), h.begin(), h.end());
    pd.push_back(e);
    append(out, bytes(c.type));
    put(out, c.flags, 4);
    put(out, c.payload.size());
    append(out, c.payload);
  }
  if (edit_pdir) edit_pdir(pd);
  uint64_t lo = out.size(), ll = 8 + 64 * ld.size();
  ByteBuffer ldir = bytes("LDIR");
  put(ldir, 1, 4);
  put(ldir, ll);
  put(ldir, ld.size());
  for (const auto& e : ld) append(ldir, e);
  append(out, ldir);
  uint64_t po = out.size(), pl = 8 + 112 * pd.size();
  append(out, bytes("PDIR"));
  put(out, 1, 4);
  put(out, pl);
  put(out, pd.size());
  for (const auto& e : pd) append(out, e);
  put(out, lo);
  put(out, ll);
  put(out, po);
  put(out, pl);
  auto er = tree(ld);
  out.insert(out.end(), er.begin(), er.end());
  ByteBuffer last = bytes(std::string("SICX1-LDIR\0", 11));
  auto lh = hash(ldir);
  last.insert(last.end(), lh.begin(), lh.end());
  pd.push_back(last);
  auto fr = tree(pd);
  out.insert(out.end(), fr.begin(), fr.end());
  put(out, out.size() + 16);
  append(out, ByteBuffer{'S', 'I', 'C', 'X', 'E', 'N', 'D', 26});
  return out;
}
}  // namespace fixture
