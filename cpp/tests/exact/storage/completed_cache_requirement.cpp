// SPDX-License-Identifier: AGPL-3.0-or-later
#include <fstream>

#include "assertions.hpp"
#include "exact/storage/completed_class_cache.hpp"
#include "temporary_directory.hpp"

using sic::exact::CompletedCacheRequirement;
namespace {
template <class F>
bool rejects(F&& action) {
  try {
    action();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}
}  // namespace

int main() {
  const auto disabled = CompletedCacheRequirement::parse(nullptr);
  SIC_CHECK(!disabled.enabled);
  SIC_CHECK(!CompletedCacheRequirement::parse("0").enabled);
  const auto strict = CompletedCacheRequirement::parse("1");
  SIC_CHECK(strict.enabled);
  for (const char* invalid : {"", "true", "01", "2", "-1", " 1"})
    SIC_CHECK(rejects([&] { (void)CompletedCacheRequirement::parse(invalid); }));
  disabled.require_context(false, false, nullptr, nullptr, nullptr);
  disabled.enforce(528, 0, 64, 0);  // Disabled mode adds no old-path conditions.
  SIC_CHECK(rejects([&] { strict.require_context(true, true, nullptr, nullptr, nullptr); }));
  SIC_CHECK(rejects([&] { strict.enforce(528, 1, 1, 2); }));
  SIC_CHECK(rejects([&] { strict.enforce(528, 0, 2, 2); }));
  SIC_CHECK(!rejects([&] { strict.enforce(528, 2, 0, 2); }));
  // The same guard is applied again to the inventory of a recognition retry.
  SIC_CHECK(rejects([&] { strict.enforce(656, 0, 2, 2); }));
  SIC_CHECK(rejects([&] { strict.enforce(528, 3, 0, 2); }));
  SIC_CHECK(rejects([&] { strict.enforce(528, 1, 0, 2); }));
  SIC_CHECK(rejects([&] { strict.enforce(528, 0, 0, 0); }));
  sic_test::TemporaryDirectory scratch("sic-cache-only-context");
  const auto work = scratch.path;
  const auto group = (work / "group").string();
  std::ofstream(group) << "context fixture, not a validated Group";
  std::ofstream(group + ".sha256") << "context fixture";
  const auto directory = work.string();
  SIC_CHECK(!rejects([&] {
    strict.require_context(true, true, "explicit-fresh-cache", group.c_str(), directory.c_str());
  }));
  SIC_CHECK(rejects(
      [&] { strict.require_context(false, true, "cache", group.c_str(), directory.c_str()); }));
  SIC_CHECK(rejects(
      [&] { strict.require_context(true, false, "cache", group.c_str(), directory.c_str()); }));
  std::filesystem::remove(group);
  SIC_CHECK(rejects(
      [&] { strict.require_context(true, true, "cache", group.c_str(), directory.c_str()); }));
  return sic_test::report();
}
