// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/data/catalogue.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "sic/error.hpp"
#include "sic/support/json.hpp"
#include "sic/support/parse_number.hpp"
#include "support/sha256.hpp"
namespace sic::data {
std::vector<CatalogueEntry> read_catalogue(const std::string& path) try {
  std::ifstream in(path);
  if (!in) throw sic::Error(sic::ErrorKind::io, "cannot open catalogue " + path);
  std::vector<CatalogueEntry> out;
  if (std::filesystem::path(path).extension() == ".json") {
    std::string text;
    try {
      text.assign(std::istreambuf_iterator<char>(in), {});
      if (in.bad()) throw sic::Error(ErrorKind::io, "cannot read catalogue " + path);
    } catch (const std::ios_base::failure& error) {
      throw sic::Error(ErrorKind::io, path + ": " + error.what());
    }
    while (!text.empty() && text.back() == '\n') text.pop_back();
    const auto catalogue = support::JsonParser::parse(text);
    if (catalogue.at("schema").as_str() != "sic-release-catalogue/1.0")
      throw sic::Error(sic::ErrorKind::unsupported, "unsupported release catalogue schema");
    std::set<std::string> labels;
    for (const auto& dim : catalogue.at("dimensions").as_array()) {
      const auto dimension = dim.at("d").as_uint();
      if (dimension < 2 || dimension > (1U << 24) || (dimension & (dimension - 1)))
        throw sic::Error(sic::ErrorKind::invalid_format, "invalid release dimension");
      for (const auto& multiplet : dim.at("multiplets").as_array()) {
        const std::string id = multiplet.at("id").as_str();
        const std::string prefix = "d" + std::to_string(dimension) + "-m";
        if (!id.starts_with(prefix) || id.size() == prefix.size() || id[prefix.size()] == '0' ||
            id.find_first_not_of("0123456789", prefix.size()) != std::string::npos)
          throw sic::Error(sic::ErrorKind::invalid_format, "invalid release multiplet id");
        const std::string directory =
            "d" + std::to_string(dimension) + "/" + id.substr(prefix.size() - 1) + "/";
        for (const auto& ray : multiplet.at("rays").as_array()) {
          const std::string label = ray.at("label").as_str();
          if (label.size() < 2 || label[0] != 'c' ||
              label.find_first_not_of("0123456789", 1) != std::string::npos ||
              (label.size() > 2 && label[1] == '0') || ray.at("id").as_str() != id + "-" + label)
            throw sic::Error(sic::ErrorKind::invalid_format, "invalid release ray id");
          CatalogueEntry e;
          e.d = static_cast<int>(dimension);
          e.label = ray.at("id").as_str();
          e.order = multiplet.at("labels").has("order")
                        ? multiplet.at("labels").at("order").as_str()
                        : "unspecified";
          e.status = multiplet.at("labels").has("status")
                         ? multiplet.at("labels").at("status").as_str()
                         : "unspecified";
          const std::string expected = directory + label + ".csv";
          for (const auto& f : ray.at("files").as_array()) {
            if (f.at("path").as_str() != expected) continue;
            if (!e.csv.empty())
              throw sic::Error(sic::ErrorKind::invalid_format, "duplicate release CSV");
            e.csv = (std::filesystem::absolute(path).parent_path() / expected).string();
          }
          if (e.csv.empty() || !labels.insert(e.label).second)
            throw sic::Error(sic::ErrorKind::invalid_format,
                             "missing CSV or duplicate release ray");
          out.push_back(std::move(e));
        }
      }
    }
    return out;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    CatalogueEntry e;
    std::string extra;
    if (!(ls >> e.d >> e.label >> e.order >> e.status >> e.csv) || (ls >> extra) || e.d < 1)
      throw sic::Error(sic::ErrorKind::invalid_format, path + ": bad catalogue line: " + line);
    out.push_back(e);
  }
  if (in.bad()) throw sic::Error(ErrorKind::io, "cannot read catalogue " + path);
  return out;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(ErrorKind::io, error.what());
}
}  // namespace sic::data

#ifdef SIC_HAVE_FLINT
#include "data/catalogue.hpp"
#include "sic/data/layout.hpp"
namespace sic::data {
sic::support::Json file_entry(const std::filesystem::path& path,
                              const std::filesystem::path& root) try {
  sic::support::Json f = sic::support::Json::object_value();
  f["path"] =
      sic::support::Json::string_value(std::filesystem::relative(path, root).generic_string());
  f["bytes"] = sic::support::Json::string_value(std::to_string(std::filesystem::file_size(path)));
  f["sha256"] = sic::support::Json::string_value(support::sha256::file_hex(path));
  return f;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(ErrorKind::io, error.what());
}

sic::support::Json read_canonical(const std::filesystem::path& p) {
  std::string t = transport::read_text(p);
  while (!t.empty() && t.back() == '\n') t.pop_back();
  return sic::support::JsonParser::parse(t);
}

uint64_t numeric_suffix(const std::string& name, char prefix) {
  if (name.size() < 2 || name[0] != prefix) return UINT64_MAX;
  const std::string rest = name.substr(1);
  if (rest.find_first_not_of("0123456789") != std::string::npos) return UINT64_MAX;
  return support::parse_number<uint64_t>(rest, "catalogue suffix", ErrorKind::invalid_format);
}

sic::support::Json release_catalogue(const std::filesystem::path& root) try {
  namespace filesystem = std::filesystem;
  std::map<uint64_t, std::map<uint64_t, filesystem::path>> tree;
  for (const auto& d : filesystem::directory_iterator(root)) {
    const uint64_t dn = numeric_suffix(d.path().filename().string(), 'd');
    if (!d.is_directory() || dn == UINT64_MAX) continue;
    tree[dn];
    for (const auto& m : filesystem::directory_iterator(d.path())) {
      const uint64_t mn = numeric_suffix(m.path().filename().string(), 'm');
      if (m.is_directory() && mn != UINT64_MAX) tree[dn][mn] = m.path();
    }
  }
  sic::support::Json c = sic::support::Json::object_value();
  c["schema"] = sic::support::Json::string_value("sic-release-catalogue/1.0");
  c["dimensions"] = sic::support::Json::array_value();
  std::string repository, tag;
  for (const auto& [dn, multiplets] : tree) {
    sic::support::Json dim = sic::support::Json::object_value();
    dim["d"] = sic::support::Json::string_value(std::to_string(dn));
    dim["files"] = sic::support::Json::array_value();
    std::vector<filesystem::path> own;
    for (const auto& e : filesystem::directory_iterator(root / ("d" + std::to_string(dn))))
      if (e.is_regular_file()) own.push_back(e.path());
    std::sort(own.begin(), own.end());
    for (const auto& f : own) dim["files"].array.push_back(file_entry(f, root));
    dim["multiplets"] = sic::support::Json::array_value();
    for (const auto& [mn, dir] : multiplets) {
      const sic::support::Json man = read_canonical(dir / "manifest.json");
      if (man.at("schema").as_str() != "sic-multiplet-manifest/1.0")
        sic::sicx::fail(sic::ErrorKind::unsupported, "unsupported multiplet manifest schema");
      const auto parts =
          transport::parse_parts(transport::read_text(dir / "multiplet.sicx.parts.json"));
      const std::string id = "d" + std::to_string(dn) + "-m" + std::to_string(mn);
      if (man.at("id").as_str() != id)
        sic::sicx::fail(sic::ErrorKind::integrity,
                        "manifest id does not match its directory: " + dir.string());
      if (man.at("sicx").at("sha256").as_str() != parts.sha256 ||
          parts.parts.at(0).asset != id + ".sicx.part-0000")
        sic::sicx::fail(sic::ErrorKind::integrity,
                        "manifest and parts file disagree: " + dir.string());
      if (repository.empty()) {
        repository = parts.repository;
        tag = parts.tag;
      }
      if (parts.repository != repository || parts.tag != tag)
        sic::sicx::fail(sic::ErrorKind::integrity, "multiplets name different releases");
      sic::support::Json m = sic::support::Json::object_value();
      m["id"] = sic::support::Json::string_value(id);
      m["labels"] = man.at("labels");
      m["smith"] = man.at("smith");
      sic::support::Json files = sic::support::Json::array_value();
      std::vector<filesystem::path> members;
      for (const auto& e : filesystem::directory_iterator(dir))
        if (e.is_regular_file() && e.path().filename().string().rfind("c", 0) != 0)
          members.push_back(e.path());
      std::sort(members.begin(), members.end());
      for (const auto& f : members) files.array.push_back(file_entry(f, root));
      m["files"] = files;
      if (parts.exact_root != man.at("sicx").at("exact_root").as_str() ||
          parts.file_root != man.at("sicx").at("merkle_root").as_str() ||
          parts.sha256 != man.at("sicx").at("sha256").as_str() ||
          parts.size != man.at("sicx").at("size_bytes").as_uint())
        sic::sicx::fail(sic::ErrorKind::integrity, "catalogue manifest identities disagree");
      sic::support::Json sicx = sic::support::Json::object_value();
      sicx["size_bytes"] = sic::support::Json::string_value(std::to_string(parts.size));
      sicx["sha256"] = sic::support::Json::string_value(parts.sha256);
      sicx["merkle_root"] = man.at("sicx").at("merkle_root");
      sicx["exact_root"] = man.at("sicx").at("exact_root");
      sicx["parts_file"] = sic::support::Json::string_value(
          filesystem::relative(dir / "multiplet.sicx.parts.json", root).generic_string());
      sicx["assets"] = sic::support::Json::array_value();
      for (const auto& p : parts.parts) {
        sic::support::Json a = sic::support::Json::object_value();
        a["asset"] = sic::support::Json::string_value(p.asset);
        a["size_bytes"] = sic::support::Json::string_value(std::to_string(p.size));
        a["sha256"] = sic::support::Json::string_value(p.sha256);
        sicx["assets"].array.push_back(a);
      }
      m["sicx"] = sicx;
      sic::support::Json rays = sic::support::Json::array_value();
      for (const auto& r : man.at("rays").as_array()) {
        sic::support::Json x = r;
        sic::support::Json ray_files = sic::support::Json::array_value();
        const std::string stem = r.at("label").as_str() + ".";
        std::vector<filesystem::path> siblings;
        for (const auto& e : filesystem::directory_iterator(dir))
          if (e.is_regular_file() && e.path().filename().string().rfind(stem, 0) == 0)
            siblings.push_back(e.path());
        std::sort(siblings.begin(), siblings.end());
        for (const auto& s : siblings) ray_files.array.push_back(file_entry(s, root));
        x["files"] = ray_files;
        rays.array.push_back(x);
      }
      m["rays"] = rays;
      dim["multiplets"].array.push_back(m);
    }
    c["dimensions"].array.push_back(dim);
  }
  // Exceptional entries: every ROOT/d<d>/<name>/manifest.json in a folder that is not
  // m<N> (e.g. d8/hoggar). Not Weyl-Heisenberg covariant and counted in no ray total. The key is
  // present only when such a folder exists, so trees without one give the same catalogue as before.
  sic::support::Json exceptional = sic::support::Json::array_value();
  std::vector<filesystem::path> special;
  for (const auto& d : filesystem::directory_iterator(root))
    if (d.is_directory() && numeric_suffix(d.path().filename().string(), 'd') != UINT64_MAX)
      for (const auto& e : filesystem::directory_iterator(d.path()))
        if (e.is_directory() && numeric_suffix(e.path().filename().string(), 'm') == UINT64_MAX &&
            filesystem::exists(e.path() / "manifest.json"))
          special.push_back(e.path());
  std::sort(special.begin(), special.end());
  for (const auto& dir : special) {
    // The Hoggar manifest (sic-hoggar-manifest/1.0) uses JSON booleans, which the canonical reader
    // refuses; they are read as the strings "true"/"false" (only id, d, group, status and
    // provenance are used here).
    std::string text = transport::read_text(dir / "manifest.json");
    while (!text.empty() && text.back() == '\n') text.pop_back();
    for (const std::string lit : {"true", "false"})
      for (size_t at = 0; (at = text.find(":" + lit, at)) != std::string::npos;)
        text.replace(at, lit.size() + 1, ":\"" + lit + "\"");
    const sic::support::Json man = sic::support::JsonParser::parse(text);
    if (man.at("schema").as_str() != "sic-hoggar-manifest/1.0")
      throw Error(ErrorKind::unsupported, "unsupported exceptional manifest schema");
    std::vector<filesystem::path> members;
    for (const auto& e : filesystem::directory_iterator(dir))
      if (e.is_regular_file()) members.push_back(e.path());
    std::sort(members.begin(), members.end());
    for (const auto& r : man.at("rays").as_array()) {
      sic::support::Json x = sic::support::Json::object_value();
      x["id"] = r.at("id");
      x["d"] = man.at("d");
      x["group"] = man.at("group");
      x["wh_covariant"] = sic::support::Json::string_value(
          "false");  // strings: the canonical catalogue JSON has no bare literals
      x["counted_in_ray_total"] = sic::support::Json::string_value("false");
      x["status"] = man.at("status");
      x["provenance"] = man.at("provenance");
      x["manifest"] = sic::support::Json::string_value(
          filesystem::relative(dir / "manifest.json", root).generic_string());
      x["files"] = sic::support::Json::array_value();
      for (const auto& f : members) x["files"].array.push_back(file_entry(f, root));
      exceptional.array.push_back(x);
    }
  }
  if (!exceptional.array.empty()) c["exceptional"] = exceptional;
  sic::support::Json rel = sic::support::Json::object_value();
  rel["repository"] = sic::support::Json::string_value(repository);
  rel["tag"] = sic::support::Json::string_value(tag);
  c["release"] = rel;
  return c;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(ErrorKind::io, error.what());
}
}  // namespace sic::data

#endif
