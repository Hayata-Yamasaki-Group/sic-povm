// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/analytic/step_selection.hpp"

#include <memory>

#include "sic/error.hpp"

namespace sic::exact {
namespace {
constexpr const char* kStepSelectionCheckpoint = "sic-step-selection-checkpoint/1.0";
}
std::vector<std::string> read_step_selection(const char* path, const StepSelectionKey& expected) {
  constexpr size_t kLimit = 1024 * 1024;
  FILE* file = std::fopen(path, "rb");
  if (!file) throw sic::Error(sic::ErrorKind::io, "cannot open step-selection proposal");
  std::unique_ptr<FILE, decltype(&std::fclose)> owner(file, std::fclose);
  std::string data(kLimit + 1, '\0');
  const size_t count = std::fread(data.data(), 1, data.size(), file);
  const bool failed = std::ferror(file);
  owner.reset();
  if (failed) throw sic::Error(sic::ErrorKind::io, "unreadable step-selection proposal");
  if (count > kLimit)
    throw sic::Error(sic::ErrorKind::resource, "oversized step-selection proposal");
  data.resize(count);
  std::istringstream input(data);
  std::string tag, trailing;
  StepSelectionKey actual{};
  if (!(input >> tag >> actual.dimension >> actual.order >> actual.degree >> actual.levels) ||
      tag != kStepSelectionCheckpoint)
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid step-selection proposal metadata");
  if (actual.dimension != expected.dimension || actual.order != expected.order ||
      actual.degree != expected.degree || actual.levels != expected.levels)
    throw sic::Error(sic::ErrorKind::integrity, "step-selection proposal metadata mismatch");
  std::getline(input, trailing);
  if (trailing.find_first_not_of(" \t\r") != std::string::npos)
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid step-selection proposal header");
  std::vector<std::string> names;
  for (int level = 0; level < expected.levels; ++level) {
    std::string name;
    if (!std::getline(input, name) || name.empty() || name.size() > 128 ||
        name.find('\0') != std::string::npos)
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid step-selection proposal candidate");
    names.push_back(std::move(name));
  }
  if (std::getline(input, trailing))
    throw sic::Error(sic::ErrorKind::invalid_format, "trailing step-selection proposal data");
  return names;
}

void write_step_selection(const char* path, const StepSelectionKey& key,
                          const std::vector<std::string>& names) {
  if (key.levels < 0 || names.size() != static_cast<size_t>(key.levels))
    throw sic::Error(sic::ErrorKind::invalid_argument, "invalid step-selection proposal length");
  std::ostringstream text;
  text << kStepSelectionCheckpoint << ' ' << key.dimension << ' ' << key.order << ' ' << key.degree
       << ' ' << key.levels << '\n';
  for (const auto& name : names) {
    if (name.empty() || name.size() > 128 || name.find_first_of("\r\n") != std::string::npos ||
        name.find('\0') != std::string::npos)
      throw sic::Error(sic::ErrorKind::invalid_argument, "invalid step-selection candidate name");
    text << name << '\n';
  }
  FILE* file = std::fopen(path, "wx");
  if (!file)
    throw sic::Error(sic::ErrorKind::io, "cannot exclusively create step-selection proposal");
  std::unique_ptr<FILE, decltype(&std::fclose)> owner(file, std::fclose);
  const auto data = text.str();
  const bool failed = std::fwrite(data.data(), 1, data.size(), file) != data.size();
  const int status = std::fclose(owner.release());
  if (failed || status)
    throw sic::Error(sic::ErrorKind::io, "cannot finish step-selection proposal");
}

}  // namespace sic::exact
