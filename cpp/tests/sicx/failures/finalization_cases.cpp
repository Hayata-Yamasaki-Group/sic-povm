// SPDX-License-Identifier: AGPL-3.0-or-later
#include "finalization_cases.hpp"

#include <source_location>

#include "../fixtures/fixture.hpp"
#include "sicx/io/io.hpp"

#ifdef SIC_ENABLE_TEST_HOOKS
namespace fs = std::filesystem;
using namespace sic::sicx;
using sic::ErrorKind;

namespace {
void check(bool ok, const std::string& why) {
  if (!ok) throw std::runtime_error(why);
}
template <class F>
void expect(ErrorKind kind, F&& operation,
            const std::source_location& location = std::source_location::current()) {
  try {
    operation();
  } catch (const sic::Error& error) {
    check(error.kind() == kind, std::string("wrong category at ") +
                                    std::to_string(location.line()) + ": " + error.what());
    return;
  }
  throw std::runtime_error("expected categorized failure at " + std::to_string(location.line()));
}

using sicx_tests::read_file;
void copy_fixture(const fs::path& from, const fs::path& to) {
  fs::copy_file(from, to, fs::copy_options::overwrite_existing);
}

fs::path validation_replacement;
std::string replace_on;

void fault(std::string_view operation, const fs::path& target) {
  if (operation == "finalization_validation" && !validation_replacement.empty())
    copy_fixture(validation_replacement, target);
  if (operation == replace_on) {
    fs::rename(target, target.string() + ".held");
    copy_fixture(validation_replacement, target);
  }
}

struct FaultScope {
  FaultScope() { io_fault_hook = fault; }
  ~FaultScope() {
    io_fault_hook = nullptr;
    validation_replacement.clear();
    replace_on.clear();
  }
};
}  // namespace

void finalization_publication_failures(const fs::path& fixture, const fs::path& work) {
  Reader good(fixture);
  sicx_tests::Fixture f(fixture, work / "invariant-fixture");
  auto alternate = f.good;
  auto json = sicx_tests::json(alternate[0]);
  json["elements"].array[0]["max_bits"] =
      sic::support::Json::integer_value(good.meta().elements[0].max_bits + 1);
  sicx_tests::set_json(alternate[0], json);
  validation_replacement = work / "alternate-finalized.sicx";
  sicx_tests::write(validation_replacement, alternate);
  const auto path = work / "invariant-finalize.sicx";
  copy_fixture(fixture, path);
  auto meta = good.meta();
  std::vector<FinalizeRay> rays;
  std::vector<std::vector<BallPart>> balls;
  for (size_t i = 0; i < meta.rays.size(); ++i) {
    const auto& ray = meta.rays[i];
    rays.push_back({ray.label, ray.word, static_cast<int64_t>(ray.anchor)});
    balls.push_back(good.ball_parts(i));
  }
  const auto final = build_finalization(meta, rays, balls, meta.ball_working_bits);
  bool standard = false;
  {
    FaultScope hook;
    try {
      finalize_in_place(path, meta, final, false, good.exact_root());
    } catch (const std::logic_error& error) {
      standard = std::string(error.what()).find("finalization invariant") != std::string::npos;
    }
  }
  validation_replacement.clear();
  check(standard, "finalization invariant lost its standard type");
  check(read_file(path.string() + ".partial") == read_file(work / "alternate-finalized.sicx"),
        "failed invariant removed private evidence");
  check(!fs::exists(path), "failed invariant published output");
  for (const std::string operation :
       {"finalization_before_publish", "finalization_after_publish"}) {
    const auto replaced = work / (operation + ".sicx");
    copy_fixture(fixture, replaced);
    FaultScope hook;
    replace_on = operation;
    validation_replacement = fixture;
    expect(ErrorKind::integrity,
           [&] { (void)finalize_in_place(replaced, meta, final, false, good.exact_root()); });
    replace_on.clear();
    validation_replacement.clear();
    const bool before = operation == "finalization_before_publish";
    check(fs::exists(replaced) != before, "foreign candidate was published before rename");
    const fs::path victim = before ? fs::path(replaced.string() + ".partial") : replaced;
    check(read_file(victim) == read_file(fixture), "replacement evidence was removed");
    check(
        fs::exists(victim.string() + ".held") && fs::exists(replaced.string() + ".partial.journal"),
        "replacement failure discarded recovery evidence");
  }
#if defined(__linux__)
  {
    const auto replaced = work / "finalization-link-race.sicx";
    copy_fixture(fixture, replaced);
    FaultScope hook;
    replace_on = "finalization_publish_link";
    validation_replacement = fixture;
    const auto result = finalize_in_place(replaced, meta, final, false, good.exact_root());
    replace_on.clear();
    validation_replacement.clear();
    Reader published(replaced);
    check(published.root() == result.file_root && published.root() != good.root(),
          "substituted pathname became the published file");
    check(read_file(replaced.string() + ".partial") == read_file(fixture),
          "descriptor publication removed the substituted private name");
  }
#endif
}
#endif
