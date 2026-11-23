// SPDX-License-Identifier: AGPL-3.0-or-later
// sic-certify-stabilizer: certify the elements of a sic-stabilizers/1.0 file (sic-stabilizer)
// against the high-precision Arb ball enclosures of each ray (FLINT/Arb).
//   usage: sic-certify-stabilizer STABILIZER_FILE [--catalogue JSON|TSV] [--repo DIR] [--prec BITS]
//                                 [--threshold T] [--d2-exact SICRAD]
// For every non-identity element U = D_chi U_F it encloses, for every unit vector phi inside the
// ray's normalized-gauged.balls, the quantity 1 - |<phi|U phi>|^2 / <phi|phi>^2 with
// outward-rounded ball arithmetic (U_F by Appleby's formula as a direct O(d^2) sum, no FFT), and
// reports an upper bound on its absolute value; an element passes when the bound is below
// --threshold (default 2^-100). This is a certified numerical bound, not an exact identity. With
// --d2-exact the d = 2 ray is also checked exactly (FLINT qqbar) from the radical artifact
// data/d2/d2.sicrad: U psi is proportional to psi for psi = (sqrt A, (1 + i) sqrt B).
// Exit: 0 all elements pass, 1 some element fails, 2 usage/input error.

#include <flint/acb.h>
#include <flint/arb.h>
#include <flint/fmpq.h>
#include <flint/qqbar.h>
#include <flint/ulong_extras.h>

#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "json.hpp"

// Standalone checker arithmetic. This right-factor decomposition is independent
// of the production search/application's left-factor decomposition.
namespace sic_check::stabilizer {
// Independent declarations from the text format specification; no production headers.
constexpr char kVectorBallsIdentity[] = "sic-vector-balls/1.0";
constexpr char kExactD2RadicalsIdentity[] = "sic-exact-d2-radicals/1.0";
constexpr char kStabilizersIdentity[] = "sic-stabilizers/1.0";
using Int = std::int64_t;
struct Mat2 {
  Int a = 1, b = 0, c = 0, e = 1;
  bool operator==(const Mat2&) const = default;
};
Int mod(Int x, Int m) { return (x % m + m) % m; }
Int mulmod(Int x, Int y, Int m) { return (mod(x, m) * mod(y, m)) % m; }
Int dbar_of(Int d) { return d + (d % 2 == 0 ? d : 0); }
Mat2 reduce(Mat2 f, Int n) { return {mod(f.a, n), mod(f.b, n), mod(f.c, n), mod(f.e, n)}; }
Int inverse_mod(Int a, Int n) {
  const auto r = static_cast<ulong>(mod(a, n));
  if (n_gcd(r, static_cast<ulong>(n)) != 1)
    throw std::runtime_error("noninvertible Clifford entry");
  return static_cast<Int>(n_invmod(r, static_cast<ulong>(n)));
}
struct Factorization {
  bool conjugate = false;
  std::vector<Mat2> factors;
};
Factorization factorize(Mat2 matrix, Int dimension) {
  const Int n = dbar_of(dimension);
  Mat2 g = reduce(matrix, n);
  const Int determinant = mod(g.a * g.e - g.b * g.c, n);
  Factorization result;
  if (determinant != 1) {
    if (determinant != n - 1) throw std::runtime_error("invalid Clifford determinant");
    result.conjugate = true;
    g.b = mod(-g.b, n);
    g.e = mod(-g.e, n);
  }
  if (n_gcd(static_cast<ulong>(g.b), static_cast<ulong>(n)) == 1) {
    result.factors.push_back(g);
  } else {
    // H = [0,-1;1,x], A = g H. Then g = A H^-1; both
    // upper-right entries are units when -a + b*x is a unit.
    Int x = 0;
    while (x < n && n_gcd(static_cast<ulong>(mod(-g.a + g.b * x, n)), static_cast<ulong>(n)) != 1)
      ++x;
    if (x == n) throw std::runtime_error("Clifford right decomposition failed");
    result.factors.push_back({x, 1, n - 1, 0});
    result.factors.push_back({g.b, mod(-g.a + g.b * x, n), g.e, mod(-g.c + g.e * x, n)});
  }
  return result;
}
struct CatalogueEntry {
  Int d;
  std::string label, order, status, csv, json;
};

Int unsigned_decimal(const std::string& text, Int maximum, const std::string& where) {
  Int value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || (text.size() > 1 && text.front() == '0') || text.front() < '0' ||
      text.front() > '9' || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
      value < 0 || value > maximum)
    throw std::runtime_error(where + ": invalid unsigned decimal");
  return value;
}

// JSON paths are relative to the catalogue's directory; explicit TSV paths
// retain their --repo-relative interpretation.
// A ray is usable only when both its CSV and its high-precision JSON are listed.
std::vector<CatalogueEntry> read_release_catalogue(const std::string& path) {
  const auto document = read_json(path);
  const auto data_root = std::filesystem::absolute(path).parent_path();
  if (document.at("schema").string() != "sic-release-catalogue/1.0")
    throw std::runtime_error(path + ": unsupported catalogue schema");
  std::vector<CatalogueEntry> rays;
  std::set<Int> dimensions;
  std::set<std::string> multiplets, ids;
  for (const auto& dimension : document.at("dimensions").array()) {
    const auto& ds = dimension.at("d").string();
    const Int d = unsigned_decimal(ds, Int{1} << 24, path);
    if (d < 2 || !dimensions.insert(d).second)
      throw std::runtime_error(path + ": invalid or duplicate dimension");
    for (const auto& multiplet : dimension.at("multiplets").array()) {
      const auto& mid = multiplet.at("id").string();
      const std::string prefix = "d" + ds + "-";
      if (!mid.starts_with(prefix + "m") ||
          unsigned_decimal(mid.substr(prefix.size() + 1), 999999999999999999LL, path) == 0 ||
          !multiplets.insert(mid).second)
        throw std::runtime_error(path + ": invalid or duplicate multiplet id");
      for (const auto& ray : multiplet.at("rays").array()) {
        const auto& id = ray.at("id").string();
        const auto& label = ray.at("label").string();
        if (!label.starts_with('c')) throw std::runtime_error(path + ": invalid ray label");
        unsigned_decimal(label.substr(1), 999999999999999999LL, path);
        ray.at("sicx_ray_label").string();
        if (id != mid + "-" + label || !ids.insert(id).second)
          throw std::runtime_error(path + ": invalid or duplicate ray id");
        const std::string stem = "d" + ds + "/" + mid.substr(prefix.size()) + "/" + label;
        CatalogueEntry entry{d, id, "-", "-", {}, {}};
        for (const auto& file : ray.at("files").array()) {
          const auto& name = file.at("path").string();
          const auto extension = std::filesystem::path(name).extension();
          if (extension != ".csv" && extension != ".json") continue;
          if (name != stem + extension.string())
            throw std::runtime_error(path + ": ray file path differs from its id");
          auto& destination = extension == ".csv" ? entry.csv : entry.json;
          if (!destination.empty()) throw std::runtime_error(path + ": duplicate ray file");
          destination = (data_root / name).string();
        }
        if (entry.csv.empty() || entry.json.empty())
          throw std::runtime_error(path + ": catalogue ray lacks listed CSV or JSON");
        rays.push_back(std::move(entry));
      }
    }
  }
  return rays;
}

std::vector<CatalogueEntry> read_catalogue(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot read catalogue " + path);
  input >> std::ws;
  if (input.peek() == '{') return read_release_catalogue(path);
  std::vector<CatalogueEntry> rays;
  for (std::string row; std::getline(input, row);) {
    if (row.empty() || row.front() == '#') continue;
    CatalogueEntry ray;
    std::string tail;
    std::istringstream fields(row);
    if (!(fields >> ray.d >> ray.label >> ray.order >> ray.status >> ray.csv) || (fields >> tail) ||
        ray.d < 2 || ray.d > (Int{1} << 24))
      throw std::runtime_error("invalid catalogue row: " + row);
    rays.push_back(ray);
  }
  return rays;
}
}  // namespace sic_check::stabilizer
namespace stabilizer = sic_check::stabilizer;
using sic_check::stabilizer::Int;

namespace sic_check::stabilizer {
namespace {

struct Element {
  int index = 0;
  Int sign = 1;
  stabilizer::Mat2 F;
  Int chi1 = 0, chi2 = 0;
};

// RAII vector of acb_t.
class AcbVec {
 public:
  explicit AcbVec(Int n) : n_(n), v_(_acb_vec_init(n)) {}
  ~AcbVec() { _acb_vec_clear(v_, n_); }
  AcbVec(const AcbVec&) = delete;
  AcbVec& operator=(const AcbVec&) = delete;
  acb_ptr get() { return v_; }
  acb_ptr operator[](Int i) { return v_ + i; }
  Int size() const { return n_; }

 private:
  Int n_;
  acb_ptr v_;
};

class ArbVar {
 public:
  ArbVar() { arb_init(x_); }
  ~ArbVar() { arb_clear(x_); }
  ArbVar(const ArbVar&) = delete;
  ArbVar& operator=(const ArbVar&) = delete;
  arb_ptr get() { return x_; }

 private:
  arb_t x_;
};

class AcbVar {
 public:
  AcbVar() { acb_init(x_); }
  ~AcbVar() { acb_clear(x_); }
  AcbVar(const AcbVar&) = delete;
  AcbVar& operator=(const AcbVar&) = delete;
  acb_ptr get() { return x_; }

 private:
  acb_t x_;
};

void load_ball(arb_ptr x, const std::string& dump, const std::string& where) {
  if (arb_load_str(x, dump.c_str()) != 0 || !arb_is_finite(x))
    throw std::runtime_error(where + ": bad Arb dump");
}

// sic-vector-balls/1.0 / d N / N lines "re_dump | im_dump" / end
void read_balls(const std::string& path, Int d, AcbVec& out) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path);
  std::string line;
  if (!std::getline(in, line) || line != kVectorBallsIdentity)
    throw std::runtime_error(path + ": bad header");
  if (!std::getline(in, line) || line != "d " + std::to_string(d))
    throw std::runtime_error(path + ": dimension mismatch");
  for (Int k = 0; k < d; ++k) {
    if (!std::getline(in, line)) throw std::runtime_error(path + ": truncated");
    const auto bar = line.find(" | ");
    if (bar == std::string::npos) throw std::runtime_error(path + ": bad row");
    load_ball(acb_realref(out[k]), line.substr(0, bar), path);
    load_ball(acb_imagref(out[k]), line.substr(bar + 3), path);
  }
  if (!std::getline(in, line) || line != "end") throw std::runtime_error(path + ": missing end");
  if (std::getline(in, line)) throw std::runtime_error(path + ": trailing data");
}

// Exact fixed-point decimal conversion, independent of the release writer. The
// rational midpoint is enclosed at the requested precision; arb_add_error then
// adds an upper bound for the rational radius, including conversion roundoff.
class DecimalBalls {
 public:
  DecimalBalls(Int places, Int accuracy, slong prec) : places_(places), prec_(prec) {
    fmpz_init(scale_);
    fmpz_init(integer_);
    fmpq_init(value_);
    fmpq_init(radius_limit_);
    fmpz_ui_pow_ui(scale_, 10, static_cast<ulong>(places));
    fmpq_one(radius_limit_);
    fmpq_div_2exp(radius_limit_, radius_limit_, static_cast<ulong>(accuracy));
  }
  ~DecimalBalls() {
    fmpq_clear(radius_limit_);
    fmpq_clear(value_);
    fmpz_clear(integer_);
    fmpz_clear(scale_);
  }
  DecimalBalls(const DecimalBalls&) = delete;
  DecimalBalls& operator=(const DecimalBalls&) = delete;

  void read(arb_ptr out, const std::string& midpoint, const std::string& radius,
            const std::string& where) {
    parse(midpoint, where);
    arb_set_fmpq(out, value_, prec_);
    parse(radius, where);
    if (fmpq_sgn(value_) < 0 || fmpq_cmp(value_, radius_limit_) >= 0)
      throw std::runtime_error(where + ": invalid hp radius or accuracy claim");
    ArbVar error;
    arb_set_fmpq(error.get(), value_, prec_);
    arb_add_error(out, error.get());
    if (!arb_is_finite(out)) throw std::runtime_error(where + ": nonfinite hp enclosure");
  }

 private:
  void parse(const std::string& text, const std::string& where) {
    const auto dot = text.find('.');
    const auto start = !text.empty() && text.front() == '-' ? 1u : 0u;
    if (dot == std::string::npos || dot <= start || text.size() - dot - 1 != places_)
      throw std::runtime_error(where + ": hp value has wrong fixed-point format");
    for (std::size_t k = start; k < text.size(); ++k)
      if (k != dot && (text[k] < '0' || text[k] > '9'))
        throw std::runtime_error(where + ": invalid hp decimal");
    std::string digits = text;
    digits.erase(dot, 1);
    if (fmpz_set_str(integer_, digits.c_str(), 10) != 0)
      throw std::runtime_error(where + ": invalid hp integer");
    fmpq_set_fmpz_frac(value_, integer_, scale_);
  }
  Int places_;
  slong prec_;
  fmpz_t scale_, integer_;
  fmpq_t value_, radius_limit_;
};

void read_json_balls(const std::string& path, const CatalogueEntry& entry, slong prec,
                     AcbVec& out) {
  const auto document = read_json(path);
  if (document.at("schema").string() != "sic-fiducial-json/1.0" ||
      !document.at("required_features").array().empty())
    throw std::runtime_error(path + ": unsupported ray schema or required features");
  if (unsigned_decimal(document.at("d").string(), Int{1} << 24, path) != entry.d ||
      document.at("id").string() != entry.label)
    throw std::runtime_error(path + ": ray identity differs from catalogue");
  const auto& hp = document.at("hp");
  // Explicit reader limits bound rational conversion independently of --prec.
  const auto places = unsigned_decimal(hp.at("decimal_places").string(), 1000000, path);
  const auto accuracy = unsigned_decimal(hp.at("accuracy_bits").string(), 1000000, path);
  if (places == 0) throw std::runtime_error(path + ": hp requires fractional digits");
  const auto& values = hp.at("values").array();
  if (values.size() != static_cast<std::size_t>(entry.d))
    throw std::runtime_error(path + ": hp coordinate count differs from dimension");
  DecimalBalls reader(places, accuracy, prec);
  for (Int k = 0; k < entry.d; ++k) {
    const auto& parts = values[k].array();
    if (parts.size() != 4) throw std::runtime_error(path + ": hp coordinate needs four parts");
    reader.read(acb_realref(out[k]), parts[0].string(), parts[1].string(), path);
    reader.read(acb_imagref(out[k]), parts[2].string(), parts[3].string(), path);
  }
}

// Ball arithmetic for U = D_chi U_F.
class BallClifford {
 public:
  BallClifford(Int d, slong prec) : d_(d), prec_(prec), roots_(2 * d) {
    fmpq_t q;
    fmpq_init(q);
    for (Int m = 0; m < 2 * d; ++m) {
      fmpq_set_si(q, m, static_cast<ulong>(d));  // exp(pi i m/d)
      arb_sin_cos_pi_fmpq(acb_imagref(roots_[m]), acb_realref(roots_[m]), q, prec);
    }
    fmpq_clear(q);
  }
  // exp(pi i m / d)
  acb_srcptr root(Int m) { return roots_[stabilizer::mod(m, 2 * d_)]; }
  acb_srcptr tau(Int k) {
    return root(stabilizer::mulmod(stabilizer::mod(k, 2 * d_), d_ + 1, 2 * d_));
  }

  void apply(const Element& el, AcbVec& in, AcbVec& out) {
    const Int db = stabilizer::dbar_of(d_);
    const stabilizer::Factorization fz = stabilizer::factorize(el.F, d_);
    AcbVec w(d_), next(d_);
    for (Int k = 0; k < d_; ++k) {
      acb_set(w[k], in[k]);
      if (fz.conjugate) acb_conj(w[k], w[k]);
    }
    ArbVar scale;
    arb_set_si(scale.get(), d_);
    arb_rsqrt(scale.get(), scale.get(), prec_);
    AcbVar t;
    for (const stabilizer::Mat2& g : fz.factors) {
      // next_u = d^(-1/2) sum_v tau^(b'(a v^2 - 2 u v + e u^2)) w_v
      const Int bi = stabilizer::inverse_mod(g.b, db), m = 2 * d_;
      const Int ka = stabilizer::mulmod(bi, g.a, m), ke = stabilizer::mulmod(bi, g.e, m);
      for (Int v = 0; v < d_; ++v)
        acb_mul(w[v], w[v], tau(stabilizer::mulmod(ka, stabilizer::mulmod(v, v, m), m)), prec_);
      for (Int u = 0; u < d_; ++u) {
        acb_zero(next[u]);
        for (Int v = 0; v < d_; ++v)
          acb_addmul(next[u], w[v],
                     tau(stabilizer::mulmod(-2 * bi, stabilizer::mulmod(u, v, m), m)), prec_);
        acb_mul(next[u], next[u], tau(stabilizer::mulmod(ke, stabilizer::mulmod(u, u, m), m)),
                prec_);
        acb_mul_arb(next[u], next[u], scale.get(), prec_);
      }
      _acb_vec_swap(w.get(), next.get(), d_);
    }
    // (D_chi w)_k = tau^(-chi1 chi2) omega^(chi2 k) w_(k - chi1)
    for (Int k = 0; k < d_; ++k) {
      acb_mul(out[k], w[stabilizer::mod(k - el.chi1, d_)],
              root(2 * stabilizer::mulmod(el.chi2, k, d_)), prec_);
      acb_mul(out[k], out[k], tau(-stabilizer::mulmod(el.chi1, el.chi2, 2 * d_)), prec_);
    }
  }

 private:
  Int d_;
  slong prec_;
  AcbVec roots_;
};

// Exact check for d = 2 from the radical artifact.
bool exact_d2(const std::string& sicrad, const std::vector<Element>& elems) {
  std::ifstream in(sicrad);
  if (!in) throw std::runtime_error("cannot open " + sicrad);
  std::string line, key;
  std::vector<std::string> lines;
  while (std::getline(in, line)) lines.push_back(line);
  if (lines.size() != 6 || lines[0] != kExactD2RadicalsIdentity || lines[1] != "dimension 2" ||
      lines[2] != "radicand 3" || lines[5] != "end")
    throw std::runtime_error(sicrad + ": unexpected layout");
  qqbar_t sqrt3, A, B, tmp, alpha, beta, one_i;
  qqbar_init(sqrt3);
  qqbar_init(A);
  qqbar_init(B);
  qqbar_init(tmp);
  qqbar_init(alpha);
  qqbar_init(beta);
  qqbar_init(one_i);
  qqbar_set_ui(sqrt3, 3);
  qqbar_sqrt(sqrt3, sqrt3);
  auto parse = [&](const std::string& l, const char* name, qqbar_t out) {
    std::istringstream ls(l);
    std::string k, a, b, extra;
    if (!(ls >> k >> a >> b) || (ls >> extra) || k != name)
      throw std::runtime_error(sicrad + ": bad record " + l);
    fmpq_t qa, qb;
    fmpq_init(qa);
    fmpq_init(qb);
    if (fmpq_set_str(qa, a.c_str(), 10) != 0 || fmpq_set_str(qb, b.c_str(), 10) != 0)
      throw std::runtime_error(sicrad + ": bad rational");
    qqbar_set_fmpq(out, qa);
    qqbar_set_fmpq(tmp, qb);
    qqbar_mul(tmp, tmp, sqrt3);
    qqbar_add(out, out, tmp);
    fmpq_clear(qa);
    fmpq_clear(qb);
  };
  parse(lines[3], "alpha_squared", A);
  parse(lines[4], "beta_squared", B);
  qqbar_sqrt(alpha, A);
  qqbar_sqrt(beta, B);
  qqbar_i(one_i);
  qqbar_add_ui(one_i, one_i, 1);
  const Int d = 2, m = 4;
  std::vector<qqbar_struct> psi(2), w(2), next(2), out(2);
  for (auto* v : {&psi, &w, &next, &out})
    for (auto& z : *v) qqbar_init(&z);
  qqbar_set(&psi[0], alpha);
  qqbar_mul(&psi[1], one_i, beta);
  auto tau = [&](qqbar_t res, Int k) {  // tau^k = exp(pi i k (d+1)/d) = exp(2 pi i k(d+1) / (2d))
    qqbar_root_of_unity(res,
                        static_cast<slong>(stabilizer::mulmod(stabilizer::mod(k, m), d + 1, m)),
                        static_cast<ulong>(m));
  };
  qqbar_t scale, t;
  qqbar_init(scale);
  qqbar_init(t);
  qqbar_set_ui(scale, 2);
  qqbar_sqrt(scale, scale);
  qqbar_inv(scale, scale);
  bool all = true;
  for (const Element& el : elems) {
    const stabilizer::Factorization fz = stabilizer::factorize(el.F, d);
    for (Int k = 0; k < d; ++k) {
      qqbar_set(&w[k], &psi[k]);
      if (fz.conjugate) qqbar_conj(&w[k], &w[k]);
    }
    for (const stabilizer::Mat2& g : fz.factors) {
      const Int bi = stabilizer::inverse_mod(g.b, m);
      for (Int u = 0; u < d; ++u) {
        qqbar_zero(&next[u]);
        for (Int v = 0; v < d; ++v) {
          tau(t, bi * (g.a * v * v - 2 * u * v + g.e * u * u));
          qqbar_mul(t, t, &w[v]);
          qqbar_add(&next[u], &next[u], t);
        }
        qqbar_mul(&next[u], &next[u], scale);
      }
      for (Int k = 0; k < d; ++k) qqbar_set(&w[k], &next[k]);
    }
    for (Int k = 0; k < d; ++k) {
      tau(t, -el.chi1 * el.chi2 + 2 * el.chi2 * k);  // tau^(-chi1 chi2) omega^(chi2 k)
      qqbar_mul(&out[k], t, &w[stabilizer::mod(k - el.chi1, d)]);
    }
    // U psi proportional to psi: out_0 psi_1 - out_1 psi_0 = 0, and U psi != 0.
    qqbar_mul(t, &out[0], &psi[1]);
    qqbar_mul(tmp, &out[1], &psi[0]);
    qqbar_sub(t, t, tmp);
    const bool ok = qqbar_is_zero(t) && !(qqbar_is_zero(&out[0]) && qqbar_is_zero(&out[1]));
    std::printf("exact d2 %d det %+lld: U psi %s psi (qqbar)\n", el.index,
                static_cast<long long>(el.sign),
                ok ? "is proportional to" : "is NOT proportional to");
    all = all && ok;
  }
  for (auto* v : {&psi, &w, &next, &out})
    for (auto& z : *v) qqbar_clear(&z);
  qqbar_clear(sqrt3);
  qqbar_clear(A);
  qqbar_clear(B);
  qqbar_clear(tmp);
  qqbar_clear(alpha);
  qqbar_clear(beta);
  qqbar_clear(one_i);
  qqbar_clear(scale);
  qqbar_clear(t);
  return all;
}

}  // namespace

}  // namespace sic_check::stabilizer

namespace sic_check::stabilizer {
int certify(const std::string& stab_path, const std::string& root, const std::string& catalogue,
            const std::string& d2_exact, slong prec, double threshold, long dimension) {
  std::map<std::string, CatalogueEntry> by_label;
  for (const auto& entry : read_catalogue(catalogue)) {
    if (dimension && entry.d != dimension) continue;
    if (!by_label.emplace(entry.label, entry).second)
      throw std::runtime_error("duplicate catalogue ray " + entry.label);
  }
  if (by_label.empty()) throw std::runtime_error("catalogue has no rays for the requested table");
  std::map<std::string, std::vector<Element>> elems;
  std::vector<std::string> order;
  std::ifstream in(stab_path);
  if (!in) throw std::runtime_error("cannot open " + stab_path);
  std::string line;
  if (!std::getline(in, line) || line != std::string("# ") + kStabilizersIdentity)
    throw std::runtime_error(stab_path + ": bad header");
  std::string current;
  size_t declared = 0, declared_unitary = 0, unitary = 0;
  bool footer = false;
  const auto complete_ray = [&] {
    if (!current.empty() && (elems.at(current).size() != declared || unitary != declared_unitary))
      throw std::runtime_error(stab_path + ": declared element counts differ for " + current);
  };
  while (std::getline(in, line)) {
    if (line.empty() || line.starts_with('#')) continue;
    std::istringstream ls(line);
    std::string tag, label, kdet, kf, kchi;
    ls >> tag;
    if (footer) throw std::runtime_error(stab_path + ": record after footer");
    if (tag == "end") {
      complete_ray();
      std::string count, tail;
      if (!(ls >> count) || (ls >> tail) ||
          static_cast<size_t>(unsigned_decimal(count, INT64_MAX, stab_path)) != order.size())
        throw std::runtime_error(stab_path + ": invalid footer count");
      footer = true;
      continue;
    }
    if (tag == "ray") {
      complete_ray();
      std::string kd, d, ko, o, ks, status, kg, group_count, ku, unitary_count;
      if (!(ls >> label >> kd >> d >> ko >> o >> ks >> status >> kg >> group_count >> ku >>
            unitary_count) ||
          kd != "d" || ko != "order" || ks != "status" || kg != "group_order" ||
          ku != "unitary_order" || !by_label.contains(label) ||
          unsigned_decimal(d, 1L << 24, stab_path) != by_label.at(label).d ||
          !elems.try_emplace(label).second)
        throw std::runtime_error(stab_path + ": invalid or duplicate ray declaration: " + line);
      declared = unsigned_decimal(group_count, INT_MAX, stab_path);
      declared_unitary = unsigned_decimal(unitary_count, INT_MAX, stab_path);
      if (!declared_unitary || declared_unitary > declared)
        throw std::runtime_error(stab_path + ": invalid declared element counts");
      current = label;
      order.push_back(label);
      unitary = 0;
      continue;
    }
    if (tag != "elem" || current.empty())
      throw std::runtime_error(stab_path + ": unexpected table record: " + line);
    Element el;
    long long s = 0, a = 0, b = 0, c = 0, e = 0, x = 0, y = 0;
    if (!(ls >> label >> el.index >> kdet >> s >> kf >> a >> b >> c >> e >> kchi >> x >> y) ||
        kdet != "det" || kf != "F" || kchi != "chi" || label != current ||
        elems.at(label).size() >= declared || el.index != static_cast<int>(elems.at(label).size()))
      throw std::runtime_error(stab_path + ": bad element line: " + line);
    el.sign = s;
    el.F = {a, b, c, e};
    el.chi1 = x;
    el.chi2 = y;
    unitary += el.sign == 1;
    elems.at(label).push_back(el);
  }
  if (in.bad() || !footer) throw std::runtime_error(stab_path + ": missing or unreadable footer");
  if (order.empty())
    throw std::runtime_error(stab_path + ": table does not cover the catalogue rays");
  if (!dimension) dimension = by_label.at(order.front()).d;
  const auto expected =
      std::count_if(by_label.begin(), by_label.end(),
                    [dimension](const auto& ray) { return ray.second.d == dimension; });
  if (order.size() != static_cast<size_t>(expected) ||
      std::any_of(order.begin(), order.end(),
                  [&](const auto& label) { return by_label.at(label).d != dimension; }))
    throw std::runtime_error(stab_path + ": table does not cover the catalogue rays");
  bool all = true;
  double worst = 0;
  std::size_t checked = 0;
  for (const auto& label : order) {
    const auto it = by_label.find(label);
    if (it == by_label.end()) throw std::runtime_error("ray " + label + " is not in the catalogue");
    const Int d = it->second.d;
    const Int modulus = stabilizer::dbar_of(d);
    std::set<std::tuple<Int, Int, Int, Int, Int, Int, Int>> seen;
    for (auto& element : elems[label]) {
      element.F = stabilizer::reduce(element.F, modulus);
      const auto& f = element.F;
      if ((element.sign != 1 && element.sign != -1) ||
          stabilizer::mod(f.a * f.e - f.b * f.c, modulus) != stabilizer::mod(element.sign, modulus))
        throw std::runtime_error("element determinant differs from its sign");
      element.chi1 = stabilizer::mod(element.chi1, 2 * d);
      element.chi2 = stabilizer::mod(element.chi2, 2 * d);
      if (!seen.emplace(element.sign, f.a, f.b, f.c, f.e, element.chi1, element.chi2).second)
        throw std::runtime_error("duplicate stabilizer element");
    }
    if (!seen.contains({1, 1, 0, 0, 1, 0, 0}))
      throw std::runtime_error("stabilizer ray lacks the identity");
    AcbVec phi(d), u(d);
    if (it->second.json.empty()) {
      const auto balls =
          (std::filesystem::path(root) / it->second.csv).parent_path() / "normalized-gauged.balls";
      read_balls(balls.string(), d, phi);
    } else {
      read_json_balls((std::filesystem::path(root) / it->second.json).string(), it->second, prec,
                      phi);
    }
    BallClifford bc(d, prec);
    ArbVar n2, val, tmp;
    AcbVar s;
    arb_zero(n2.get());
    for (Int k = 0; k < d; ++k) {
      acb_abs(tmp.get(), phi[k], prec);
      arb_addmul(n2.get(), tmp.get(), tmp.get(), prec);
    }
    for (const Element& el : elems[label]) {
      if (el.sign == 1 && stabilizer::reduce(el.F, modulus) == stabilizer::Mat2{1, 0, 0, 1} &&
          el.chi1 == 0 && el.chi2 == 0)
        continue;
      bc.apply(el, phi, u);
      acb_zero(s.get());
      AcbVar c;
      for (Int k = 0; k < d; ++k) {
        acb_conj(c.get(), phi[k]);
        acb_addmul(s.get(), c.get(), u[k], prec);
      }
      acb_abs(val.get(), s.get(), prec);
      arb_sqr(val.get(), val.get(), prec);
      arb_sqr(tmp.get(), n2.get(), prec);
      arb_div(val.get(), val.get(), tmp.get(), prec);
      arb_sub_ui(val.get(), val.get(), 1, prec);
      arf_t ub;
      arf_init(ub);
      arb_get_abs_ubound_arf(ub, val.get(), 64);
      const double bound = arf_get_d(ub, ARF_RND_UP);
      arf_clear(ub);
      const bool ok = std::isfinite(bound) && bound < threshold;
      worst = std::max(worst, bound);
      ++checked;
      all = all && ok;
      std::printf("cert %s %d det %+lld |1 - |<phi|U phi>|^2| <= %.3e %s\n", label.c_str(),
                  el.index, static_cast<long long>(el.sign), bound, ok ? "PASS" : "FAIL");
    }
    if (!d2_exact.empty() && d == 2) all = exact_d2(d2_exact, elems[label]) && all;
  }
  std::printf(
      "certified %zu non-identity elements, max bound %.3e, threshold %.3e, precision %ld bits: "
      "%s\n",
      checked, worst, threshold, static_cast<long>(prec), all ? "PASS" : "FAIL");
  return all ? 0 : 1;
}
}  // namespace sic_check::stabilizer
