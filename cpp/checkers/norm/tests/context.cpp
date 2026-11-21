// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
namespace sic_check::norm::test {
using namespace sic_check::norm;
using fixture::Document;
using fixture::Node;
using fixture::num;
using fixture::overwrite;
void check(bool ok, const std::string& reason) {
  if (!ok) throw std::runtime_error(reason);
}

std::string hex(const Hash& h) {
  std::string s;
  const char* digits = "0123456789abcdef";
  for (auto b : h) {
    s += digits[b >> 4];
    s += digits[b & 15];
  }
  return s;
}

void write(const std::filesystem::path& path, const ByteBuffer& b) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
  check(bool(out), "test fixture write");
}

Hash root(const ByteBuffer& b, size_t relative) {
  Hash h;
  std::copy_n(b.end() - 112 + relative, 32, h.begin());
  return h;
}

std::string integer_text(const fmpz_t value) {
  char* raw = fmpz_get_str(nullptr, 10, value);
  std::string text(raw);
  flint_free(raw);
  return text;
}

void big_integer(ByteBuffer& out, const fmpz_t value) {
  Integer magnitude;
  fmpz_abs(magnitude.v, value);
  size_t bytes = (fmpz_bits(magnitude.v) + 7) / 8;
  fixture::var(out, 2 * bytes + (fmpz_sgn(value) < 0));
  for (size_t i = 0; i < bytes; ++i) {
    out.push_back(uint8_t(fmpz_get_ui(magnitude.v)));
    fmpz_fdiv_q_2exp(magnitude.v, magnitude.v, 8);
  }
}

void real_leaf(Document& d, size_t id, const std::vector<Integer>& coordinates) {
  Integer gcd, primitive;
  uint64_t maximum = 0;
  for (const auto& c : coordinates) {
    fmpz_gcd(gcd.v, gcd.v, c.v);
    maximum = std::max<uint64_t>(maximum, fmpz_bits(c.v));
  }
  if (!maximum) fmpz_one(gcd.v);
  auto& e = d.exmt["elements"].array[id];
  e["scale"] = integer_text(gcd.v);
  e["max_bits"] = num(maximum);
  e["levels"] = coordinates.size() == 1 ? Node::a() : Node::a({"0"});
  ByteBuffer out;
  fixture::var(out, id);
  fixture::var(out, 0);
  fixture::var(out, coordinates.size());
  for (size_t i = 0; i < coordinates.size();) {
    bool data = !fmpz_is_zero(coordinates[i].v);
    size_t end = i + 1;
    while (end < coordinates.size() && !fmpz_is_zero(coordinates[end].v) == data) ++end;
    fixture::var(out, 2 * (end - i) + data);
    for (; i < end; ++i)
      if (data) {
        fmpz_divexact(primitive.v, coordinates[i].v, gcd.v);
        big_integer(out, primitive.v);
        fixture::integer(out, int64_t(0));
      }
  }
  d.leaves[id].payload = out;
  d.leaves[id].count = coordinates.size();
  d.physical[id + 1].count = coordinates.size();
  d.sync_leaves();
}

std::string positive_real_branch(const std::vector<Integer>& coefficients, uint64_t bits,
                                 bool smallest) {
  fmpz_poly_t polynomial;
  fmpz_poly_init(polynomial);
  size_t degree = coefficients.size() - 1;
  for (size_t k = 0; k <= degree; ++k)
    fmpz_poly_set_coeff_fmpz(polynomial, slong(k), coefficients[k].v);
  acb_ptr roots = _acb_vec_init(slong(degree));
  slong precision = slong(2 * bits + 2048);
  arb_fmpz_poly_complex_roots(roots, polynomial, 0, precision);
  int selected = -1;
  for (size_t k = 0; k < degree; ++k)
    if (arb_is_zero(acb_imagref(roots + k)) && arb_is_positive(acb_realref(roots + k)) &&
        (selected < 0 ||
         (smallest ? arb_lt(acb_realref(roots + k), acb_realref(roots + selected))
                   : arb_gt(acb_realref(roots + k), acb_realref(roots + selected)))))
      selected = int(k);
  check(selected >= 0, "fixture positive real root");
  ComplexBall scaled, half, box;
  arb_mul_2exp_si(acb_realref(scaled.v), acb_realref(roots + selected), slong(bits));
  arb_one(acb_realref(half.v));
  arb_mul_2exp_si(acb_realref(half.v), acb_realref(half.v), -1);
  arb_add(acb_realref(scaled.v), acb_realref(scaled.v), acb_realref(half.v), precision);
  arb_floor(acb_realref(scaled.v), acb_realref(scaled.v), precision);
  Integer rounded;
  check(arb_get_unique_fmpz(rounded.v, acb_realref(scaled.v)), "fixture unambiguous rounding");
  arb_set_fmpz(acb_realref(box.v), rounded.v);
  arb_mul_2exp_si(acb_realref(box.v), acb_realref(box.v), -slong(bits));
  fmpq_t rational, evaluated;
  fmpq_init(rational);
  fmpq_init(evaluated);
  fmpq_set_fmpz(rational, rounded.v);
  fmpq_div_2exp(rational, rational, bits);
  fmpz_poly_evaluate_fmpq(evaluated, polynomial, rational);
  if (!fmpq_is_zero(evaluated)) mag_set_ui_2exp_si(arb_radref(acb_realref(box.v)), 1, -slong(bits));
  char* dumped = arb_dump_str(acb_realref(box.v));
  std::string result(dumped);
  flint_free(dumped);
  fmpq_clear(evaluated);
  fmpq_clear(rational);
  _acb_vec_clear(roots, slong(degree));
  fmpz_poly_clear(polynomial);
  return result;
}

std::string sqrt_branch(unsigned n, uint64_t bits, bool negative) {
  Integer value, floor, threshold, comparison;
  fmpz_set_ui(value.v, n);
  fmpz_mul_2exp(value.v, value.v, 2 * bits);
  fmpz_sqrt(floor.v, value.v);
  fmpz_mul_2exp(threshold.v, floor.v, 1);
  fmpz_add_ui(threshold.v, threshold.v, 1);
  fmpz_mul(threshold.v, threshold.v, threshold.v);
  fmpz_mul_2exp(comparison.v, value.v, 2);
  if (fmpz_cmp(comparison.v, threshold.v) > 0) fmpz_add_ui(floor.v, floor.v, 1);
  if (negative) fmpz_neg(floor.v, floor.v);
  unsigned shift = unsigned(fmpz_val2(floor.v));
  fmpz_tdiv_q_2exp(floor.v, floor.v, shift);
  char* c = fmpz_get_str(nullptr, 16, floor.v);
  std::string mantissa(c);
  flint_free(c);
  Integer exponent;
  fmpz_set_si(exponent.v, -slong(bits) + shift);
  c = fmpz_get_str(nullptr, 16, exponent.v);
  std::string e(c);
  flint_free(c);
  fmpz_set_si(exponent.v, -slong(bits));
  c = fmpz_get_str(nullptr, 16, exponent.v);
  std::string re(c);
  flint_free(c);
  return mantissa + " " + e + " 1 " + re;
}

void rebuild(Document& d) {
  auto tail = d.physical;
  d.physical.resize(1);
  for (size_t id = 0; id < d.leaves.size(); ++id) {
    auto& l = d.leaves[id];
    check(l.id < 128, "test-only small id");
    size_t skip = 1;
    ByteBuffer payload;
    fixture::var(payload, id);
    payload.insert(payload.end(), l.payload.begin() + skip, l.payload.end());
    l.payload = payload;
    l.id = id;
    d.physical.push_back({l.kind, 1, 0, 0, id + 1, 1, id, l.first, l.count, l.payload});
  }
  for (auto& c : tail)
    if (c.type == "PROV" || c.type == "RAYS" || c.type == "BALL" || c.type == "PREC")
      d.physical.push_back(c);
}

auto TestContext::run(const ByteBuffer& b, const std::string& name, int expected,
                      ReadOptions options, const std::string& diagnostic) -> void {
  cases << name << "\t" << (expected == 0 ? "accept" : expected == 1 ? "reject" : "refuse") << "\n";
  write(scratch, b);
  try {
    Reader r(scratch.string(), options);
    check(expected == 0, name + ": unexpectedly accepted");
    ++accepted;
  } catch (const InvalidInput& e) {
    if (expected != 1) throw std::runtime_error(name + ": " + e.what());
    check(std::string(e.what()).find(diagnostic) != std::string::npos,
          name + ": wrong rejection: " + e.what());
    ++rejected;
  } catch (const ResourceRefusal& e) {
    if (expected != 2) throw std::runtime_error(name + ": " + e.what());
    check(std::string(e.what()).find(diagnostic) != std::string::npos,
          name + ": wrong refusal: " + e.what());
    ++refused;
  }
}

auto TestContext::expect_invalid(const std::function<void()>& f, const std::string& reason,
                                 const std::string& diagnostic) -> void {
  bool caught = false;
  try {
    f();
  } catch (const InvalidInput& e) {
    check(std::string(e.what()).find(diagnostic) != std::string::npos,
          reason + ": wrong rejection: " + e.what());
    caught = true;
  }
  check(caught, reason);
}

auto TestContext::mutation(const std::string& name, const std::function<void(Document&)>& edit,
                           int outcome) -> void {
  const auto& original = *base_document;
  Document d = original;
  edit(d);
  run(d.encode(), name, outcome);
}

auto TestContext::raw(const std::string& name, const std::function<void(ByteBuffer&)>& edit,
                      int outcome) -> void {
  const auto& valid = *base_bytes;
  ByteBuffer b = valid;
  edit(b);
  run(b, name, outcome);
}

auto TestContext::pdir(const std::string& name, size_t entry, size_t offset, uint64_t value,
                       unsigned width) -> void {
  mutation(name, [=](auto& d) {
    d.edit_pdir = [=](auto& e) { overwrite(e.at(entry), offset, value, width); };
  });
}

auto TestContext::ldir(const std::string& name, size_t entry, size_t offset, uint64_t value,
                       unsigned width) -> void {
  mutation(name, [=](auto& d) {
    d.edit_ldir = [=](auto& e) { overwrite(e.at(entry), offset, value, width); };
  });
}

auto TestContext::partial_bad(Document d, const std::string& name) -> void {
  auto b = d.encode();
  ReadOptions options;
  options.partial = true;
  options.exact_root = root(b, 32);
  run(b, name, 1, options);
}

auto TestContext::exmt(const std::string& name, const std::function<void(Node&)>& edit, int outcome)
    -> void {
  mutation(
      name, [&](auto& d) { edit(d.exmt); }, outcome);
}

auto TestContext::expect_resource(const std::function<void()>& f, const std::string& reason)
    -> void {
  bool caught = false;
  try {
    f();
  } catch (const ResourceRefusal& e) {
    caught = std::string(e.what()).find(reason) != std::string::npos;
  }
  check(caught, reason);
}
}  // namespace sic_check::norm::test
