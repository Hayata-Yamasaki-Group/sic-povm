// SPDX-License-Identifier: AGPL-3.0-or-later
#include "metadata.hpp"

#include "../runtime/diagnostics.hpp"
#include "ball_codec.hpp"
#include "canonical_json.hpp"
#include "container.hpp"
#include "integer_codec.hpp"
#include "sha256.hpp"
#include "units.hpp"
namespace sic_check::verifier {
namespace {
constexpr const char* kEntryIdentity = "sicx-divisors/1.0";
constexpr const char* kRescalingIdentity = "sicx-2adic-greedy/1.0";
}  // namespace
Meta::Meta() { fmpz_init(D0); }

Meta::~Meta() { fmpz_clear(D0); }

auto Meta::t() const -> int { return (int)deg.size(); }

string ekey(const string& kind, Int64 a, Int64 b, Int64 c) {
  return kind + ":" + std::to_string(a) + ":" + std::to_string(b) + ":" + std::to_string(c);
}

bool is_prime(Uint64 p) {
  if (p < 2) return false;
  for (Uint64 q = 2; q * q <= p; q++)
    if (p % q == 0) return false;
  return true;
}

Uint64 squarefree_part_signed_pos(Uint64 n) {  // n > 0
  Uint64 r = 1;
  for (Uint64 p = 2; p * p <= n; p++) {
    int e = 0;
    while (n % p == 0) {
      n /= p;
      e++;
    }
    if (e & 1) r *= p;
  }
  return r * n;
}

void parse_rays(Meta& M, const Jv& balls, const Jv& rays) {
  j_keys(balls, {"limbs", "working_bits"}, "balls");
  M.limbs = to_u64(j_get(balls, "limbs", 's').s, "limbs");
  M.working_bits = to_u64(j_get(balls, "working_bits", 's').s, "working_bits");
  if (M.limbs < 1 || M.limbs > 1024) reject("limbs out of range 1..1024");
  if (M.working_bits < 1 || M.working_bits > (1u << 20)) reject("working_bits > 2^20");
  if (rays.t != 'a' || rays.a.empty()) reject("rays: non-empty array expected");
  std::set<string> labels;
  Uint64 previous = 0;
  bool have_previous = false;
  for (auto& r : rays.a) {
    j_keys(r, {"accuracy_bits", "anchor", "label", "word"}, "ray");
    RayRec R;
    R.label = j_get(r, "label", 's').s;
    if (R.label.empty()) reject("empty ray label");
    if (!labels.insert(R.label).second) reject("duplicate ray label " + R.label);
    if (R.label.size() < 2 || R.label[0] != 'c') reject("noncanonical ray label");
    Uint64 suffix = to_u64(R.label.substr(1), "ray label suffix");
    if (have_previous && suffix <= previous) reject("ray labels not in numeric order");
    previous = suffix;
    have_previous = true;
    R.word = j_uvec(j_get(r, "word", 'a'), "ray word");
    if (R.word.size() != M.smith.size()) reject("ray word length != number of Smith factors");
    for (size_t l = 0; l < R.word.size(); l++)
      if (R.word[l] >= M.smith[l]) reject("ray word digit out of range");
    if (!M.rays.empty() &&
        !std::lexicographical_compare(M.rays.back().word.begin(), M.rays.back().word.end(),
                                      R.word.begin(), R.word.end()))
      reject("ray words not strictly increasing with numeric labels");
    R.anchor = to_u64(j_get(r, "anchor", 's').s, "anchor");
    if (R.anchor >= M.d) reject("anchor out of range");
    R.accuracy_bits = to_i64(j_get(r, "accuracy_bits", 's').s, "accuracy_bits");
    if (R.accuracy_bits > (1 << 20) || R.accuracy_bits < -(1 << 20))
      reject("|accuracy_bits| > 2^20");
    M.rays.push_back(R);
  }
  M.has_rays = true;
}

void parse_exmt(Meta& M, const Uint8* b, size_t n) {
  if (n > (64u << 20)) reject("EXMT larger than 64 MiB");
  Jv root = parse_canonical(b, n, "EXMT");
  j_keys(root,
         {"d", "elements", "entry_rule", "format", "galois", "generator_rescaling",
          "required_features", "tower", "version"},
         "EXMT");
  if (j_get(root, "format", 's').s != "SICX") reject("EXMT.format != SICX");
  {
    auto v = j_uvec(j_get(root, "version", 'a'), "version");
    if (v.size() != 2 || v[0] != 1 || v[1] != 0) reject("EXMT.version major must be 1");
    M.minor = v[1];
  }
  if (!j_get(root, "required_features", 'a').a.empty()) reject("required_features non-empty");
  M.d = to_u64(j_get(root, "d", 's').s, "d");
  if (M.d < 2 || M.d > (1u << 20)) reject("d is outside [2, 2^20]");
  // D0 = squarefree part with sign of (d+1)(d-3)
  {
    if (M.d == 2)
      fmpz_set_si(M.D0, -3);
    else if (M.d == 3)
      fmpz_one(M.D0);
    else
      fmpz_set_ui(M.D0, squarefree_part_signed_pos((M.d + 1) * (M.d - 3)));
  }
  M.units = unit_generators(M.d);
  M.representatives = divisor_positions(M.d);
  {
    const Jv& T = j_get(root, "tower", 'o');
    j_keys(T, {"branch_bits", "branches", "degrees"}, "tower");
    M.branch_bits = to_u64(j_get(T, "branch_bits", 's').s, "branch_bits");
    if (M.branch_bits < 128 || (M.branch_bits & (M.branch_bits - 1)))
      reject("branch_bits must be 128 times a power of two");
    if (M.branch_bits > (1u << 20))
      throw PrecErr("branch_bits exceeds reader precision resource limit 2^20");
    M.deg = j_uvec(j_get(T, "degrees", 'a'), "degrees");
    if (M.deg.empty() || M.deg.size() > 62) reject("tower must have 1..62 levels");
    for (Uint64 p : M.deg) {
      if (p > 31 || !is_prime(p)) reject("tower degree not a prime <= 31");
      if (M.D > (1ull << 40) / p) reject("D too large");
      M.D *= p;
      if (M.D > (1ull << 40)) reject("D too large");
    }
    const Jv& br = j_get(T, "branches", 'a');
    if (br.a.size() != M.deg.size()) reject("branches count != degrees count");
    for (auto& x : br.a) {
      if (x.t != 'a' || x.a.size() != 2 || x.a[0].t != 's' || x.a[1].t != 's')
        reject("branch must be [re, im] strings");
      M.branch_re.push_back({x.a[0].s, x.a[1].s});
      Integer a1, a2, a3, a4;
      parse_arbdump(x.a[0].s, a1, a2, a3, a4);
      parse_arbdump(x.a[1].s, a1, a2, a3, a4);
    }
  }
  {
    const Jv& G = j_get(root, "galois", 'o');
    j_keys(G, {"smith", "unit_generator_words"}, "galois");
    M.smith = j_uvec(j_get(G, "smith", 'a'), "smith");
    if (M.smith.empty()) reject("Smith list empty");
    Uint64 prod = 1;
    for (Uint64 s : M.smith) {
      if (s < 2) reject("Smith factor < 2");
      if (M.D % s) reject("Smith factor does not divide D");
      if (prod > M.D / s) reject("Smith product overflow/exceeds D");
      prod *= s;
    }
    if (prod != M.D) reject("product of Smith factors != D");
    const Jv& words = j_get(G, "unit_generator_words", 'a');
    if (words.a.size() != M.units.size()) reject("unit word count differs from derived generators");
    for (size_t j = 0; j < words.a.size(); ++j) {
      auto word = j_uvec(words.a[j], "unit generator word");
      if (word.size() != M.smith.size()) reject("unit word rank differs from Smith rank");
      for (size_t l = 0; l < word.size(); ++l) {
        if (word[l] >= M.smith[l]) reject("unit word residue out of range");
        if ((M.units[j].order * word[l]) % M.smith[l])
          reject("unit generator order relation failed");
      }
      M.unit_words.push_back(std::move(word));
    }
  }
  {
    const Jv& R = j_get(root, "generator_rescaling", 'o');
    j_keys(R, {"rule", "shifts"}, "generator_rescaling");
    if (j_get(R, "rule", 's').s != kRescalingIdentity) reject("unknown rescaling rule");
    M.shifts = j_uvec(j_get(R, "shifts", 'a'), "rescaling shifts");
    if (M.shifts.size() != M.deg.size()) reject("rescaling shifts count != levels");
  }
  {
    const Jv& E = j_get(root, "entry_rule", 'o');
    j_keys(E, {"name", "representatives"}, "entry_rule");
    if (j_get(E, "name", 's').s != kEntryIdentity) reject("unknown entry rule");

    if (j_uvec(j_get(E, "representatives", 'a'), "representatives") != M.representatives)
      reject("entry rule representatives mismatch");
  }
  // elements, canonical order
  const Jv& E = j_get(root, "elements", 'a');
  int t = M.t();
  size_t naxes = M.smith.size();
  size_t pos = 0;
  auto add = [&](const Jv& o, const string& kind, Int64 level, Int64 axis, Int64 k,
                 Int64 position) {
    auto e = std::make_unique<Elem>();
    e->kind = kind;
    e->level = level;
    e->axis = axis;
    e->k = k;
    e->position = position;
    // key set by kind
    std::set<string> want = {"kind", "levels", "scale", "max_bits"};
    if (kind == "rep")
      want.insert("position");
    else
      want.insert("level");
    if (kind == "act" || kind == "actodd") want.insert("axis");
    if (kind == "fcoef" || kind == "actodd") want.insert("k");
    for (auto& kv : o.o)
      if (!want.count(kv.first))
        reject("element: unknown/inapplicable key " + kv.first + " for kind " + kind);
    for (auto& w : want)
      if (!o.get(w)) reject("element: missing key " + w + " for kind " + kind);
    e->nfield = kind == "rep" ? t : (int)level;
    auto lv = j_uvec(j_get(o, "levels", 'a'), "levels");
    for (size_t i = 0; i < lv.size(); i++) {
      if ((Int64)lv[i] >= e->nfield) reject("level set outside the element's field");
      if (i && lv[i] <= lv[i - 1]) reject("level set not increasing");
      e->levels.push_back((int)lv[i]);
    }
    e->scale_s = j_get(o, "scale", 's').s;
    if (!is_bigdec(e->scale_s) || e->scale_s.size() > 4095) reject("invalid scale string");
    fmpz_set_str(e->scale, e->scale_s.c_str(), 10);
    if (fmpz_sgn(e->scale) <= 0) reject("scale must be positive");
    e->max_bits = to_u64(j_get(o, "max_bits", 's').s, "max_bits");
    if (e->max_bits > (1u << 23)) reject("max_bits > 2^23");
    e->ncoord = 1;
    for (int j : e->levels) {
      e->ncoord *= M.deg[j];
      if (e->ncoord > (1ull << 48)) reject("element too large");
    }
    // Fixed logical subtree rule, spec 4.3 (frozen L14 constant).
    Uint64 W = 1;
    size_t kk = 0;
    for (size_t j = 0; j < e->levels.size(); j++) {
      Uint64 W2 = W * M.deg[e->levels[j]];
      if (W2 <= (1ull << 14)) {
        W = W2;
        kk = j + 1;
      } else
        break;
    }
    (void)kk;
    e->W = W;
    e->nblocks = e->ncoord / W;
    M.idx[ekey(kind, kind == "rep" ? position : level, axis, k)] = (int)M.el.size();
    M.el.push_back(std::move(e));
  };
  auto next = [&]() -> const Jv& {
    if (pos >= E.a.size()) reject("elements: list ends early (missing elements)");
    const Jv& o = E.a[pos];
    if (o.t != 'o') reject("element must be an object");
    return o;
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
  for (int i = 0; i < t; i++) {
    Uint64 p = M.deg[i];
    if (p == 2) {
      if (!peek("wsq", i, -1, -1)) reject("elements: expected wsq at level " + std::to_string(i));
      add(next(), "wsq", i, -1, -1, -1);
      pos++;
    } else {
      for (Uint64 k = 0; k < p; k++) {
        if (!peek("fcoef", i, -1, k)) reject("elements: expected fcoef");
        add(next(), "fcoef", i, -1, k, -1);
        pos++;
      }
    }
    bool has_delta = false;
    if (p != 2 && peek("actdelta", i, -1, -1)) {
      add(next(), "actdelta", i, -1, -1, -1);
      pos++;
      has_delta = true;
    }
    bool any_odd = false;
    for (size_t l = 0; l < naxes; l++) {
      if (p == 2) {
        if (peek("act", i, l, -1)) {
          add(next(), "act", i, l, -1, -1);
          pos++;
        }
      } else if (peek("actodd", i, l, 0)) {
        for (Uint64 k = 0; k < p; k++) {
          if (!peek("actodd", i, l, k)) reject("elements: actodd group incomplete");
          add(next(), "actodd", i, l, k, -1);
          pos++;
        }
        any_odd = true;
      }
    }
    if (p != 2 && has_delta != any_odd)
      reject("elements: actdelta present iff some actodd present violated");
  }
  for (Uint64 r : M.representatives) {
    const Jv& o = next();
    const Jv* kd = o.get("kind");
    const Jv* po = o.get("position");
    if (!kd || kd->s != "rep" || !po || po->s != std::to_string(r))
      reject("elements: expected rep " + std::to_string(r));
    add(o, "rep", -1, -1, -1, (Int64)r);
    pos++;
  }
  if (pos != E.a.size()) reject("elements: extra or out-of-order elements");
}
}  // namespace sic_check::verifier
