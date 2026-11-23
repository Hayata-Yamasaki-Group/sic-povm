// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/ray_list.hpp"

#include <bit>
#include <sys/stat.h>

#include "sic/sicx/format.hpp"
#include "sic/support/durable_file.hpp"
#include "sic/support/json.hpp"
#include "sic/support/parse_number.hpp"
#include "sicx/model/metadata.hpp"
#include "support/sha256.hpp"

namespace sic::sicx {
namespace {
using sic::support::Json;
namespace resources = sic::support::resources;
constexpr std::string_view kRayListSchema = "sic-ray-list/1.0";
constexpr size_t kRayListBytes = resources::kMib;

bool digest_text(std::string_view text) {
  return text.size() == 64 && text.find_first_not_of("0123456789abcdef") == text.npos;
}

SmithWord parse_word(std::string_view text, size_t rank) {
  SmithWord word;
  for (;;) {
    const auto comma = text.find(',');
    const auto digit = text.substr(0, comma);
    if (word.size() == rank || digit.empty() || (digit.size() > 1 && digit.front() == '0') ||
        digit.find_first_not_of("0123456789") != digit.npos)
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid ray word");
    word.push_back(sic::support::parse_number<long>(digit, "ray word digit",
                                                    sic::ErrorKind::invalid_format, 0));
    if (comma == text.npos) return word;
    text.remove_prefix(comma + 1);
  }
}

Json entry_json(const RayList& list) {
  Json entry;
  entry["smith"] = Json::integer_array(list.smith);
  entry["words"] = Json::array_value();
  for (const auto& word : list.words) {
    std::string text;
    for (long digit : word) {
      if (!text.empty()) text += ',';
      text += std::to_string(digit);
    }
    entry["words"].array.push_back(Json::string_value(std::move(text)));
  }
  return entry;
}
}  // namespace

RayList read_ray_list(const std::filesystem::path& path, const resources::Budget& budget,
                      bool allow_entry, std::string* digest) {
  const sic::support::sha256::Descriptor file(
      ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
  if (file.fd < 0) throw sic::Error(sic::ErrorKind::io, "cannot open ray list: " + path.string());
  struct stat stat {};
  if (::fstat(file.fd, &stat) || !S_ISREG(stat.st_mode) || stat.st_size < 0)
    throw sic::Error(sic::ErrorKind::io, "ray list is not a regular file: " + path.string());
  if (static_cast<uint64_t>(stat.st_size) > kRayListBytes)
    throw sic::Error(sic::ErrorKind::resource, "ray list exceeds 1 MiB");
  const auto bytes = static_cast<size_t>(stat.st_size);
  // Admit input, parser nodes, strings, map entries, and transient vector growth together.
  budget.require_additional("ray list input and JSON",
                            resources::add(4096, resources::mul(bytes, 4 * sizeof(Json) + 129)));
  std::string text(bytes, '\0');
  sic::support::sha256::read_exact(file.fd, 0, text.data(), bytes);
  if (::fstat(file.fd, &stat) || stat.st_size != static_cast<off_t>(bytes))
    throw sic::Error(sic::ErrorKind::io, "ray list changed while reading: " + path.string());
  if (digest) *digest = sic::support::sha256::digest_hex(text);
  const auto entry = sic::support::JsonParser::parse_integer_document(text);
  const bool bound = entry.has("schema");
  RayList list;
  if (bound) {
    entry.keys({"schema", "dimension", "exact_root", "group_source", "labels", "smith", "words"});
    if (entry.at("schema").as_str() != kRayListSchema)
      throw sic::Error(sic::ErrorKind::unsupported, "unsupported ray list schema");
    const auto& dimension = entry.at("dimension");
    list.dimension = dimension.as_uint();
    list.exact_root = entry.at("exact_root").as_str();
    list.source = entry.at("group_source").as_str();
    if (dimension.kind != Json::Kind::integer_value || list.dimension < 2 ||
        !std::has_single_bit(list.dimension) || list.dimension > kMaximumDimension ||
        !digest_text(list.exact_root) ||
        (list.dimension == 2 ? list.source != "closed-form" : !digest_text(list.source)))
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid ray list source binding");
  } else {
    if (!allow_entry)
      throw sic::Error(sic::ErrorKind::invalid_format, "finalization needs a bound ray list");
    entry.keys({"smith", "words"}, {"map", "smithSource"});
  }
  for (const auto& factor : entry.at("smith").as_array())
    if (factor.kind != Json::Kind::integer_value)
      throw sic::Error(sic::ErrorKind::invalid_format, "Smith factors must be JSON integers");
  list.smith = entry.at("smith").as_uints<long>();
  for (const auto& word : entry.at("words").as_array())
    list.words.push_back(parse_word(word.as_str(), list.smith.size()));
  if (bound) {
    const auto& labels = entry.at("labels").as_array();
    if (list.dimension == 2 &&
        (list.smith != std::vector<long>{2} || list.words != std::vector<SmithWord>{{0}} ||
         labels.size() != 1 || labels.front().as_str() != "c0"))
      throw sic::Error(sic::ErrorKind::invalid_format,
                       "closed-form ray list must contain only c0 at word 0 in Smith basis [2]");
    if (list.words.empty() || labels.size() != list.words.size())
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid ray list label count");
    for (size_t i = 0; i < labels.size(); ++i)
      validate_ray_identity(i, labels[i].as_str(), list.words[i], list.smith,
                            i ? &list.words[i - 1] : nullptr);
  }
  return list;
}

std::string ray_entry_json(const RayList& list) {
  return sic::support::dump_integer_json(entry_json(list)) + '\n';
}

std::string bound_ray_list_json(const RayList& list) {
  Json entry = entry_json(list);
  entry["schema"] = Json::string_value(std::string(kRayListSchema));
  entry["dimension"] = Json::integer_value(list.dimension);
  entry["exact_root"] = Json::string_value(list.exact_root);
  entry["group_source"] = Json::string_value(list.source);
  entry["labels"] = Json::array_value();
  for (size_t i = 0; i < list.words.size(); ++i)
    entry["labels"].array.push_back(Json::string_value(ray_label(i)));
  return sic::support::dump_integer_json(entry) + '\n';
}

WrittenRayList write_ray_list(const std::filesystem::path& path, const RayList& list,
                              const resources::Budget& budget, bool resume) {
  const auto text = bound_ray_list_json(list);
  if (text.size() > kRayListBytes)
    throw sic::Error(sic::ErrorKind::resource, "ray list exceeds 1 MiB");
  WrittenRayList written{text.size(), sic::support::sha256::digest_hex(text)};
  const auto type = std::filesystem::symlink_status(path).type();
  if (resume && type != std::filesystem::file_type::not_found) {
    if (type != std::filesystem::file_type::regular)
      throw sic::Error(sic::ErrorKind::io, "resumed ray list is not a regular file");
    std::string digest;
    (void)read_ray_list(path, budget, false, &digest);
    if (digest != written.sha256)
      throw sic::Error(sic::ErrorKind::integrity, "resumed ray list differs from regenerated list");
    return written;
  }
  sic::support::atomic_write(path, text, false);
  return written;
}
}  // namespace sic::sicx
