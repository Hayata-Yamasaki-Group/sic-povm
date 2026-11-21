// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_writer.hpp"

namespace fixture {
void append(ByteBuffer& a, const ByteBuffer& b) { a.insert(a.end(), b.begin(), b.end()); }

ByteBuffer text(const std::string& s) { return ByteBuffer(s.begin(), s.end()); }

void word(ByteBuffer& b, U n, int width) {
  for (int j = 0; j < width; ++j) b.push_back(n >> (8 * j));
}

void set(ByteBuffer& b, size_t p, U n, int width) {
  for (int j = 0; j < width; ++j) b.at(p + j) = n >> (8 * j);
}

U get(const ByteBuffer& b, size_t p) {
  U n = 0;
  for (int j = 7; j >= 0; --j) n = n * 256 + b.at(p + j);
  return n;
}

Hash digest(const ByteBuffer& b) {
  Hash h;
  SHA256(b.data(), b.size(), h.data());
  return h;
}

Hash tree(const std::vector<ByteBuffer>& entries, size_t begin, size_t end) {
  if (begin == end) return digest({});
  if (end - begin == 1) {
    ByteBuffer b{0};
    append(b, entries[begin]);
    return digest(b);
  }
  size_t k = 1;
  while (k * 2 < end - begin) k *= 2;
  auto a = tree(entries, begin, begin + k), b = tree(entries, begin + k, end);
  ByteBuffer bytes{1};
  bytes.insert(bytes.end(), a.begin(), a.end());
  bytes.insert(bytes.end(), b.begin(), b.end());
  return digest(bytes);
}

void var(ByteBuffer& b, U n) {
  while (n >= 128) {
    b.push_back((n & 127) | 128);
    n >>= 7;
  }
  b.push_back(n);
}

void integer(ByteBuffer& b, int64_t n) {
  U a = n < 0 ? U(-(n + 1)) + 1 : U(n);
  unsigned bytes = 0;
  for (U v = a; v; v >>= 8) ++bytes;
  var(b, 2 * bytes + (n < 0));
  word(b, a, bytes);
}

std::string str(const std::string& s) {
  std::string b = "\"";
  const char* hex = "0123456789abcdef";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      b += '\\';
      b += c;
    } else if (c < 32) {
      b += "\\u00";
      b += hex[c / 16];
      b += hex[c % 16];
    } else
      b += c;
  }
  return b + '"';
}

std::string arr(const std::vector<std::string>& a) {
  std::string b = "[";
  for (auto& s : a) {
    if (b.size() > 1) b += ',';
    b += s;
  }
  return b + ']';
}

std::string obj(const std::map<std::string, std::string>& a) {
  std::string b = "{";
  for (auto& [k, v] : a) {
    if (b.size() > 1) b += ',';
    b += str(k) + ':' + v;
  }
  return b + '}';
}

std::string nums(const std::vector<U>& a) {
  std::vector<std::string> out;
  for (auto n : a) out.push_back(str(std::to_string(n)));
  return arr(out);
}

Big::Big() { fmpz_init(z); }

Big::~Big() { fmpz_clear(z); }

std::string hex(const fmpz_t n) {
  char* p = fmpz_get_str(nullptr, 16, n);
  std::string s = p;
  flint_free(p);
  return s;
}

ByteBuffer leaf(U id, U first, U count, const std::vector<std::array<int64_t, 3>>& pairs, int D0) {
  ByteBuffer b;
  var(b, id);
  var(b, first);
  var(b, count);
  U at = 0;
  size_t pos = 0;
  while (at < count) {
    bool data = pos < pairs.size() && U(pairs[pos][0]) == first + at;
    U run = 0;
    size_t start = pos;
    if (data) {
      while (pos < pairs.size() && U(pairs[pos][0]) == first + at + run) {
        ++run;
        ++pos;
      }
    } else {
      U stop = pos < pairs.size() ? U(pairs[pos][0]) - first : count;
      run = stop - at;
    }
    var(b, 2 * run + data);
    if (data)
      for (size_t j = start; j < pos; ++j) {
        auto [sub, x, y] = pairs[j];
        (void)sub;
        int64_t r = x;
        if (D0 > 1 && D0 % 4 == 1) {  // Small fixture coordinates only; exact integer square root
                                      // via a checked integer loop.
          U n = U(y < 0 ? -y : y);
          n = n * n * D0;
          U a = 0;
          while ((a + 1) * (a + 1) <= n) ++a;
          int64_t signed_a = y < 0 ? -(int64_t)a - 1 : (int64_t)a;
          int64_t delta = signed_a - y;
          int64_t k = delta >= 0 ? delta / 2 : -((-delta + 1) / 2);
          r = x - k;
        } else if (D0 < 0 && (D0 % 4 + 4) % 4 == 1) {
          int64_t half = y >= 0 ? y / 2 : -((-y + 1) / 2);
          r = x + half;
        }
        integer(b, r);
        integer(b, y);
      }
    at += run;
  }
  return b;
}

std::string descriptor(const std::string& kind, U index, const std::vector<U>& levels,
                       const std::string& scale, U bits, int axis, int k) {
  std::map<std::string, std::string> o{{"kind", str(kind)},
                                       {"levels", nums(levels)},
                                       {"max_bits", str(std::to_string(bits))},
                                       {"scale", str(scale)}};
  o[kind == "rep" ? "position" : "level"] = str(std::to_string(index));
  if (axis >= 0) o["axis"] = str(std::to_string(axis));
  if (k >= 0) o["k"] = str(std::to_string(k));
  return obj(o);
}

std::string zero_unit_words(U d, const std::vector<U>& word) {
  std::vector<std::string> words;
  for (U p = 2; p <= d; ++p) {
    if (d % p) continue;
    U q = 1;
    do {
      q *= p;
      d /= p;
    } while (d % p == 0);
    if (p != 2 || q >= 4) words.push_back(nums(word));
    if (p == 2 && q >= 8) words.push_back(nums(word));
  }
  return arr(words);
}

File::File(U dimension, bool finalized, bool grouped) : d(dimension) {
  std::vector<std::string> elements, br;
  std::vector<U> degrees, smith, shifts;
  unsigned levels = grouped ? 16 : 1;
  const U primes[] = {2, 3, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59};
  for (unsigned j = 0; j < levels; ++j) {
    degrees.push_back(2);
    smith.push_back(2);
    shifts.push_back(0);
    U n = grouped ? primes[j] : 2, scale = n * (U(1) << j);
    U bits = 0;
    for (U v = scale; v; v >>= 1) ++bits;
    U id = elements.size();
    elements.push_back(descriptor("wsq", j, {}, std::to_string(scale), bits));
    br.push_back(arr({str(sqrt_box(n)), str("0 0 0 0")}));
    L l;
    l.kind = "TOWR";
    l.element = id;
    l.bytes = leaf(id, 0, 1, {{0, 1, 0}}, d == 2 ? -3 : 5);
    leaves.push_back(l);
    if (grouped) {
      id = elements.size();
      elements.push_back(descriptor("act", j, {}, std::to_string(scale), bits, j));
      l.element = id;
      l.bytes = leaf(id, 0, 1, {{0, -1, 0}});
      leaves.push_back(l);
    }
  }
  std::vector<U> positions;
  positions.push_back(0);
  for (U r = 1; r < d; ++r)
    if (d % r == 0) positions.push_back(r);
  for (U r : positions) {
    U id = elements.size();
    std::vector<U> active;
    if (grouped)
      for (unsigned j = 0; j < levels; ++j) active.push_back(j);
    elements.push_back(descriptor("rep", r, active, "1", 1));
    if (grouped) {
      for (U b = 0; b < 4; ++b) {
        L l;
        l.kind = "BODY";
        l.element = id;
        l.first = b * 16384;
        l.count = 16384;
        l.bytes = leaf(id, l.first, l.count,
                       b == 3 ? std::vector<std::array<int64_t, 3>>{{65535, 1, 0}}
                              : std::vector<std::array<int64_t, 3>>{});
        leaves.push_back(l);
      }
    } else {
      L l;
      l.kind = "BODY";
      l.element = id;
      l.bytes = leaf(id, 0, 1, {{0, 1, 0}}, d == 2 ? -3 : 5);
      leaves.push_back(l);
    }
  }
  std::vector<U> words(levels, 0);
  meta =
      obj({{"d", str(std::to_string(d))},
           {"elements", arr(elements)},
           {"entry_rule",
            obj({{"name", str("sicx-divisors/1.0")}, {"representatives", nums(positions)}})},
           {"format", str("SICX")},
           {"galois",
            obj({{"smith", nums(smith)}, {"unit_generator_words", zero_unit_words(d, words)}})},
           {"generator_rescaling",
            obj({{"rule", str("sicx-2adic-greedy/1.0")}, {"shifts", nums(shifts)}})},
           {"required_features", "[]"},
           {"tower",
            obj({{"branch_bits", str("128")}, {"branches", arr(br)}, {"degrees", nums(degrees)}})},
           {"version", nums({1, 0})}});
  rebuild_physical(false);
  if (finalized) finalize();
}

auto File::rebuild_physical(bool join) -> void {
  physical.clear();
  P ex;
  ex.type = "EXMT";
  ex.first_leaf = 0;
  ex.leaf_count = 1;
  physical.push_back(ex);
  for (size_t i = 0; i < leaves.size();) {
    const auto& l = leaves[i];
    P p;
    p.type = l.kind;
    p.first_leaf = i + 1;
    p.object = l.element;
    p.first = l.first;
    do {
      append(p.bytes, leaves[i].bytes);
      p.count += leaves[i].count;
      ++p.leaf_count;
      ++i;
    } while (join && i < leaves.size() && leaves[i].element == l.element);
    physical.push_back(p);
  }
}

auto File::finalize() -> void {
  P rays;
  rays.type = "RAYS";
  rays.bytes = text(obj({{"balls", obj({{"limbs", str("1")}, {"working_bits", str("128")}})},
                         {"rays", arr({obj({{"accuracy_bits", str("127")},
                                            {"anchor", str("0")},
                                            {"label", str("c0")},
                                            {"word", nums(std::vector<U>(get_rank(), 0))}})})}}));
  physical.push_back(rays);
  P ball;
  ball.type = "BALL";
  ball.count = d;
  for (U j = 0; j < d; ++j)
    for (int k = 0; k < 2; ++k) {
      bool anchor = j == 0 && k == 0;
      word(ball.bytes, 0);
      word(ball.bytes, 0);
      word(ball.bytes, anchor ? 1 : 0);
      word(ball.bytes, anchor ? U(int64_t(-128)) : 0);
      word(ball.bytes, anchor ? 1 : 0);
    }
  physical.push_back(ball);
}

auto File::get_rank() const -> unsigned {
  return meta.find("\"degrees\":[\"2\",\"2\"") != std::string::npos ? 16 : 1;
}

auto File::encode() const -> ByteBuffer {
  std::vector<L> logical = leaves;
  L ex;
  ex.kind = "EXMT";
  ex.count = 1;
  ex.bytes = text(meta);
  logical.insert(logical.begin(), ex);
  ByteBuffer out = text("SICX\r\n\x1a\n");
  word(out, 1, 2);
  word(out, 0, 2);
  word(out, 0, 4);
  std::vector<ByteBuffer> ps, ls;
  for (const auto& l : logical) {
    ByteBuffer e = text(l.kind);
    word(e, l.reserved, 4);
    word(e, l.element);
    word(e, l.first);
    word(e, l.count);
    ByteBuffer tagged = text(std::string("SICX1-LEAF\0", 11));
    append(tagged, text(l.kind));
    word(tagged, l.element);
    word(tagged, l.first);
    word(tagged, l.count);
    word(tagged, l.bytes.size());
    append(tagged, l.bytes);
    auto h = digest(tagged);
    e.insert(e.end(), h.begin(), h.end());
    ls.push_back(e);
  }
  for (size_t i = 0; i < physical.size(); ++i) {
    auto p = physical[i];
    if (i == 0) p.bytes = text(meta);
    ByteBuffer e = text(p.type);
    word(e, p.flags, 4);
    word(e, p.offset ? p.offset : out.size());
    word(e, p.bytes.size());
    word(e, p.codec, 4);
    word(e, p.reserved, 4);
    word(e, p.decoded ? p.decoded : p.bytes.size());
    word(e, p.first_leaf);
    word(e, p.leaf_count);
    word(e, p.object);
    word(e, p.first);
    word(e, p.count);
    auto h = digest(p.bytes);
    e.insert(e.end(), h.begin(), h.end());
    ps.push_back(e);
    append(out, text(p.type));
    word(out, p.flags, 4);
    word(out, p.bytes.size());
    append(out, p.bytes);
  }
  U lo = out.size();
  ByteBuffer lb = text("LDIR");
  word(lb, 1, 4);
  word(lb, 8 + 64 * ls.size());
  word(lb, ls.size());
  for (auto& e : ls) append(lb, e);
  append(out, lb);
  U po = out.size();
  append(out, text("PDIR"));
  word(out, 1, 4);
  word(out, 8 + 112 * ps.size());
  word(out, ps.size());
  for (auto& e : ps) append(out, e);
  word(out, lo);
  word(out, lb.size() - 16);
  word(out, po);
  word(out, 8 + 112 * ps.size());
  auto er = tree(ls, 0, ls.size());
  out.insert(out.end(), er.begin(), er.end());
  ByteBuffer bind = text(std::string("SICX1-LDIR\0", 11));
  auto ld = digest(lb);
  bind.insert(bind.end(), ld.begin(), ld.end());
  ps.push_back(bind);
  auto fr = tree(ps, 0, ps.size());
  out.insert(out.end(), fr.begin(), fr.end());
  word(out, out.size() + 16);
  append(out, text(std::string("SICXEND\x1a", 8)));
  return out;
}

File odd(bool actions) {
  File f;
  f.leaves.clear();
  std::vector<std::string> descriptors;
  auto add = [&](const std::string& kind, U index, int64_t x, const std::string& scale, U bits,
                 int axis = -1, int k = -1) {
    U id = descriptors.size();
    descriptors.push_back(descriptor(kind, index, {}, scale, bits, axis, k));
    L l;
    l.kind = kind == "rep" ? "BODY" : "TOWR";
    l.element = id;
    l.bytes = leaf(
        id, 0, 1,
        x ? std::vector<std::array<int64_t, 3>>{{0, x, 0}} : std::vector<std::array<int64_t, 3>>{});
    f.leaves.push_back(l);
  };
  add("fcoef", 0, -1, "2", 2, -1, 0);
  add("fcoef", 0, 0, "1", 0, -1, 1);
  add("fcoef", 0, 0, "1", 0, -1, 2);
  if (actions) {
    add("actdelta", 0, 1, "1", 1);
    for (int k = 0; k < 3; ++k) add("actodd", 0, k == 1 ? 1 : 0, "1", k == 1 ? 1 : 0, 0, k);
  }
  for (U r : {0, 1, 2}) add("rep", r, 1, "1", 1);
  f.meta = obj({{"d", str("4")},
                {"elements", arr(descriptors)},
                {"entry_rule",
                 obj({{"name", str("sicx-divisors/1.0")}, {"representatives", nums({0, 1, 2})}})},
                {"format", str("SICX")},
                {"galois", obj({{"smith", nums({3})}, {"unit_generator_words", arr({nums({0})})}})},
                {"generator_rescaling",
                 obj({{"rule", str("sicx-2adic-greedy/1.0")}, {"shifts", nums({0})}})},
                {"required_features", "[]"},
                {"tower", obj({{"branch_bits", str("128")},
                               {"branches", arr({arr({str(cbrt2_box()), str("0 0 0 0")})})},
                               {"degrees", nums({3})}})},
                {"version", nums({1, 0})}});
  f.rebuild_physical(false);
  return f;
}

File d2_spec() {
  File f(2);
  f.leaves.clear();
  std::vector<std::string> descriptors{
      descriptor("wsq", 0, {}, "1", 1), descriptor("act", 0, {}, "1", 1, 0),
      descriptor("rep", 0, {}, "2", 2), descriptor("rep", 1, {0}, "2", 2)};
  for (U id = 0; id < 4; ++id) {
    L l;
    l.kind = id < 2 ? "TOWR" : "BODY";
    l.element = id;
    l.count = id == 3 ? 2 : 1;
    l.bytes = leaf(id, 0, l.count,
                   id == 3 ? std::vector<std::array<int64_t, 3>>{{0, 0, -1}, {1, 1, -1}}
                           : std::vector<std::array<int64_t, 3>>{{0, id == 0 ? -1 : 1, 0}},
                   -3);
    f.leaves.push_back(l);
  }
  f.meta = obj(
      {{"d", str("2")},
       {"elements", arr(descriptors)},
       {"entry_rule", obj({{"name", str("sicx-divisors/1.0")}, {"representatives", nums({0, 1})}})},
       {"format", str("SICX")},
       {"galois", obj({{"smith", nums({2})}, {"unit_generator_words", "[]"}})},
       {"generator_rescaling",
        obj({{"rule", str("sicx-2adic-greedy/1.0")}, {"shifts", nums({0})}})},
       {"required_features", "[]"},
       {"tower", obj({{"branch_bits", str("128")},
                      {"branches", arr({arr({str("0 0 0 0"), str("1 0 0 0")})})},
                      {"degrees", nums({2})}})},
       {"version", nums({1, 0})}});
  f.rebuild_physical(false);
  return f;
}

std::string decimal(const fmpz_t n) {
  char* p = fmpz_get_str(nullptr, 10, n);
  std::string s = p;
  flint_free(p);
  return s;
}

File faithful_units(U dimension, std::vector<U> orders) {
  File f(dimension);
  std::vector<U> smith = orders.empty() ? std::vector<U>{2} : orders;
  std::vector<U> degrees, shifts, positions{0};
  for (U n : smith)
    for (U p = 2; n > 1; ++p)
      while (n % p == 0) {
        degrees.push_back(p);
        n /= p;
      }
  std::vector<std::string> descriptors, branches, words;
  f.leaves.clear();
  U N = 1;
  auto add = [&](const std::string& kind, U index, int64_t x, U scale, int axis = -1, int k = -1) {
    U id = descriptors.size(), bits = 0;
    for (U n = U(x < 0 ? -x : x) * scale; n; n >>= 1) ++bits;
    descriptors.push_back(descriptor(kind, index, {}, std::to_string(scale), bits, axis, k));
    L leaf_data;
    leaf_data.kind = kind == "rep" ? "BODY" : "TOWR";
    leaf_data.element = id;
    leaf_data.bytes = leaf(
        id, 0, 1,
        x ? std::vector<std::array<int64_t, 3>>{{0, x, 0}} : std::vector<std::array<int64_t, 3>>{});
    f.leaves.push_back(leaf_data);
  };
  const U radicands[]{2, 3, 5, 7, 11, 13};
  for (size_t i = 0; i < degrees.size(); ++i) {
    U p = degrees[i];
    shifts.push_back(0);
    if (p == 2) {
      add("wsq", i, 1, N * radicands[i]);
      add("act", i, 1, 1, 0);
      branches.push_back(arr({str(sqrt_box(radicands[i])), str("0 0 0 0")}));
    } else {
      if (p != 3) throw std::runtime_error("faithful fixture supports degrees two and three only");
      for (U k = 0; k < p; ++k) add("fcoef", i, k == 0 ? -1 : 0, k == 0 ? 2 * N : 1, -1, k);
      branches.push_back(arr({str(cbrt2_box()), str("0 0 0 0")}));
    }
    N *= p;
  }
  for (U g = 1; g < dimension; ++g)
    if (dimension % g == 0) positions.push_back(g);
  for (U g : positions) add("rep", g, 1, 1);
  for (size_t j = 0; j < orders.size(); ++j) {
    std::vector<U> word(smith.size(), 0);
    word[j] = 1;
    words.push_back(nums(word));
  }
  f.meta = obj({{"d", str(std::to_string(dimension))},
                {"elements", arr(descriptors)},
                {"entry_rule",
                 obj({{"name", str("sicx-divisors/1.0")}, {"representatives", nums(positions)}})},
                {"format", str("SICX")},
                {"galois", obj({{"smith", nums(smith)}, {"unit_generator_words", arr(words)}})},
                {"generator_rescaling",
                 obj({{"rule", str("sicx-2adic-greedy/1.0")}, {"shifts", nums(shifts)}})},
                {"required_features", "[]"},
                {"tower", obj({{"branch_bits", str("128")},
                               {"branches", arr(branches)},
                               {"degrees", nums(degrees)}})},
                {"version", nums({1, 0})}});
  f.rebuild_physical(false);
  return f;
}

void replace(std::string& s, const std::string& a, const std::string& b) {
  auto pos = s.find(a);
  if (pos == std::string::npos) throw std::runtime_error("missing fixture mutation target: " + a);
  s.replace(pos, a.size(), b);
}

void reseal(ByteBuffer& out) {
  size_t footer = out.size() - 112;
  U lo = get(out, footer), po = get(out, footer + 16);
  U nl = get(out, lo + 16), np = get(out, po + 16);
  std::vector<ByteBuffer> ls, ps;
  for (U i = 0; i < nl; ++i)
    ls.emplace_back(out.begin() + lo + 24 + 64 * i, out.begin() + lo + 24 + 64 * (i + 1));
  for (U i = 0; i < np; ++i)
    ps.emplace_back(out.begin() + po + 24 + 112 * i, out.begin() + po + 24 + 112 * (i + 1));
  auto er = tree(ls, 0, ls.size());
  std::copy(er.begin(), er.end(), out.begin() + footer + 32);
  ByteBuffer bind = text(std::string("SICX1-LDIR\0", 11));
  ByteBuffer lb(out.begin() + lo, out.begin() + po);
  auto lh = digest(lb);
  bind.insert(bind.end(), lh.begin(), lh.end());
  ps.push_back(bind);
  auto fr = tree(ps, 0, ps.size());
  std::copy(fr.begin(), fr.end(), out.begin() + footer + 64);
}

std::string sqrt_box(U number, U p) {
  Big N, q, t;
  fmpz_one(N.z);
  fmpz_mul_ui(N.z, N.z, number);
  fmpz_mul_2exp(N.z, N.z, 2 * p);
  fmpz_sqrt(q.z, N.z);
  fmpz_mul_2exp(t.z, q.z, 1);
  fmpz_add_ui(t.z, t.z, 1);
  fmpz_mul(t.z, t.z, t.z);
  fmpz_mul_2exp(N.z, N.z, 2);
  if (fmpz_cmp(N.z, t.z) > 0) fmpz_add_ui(q.z, q.z, 1);
  U trailing = fmpz_val2(q.z);
  fmpz_fdiv_q_2exp(q.z, q.z, trailing);
  fmpz_set_si(t.z, (int64_t)trailing - (int64_t)p);
  std::string s = hex(q.z) + " " + hex(t.z) + " 1 ";
  fmpz_set_si(t.z, -(int64_t)p);
  return s + hex(t.z);
}

std::string cbrt2_box(U p) {
  Big N, q, t;
  fmpz_one(N.z);
  fmpz_mul_2exp(N.z, N.z, 3 * p + 1);
  fmpz_root(q.z, N.z, 3);
  fmpz_mul_2exp(t.z, q.z, 1);
  fmpz_add_ui(t.z, t.z, 1);
  fmpz_pow_ui(t.z, t.z, 3);
  fmpz_mul_2exp(N.z, N.z, 3);
  if (fmpz_cmp(N.z, t.z) > 0) fmpz_add_ui(q.z, q.z, 1);
  U trailing = fmpz_val2(q.z);
  fmpz_fdiv_q_2exp(q.z, q.z, trailing);
  fmpz_set_si(t.z, (int64_t)trailing - (int64_t)p);
  std::string s = hex(q.z) + " " + hex(t.z) + " 1 ";
  fmpz_set_si(t.z, -(int64_t)p);
  return s + hex(t.z);
}
}  // namespace fixture
