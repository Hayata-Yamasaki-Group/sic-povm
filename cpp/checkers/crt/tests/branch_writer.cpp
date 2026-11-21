// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_writer.hpp"
namespace fixture {

std::string sqrt2_box(U p) { return sqrt_box(2, p); }

std::string root2_box(U n, U p) {
  Big N, q, t;
  fmpz_one(N.z);
  fmpz_mul_2exp(N.z, N.z, n * p + 1);
  fmpz_root(q.z, N.z, n);
  fmpz_mul_2exp(t.z, q.z, 1);
  fmpz_add_ui(t.z, t.z, 1);
  fmpz_pow_ui(t.z, t.z, n);
  fmpz_mul_2exp(N.z, N.z, n);
  if (fmpz_cmp(N.z, t.z) > 0) fmpz_add_ui(q.z, q.z, 1);
  U trailing = fmpz_val2(q.z);
  fmpz_fdiv_q_2exp(q.z, q.z, trailing);
  fmpz_set_si(t.z, (int64_t)trailing - (int64_t)p);
  auto result = hex(q.z) + " " + hex(t.z) + " 1 ";
  fmpz_set_si(t.z, -(int64_t)p);
  return result + hex(t.z);
}

File odd_quadratic(bool imaginary, bool singleton, bool negative) {
  File f = odd();
  f.leaves.resize(3);
  std::vector<std::string> descriptors;
  for (U k = 0; k < 3; ++k)
    descriptors.push_back(descriptor("fcoef", 0, {}, k == 0 ? "2" : "1", k == 0 ? 2 : 0, -1, k));
  std::vector<U> degrees{3}, shifts{0}, active;
  std::vector<std::string> branches{arr({str(cbrt2_box()), str("0 0 0 0")})};
  U N = 3;
  for (U j = 1; j <= 3; ++j) {
    active.push_back(j - 1);
    bool last = j == 3;
    U scale = last && singleton ? N : 2 * N;
    auto levels = last && singleton ? std::vector<U>{} : active;
    U bits = 0;
    for (U n = scale; n; n >>= 1) ++bits;
    descriptors.push_back(descriptor("wsq", j, levels, std::to_string(scale), bits));
    L l;
    l.kind = "TOWR";
    l.element = descriptors.size() - 1;
    l.count = last && singleton ? 1 : N;
    l.bytes = leaf(l.element, 0, l.count, {{int64_t(l.count - 1), last && imaginary ? -1 : 1, 0}});
    f.leaves.push_back(l);
    degrees.push_back(2);
    shifts.push_back(0);
    auto component = last && singleton ? std::string("1 0 0 0") : root2_box(2 * N);
    if (last && negative) component = "-" + component;
    branches.push_back(last && imaginary ? arr({str("0 0 0 0"), str(component)})
                                         : arr({str(component), str("0 0 0 0")}));
    N *= 2;
  }
  active.push_back(3);
  for (U position : {0, 1, 2}) {
    bool full = position == 1;
    U id = descriptors.size();
    descriptors.push_back(descriptor("rep", position, full ? active : std::vector<U>{}, "1", 1));
    L l;
    l.kind = "BODY";
    l.element = id;
    l.count = full ? N : 1;
    l.bytes = leaf(id, 0, l.count, {{int64_t(l.count - 1), 1, 0}});
    f.leaves.push_back(l);
  }
  auto begin = f.meta.find("\"elements\":") + 11;
  auto end = f.meta.find(",\"entry_rule\":", begin);
  f.meta.replace(begin, end - begin, arr(descriptors));
  replace(f.meta, "\"smith\":[\"3\"]", "\"smith\":[\"24\"]");
  replace(f.meta, "\"shifts\":[\"0\"]", "\"shifts\":" + nums(shifts));
  replace(f.meta, "\"degrees\":[\"3\"]", "\"degrees\":" + nums(degrees));
  begin = f.meta.find("\"branches\":") + 11;
  end = f.meta.find(",\"degrees\":", begin);
  f.meta.replace(begin, end - begin, arr(branches));
  f.rebuild_physical(false);
  return f;
}

File odd_quadratic_tie(bool negative, bool wrong) {
  File f = odd();
  f.leaves.clear();
  std::vector<std::string> descriptors;
  Big scale;
  for (U k = 0; k < 3; ++k) {
    fmpz_one(scale.z);
    fmpz_mul_2exp(scale.z, scale.z, 258);
    fmpz_add_ui(scale.z, scale.z, k == 0 ? 1 : 3);
    if (k == 2) fmpz_set_ui(scale.z, 3);
    char* decimal = fmpz_get_str(nullptr, 10, scale.z);
    descriptors.push_back(descriptor("fcoef", 0, {}, decimal, fmpz_bits(scale.z), -1, k));
    flint_free(decimal);
    L l;
    l.kind = "TOWR";
    l.element = k;
    l.bytes = leaf(k, 0, 1, {{0, k == 1 ? 1 : -1, 0}});
    f.leaves.push_back(l);
  }
  descriptors.push_back(descriptor("wsq", 1, {0}, "1", 131));
  L l;
  l.kind = "TOWR";
  l.element = 3;
  l.count = 3;
  for (U value : {3, 0, 3, 3, 2, 1, 0, 2, 3, 34}) var(l.bytes, value);
  l.bytes.insert(l.bytes.end(), 16, 0);
  l.bytes.push_back(4);  // 2^130, magnitude little endian
  integer(l.bytes, 0);
  f.leaves.push_back(l);
  for (U position : {0, 1, 2}) {
    U id = descriptors.size();
    descriptors.push_back(descriptor("rep", position, {}, "1", 1));
    L rep;
    rep.kind = "BODY";
    rep.element = id;
    rep.bytes = leaf(id, 0, 1, {{0, 1, 0}});
    f.leaves.push_back(rep);
  }
  auto begin = f.meta.find("\"elements\":") + 11;
  auto end = f.meta.find(",\"entry_rule\":", begin);
  f.meta.replace(begin, end - begin, arr(descriptors));
  replace(f.meta, "\"smith\":[\"3\"]", "\"smith\":[\"6\"]");
  replace(f.meta, "\"shifts\":[\"0\"]", "\"shifts\":[\"0\",\"0\"]");
  replace(f.meta, "\"degrees\":[\"3\"]", "\"degrees\":[\"3\",\"2\"]");
  auto last =
      wrong ? std::string("100000000000000000000000000000001 -80 1 -80") : std::string("1 0 1 -80");
  if (negative) last = "-" + last;
  begin = f.meta.find("\"branches\":") + 11;
  end = f.meta.find(",\"degrees\":", begin);
  f.meta.replace(begin, end - begin,
                 arr({arr({str("1 0 0 0"), str("0 0 0 0")}), arr({str(last), str("0 0 0 0")})}));
  f.rebuild_physical(false);
  return f;
}

File odd_admission() {
  File f = odd();
  f.leaves.clear();
  std::vector<std::string> descriptors;
  for (unsigned k = 0; k < 5; ++k) {
    descriptors.push_back(descriptor("fcoef", 0, {}, "1", 1, -1, k));
    L l;
    l.kind = "TOWR";
    l.element = k;
    l.bytes = leaf(k, 0, 1, {{0, 0, 1}});
    f.leaves.push_back(l);
  }
  for (unsigned position : {0, 1, 2}) {
    unsigned id = descriptors.size();
    descriptors.push_back(descriptor("rep", position, {}, "1", 1));
    L l;
    l.kind = "BODY";
    l.element = id;
    l.bytes = leaf(id, 0, 1, {{0, 1, 0}});
    f.leaves.push_back(l);
  }
  auto begin = f.meta.find("\"elements\":") + 11, end = f.meta.find(",\"entry_rule\":");
  f.meta.replace(begin, end - begin, arr(descriptors));
  auto replace = [&](const std::string& a, const std::string& b) {
    f.meta.replace(f.meta.find(a), a.size(), b);
  };
  replace("\"smith\":[\"3\"]", "\"smith\":[\"5\"]");
  replace("\"degrees\":[\"3\"]", "\"degrees\":[\"5\"]");
  f.rebuild_physical(false);
  return f;
}

File dyadic(bool tiny, unsigned tie, bool negative) {
  File f;
  f.leaves.clear();
  std::vector<std::string> descriptors;
  Big scale, second, q, exp;
  unsigned exponent = tiny ? 400 : 258;
  fmpz_one(scale.z);
  fmpz_mul_2exp(scale.z, scale.z, 2 * exponent);
  descriptors.push_back(descriptor("wsq", 0, {}, decimal(scale.z), 2 * exponent + 1));
  descriptors.push_back(descriptor("act", 0, {}, "1", 1, 0));
  if (tiny)
    fmpz_set_ui(second.z, 2);
  else {
    fmpz_one(second.z);
    fmpz_mul_2exp(second.z, second.z, 129);
    fmpz_add_ui(second.z, second.z, tie);
    fmpz_mul(second.z, second.z, second.z);
    fmpz_mul_2exp(second.z, second.z, 1);
  }
  descriptors.push_back(descriptor("wsq", 1, {0}, decimal(second.z), fmpz_bits(second.z)));
  for (U r : {0, 1, 2}) descriptors.push_back(descriptor("rep", r, {}, "1", 1));
  for (U id = 0; id < descriptors.size(); ++id) {
    L l;
    l.kind = id < 3 ? "TOWR" : "BODY";
    l.element = id;
    l.count = id == 2 ? 2 : 1;
    l.bytes = leaf(id, 0, l.count, {{id == 2 ? 1 : 0, 1, 0}});
    f.leaves.push_back(l);
  }
  fmpz_set_ui(exp.z, exponent);
  std::string first = "1 " + hex(exp.z) + " 0 0", last;
  if (tiny)
    last = negative ? "-1 -c8 0 0" : "1 -c8 0 0";
  else {
    fmpz_one(q.z);
    fmpz_mul_2exp(q.z, q.z, 128);
    fmpz_add_ui(q.z, q.z, tie == 1 ? 0 : 2);
    if (negative) fmpz_neg(q.z, q.z);
    U v = fmpz_val2(q.z);
    fmpz_tdiv_q_2exp(q.z, q.z, v);
    fmpz_set_si(exp.z, (int64_t)v - 128);
    last = hex(q.z) + " " + hex(exp.z) + " 1 -80";
  }
  f.meta = obj(
      {{"d", str("4")},
       {"elements", arr(descriptors)},
       {"entry_rule",
        obj({{"name", str("sicx-divisors/1.0")}, {"representatives", nums({0, 1, 2})}})},
       {"format", str("SICX")},
       {"galois", obj({{"smith", nums({2, 2})}, {"unit_generator_words", arr({nums({0, 0})})}})},
       {"generator_rescaling",
        obj({{"rule", str("sicx-2adic-greedy/1.0")}, {"shifts", nums({0, 0})}})},
       {"required_features", "[]"},
       {"tower", obj({{"branch_bits", str(tiny ? "256" : "128")},
                      {"branches",
                       arr({arr({str(first), str("0 0 0 0")}), arr({str(last), str("0 0 0 0")})})},
                      {"degrees", nums({2, 2})}})},
       {"version", nums({1, 0})}});
  f.rebuild_physical(false);
  return f;
}

std::string base_square_box(U D0, bool half, U p) {
  U k = half ? p - 1 : p;
  Big N, q, t, offset, exp;
  fmpz_set_ui(N.z, D0);
  fmpz_mul_2exp(N.z, N.z, 2 * k);
  fmpz_sqrt(q.z, N.z);
  fmpz_mul_2exp(t.z, q.z, 1);
  fmpz_add_ui(t.z, t.z, 1);
  fmpz_mul(t.z, t.z, t.z);
  fmpz_mul_2exp(N.z, N.z, 2);
  if (fmpz_cmp(N.z, t.z) > 0) fmpz_add_ui(q.z, q.z, 1);
  fmpz_one(offset.z);
  fmpz_mul_2exp(offset.z, offset.z, k);
  if (half)
    fmpz_add(q.z, q.z, offset.z);
  else
    fmpz_sub(q.z, q.z, offset.z);
  U trailing = fmpz_val2(q.z);
  fmpz_fdiv_q_2exp(q.z, q.z, trailing);
  fmpz_set_si(exp.z, (int64_t)trailing - (int64_t)p);
  return hex(q.z) + " " + hex(exp.z) + " 1 -80";
}
}  // namespace fixture
