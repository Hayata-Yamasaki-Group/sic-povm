// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/data/layout.hpp"

#include "sic/sicx/text.hpp"
namespace sic::data {
void write_exclusive(const std::filesystem::path& path, const std::string& text) {
  support::atomic_write(path, text, false);
}

uint64_t multiplet_from_provenance(const std::string& all, const std::string& conductor) {
  std::set<uint64_t> values;
  size_t at = 0;
  while (at <= all.size()) {
    size_t end = all.find(',', at);
    if (end == std::string::npos) end = all.size();
    values.insert(transport::parse_u64(all.substr(at, end - at)));
    at = end + 1;
  }
  const uint64_t c = transport::parse_u64(conductor);
  if (!values.contains(c))
    sic::sicx::fail(sic::ErrorKind::invalid_format,
                    "the provenance conductor is not in the provenance list");
  return static_cast<uint64_t>(std::distance(values.begin(), values.find(c))) + 1;
}

std::filesystem::path release_layout(const std::filesystem::path& sicx_path,
                                     const std::filesystem::path& root,
                                     const ReleaseOptions& opt) try {
  if (opt.multiplet == 0)
    sic::sicx::fail(sic::ErrorKind::invalid_argument, "a multiplet number N >= 1 is required");
  sic::sicx::Reader reader(sicx_path);
  (void)sic::sicx::verify_all(reader, opt.threads);
  const sic::sicx::Meta& meta = reader.meta();
  const std::string m = "m" + std::to_string(opt.multiplet);
  const std::string prefix = "d" + std::to_string(meta.d) + "-" + m;
  const auto dir = root / ("d" + std::to_string(meta.d)) / m;
  std::filesystem::create_directories(dir);
  if (std::filesystem::exists(dir / "manifest.json") ||
      std::filesystem::exists(dir / "multiplet.sicx.parts.json"))
    sic::sicx::fail(sic::ErrorKind::io, "output exists: " + dir.string());
  std::set<std::string> classes;
  for (size_t ri = 0; ri < meta.rays.size(); ++ri)
    if (!classes.insert(sic::sicx::ray_class(meta.rays[ri].label)).second)
      throw std::logic_error("two rays map to the same class label");
  transport::SplitOptions so;
  so.file = sicx_path;
  so.parts_path = dir / "multiplet.sicx.parts.json";
  so.assets_dir = opt.assets_dir;
  so.prefix = prefix;
  so.repository = opt.repository;
  so.tag = opt.tag;
  so.part_size = opt.part_size;
  const transport::Parts parts = transport::split(so);
  for (size_t ri = 0; ri < meta.rays.size(); ++ri)
    write_exclusive(dir / sic::sicx::ray_file_name(meta, ri),
                    sic::support::dump_json(
                        sic::sicx::ray_json(reader, ri, prefix + ".sicx", parts.sha256, false, m)) +
                        "\n");
  sic::support::Json j = sic::support::Json::object_value();
  j["schema"] = sic::support::Json::string_value("sic-multiplet-manifest/1.0");
  j["id"] = sic::support::Json::string_value(prefix);
  j["d"] = sic::support::Json::string_value(std::to_string(meta.d));
  j["multiplet"] = sic::support::Json::string_value(m);
  sic::support::Json labels = sic::support::Json::object_value();
  for (const auto& [key, value] : meta.catalogue.object)
    if (key != "conductor" && key != "order_index" && key != "multiplet") labels[key] = value;
  j["labels"] = labels;
  sic::support::Json smith = sic::support::Json::array_value();
  for (auto n : meta.smith)
    smith.array.push_back(sic::support::Json::string_value(std::to_string(n)));
  j["smith"] = smith;
  sic::support::Json rays = sic::support::Json::array_value();
  for (size_t ri = 0; ri < meta.rays.size(); ++ri) {
    const auto& r = meta.rays[ri];
    sic::support::Json x = sic::support::Json::object_value();
    x["id"] = sic::support::Json::string_value(sic::sicx::ray_full_id(meta, ri, m));
    x["label"] = sic::support::Json::string_value(sic::sicx::ray_class(r.label));
    x["sicx_ray_label"] = sic::support::Json::string_value(r.label);
    x["exponents"] = sic::support::Json::array_value();
    for (auto w : r.word)
      x["exponents"].array.push_back(sic::support::Json::string_value(std::to_string(w)));
    x["accuracy_bits"] = sic::support::Json::string_value(std::to_string(r.accuracy_bits));
    rays.array.push_back(x);
  }
  j["rays"] = rays;
  sic::support::Json sicx = sic::support::Json::object_value();
  sicx["parts_file"] = sic::support::Json::string_value("multiplet.sicx.parts.json");
  sicx["size_bytes"] = sic::support::Json::string_value(std::to_string(parts.size));
  sicx["sha256"] = sic::support::Json::string_value(parts.sha256);
  sicx["merkle_root"] = sic::support::Json::string_value(sic::sicx::hex(reader.root()));
  sicx["exact_root"] = sic::support::Json::string_value(sic::sicx::hex(reader.exact_root()));
  j["sicx"] = sicx;
  write_exclusive(dir / "manifest.json", sic::support::dump_json(j) + "\n");
  return dir;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(ErrorKind::io, error.what());
}
}  // namespace sic::data
