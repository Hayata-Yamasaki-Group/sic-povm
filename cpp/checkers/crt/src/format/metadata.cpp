// SPDX-License-Identifier: AGPL-3.0-or-later
#include "metadata.hpp"

#include "../runtime/diagnostics.hpp"
#include "canonical_json.hpp"
#include "container.hpp"
#include "integer_codec.hpp"
#include "sicx_input.hpp"
namespace sic_check::crt::sicx {
namespace {
constexpr const char* kDecoderIdentity = "sic-decode/1.0";
constexpr const char* kEntryIdentity = "sicx-divisors/1.0";
constexpr const char* kRescalingIdentity = "sicx-2adic-greedy/1.0";
}  // namespace
BallString::BallString(const string& s) {
  size_t pos = 0;
  Z* parts[] = {&M, &E, &m, &e};
  for (int j = 0; j < 4; ++j) {
    size_t end = j == 3 ? s.size() : s.find(' ', pos);
    if (end == string::npos || end == pos) reject("Arb: exactly four fields required");
    string a = s.substr(pos, end - pos);
    bool neg = a[0] == '-';
    string digits = neg ? a.substr(1) : a;
    if (digits.empty() || (digits.size() > 1 && digits[0] == '0') || (neg && digits == "0") ||
        (j == 2 && neg))
      reject("Arb: noncanonical integer");
    if (digits.size() > (1u << 21)) resource("Arb mantissa size");
    for (char c : digits)
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        reject("Arb: lowercase hexadecimal expected");
    if (fmpz_set_str(parts[j]->v, a.c_str(), 16)) reject("Arb integer");
    pos = end + 1;
  }
  if ((fmpz_is_zero(M.v) && !fmpz_is_zero(E.v)) || (!fmpz_is_zero(M.v) && fmpz_is_even(M.v)))
    reject("Arb midpoint normalization");
  if ((fmpz_is_zero(m.v) && !fmpz_is_zero(e.v)) ||
      (!fmpz_is_zero(m.v) && (fmpz_is_even(m.v) || fmpz_bits(m.v) > 30)))
    reject("Arb radius normalization");
}

auto BallString::ball(arb_t out) const -> void {
  arb_set_fmpz_2exp(out, M.v, E.v);
  if (!fmpz_is_zero(m.v)) {
    arf_t r;
    arf_init(r);
    arf_set_fmpz_2exp(r, m.v, e.v);
    arb_add_error_arf(out, r);
    arf_clear(r);
  }
}

auto Input::is_prime(Uint64 p) -> bool {
  if (p < 2) return false;
  for (Uint64 q = 2; q <= p / q; ++q)
    if (p % q == 0) return false;
  return true;
}

auto Input::squarefree_part(Uint64 n) -> Uint64 {
  Uint64 r = 1;
  for (Uint64 p = 2; p <= n / p; ++p) {
    bool odd = false;
    while (n % p == 0) {
      n /= p;
      odd = !odd;
    }
    if (odd) r = mul(r, p);
  }
  return mul(r, n);
}

auto Input::powmod(Uint64 a, Uint64 e, Uint64 n) -> Uint64 {
  Uint64 r = 1;
  for (; e; e >>= 1, a = a * a % n)
    if (e & 1) r = r * a % n;
  return r;
}

auto Input::canonical_units(Uint64 dimension) -> vector<UnitGenerator> {
  vector<UnitGenerator> out;
  Uint64 rest = dimension;
  for (Uint64 p = 2; p <= rest; ++p) {
    if (rest % p) continue;
    Uint64 q = 1;
    do {
      q *= p;
      rest /= p;
    } while (rest % p == 0);
    auto lift = [&](Uint64 a, Uint64 order) {
      Uint64 cofactor = dimension / q;
      Uint64 u = 1;
      while (u % q != a) u += cofactor;
      out.push_back({u, order});
    };
    if (p == 2) {
      if (q >= 4) lift(q - 1, 2);
      if (q >= 8) lift(5, q / 4);
    } else {
      Uint64 phi = (q / p) * (p - 1), n = phi;
      vector<Uint64> factors;
      for (Uint64 v = 2; v <= n; ++v) {
        if (n % v) continue;
        factors.push_back(v);
        do {
          n /= v;
        } while (n % v == 0);
      }
      Uint64 a = 2;
      for (;; ++a) {
        if (a % p == 0) continue;
        bool primitive = true;
        for (Uint64 v : factors)
          if (powmod(a, phi / v, q) == 1) primitive = false;
        if (primitive) break;
      }
      lift(a, phi);
    }
  }
  return out;
}

auto Input::entry_position(Uint64 m) const -> Uint64 {
  if (m >= d) reject("entry coordinate out of range");
  return m == 0 ? 0 : std::gcd(m, d);
}

auto Input::entry_word(Uint64 m) const -> vector<Uint64> {
  if (m >= d) reject("entry coordinate out of range");
  // Enumerate the direct product once, with the first generator the least significant digit.
  // This derived table stores one mixed-radix index per residue, not a dense Smith word array.
  if (unit_indices.empty()) {
    if (d > limits.dense_coordinates) resource("unit lookup allocation limit");
    unit_indices.assign(d, UINT64_MAX);
    std::function<void(size_t, Uint64, Uint64, Uint64)> enumerate =
        [&](size_t j, Uint64 u, Uint64 index, Uint64 stride) {
          if (j == unit_generators.size()) {
            unit_indices[u] = index;
            return;
          }
          const auto g = unit_generators[j];
          for (Uint64 a = 0; a < g.order; ++a, u = u * g.unit % d)
            enumerate(j + 1, u, index + a * stride, stride * g.order);
        };
    enumerate(0, 1, 0, 1);
  }
  Uint64 g = std::gcd(m, d), n = d / g, u = n == 1 ? 1 : m / g;
  while (std::gcd(u, d) != 1) u += n;
  Uint64 index = unit_indices[u];
  vector<Uint64> word(smith.size(), 0);
  for (size_t j = 0; j < unit_generators.size(); ++j) {
    Uint64 a = index % unit_generators[j].order;
    index /= unit_generators[j].order;
    for (size_t l = 0; l < smith.size(); ++l) word[l] = (word[l] + a * unit_words[j][l]) % smith[l];
  }
  return word;
}

auto Input::parse_meta(const vector<Uint8>& b, Uint64 hminor) -> void {
  Jv root = parse_canonical(b.data(), b.size(), "EXMT");
  j_keys(root,
         {"d", "elements", "entry_rule", "format", "galois", "generator_rescaling",
          "required_features", "tower", "version"},
         "EXMT");
  if (j_get(root, "format", 's').s != "SICX") reject("EXMT.format != SICX");
  {
    auto v = j_uvec(j_get(root, "version", 'a'), "version");
    if (v.size() != 2 || v[0] != 1 || v[1] != hminor) reject("EXMT.version");
  }
  if (!j_get(root, "required_features", 'a').a.empty()) reject("required_features non-empty");
  d = to_u64(j_get(root, "d", 's').s, "d");
  if (d < 2 || d > (1u << 20)) reject("d outside [2, 2^20]");
  D0 = d == 2 ? -3 : d == 3 ? 1 : (Int64)squarefree_part(mul(d + 1, d - 3));
  positions = {0};
  for (Uint64 g = 1; g < d; ++g)
    if (d % g == 0) positions.push_back(g);
  unit_generators = canonical_units(d);
  {
    const Jv& T = j_get(root, "tower", 'o');
    j_keys(T, {"branch_bits", "branches", "degrees"}, "tower");
    deg = j_uvec(j_get(T, "degrees", 'a'), "degrees");
    if (deg.empty() || deg.size() > 62) reject("tower must have 1..62 levels");
    for (Uint64 p : deg) {
      if (p > 31 || !is_prime(p)) reject("tower degree not a prime <= 31");
      D = mul(D, p);
      if (D > (1ull << 40)) reject("D too large");
    }
    branch_bits = to_u64(j_get(T, "branch_bits", 's').s, "branch_bits");
    if (branch_bits < 128 || (branch_bits & (branch_bits - 1)))
      reject("branch_bits must be 128 times a power of two");
    if (branch_bits > limits.branch_bits) resource("branch precision limit");
    const Jv& br = j_get(T, "branches", 'a');
    if (br.a.size() != deg.size()) reject("branches count");
    for (auto& x : br.a) {
      if (x.t != 'a' || x.a.size() != 2 || x.a[0].t != 's' || x.a[1].t != 's')
        reject("branch must be [re, im] strings");
      for (const auto& part : x.a) {
        BallString b(part.s);
        if (fmpz_bits(b.M.v) > limits.branch_bits + 64 ||
            fmpz_cmp_si(b.E.v, -(slong)limits.branch_bits) < 0 ||
            fmpz_cmp_ui(b.E.v, limits.branch_bits) > 0 ||
            fmpz_cmp_si(b.e.v, -(slong)limits.branch_bits) < 0 ||
            fmpz_cmp_ui(b.e.v, limits.branch_bits) > 0)
          resource("branch string size/exponent limit");
      }
      branches.push_back({x.a[0].s, x.a[1].s});
    }
  }
  {
    const Jv& G = j_get(root, "galois", 'o');
    j_keys(G, {"smith", "unit_generator_words"}, "galois");
    smith = j_uvec(j_get(G, "smith", 'a'), "smith");
    if (smith.empty()) reject("Smith list empty");
    Uint64 prod = 1;
    for (Uint64 s : smith) {
      if (s < 2 || D % s) reject("bad Smith factor");
      prod = mul(prod, s);
    }
    if (prod != D) reject("product of Smith factors != D");
    const auto& words = j_get(G, "unit_generator_words", 'a');
    if (words.a.size() != unit_generators.size()) reject("unit generator word count");
    for (size_t j = 0; j < words.a.size(); ++j) {
      auto word = j_uvec(words.a[j], "unit generator word");
      if (word.size() != smith.size()) reject("word length");
      for (size_t l = 0; l < smith.size(); ++l) {
        if (word[l] >= smith[l]) reject("word digit out of range");
        if (mul(unit_generators[j].order, word[l]) % smith[l])
          reject("unit generator word order relation");
      }
      unit_words.push_back(std::move(word));
    }
  }
  {
    const Jv& R = j_get(root, "generator_rescaling", 'o');
    j_keys(R, {"rule", "shifts"}, "generator_rescaling");
    if (j_get(R, "rule", 's').s != kRescalingIdentity) reject("rescaling rule");
    shifts = j_uvec(j_get(R, "shifts", 'a'), "shifts");
    if (shifts.size() != deg.size()) reject("rescaling shift count");
    const Jv& E = j_get(root, "entry_rule", 'o');
    j_keys(E, {"name", "representatives"}, "entry_rule");
    if (j_get(E, "name", 's').s != kEntryIdentity) reject("entry rule name");
    if (j_uvec(j_get(E, "representatives", 'a'), "representatives") != positions)
      reject("entry rule representatives");
  }
  const Jv& E = j_get(root, "elements", 'a');
  const int tt = t();
  const size_t naxes = smith.size();
  size_t pos = 0;
  auto add = [&](const Jv& o, const string& kind, Int64 level, Int64 axis, Int64 k,
                 Int64 position) {
    Elem e;
    e.kind = kind;
    e.level = level;
    e.axis = axis;
    e.k = k;
    e.position = position;
    std::set<string> want = {"kind", "levels", "scale", "max_bits"};
    if (kind == "rep")
      want.insert("position");
    else
      want.insert("level");
    if (kind == "act" || kind == "actodd") want.insert("axis");
    if (kind == "fcoef" || kind == "actodd") want.insert("k");
    if (o.t != 'o') reject("element must be an object");
    for (auto& kv : o.o)
      if (!want.count(kv.first))
        reject("element: key " + kv.first + " not allowed for kind " + kind);
    for (auto& w : want)
      if (!o.get(w)) reject("element: missing key " + w);
    e.field = kind == "rep" ? tt : (int)level;
    auto lv = j_uvec(j_get(o, "levels", 'a'), "levels");
    for (size_t i = 0; i < lv.size(); i++) {
      if ((Int64)lv[i] >= e.field) reject("level set outside the element's field");
      if (i && lv[i] <= lv[i - 1]) reject("level set not increasing");
      e.levels.push_back((int)lv[i]);
    }
    e.scale = j_get(o, "scale", 's').s;
    if (e.scale.empty() || e.scale.size() > 4095 || e.scale[0] < '1' || e.scale[0] > '9')
      reject("invalid scale");
    for (char c : e.scale)
      if (c < '0' || c > '9') reject("invalid scale");
    e.max_bits = to_u64(j_get(o, "max_bits", 's').s, "max_bits");
    if (e.max_bits > (1u << 23)) reject("max_bits exceeds 2^23");
    for (int j : e.levels) e.ncoord = mul(e.ncoord, deg[j]);
    Uint64 W = 1;
    for (size_t j = 0; j < e.levels.size(); j++) {
      Uint64 W2 = mul(W, deg[e.levels[j]]);
      if (W2 <= (1ull << 14))
        W = W2;
      else
        break;
    }
    e.W = W;
    e.nblocks = e.ncoord / W;
    el.push_back(std::move(e));
  };
  auto peek = [&](const char* kind, Int64 level, Int64 axis, Int64 k) -> bool {
    if (pos >= E.a.size()) return false;
    const Jv& o = E.a[pos];
    if (o.t != 'o') return false;
    const Jv* kd = o.get("kind");
    if (!kd || kd->t != 's' || kd->s != kind) return false;
    const Jv* l = o.get("level");
    if (!l || l->t != 's' || l->s != std::to_string(level)) return false;
    if (axis >= 0) {
      const Jv* a = o.get("axis");
      if (!a || a->t != 's' || a->s != std::to_string(axis)) return false;
    }
    if (k >= 0) {
      const Jv* kk = o.get("k");
      if (!kk || kk->t != 's' || kk->s != std::to_string(k)) return false;
    }
    return true;
  };
  for (int i = 0; i < tt; i++) {
    const Uint64 p = deg[i];
    if (p == 2) {
      if (!peek("wsq", i, -1, -1)) reject("expected wsq");
      add(E.a[pos++], "wsq", i, -1, -1, -1);
    } else
      for (Uint64 k = 0; k < p; k++) {
        if (!peek("fcoef", i, -1, (Int64)k)) reject("expected fcoef");
        add(E.a[pos++], "fcoef", i, -1, (Int64)k, -1);
      }
    bool has_delta = false, any_odd = false;
    if (p != 2 && peek("actdelta", i, -1, -1)) {
      add(E.a[pos++], "actdelta", i, -1, -1, -1);
      has_delta = true;
    }
    for (size_t l = 0; l < naxes; l++) {
      if (p == 2) {
        if (peek("act", i, (Int64)l, -1)) add(E.a[pos++], "act", i, (Int64)l, -1, -1);
      } else if (peek("actodd", i, (Int64)l, 0)) {
        for (Uint64 k = 0; k < p; k++) {
          if (!peek("actodd", i, (Int64)l, (Int64)k)) reject("actodd group incomplete");
          add(E.a[pos++], "actodd", i, (Int64)l, (Int64)k, -1);
        }
        any_odd = true;
      }
    }
    if (p != 2 && has_delta != any_odd) reject("actdelta present iff some actodd present");
  }
  for (Uint64 r : positions) {
    if (pos >= E.a.size()) reject("elements: missing representatives");
    const Jv& o = E.a[pos];
    const Jv* kd = o.get("kind");
    const Jv* po = o.get("position");
    if (!kd || kd->s != "rep" || !po || po->s != std::to_string(r))
      reject("expected rep " + std::to_string(r));
    add(o, "rep", -1, -1, -1, (Int64)r);
    pos++;
    rep[(Int64)r] = el.size() - 1;
  }
  if (pos != E.a.size()) reject("extra or out-of-order elements");
}

auto Input::parse_rays(const vector<Uint8>& b) -> void {
  Jv root = parse_canonical(b.data(), b.size(), "RAYS");
  if (root.get("decoder")) {
    j_keys(root, {"balls", "decoder", "rays"}, "RAYS");
    const auto& dec = j_get(root, "decoder", 'o');
    j_keys(dec, {"name", "source_commit"}, "decoder");
    const string& n = j_get(dec, "name", 's').s;
    const string& commit = j_get(dec, "source_commit", 's').s;
    if (n != kDecoderIdentity || (commit.size() != 40 && commit.size() != 64))
      reject("decoder name/commit length");
    for (char c : commit)
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) reject("decoder source_commit");
  } else
    j_keys(root, {"balls", "rays"}, "RAYS");
  const auto& balls = j_get(root, "balls", 'o');
  j_keys(balls, {"limbs", "working_bits"}, "balls");
  Uint64 parsed_limbs = to_u64(j_get(balls, "limbs", 's').s, "limbs"),
         parsed_working = to_u64(j_get(balls, "working_bits", 's').s, "working_bits");
  if (!parsed_limbs || parsed_limbs > 1024 || !parsed_working || parsed_working > (1u << 20))
    reject("BALL width/working_bits");
  const auto& rs = j_get(root, "rays", 'a');
  if (rs.a.empty()) reject("empty RAYS list");
  vector<Ray> parsed;
  for (const auto& x : rs.a) {
    j_keys(x, {"accuracy_bits", "anchor", "label", "word"}, "ray");
    Ray r;
    r.label = j_get(x, "label", 's').s;
    if (r.label.empty() || r.label[0] != 'c') reject("ray label spelling");
    Uint64 suffix = to_u64(r.label.substr(1), "ray label suffix");
    if (suffix != parsed.size()) reject("ray label must equal array index");
    r.word = j_uvec(j_get(x, "word", 'a'), "ray word");
    if (r.word.size() != smith.size()) reject("ray word length");
    for (size_t l = 0; l < smith.size(); ++l)
      if (r.word[l] >= smith[l]) reject("ray word residue");
    if (!parsed.empty() &&
        !std::lexicographical_compare(parsed.back().word.begin(), parsed.back().word.end(),
                                      r.word.begin(), r.word.end()))
      reject("ray words duplicate/out of increasing order");
    r.anchor = to_u64(j_get(x, "anchor", 's').s, "anchor");
    if (r.anchor >= d) reject("ray anchor range");
    r.accuracy = signed_decimal(j_get(x, "accuracy_bits", 's').s, "accuracy_bits");
    if (r.accuracy < -(1 << 20) || r.accuracy > (1 << 20)) reject("accuracy_bits range");
    parsed.push_back(r);
  }
  size_t nb = 0;
  Uint64 bytes = mul(mul(d, 2), add(32, mul(8, parsed_limbs)));
  for (const auto& c : ch)
    if (c.type == "BALL") {
      if (c.len != bytes) reject("BALL directory length");
      ++nb;
    }
  if (nb != parsed.size()) reject("BALL/RAYS count mismatch");
  limbs = parsed_limbs;
  working_bits = parsed_working;
  rays = std::move(parsed);
}

auto Input::check_ball(size_t ci, size_t ray) -> void {
  auto b = payload(ci);
  Uint64 width = add(32, mul(8, limbs));
  if (b.size() != mul(mul(d, 2), width)) reject("BALL length");
  A sum, part, square;
  arb_zero(sum.v);
  bool any = false;
  Int64 largest = INT64_MIN;
  Uint64 prec = add(working_bits, 64);
  for (Uint64 j = 0; j < d; ++j)
    for (int im = 0; im < 2; ++im) {
      const Uint8* p = b.data() + (j * 2 + im) * width;
      Uint64 flags = le64(p);
      Int64 E, e;
      Uint64 Eu = le64(p + 8), eu = le64(p + 24);
      memcpy(&E, &Eu, 8);
      memcpy(&e, &eu, 8);
      Uint64 m = le64(p + 16);
      if (flags > 1 || E < -(1ll << 40) || E > (1ll << 40) || e < -(1ll << 40) || e > (1ll << 40))
        reject("BALL flags/exponent range");
      Z M, ez(E);
      vector<ulong> words(limbs);
      for (size_t k = 0; k < limbs; ++k) words[k] = le64(p + 32 + 8 * k);
      fmpz_set_ui_array(M.v, words.data(), (slong)limbs);
      if ((fmpz_is_zero(M.v) && (flags || E)) || (!fmpz_is_zero(M.v) && fmpz_is_even(M.v)))
        reject("BALL midpoint normalization");
      if ((m == 0 && e) || (m && (!(m & 1) || m >= (1u << 30))))
        reject("BALL radius normalization");
      if (flags) fmpz_neg(M.v, M.v);
      arb_set_fmpz_2exp(part.v, M.v, ez.v);
      if (m) {
        Z mz((Int64)m), ex(e);
        arf_t r;
        arf_init(r);
        arf_set_fmpz_2exp(r, mz.v, ex.v);
        arb_add_error_arf(part.v, r);
        arf_clear(r);
        unsigned bits = 0;
        for (Uint64 n = m; n; n >>= 1) ++bits;
        largest = std::max<Int64>(largest, e + bits);
        any = true;
      }
      if (j == rays.at(ray).anchor) {
        if (im) {
          if (!arb_is_zero(part.v)) reject("anchor imaginary part is not exactly zero");
        } else if (!arb_is_positive(part.v))
          reject("anchor is not strictly positive");
      }
      arb_sqr(square.v, part.v, (slong)prec);
      arb_add(sum.v, sum.v, square.v, (slong)prec);
    }
  if (!any) reject("BALL all-zero radii");
  if (rays.at(ray).accuracy != -largest) reject("accuracy_bits is not tight");
  if (!arb_contains_si(sum.v, 1)) reject("BALL squared norm enclosure excludes 1");
  scope.chunks.insert(ci);
}
}  // namespace sic_check::crt::sicx
