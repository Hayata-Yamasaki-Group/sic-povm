// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/stages/dimension_two.hpp"

#include "sic/data/text_formats.hpp"

namespace sic::construct::dimension_two::detail {
std::string ball_text(const arb_t x) {
  std::unique_ptr<char, decltype(&flint_free)> raw(arb_dump_str(x), flint_free);
  std::string text(raw.get());
  return sic::sicx::ball_part_text(sic::sicx::parse_ball_part(text, 4));
}

void read_radicals(const std::string& path, fmpq_t a2x, fmpq_t a2y, fmpq_t b2x, fmpq_t b2y) {
  std::ifstream file;
  std::istringstream embedded{std::string(kPinnedRadicals)};
  if (!path.empty()) {
    file.open(path);
    if (!file) throw sic::Error(sic::ErrorKind::io, "cannot read " + path);
  }
  std::istream& in =
      path.empty() ? static_cast<std::istream&>(embedded) : static_cast<std::istream&>(file);
  std::string line, magic;
  std::getline(in, magic);
  if (in.bad()) throw sic::Error(sic::ErrorKind::io, "cannot read " + path);
  if (magic != sic::data::kExactD2RadicalsIdentity)
    throw sic::Error(sic::ErrorKind::invalid_format, "not a d2 radical file");
  bool end = false, have_radicand = false;
  int radicand = 0;
  while (std::getline(in, line)) {
    std::istringstream rec(line);
    std::string key, x, y;
    rec >> key;
    if (key == "radicand") {
      if (!(rec >> radicand))
        throw sic::Error(sic::ErrorKind::invalid_format, "bad radicand in radical file");
      have_radicand = true;
    } else if (key == "alpha_squared" || key == "beta_squared") {
      rec >> x >> y;
      fmpq* dst = key == "alpha_squared" ? a2x : b2x;
      fmpq* dsty = key == "alpha_squared" ? a2y : b2y;
      if (fmpq_set_str(dst, x.c_str(), 10) || fmpq_set_str(dsty, y.c_str(), 10))
        throw sic::Error(sic::ErrorKind::invalid_format, "bad rational in radical file");
    } else if (key == "end") {
      end = true;
      break;
    }
  }
  if (in.bad()) throw sic::Error(sic::ErrorKind::io, "cannot read " + path);
  if (!have_radicand)
    throw sic::Error(sic::ErrorKind::invalid_format, "missing radicand in radical file");
  if (!end) throw sic::Error(sic::ErrorKind::invalid_format, "unterminated radical file");
  if (radicand != 3)
    throw sic::Error(sic::ErrorKind::unsupported, "unsupported radical file (need radicand 3)");
}

}  // namespace sic::construct::dimension_two::detail
namespace sic::construct::dimension_two {
int make(const std::filesystem::path& out, const std::string& radicals,
         const std::string& keep_balls,
         const std::function<void(acb_srcptr, acb_srcptr)>& check_sic,
         const sic::construct::ProductionPolicy& policy) try {
  flint_set_num_threads(1);

  sic::sicx::Meta meta;
  meta.d = 2;
  meta.D0 = sic::sicx::field_radicand(2);
  if (meta.D0 != -3) throw std::logic_error("D0 of d = 2 must be -3");
  meta.degrees = {2};
  meta.width = {1, 2};
  meta.D = 2;
  meta.smith = {2};
  meta.units = std::make_shared<const sic::sicx::UnitGroup>(meta.d);
  meta.unit_generator_words = {};
  {
    sic::support::flint::Arb re, im;

    arb_zero(re);
    arb_one(im);  // iota(g_0) = i, exactly
    std::unique_ptr<char, decltype(&flint_free)> a(arb_dump_str(re), flint_free);
    std::unique_ptr<char, decltype(&flint_free)> b(arb_dump_str(im), flint_free);
    meta.branches.push_back({a.get(), b.get()});
  }
  meta.catalogue = sic::support::Json::object_value();
  meta.catalogue["multiplet"] = sic::support::Json::string_value("d2");
  meta.catalogue["order_index"] = sic::support::Json::string_value("1");
  meta.block_coordinates = uint64_t(1) << sic::sicx::kLogicalLeafLog2Limit;
  meta.rescaling_shifts = {0};

  struct Item {
    std::string kind;
    int64_t axis, position;
    std::vector<unsigned> levels;
    std::string scale;
    uint64_t max_bits;
    std::vector<Leaf> coordinates;
  };
  const std::vector<Item> items = {
      {"wsq", -1, -1, {}, "1", 1, {{0, -1, 0}}},
      {"act", 0, -1, {}, "1", 1, {{0, 1, 0}}},
      {"rep", -1, 0, {}, "2", 2, {{0, 1, 0}}},
      {"rep", -1, 1, {0}, "2", 2, {{0, 0, -1}, {1, 1, -1}}},
  };
  for (const auto& it : items) {
    sic::sicx::ElementInfo e;
    e.kind = it.kind;
    e.level = it.kind == "rep" ? 1 : 0;
    e.axis = it.axis;
    e.position = it.position;
    e.levels = it.levels;
    e.scale = it.scale;
    e.max_bits = it.max_bits;
    e.layout = sic::sicx::SubLayout(meta.degrees, meta.width, e.levels);
    sic::sicx::derive_blocks(e, meta.block_coordinates);
    meta.elements.push_back(std::move(e));
  }
  // Canonical generator rescaling (spec section 3.7) of these leaves: the rule gives J = [0] (c_0 =
  // -1 is odd), so the data above are already canonical; computed, not assumed, and recorded in
  // PROV.
  sic::sicx::GeneratorRescaling rescaling;
  {
    sic::sicx::RescalingCollector collector(meta);
    for (size_t id = 0; id < items.size(); ++id) {
      const auto& it = items[id];
      const auto& e = meta.elements[id];
      collector.add({it.kind, e.level, it.axis, -1, it.position},
                    [&](const sic::sicx::LeafVisitor& visit) {
                      sic::support::flint::Fmpz x, y, u;

                      fmpz_set_str(u, it.scale.c_str(), 10);
                      for (const auto& c : it.coordinates) {
                        fmpz_mul_si(x, u, c.x);
                        fmpz_mul_si(y, u, c.y);
                        visit(e.layout.position(c.s), x, y);
                      }
                    });
    }
    rescaling = collector.compute(1);
    if (!rescaling.identity())
      throw sic::Error(sic::ErrorKind::construction,
                       "d = 2: the canonical rescaling is not the identity");
  }
  const std::string meta_text = sic::support::dump_json(sic::sicx::meta_to_json(meta));
  const sic::sicx::Meta checked = sic::sicx::meta_from_json(
      sic::support::JsonParser::parse(meta_text));  // reader rules before writing

  // ---- numerical vector from the exact leaves, 256-bit balls
  const slong prec = 256;
  sic::support::flint::Acb omega, g, z, t0, t1;

  sic::support::flint::Arb s3;

  arb_set_ui(s3, 3);
  arb_sqrt(s3, s3, prec);
  arb_set_si(acb_realref(omega), 1);
  arb_neg(acb_imagref(omega), s3);
  acb_mul_2exp_si(omega, omega, -1);  // omega = (1 - sqrt(-3))/2 = (1 - i sqrt 3)/2
  acb_onei(g);                        // iota(g_0) = i
  // eta_1 = lambda_0/2 + lambda_1/(2c) g, c = -1, lambda = (-2 omega, 2 - 2 omega)
  acb_set(t0, omega);
  acb_neg(t0, t0);                 // lambda_0/2 = -omega
  acb_sub_ui(t1, omega, 1, prec);  // lambda_1/(2c) = (2 - 2 omega)/(-2) = omega - 1
  acb_mul(t1, t1, g, prec);
  acb_add(z, t0, t1, prec);
  // radical check: z = (1 + i) beta/alpha for the d2.sicrad record (alpha, beta > 0)
  {
    sic::support::flint::Fmpq a2x, a2y, b2x, b2y;

    read_radicals(radicals, a2x, a2y, b2x, b2y);
    sic::support::flint::Arb a2, b2, alpha, beta, q;

    arb_set_fmpq(a2, a2y, prec);
    arb_mul(a2, a2, s3, prec);
    arb_set_fmpq(q, a2x, prec);
    arb_add(a2, a2, q, prec);
    arb_set_fmpq(b2, b2y, prec);
    arb_mul(b2, b2, s3, prec);
    arb_set_fmpq(q, b2x, prec);
    arb_add(b2, b2, q, prec);
    arb_sqrt(alpha, a2, prec);
    arb_sqrt(beta, b2, prec);
    sic::support::flint::Acb expect;

    arb_set_si(acb_realref(expect), 1);
    arb_set_si(acb_imagref(expect), 1);
    acb_mul_arb(expect, expect, beta, prec);
    acb_div_arb(expect, expect, alpha, prec);
    if (!acb_overlaps(expect, z))
      throw sic::Error(sic::ErrorKind::construction,
                       "the SICX vector disagrees with the radical record of d2.sicrad");
    sic::support::flint::Arb nrm;

    arb_mul(nrm, beta, beta, prec);
    arb_mul_ui(nrm, nrm, 2, prec);
    arb_add(nrm, nrm, a2, prec);
    if (!arb_contains_si(nrm, 1))
      throw sic::Error(sic::ErrorKind::construction,
                       "alpha^2 + 2 beta^2 != 1 in the radical record");
    sic::support::progressf(
        "RADICALS ok: (1, z) is proportional to (alpha, (1+i) beta); alpha^2 + 2 beta^2 contains "
        "1\n");
  }
  // normalized gauged vector: anchor 0 (v_0 = 1)
  sic::support::flint::Arb norm;

  acb_abs(norm, z, prec);
  arb_mul(norm, norm, norm, prec);
  arb_add_ui(norm, norm, 1, prec);
  arb_sqrt(norm, norm, prec);
  sic::support::flint::Acb w0, w1;

  arb_one(acb_realref(w0));
  arb_zero(acb_imagref(w0));
  acb_div_arb(w0, w0, norm, prec);
  acb_div_arb(w1, z, norm, prec);
  if (check_sic) check_sic(w0.get(), w1.get());
  const std::filesystem::path balls_path = keep_balls.empty()
                                               ? std::filesystem::path(out.string() + ".balls.tmp")
                                               : std::filesystem::path(keep_balls);
  {
    std::ofstream f(balls_path, std::ios::binary | std::ios::trunc);
    f << sic::data::kVectorBallsIdentity << "\nd 2\n";
    for (const acb_struct* w : {w0.get(), w1.get()})
      f << ball_text(acb_realref(w)) << " | " << ball_text(acb_imagref(w)) << '\n';
    f << "end\n";
    f.close();
    if (!f) throw sic::Error(sic::ErrorKind::io, "cannot write the balls file");
  }

  // ---- stage 1: EXMT, TOWR, BODY, PROV
  sic::sicx::StreamOptions options{false, false, {}, false};
  options.limits = policy.resources;
  sic::sicx::StreamWriter writer(out, options);
  writer.chunk("EXMT", sic::sicx::kRequired, sic::sicx::text_span(meta_text));
  for (size_t id = 0; id < items.size(); ++id) {
    const auto& e = meta.elements[id];
    std::vector<std::unique_ptr<sic::sicx::FmpzPair>> values;
    for (const auto& c : items[id].coordinates) {
      auto pair = std::make_unique<sic::sicx::FmpzPair>();
      fmpz_set_si(pair->x, c.x);
      fmpz_set_si(pair->y, c.y);
      values.push_back(std::move(pair));
    }
    sic::sicx::BlockEncoder encoder(meta.D0);
    const sic::sicx::ByteBuffer encoded =
        encoder.encode(id, 0, e.block, values.size(), [&](size_t i) {
          return std::tuple<uint64_t, const fmpz*, const fmpz*>(items[id].coordinates[i].s,
                                                                values[i]->x, values[i]->y);
        });
    writer.chunk(items[id].kind == "rep" ? "BODY" : "TOWR", sic::sicx::kRequired, encoded, id, 0,
                 e.block);
  }

  {
    sic::support::Json prov = sic::support::Json::object_value();
    prov["packer"] = sic::support::Json::string_value("native closed form");

    prov["derivation"] = sic::support::Json::string_value(
        "v = (1, z), z = psi_1/psi_0 = (1+i)(sqrt(3)-1)/2 = -omega + (omega-1) g_0, g_0^2 = -1, "
        "from the tetrahedral fiducial of d2.sicrad; BALL computed from the exact leaves with "
        "256-bit Arb balls");
    {
      prov["source"] = sic::support::Json::object_value();
      prov["source"]["name"] = sic::support::Json::string_value(
          radicals.empty() ? "d2.sicrad" : std::filesystem::path(radicals).filename().string());
      prov["source"]["sha256"] = sic::support::Json::string_value(
          radicals.empty() ? sic::sicx::hex(sic::sicx::sha256(
                                 reinterpret_cast<const uint8_t*>(kPinnedRadicals.data()),
                                 kPinnedRadicals.size()))
                           : sic::support::sha256::file_hex(radicals));
    }
    writer.chunk("PROV", 0, sic::sicx::text_span(sic::support::dump_json(prov)));
  }
  // The decoder constructs RAYS/BALL/PREC from its own certified enclosures.
  const auto root = writer.finish();
  if (keep_balls.empty()) std::filesystem::remove(balls_path);
  sic::support::progressf("WRITTEN %s bytes=%llu merkle=%s\n", out.c_str(),
                          (unsigned long long)std::filesystem::file_size(out),
                          sic::sicx::hex(root).c_str());
  // reader check
  sic::sicx::Reader reader(out, policy.resources);
  (void)checked;
  sic::support::progressf("READ ok: d=%llu D0=%lld rays=%zu finalized=%d\n",
                          (unsigned long long)reader.meta().d, (long long)reader.meta().D0,
                          reader.meta().rays.size(), (int)reader.finalized());

  return 0;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

}  // namespace sic::construct::dimension_two
