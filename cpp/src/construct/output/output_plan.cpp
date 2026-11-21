// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/output_plan.hpp"

#include "construct/model/session_state.hpp"
#include "sic/support/flint_raii.hpp"
#include "sic/support/parse_number.hpp"
namespace sic::construct {
sic::support::resources::Bytes sicx_coordinate_bytes(sic::support::resources::BitCount bits) {
  return sic::support::resources::add(
      10, sic::support::resources::mul(
              2, sic::support::resources::add(10, sic::support::resources::add(bits, 24 + 7) / 8)));
}
sic::support::resources::Bytes sicx_element_bound(sic::support::resources::Count coordinates,
                                                  sic::support::resources::BitCount bits,
                                                  sic::support::resources::Count block) {
  const auto blocks = sic::support::resources::add(coordinates, block - 1) / block;
  return sic::support::resources::add(
      sic::support::resources::mul(coordinates, sicx_coordinate_bytes(bits)),
      sic::support::resources::mul(blocks, 30 + 16 + 64 + 112));
}
sic::support::resources::Bytes sicx_free(const std::filesystem::path& p) {
  struct statvfs v {};
  if (statvfs(p.c_str(), &v) != 0)
    throw sic::Error(sic::ErrorKind::io, "SICX admission: statvfs failed for " + p.string());
  return sic::support::resources::mul((sic::support::resources::Count)v.f_bavail,
                                      (sic::support::resources::Bytes)v.f_frsize);
}
void sicx_admit(const std::filesystem::path& sicx_dir, const char* stage,
                sic::support::resources::Bytes required) {
  const sic::support::resources::Bytes reserve = 1ull << 30;
  const sic::support::resources::Bytes available = sicx_free(sicx_dir);
  sic::support::progressf(
      "SICX disk admission (%s): certified remaining bound=%llu + reserve=%llu, available=%llu\n",
      stage, (unsigned long long)required, (unsigned long long)reserve,
      (unsigned long long)available);
  sic::support::admission::record(
      "disk", stage,
      sic::support::resources::add(required, reserve) <= available ? "admit" : "refuse", reserve,
      required, available);
  if (sic::support::resources::add(required, reserve) > available)
    throw sic::Error(sic::ErrorKind::resource,
                     std::string("SICX disk admission refused (") + stage + ")");
}

}  // namespace sic::construct

namespace sic::construct {
void Construction::plan_output(slong wp, int representative_count,
                               const std::vector<std::unique_ptr<ActionElement>>& el,
                               const std::filesystem::path& sicx_dir, sic::sicx::Meta& sicx_header,
                               sic::sicx::StreamOptions& sicx_options,
                               sic::support::resources::Bytes& sicx_tower_bound,
                               std::vector<sic::support::resources::Bytes>& sicx_rep_bounds,
                               sic::support::resources::Bytes sicx_meta_reserve) {
  sic::sicx::Meta h = entry_metadata;
  h.d = (uint64_t)d;
  h.D0 = sic::sicx::field_radicand(h.d);
  if (h.D0 != D0)
    throw sic::Error(sic::ErrorKind::construction,
                     "SICX: D0 differs from the squarefree part of (d+1)(d-3)");
  for (long p : ps) {
    h.degrees.push_back((unsigned)p);
    h.width.push_back(h.width.back() * (uint64_t)p);
  }
  h.D = h.width.back();

  auto word_of = [&](long mm) { return h.entry(mm).word; };
  for (long mm = 0; mm < d; ++mm) {
    long sg;
    const int r = rep_of(mm, sg);
    const auto e = h.entry((uint64_t)mm);
    if (e.representative != rep_entry(r) || e.word != word_of(mm))
      throw sic::Error(sic::ErrorKind::construction,
                       "SICX: entry " + std::to_string(mm) + " does not follow the scalar rule");
  }
  for (int i = 0; i < t; ++i) {
    sic::support::flint::String re(arb_dump_str(acb_realref(path.root[i])));
    sic::support::flint::String im(arb_dump_str(acb_imagref(path.root[i])));
    h.branches.push_back({re.get(), im.get()});
  }
  h.catalogue = sic::support::Json::object_value();
  h.catalogue["order_index"] = sic::support::Json::string_value(std::to_string((*ar_).order_index));
  // Certified bounds: representatives from their recognition height bounds (|x|, |y| <= 2^(B+2)).
  sic::support::resources::BitCount max_bits = 0;
  for (int r = 0; r < representative_count; ++r)
    max_bits = std::max(
        max_bits, std::max<sic::support::resources::BitCount>(
                      (sic::support::resources::BitCount)std::ceil(representative_height(sdt, r)),
                      (sic::support::resources::BitCount)wp) +
                      3);
  h.block_coordinates = uint64_t(1) << sic::sicx::kLogicalLeafLog2Limit;
  sic::support::resources::Bytes tower_bound = 0;
  auto leaves_bits = [](const std::vector<Leaf>& lv,
                        const ExactSource& src) -> sic::support::resources::BitCount {
    if (src) return (sic::support::resources::BitCount)src.max_bits();
    sic::support::resources::BitCount b = 0;
    for (const auto& leaf : lv)
      b = std::max<sic::support::resources::BitCount>(
          b, std::max(fmpz_bits(leaf.x), fmpz_bits(leaf.y)));
    return b;
  };
  for (size_t q = 0; q < items.size(); ++q)
    tower_bound = sic::support::resources::add(
        tower_bound, sicx_element_bound(Ncos[items[q].i], leaves_bits(ilv[q], item_sources[q]),
                                        h.block_coordinates));
  for (auto& ep : el)
    tower_bound = sic::support::resources::add(
        tower_bound,
        sicx_element_bound(Ncos[ep->i], leaves_bits(ep->lv, ep->source), h.block_coordinates));
  sic::support::resources::Bytes reps_bound = 0;
  for (int r = 0; r < representative_count; ++r) {
    const auto& field = scalar_fields->fields[scalar_fields->representative_field[r]];
    const sic::support::resources::Count coordinates =
        2 * (sic::support::resources::Count)field.degree;
    sicx_rep_bounds[r] = sicx_element_bound(
        coordinates,
        std::max<sic::support::resources::BitCount>(
            (sic::support::resources::BitCount)std::ceil(representative_height(sdt, r)),
            (sic::support::resources::BitCount)wp) +
            3,
        h.block_coordinates);
    reps_bound = sic::support::resources::add(reps_bound, sicx_rep_bounds[r]);
  }
  sicx_admit(sicx_dir, "whole file, up-front forecast",
             sic::support::resources::add(
                 tower_bound, sic::support::resources::add(reps_bound, sicx_meta_reserve)));
  sic::sicx::StreamOptions options;
  options.limits = producer_options.policy.resources;
  options.durable = true;
  options.resume = producer_options.resume;
  {
    // Plan: the producer binary (SHA-256 of /proc/self/exe: a resume adopts finished elements,
    // including representatives it does not recompute, so the build must be the same), every
    // output-relevant input (arguments, SIC_* options except threads, memory, progress,
    // stop/resume controls). The exact leaves do not depend on the working precision, so wp
    // is not part of the plan (a kill during an escalated-precision attempt stays resumable).
    // SIC_GROUP_CACHE is bound by its path; its content is a cache checked on load.
    std::string pin =
        "sic-construct plan 5\n";  // 5: canonical CLI arguments; runtime flags excluded
    {
      std::ifstream exe("/proc/self/exe", std::ios::binary);
      const std::vector<uint8_t> image((std::istreambuf_iterator<char>(exe)), {});
      if (!exe.good() && !exe.eof())
        throw sic::Error(sic::ErrorKind::io, "SICX plan: cannot read /proc/self/exe");
      if (image.empty())
        throw sic::Error(sic::ErrorKind::io, "SICX plan: empty producer binary image");
      pin += "binary " + sic::sicx::hex(sic::sicx::sha256(image.data(), image.size())) + "\n";
      pin += std::string("flint ") + flint_version + "\n";  // the dynamically linked library
    }
    pin += "exponent " + std::to_string(producer_options.exponent) + "\n";
    pin += "initial bits " + std::to_string(producer_options.initial_bits) + "\n";
    pin += "seed " + std::to_string(producer_options.seed) + "\n";
    for (const auto& kv : producer_options.plan_environment()) pin += "env " + kv + "\n";
    pin += "generator choice " + component_checkpoints.choice_digest + "\n";
    pin += "rescaling rule " + components.rescaling->name() + "\n";
    if (components.evaluator_family != kDyadicEvaluator)
      pin += "evaluator family " + components.evaluator_family + "\n";
    options.plan = sic::sicx::sha256(reinterpret_cast<const uint8_t*>(pin.data()), pin.size());
  }
  sicx_header = h;
  sicx_options = options;
  sicx_tower_bound = tower_bound;
  sic::support::progressf(
      "SICX output %s: block_coordinates=%llu, tower+action bound %llu bytes, representative "
      "bound %llu bytes "
      "(written after every representative is recognized and the generator rescaling is fixed)\n",
      outp, (unsigned long long)h.block_coordinates, (unsigned long long)tower_bound,
      (unsigned long long)reps_bound);
}
}  // namespace sic::construct

namespace sic::construct {
sic::sicx::LeafSource tower_source_of(std::vector<Leaf>& lv, ExactSource& src) {
  return [&lv, &src](const sic::sicx::LeafVisitor& visit) {
    if (src)
      src.read_blocks(
          [&](size_t start, std::span<Leaf> block) {
            for (size_t q = 0; q < block.size(); ++q) visit(start + q, block[q].x, block[q].y);
          },
          65536);
    else
      for (size_t q = 0; q < lv.size(); ++q) visit(q, lv[q].x, lv[q].y);
  };
}
}  // namespace sic::construct

#include "construct/output/output_plan.tpp"
namespace sic::construct {
void Construction::restore_output_prefix(const sic::sicx::Meta& sicx_header,
                                         const sic::sicx::StreamOptions& sicx_options,
                                         const std::vector<std::unique_ptr<ActionElement>>& el,
                                         std::unique_ptr<sic::sicx::ProducerWriter>& sicx_out,
                                         const std::string& rescaling_note, size_t tower_elements,
                                         size_t& tower_adopted, int representative_count,
                                         std::vector<char>& rep_adopted,
                                         std::vector<uint64_t>& journaled_shifts) {
  sicx_out = open_writer(sicx_header, sicx_options);
  bool in_prefix = true;
  for_each_tower_element(el, [&](const sic::sicx::ElementId& id, const sic::sicx::LeafSource&) {
    if (in_prefix && sicx_out->done(id))
      ++tower_adopted;
    else
      in_prefix = false;
  });
  int reps_adopted = 0;
  if (in_prefix)
    for (int r = 0;
         r < representative_count && sicx_out->done({"rep", (unsigned)t, -1, -1, rep_entry(r)});
         ++r)
      rep_adopted[r] = 1, ++reps_adopted;
  if (const sic::sicx::ByteBuffer* note = sicx_out->writer().note(rescaling_note)) {
    const std::string text(note->begin(), note->end());
    size_t at = 0;
    while (at <= text.size()) {
      const size_t comma = std::min(text.find(',', at), text.size());
      const std::string digits = text.substr(at, comma - at);
      if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos ||
          digits.size() > 9)
        throw sic::Error(sic::ErrorKind::invalid_format,
                         "SICX resume: malformed generator rescaling note");
      journaled_shifts.push_back(sic::support::parse_number<uint64_t>(
          digits, "generator rescaling", sic::ErrorKind::invalid_format, 0, 999999999));
      at = comma + 1;
    }
    if (journaled_shifts.size() != (size_t)t)
      throw sic::Error(sic::ErrorKind::invalid_format,
                       "SICX resume: rescaling note length differs from the tower");
  } else if (tower_adopted || reps_adopted) {
    throw sic::Error(sic::ErrorKind::invalid_format,
                     "SICX resume: adopted elements without a journaled generator rescaling");
  }
  sic::support::progressf(
      "SICX resume: adopted %zu of %zu tower/action elements and %d of %d representatives; %zu "
      "journaled chunks left to replay\n",
      tower_adopted, tower_elements, reps_adopted, representative_count,
      sicx_out->writer().pending_replay());
}
}  // namespace sic::construct
