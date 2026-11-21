// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/exact/exact.hpp"

#include "sic/sicx/verify.hpp"
#include "sic/support/flint_raii.hpp"
#include "sicx/codec/block.hpp"
namespace sic::sicx::exact {
std::vector<QuadraticElement> read_leaves(const Reader& reader, size_t id, const Tower& T) {
  const auto& meta = reader.meta();
  const auto& e = meta.elements[id];
  std::vector<QuadraticElement> lam(T.N[e.level]);
  sic::support::flint::Fmpz scale;
  sic::support::flint::Fmpz v;
  fmpz_set_str(scale, e.scale.c_str(), 10);
  IntCodec codec;
  PairTransform transform(meta.D0);
  FmpzPair value;
  for (size_t chunk : reader.element_blocks(id)) {
    const ByteBuffer payload = reader.leaf(chunk);
    decode_block(
        payload, meta,
        [&](uint64_t s, const fmpz_t x, const fmpz_t y) {
          QuadraticElement& dst = lam[e.layout.position(s)];
          fmpz_mul(v, x, scale);
          fmpq_set_fmpz(dst.a, v);
          fmpz_mul(v, y, scale);
          fmpq_set_fmpz(dst.b, v);
        },
        [](uint64_t, uint64_t) {}, codec, transform, value);
  }
  return lam;
}

Builder::Builder(const Reader& r, BuildOptions o)
    : reader(r), meta(r.meta()), T(r.meta()), opt(o) {}

size_t Builder::find_element(const std::string& kind, unsigned level, int64_t axis,
                             int64_t k) const {
  for (size_t id = 0; id < meta.elements.size(); ++id) {
    const auto& e = meta.elements[id];
    if (e.kind == kind && e.level == level && e.axis == axis && e.k == k) return id;
  }
  return SIZE_MAX;
}

Elt Builder::element_of(size_t id) const {
  const auto lam = read_leaves(reader, id, T);
  return T.from_leaves(lam.data(), meta.elements[id].level);
}

void Builder::build_exact() {
  const auto file_inverse = [&](const Elt& value, unsigned level, const char* name) {
    if (block_zero(value.data(), value.size()))
      fail(ErrorKind::invalid_format, std::string(name) + " element is not invertible");
    try {
      return T.inverse(value, level);
    } catch (const std::length_error&) {
      throw;
    } catch (const std::logic_error&) {
      fail(ErrorKind::invalid_format, std::string(name) + " element is not invertible");
    }
  };
  for (unsigned i = 0; i < T.t; ++i) {
    std::vector<Elt> coeff(T.p[i], T.zero(i));
    if (T.p[i] == 2) {
      const size_t id = find_element("wsq", i, -1, -1);
      if (id == SIZE_MAX) fail(sic::ErrorKind::invalid_format, "missing wsq element");
      const Elt c = element_of(id);
      if (block_zero(c.data(), c.size()))
        fail(ErrorKind::invalid_format, "wsq element is not invertible");
      for (size_t q = 0; q < c.size(); ++q) {
        fmpq_neg(coeff[0][q].a, c[q].a);
        fmpq_neg(coeff[0][q].b, c[q].b);
      }
    } else {
      for (unsigned k = 0; k < T.p[i]; ++k) {
        const size_t id = find_element("fcoef", i, -1, k);
        if (id == SIZE_MAX) fail(sic::ErrorKind::invalid_format, "missing fcoef element");
        coeff[k] = element_of(id);
      }
    }
    try {
      T.set_level(i, std::move(coeff));
    } catch (const std::length_error&) {
      throw;
    } catch (const std::logic_error&) {
      fail(ErrorKind::invalid_format, "tower polynomial has no trace-dual basis");
    }
  }
  // Galois generators: tau_l(g_i) as elements of F_{i+1}.
  tau.assign(meta.smith.size(), {});
  for (size_t l = 0; l < meta.smith.size(); ++l)
    for (unsigned i = 0; i < T.t; ++i) {
      Elt img = T.generator(i);
      const unsigned pi = T.p[i];
      if (pi == 2) {
        const size_t id = find_element("act", i, static_cast<int64_t>(l), -1);
        if (id != SIZE_MAX) {  // tau(g) = (A / c) g, c = g^2 = -f_0
          const Elt A = element_of(id);
          Elt c = T.zero(i);
          for (size_t q = 0; q < c.size(); ++q) {
            fmpq_neg(c[q].a, T.f[i][0][q].a);
            fmpq_neg(c[q].b, T.f[i][0][q].b);
          }
          const Elt ratio = T.mul(A, file_inverse(c, i, "wsq"), i);
          img = T.zero(i + 1);
          for (size_t q = 0; q < ratio.size(); ++q) img[T.N[i] + q] = ratio[q];
        }
      } else {
        const size_t delta_id = find_element("actdelta", i, -1, -1);
        if (delta_id != SIZE_MAX &&
            find_element("actodd", i, static_cast<int64_t>(l), 0) != SIZE_MAX) {
          const Elt inv_delta = file_inverse(element_of(delta_id), i, "actdelta");
          img = T.zero(i + 1);
          for (unsigned k = 0; k < pi; ++k) {
            const Elt a = T.mul(element_of(find_element("actodd", i, static_cast<int64_t>(l), k)),
                                inv_delta, i);
            for (size_t q = 0; q < a.size(); ++q) img[k * T.N[i] + q] = a[q];
          }
        }
      }
      tau[l].push_back(std::move(img));
    }
  for (const auto& [pos, id] : meta.representative) {
    rep_index[pos] = reps.size();
    reps.push_back(element_of(id));
  }
}

std::vector<Builder::RayData> Builder::build_rays() {
  if (!reader.finalized())
    fail(sic::ErrorKind::invalid_format,
         "the file is not finalized (no RAYS): finalize it before generating exact.json");
  std::map<std::vector<uint64_t>, Images> cache;
  auto images = [&](const std::vector<uint64_t>& w) -> const Images& {
    auto it = cache.find(w);
    if (it == cache.end()) it = cache.emplace(w, word_images(w)).first;
    return it->second;
  };
  std::vector<RayData> rays;
  for (size_t ri = 0; ri < meta.rays.size(); ++ri) {
    RayData r;
    require_multiplet(opt.multiplet);
    r.id = ray_full_id(meta, ri, opt.multiplet);
    r.word = meta.rays[ri].word;
    for (uint64_t mm = 0; mm < meta.d; ++mm) {
      const auto entry = meta.entry(mm);
      const auto w = words_add(r.word, entry.word, meta.smith);
      r.coordinates.push_back(T.apply(reps[rep_index.at(entry.representative)], T.t, images(w)));
    }
    rays.push_back(std::move(r));
  }
  return rays;
}

void Builder::numeric_check(const std::vector<RayData>& rays) {
  const slong prec = opt.precision;
  const auto mono = monomials(T, T.t, roots, prec);
  for (size_t ri = 0; ri < rays.size(); ++ri) {
    std::vector<ComplexBall> v(meta.d);
    sic::support::flint::Arb norm2;
    sic::support::flint::Arb t0;
    for (uint64_t j = 0; j < meta.d; ++j) {
      evaluate(v[j].v, rays[ri].coordinates[j], mono, omega.v, prec);
      arb_sqr(t0, acb_realref(v[j].v), prec);
      arb_add(norm2, norm2, t0, prec);
      arb_sqr(t0, acb_imagref(v[j].v), prec);
      arb_add(norm2, norm2, t0, prec);
    }
    arb_sqrt(norm2, norm2, prec);
    ComplexBall gauge;
    const uint64_t a = meta.rays[ri].anchor;
    acb_conj(gauge.v, v[a].v);
    acb_abs(t0, v[a].v, prec);
    acb_div_arb(gauge.v, gauge.v, t0, prec);
    const auto parts = reader.ball_parts(ri);
    ComplexBall w, pub;
    for (uint64_t j = 0; j < meta.d; ++j) {
      acb_mul(w.v, v[j].v, gauge.v, prec);
      acb_div_arb(w.v, w.v, norm2, prec);
      ball_part_arb(acb_realref(pub.v), parts[2 * j]);
      ball_part_arb(acb_imagref(pub.v), parts[2 * j + 1]);
      if (!acb_is_finite(w.v))
        fail(sic::ErrorKind::precision, "numerical check produced a non-finite ball: ray " +
                                            rays[ri].id + " coordinate " + std::to_string(j));
      for (auto* part : {acb_realref(w.v), acb_imagref(w.v)})
        if (mag_cmp_2exp_si(arb_radref(part), -100) > 0)
          fail(sic::ErrorKind::precision,
               "numerical check ball is too wide to be meaningful: ray " + rays[ri].id);
      if (!acb_overlaps(w.v, pub.v))
        fail(sic::ErrorKind::invalid_format, "power basis disagrees with BALL: ray " + rays[ri].id +
                                                 " coordinate " + std::to_string(j));
      ++report.checked;
      for (auto* part : {acb_realref(w.v), acb_imagref(w.v)}) {
        const double lg = arb_is_zero(part) || mag_is_zero(arb_radref(part))
                              ? -1e9
                              : mag_get_d_log2_approx(arb_radref(part));
        report.max_radius_log2 = std::max(report.max_radius_log2, lg);
      }
    }
  }
}

sic::support::Json Builder::build_json(const std::string& sicx_name,
                                       const std::string& sicx_sha256) {
  build_exact();
  for (size_t l = 0; l < tau.size(); ++l)
    check_endomorphism(tau[l], false, "action record " + std::to_string(l));
  certify_roots();
  find_conjugation();
  const auto rays = build_rays();
  report.coordinates = rays.size() * meta.d;
  if (opt.numeric_check) numeric_check(rays);

  sic::support::Json j = sic::support::Json::object_value();
  j["schema"] = sic::support::Json::string_value("sic-exact-json/1.0");
  j["required_features"] = sic::support::Json::array_value();
  j["d"] = sic::support::Json::string_value(std::to_string(meta.d));
  if (!opt.multiplet.empty()) j["multiplet"] = sic::support::Json::string_value(opt.multiplet);
  j["sicx"] = sic::support::Json::object_value();
  j["sicx"]["name"] = sic::support::Json::string_value(sicx_name);
  j["sicx"]["sha256"] = sic::support::Json::string_value(sicx_sha256);
  j["sicx"]["merkle_root"] = sic::support::Json::string_value(hex(reader.root()));
  sic::support::Json field = sic::support::Json::object_value();
  field["D0"] = sic::support::Json::string_value(std::to_string(meta.D0));
  field["variable"] = sic::support::Json::string_value("omega");
  {
    std::string poly = "omega^2";
    if (T.s) poly += " - omega";
    const std::string m = fmpq_dec(T.m);
    poly += (m[0] == '-' ? " + " + m.substr(1) : " - " + m);
    if (fmpq_is_zero(T.m)) poly = "omega^2" + std::string(T.s ? " - omega" : "");
    field["polynomial"] = sic::support::Json::string_value(meta.D0 == 1 ? "omega" : poly);
  }
  field["polynomial_coefficients"] = sic::support::Json::array_value();
  {
    sic::support::flint::Fmpq negm;
    fmpq_neg(negm, T.m);
    field["polynomial_coefficients"].array.push_back(
        sic::support::Json::string_value(fmpq_dec(negm)));
    if (meta.D0 != 1)
      field["polynomial_coefficients"].array.push_back(
          sic::support::Json::string_value(T.s ? "-1" : "0"));
    field["polynomial_coefficients"].array.push_back(sic::support::Json::string_value("1"));
  }
  field["embedding"] = sic::support::Json::string_value(
      meta.D0 == 1  ? "omega -> 0 (rational base field)"
      : !T.s        ? "omega -> -sqrt(D0) with sqrt(D0) > 0"
      : meta.D0 > 0 ? "omega -> (1 - sqrt(D0))/2 with sqrt(D0) > 0"
                    : "omega -> (1 - sqrt(D0))/2 with sqrt(D0) = i*sqrt(-D0)");
  j["field"] = field;
  j["element_encoding"] = sic::support::Json::string_value(
      "an element of F_n is {denominator, coefficients}: coefficients[e] = [x, y] is the "
      "coefficient (x + y*omega)/denominator of the "
      "monomial g0^e0 * g1^e1 * ... with e = e0 + p0*e1 + p0*p1*e2 + ... (little-endian mixed "
      "radix over the tower degrees); "
      "length of coefficients = p0*...*p_{n-1}");
  sic::support::Json order = sic::support::Json::array_value();
  order.array.push_back(sic::support::Json::string_value("omega"));
  for (unsigned i = 0; i < T.t; ++i)
    order.array.push_back(sic::support::Json::string_value("g" + std::to_string(i)));
  j["extension_order"] = order;
  j["tower"] = sic::support::Json::array_value();
  for (unsigned i = 0; i < T.t; ++i) {
    sic::support::Json lev = sic::support::Json::object_value();
    lev["index"] = sic::support::Json::string_value(std::to_string(i));
    lev["variable"] = sic::support::Json::string_value("g" + std::to_string(i));
    lev["base_variables"] = sic::support::Json::array_value();
    lev["base_variables"].array.push_back(sic::support::Json::string_value("omega"));
    for (unsigned q = 0; q < i; ++q)
      lev["base_variables"].array.push_back(
          sic::support::Json::string_value("g" + std::to_string(q)));
    lev["degree"] = sic::support::Json::string_value(std::to_string(T.p[i]));
    lev["polynomial"] = sic::support::Json::array_value();
    std::string infix = "g" + std::to_string(i) + "^" + std::to_string(T.p[i]);
    for (unsigned k = 0; k < T.p[i]; ++k)
      lev["polynomial"].array.push_back(element_json(T.f[i][k]));
    lev["polynomial"].array.push_back(element_json(T.one(i)));
    for (unsigned k = T.p[i]; k-- > 0;) {
      if (block_zero(T.f[i][k].data(), T.f[i][k].size())) continue;
      infix += " + (" + elt_infix(T, T.f[i][k], i) + ")" +
               (k ? "*g" + std::to_string(i) + (k > 1 ? "^" + std::to_string(k) : "") : "");
    }
    lev["polynomial_infix"] = sic::support::Json::string_value(infix);
    sic::support::Json root = sic::support::Json::object_value();
    root["re"] = sic::support::Json::array_value();
    root["im"] = sic::support::Json::array_value();
    root["re"].array = {sic::support::Json::string_value(root_text[4 * i]),
                        sic::support::Json::string_value(root_text[4 * i + 1])};
    root["im"].array = {sic::support::Json::string_value(root_text[4 * i + 2]),
                        sic::support::Json::string_value(root_text[4 * i + 3])};
    lev["root"] = root;
    lev["branch"] = sic::support::Json::array_value();
    lev["branch"].array = {sic::support::Json::string_value(meta.branches[i][0]),
                           sic::support::Json::string_value(meta.branches[i][1])};
    j["tower"].array.push_back(lev);
  }
  sic::support::Json cj = sic::support::Json::object_value();
  cj["omega"] = element_json(conj_omega());
  cj["generators"] = sic::support::Json::array_value();
  for (const auto& g : conj_generators_full()) cj["generators"].array.push_back(element_json(g));
  cj["galois_element"] = conjugation_word_json(report.conjugation);
  j["conjugation"] = cj;
  j["rays"] = sic::support::Json::object_value();
  for (const auto& r : rays) {
    sic::support::Json x = sic::support::Json::object_value();
    x["galois_element"] = sic::support::Json::object_value();
    x["galois_element"]["smith"] = sic::support::Json::array_value();
    x["galois_element"]["exponents"] = sic::support::Json::array_value();
    for (size_t l = 0; l < meta.smith.size(); ++l) {
      x["galois_element"]["smith"].array.push_back(
          sic::support::Json::string_value(std::to_string(meta.smith[l])));
      x["galois_element"]["exponents"].array.push_back(
          sic::support::Json::string_value(std::to_string(r.word[l])));
    }
    x["coordinates"] = sic::support::Json::array_value();
    for (const auto& c : r.coordinates) x["coordinates"].array.push_back(element_json(c));
    j["rays"][r.id] = x;
  }
  return j;
}
class DocumentBuilder::State : public Builder {
 public:
  using Builder::Builder;
};
DocumentBuilder::DocumentBuilder(const Reader& reader, BuildOptions options)
    : state_(std::make_unique<State>(reader, std::move(options))) {}
DocumentBuilder::~DocumentBuilder() = default;
DocumentBuilder::DocumentBuilder(DocumentBuilder&&) noexcept = default;
DocumentBuilder& DocumentBuilder::operator=(DocumentBuilder&&) noexcept = default;
sic::support::Json DocumentBuilder::build_json(const std::string& sicx_name,
                                               const std::string& sicx_sha256) {
  return state_->build_json(sicx_name, sicx_sha256);
}
const BuildReport& DocumentBuilder::report() const { return state_->report; }
}  // namespace sic::sicx::exact
