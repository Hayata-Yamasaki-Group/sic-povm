// SPDX-License-Identifier: AGPL-3.0-or-later
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <fstream>
#include <iostream>

#include "construct/components/components.hpp"
#include "construct/model/failure_test_hooks.hpp"
#include "construct/model/memory_plan.hpp"
#include "construct/model/options.hpp"
#include "construct/model/run_services.hpp"
#include "construct/output/reencode.hpp"
#include "construct/output/resumed_representative.hpp"
#include "construct/runtime/command_line.hpp"
#include "construct/runtime/construction.hpp"
#include "construct/runtime/resources/resource_report.hpp"
#include "construct/stages/dimension_two.hpp"
#include "construct/stages/table/analytic_table.hpp"
#include "sicx/container/rescaling_input.hpp"

#ifdef SIC_TEST_WRAP_ENV
namespace {
int environment_error = 0;
bool fail_clear = false, environment_fault_seen = false;
}  // namespace
extern "C" int __real_setenv(const char*, const char*, int);
extern "C" int __wrap_setenv(const char* key, const char* value, int overwrite) {
  if (environment_error && !fail_clear) {
    environment_fault_seen = true;
    errno = environment_error;
    return -1;
  }
  return __real_setenv(key, value, overwrite);
}
extern "C" int __real_unsetenv(const char*);
extern "C" int __wrap_unsetenv(const char* key) {
  if (environment_error && fail_clear) {
    environment_fault_seen = true;
    errno = environment_error;
    return -1;
  }
  return __real_unsetenv(key);
}
#endif

namespace {
using sic::ErrorKind;
using namespace sic::construct;
namespace fs = std::filesystem;
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <class F>
void category(ErrorKind kind, F operation) {
  try {
    operation();
  } catch (const sic::Error& error) {
    require(error.kind() == kind, error.what());
    return;
  }
  throw std::runtime_error("expected categorized failure");
}
void write(const fs::path& path, const std::string& text) {
  std::ofstream file(path);
  file << text;
  file.close();
  require(bool(file), "fixture write");
}
void cli(std::vector<std::string> args) {
  std::vector<char*> argv;
  for (auto& arg : args) argv.push_back(arg.data());
  ProducerCommandLine command(static_cast<int>(argv.size()), argv.data());
}
}  // namespace

int main(int argc, char** argv) {
  require(argc == 3 || argc == 4, "fixture and producer arguments");
  const std::string boundary = argc == 4 ? argv[3] : "";
  std::string temporary = "/tmp/sic-construct-failures-XXXXXX";
  require(mkdtemp(temporary.data()), "temporary directory");
  struct Cleanup {
    fs::path path;
    ~Cleanup() {
      std::error_code error;
      fs::remove_all(path, error);
    }
  } cleanup{temporary};
  const fs::path work(temporary), out = work / "out.sicx", radicals = work / "input.sicrad";
  const std::string long_path = (work / std::string(300, 'x')).string();
  try {
    if (boundary.empty()) {
      for (const char* text : {"", "garbage", "1x", "nan", "inf", "1e999", " 2", "+2"}) {
        category(ErrorKind::invalid_argument, [&] {
          ProducerOptions options(2, {}, {}, sic::support::KernelSettings{{"SIC_SEED_A", text}});
          options.validate_settings();
        });
      }
      // CPU discovery owns platform-record syntax errors; option validation must preserve them.
      {
        const char* previous = std::getenv("SLURM_CPUS_PER_TASK");
        struct RestoreJob {
          std::optional<std::string> value;
          ~RestoreJob() {
            if (value)
              setenv("SLURM_CPUS_PER_TASK", value->c_str(), 1);
            else
              unsetenv("SLURM_CPUS_PER_TASK");
          }
        } restore{previous ? std::optional<std::string>(previous) : std::nullopt};
        require(setenv("SLURM_CPUS_PER_TASK", "bad", 1) == 0, "set malformed job record");
        category(ErrorKind::invalid_format, [] {
          ProducerOptions options(5, {}, {}, sic::support::KernelSettings{});
          options.validate_settings();
        });
      }
      // Validate even settings for dimensions and stages that this run never uses.
      for (const char* key : {"SIC_THREADS",
                              "SIC_SELECTION_THREADS",
                              "SIC_GENERATOR_BLOCK",
                              "SIC_FIRST_PASS_BITS",
                              "SIC_NEWTON_MATRIX_BITS",
                              "SIC_REP_BAND_BITS",
                              "SIC_SEED_A",
                              "SIC_DIRECT_ETA_CHECK",
                              "SIC_REQUIRE_COMPLETED_CACHE",
                              "SIC_ORDER",
                              "SIC_MEMORY_MB",
                              "SIC_KRAWCZYK_FIRST_BITS",
                              "SIC_PARALLEL_GRAIN",
                              "SIC_PHASED_STORAGE",
                              "SIC_STREAM_ACTIONS",
                              "SIC_STREAM_REPS",
                              "SIC_STREAM_STEPS",
                              "SIC_COMPACT_WORK",
                              "SIC_ROOT_BACKING",
                              "SIC_STREAM_TABLES",
                              "SIC_GROUP_RESIDENT_ENTRIES",
                              "SIC_SPOOL_LATTICE",
                              "SIC_SICX_RESUME",
                              "SIC_NEWTON_MEMORY_AUDIT",
                              "SIC_NEWTON_MEMORY_OWNERS",
                              "SIC_NEWTON_PHASE_MEMORY"}) {
        category(ErrorKind::invalid_argument, [&] {
          ProducerOptions options(
              5, {}, {},
              sic::support::KernelSettings{{"SIC_OUT", (work / "unused-stage.sicx").string()},
                                           {"SIC_ENTRY_ARB", "1"},
                                           {key, "garbage"}});
          (void)run(options, {}, {});
        });
      }
      require(!fs::exists(work / "unused-stage.sicx") &&
                  !fs::exists(work / "unused-stage.sicx.partial"),
              "invalid setting began output work");
#ifdef SIC_ENABLE_TEST_HOOKS
      category(ErrorKind::invalid_argument, [&] {
        ProducerOptions options(
            2, {}, {},
            sic::support::KernelSettings{{"SIC_OUT", (work / "entry-group.sicx").string()},
                                         {"SIC_ENTRY_ARB", "1"},
                                         {"SIC_THREADS", "1"},
                                         {"SIC_PHASED_STORAGE", "1"},
                                         {"SIC_COMPACT_WORK", "1"},
                                         {"SIC_STREAM_TABLES", "1"},
                                         {"SIC_STREAM_ACTIONS", "1"},
                                         {"SIC_STREAM_STEPS", "1"},
                                         {"SIC_STREAM_REPS", "1"},
                                         {"SIC_ROOT_BACKING", "1"},
                                         {"SIC_SPOOL_LATTICE", "1"},
                                         {"SIC_NEWTON_MEMORY_OWNERS", "1"},
                                         {"SIC_NEWTON_PHASE_MEMORY", "1"},
                                         {"SIC_REP_BAND_BITS", "0"},
                                         {"SIC_CHECK_ENTRY_GROUPS", "1"},
                                         {"SIC_DEV_SPILL", "1"}});
        (void)run(options, {}, {});
      });

#endif
      for (const char* text : {"0", "-1.5", "2.5e1"}) {
        ProducerOptions options(2, {}, {}, sic::support::KernelSettings{{"SIC_SEED_A", text}});
        require(options.seed_a.get() == std::stod(text), "finite seed multiplier");
      }
      {
        ProducerOptions options(2, {}, {}, sic::support::KernelSettings{});
        require(options.seed_a.get() == 2.0, "default seed multiplier");
      }
      category(ErrorKind::invalid_argument, [] {
        ProducerOptions options(2, {}, {},
                                sic::support::KernelSettings{{"SIC_DIRECT_ETA_CHECK", "2"}});
        (void)options.direct_eta_check.get();
      });
#ifdef SIC_ENABLE_TEST_HOOKS
      category(ErrorKind::invalid_argument, [] {
        ProducerOptions options(
            2, {}, {}, sic::support::KernelSettings{{"SIC_DIRECT_ETA_TEST_FAULT", "other"}});
        (void)options.direct_eta_fault.get();
      });
#endif
      category(ErrorKind::invalid_argument, [] {
        ProducerOptions options(2, {}, {}, sic::support::KernelSettings{{"SIC_ENTRY_ARB", "1"}});
        (void)run(options, {}, {});
      });
      category(ErrorKind::invalid_argument, [&] {
        ProducerOptions options(
            2, {}, {},
            sic::support::KernelSettings{
                {"SIC_ENTRY_ARB", "1"}, {"SIC_OUT", out.string()}, {"SIC_ORDER", "wrong"}});
        (void)run(options, {}, {});
      });
      // Settings rejected by construction owners carry caller-input categories.
      for (const auto& [key, value] :
           std::vector<std::pair<std::string, std::string>>{{"SIC_GENERATOR_BLOCK", "1048577"},
                                                            {"SIC_FIRST_PASS_BITS", "0"},
                                                            {"SIC_ENTRY_ARB", ""}}) {
        category(ErrorKind::invalid_argument, [&] {
          sic::support::KernelSettings settings{
              {"SIC_OUT", out.string()}, {"SIC_ENTRY_ARB", "1"}, {key, value}};
          settings[key] = value;
          if (key == "SIC_ENTRY_ARB") settings.erase(key);
          ProducerOptions options(2, {}, {}, settings);
          (void)run(options, {}, {});
        });
      }
      category(ErrorKind::invalid_argument, [&] {
        ProducerOptions options(
            2, {}, {},
            sic::support::KernelSettings{{"SIC_OUT", out.string()}, {"SIC_ENTRY_ARB", "1"}});
        options.initial_bits = sic::kNativeMaximumPrecision + 1;
        (void)run(options, {}, {});
      });
      category(ErrorKind::io, [&] {
        ProducerOptions options(2, {}, {},
                                sic::support::KernelSettings{{"SIC_OUT", (out / "child").string()},
                                                             {"SIC_ENTRY_ARB", "1"}});
        (void)run(options, {}, {});
      });
#ifdef SIC_ENABLE_TEST_HOOKS
      for (const auto& settings :
           {sic::support::KernelSettings{{"SIC_DEV_KEEP_ALL", "bad"}},
            sic::support::KernelSettings{{"SIC_DEV_KEEP_ALL", "1"}, {"SIC_DEV_SPILL", "1"}}}) {
        sic::support::KernelSettingsScope scope(&settings);
        category(ErrorKind::invalid_argument, [] { (void)memory_control(); });
      }
#endif
      for (const auto& extra : std::vector<sic::support::KernelSettings>{
               {{"SIC_NEWTON_MATRIX_BITS", "1"}},
               {{"SIC_REP_BAND_BITS", "9"}},
               {{"SIC_COMPLETED_CLASS_CACHE", (work / "missing-root").string()}}}) {
        auto settings = extra;
        settings["SIC_ENTRY_ARB"] = "1";
        settings["SIC_THREADS"] = "1";
        settings["SIC_OUT"] = (work / "invalid-setting.sicx").string();
        settings["SIC_PHASED_STORAGE"] = "1";
        ProducerOptions options(extra.contains("SIC_NEWTON_MATRIX_BITS") ||
                                        extra.contains("SIC_TEST_NEWTON_DEFER_CLASS")
                                    ? 4
                                    : 2,
                                {}, {}, settings);
        category(ErrorKind::invalid_argument, [&] { (void)run(options, {}, {}); });
      }
      category(ErrorKind::unsupported, [&] {
        ProducerOptions options(3, {}, {},
                                sic::support::KernelSettings{
                                    {"SIC_OUT", (work / "reduced-cache.sicx").string()},
                                    {"SIC_ENTRY_ARB", "1"},
                                    {"SIC_ORDER", "maximal"},
                                    {"SIC_ANALYTIC_CACHE", (work / "unused-analytic").string()}});
        (void)run(options, {}, {});
      });
      category(ErrorKind::io, [&] {
        ProducerOptions options(
            2, {}, {},
            sic::support::KernelSettings{{"SIC_OUT", (work / "group-refused.sicx").string()},
                                         {"SIC_ENTRY_ARB", "1"},
                                         {"SIC_GROUP_CACHE", long_path},
                                         {"SIC_THREADS", "1"}});
        (void)run(options, {}, {});
      });
#ifdef SIC_ENABLE_TEST_HOOKS
      for (const auto& extra : std::vector<sic::support::KernelSettings>{
               {{"SIC_DEV_KEEP_ALL", "1"},
                {"SIC_COMPLETED_CLASS_CACHE", (work / "keep-root").string()}},
               {{"SIC_DIRECT_ETA_TEST_FAULT", "stop"}},
               {{"SIC_TEST_REP_BAND_BITS", "9"}},
               {{"SIC_TEST_NEWTON_DEFER_CLASS", "999"}}}) {
        auto settings = extra;
        settings["SIC_ENTRY_ARB"] = "1";
        settings["SIC_THREADS"] = "1";
        settings["SIC_OUT"] = (work / "invalid-hook-setting.sicx").string();
        settings["SIC_PHASED_STORAGE"] = "1";
        ProducerOptions options(extra.contains("SIC_TEST_NEWTON_DEFER_CLASS") ? 4 : 2, {}, {},
                                settings);
        category(ErrorKind::invalid_argument, [&] { (void)run(options, {}, {}); });
      }
#endif
      category(ErrorKind::invalid_argument, [] {
        ProducerOptions options(1, {}, {}, sic::support::KernelSettings{});
        (void)run(options, {}, {});
      });
      category(ErrorKind::invalid_argument, [&] { cli({"producer", "1", "--out", ""}); });
      category(ErrorKind::io, [&] { cli({"producer", "1", "--out", long_path}); });
      category(ErrorKind::io, [&] { cli({"producer", "1", "--out", (out / "child").string()}); });
      if (geteuid() != 0) {
        const auto readonly = work / "readonly";
        fs::create_directory(readonly);
        fs::permissions(readonly, fs::perms::owner_read | fs::perms::owner_exec |
                                      fs::perms::group_read | fs::perms::group_exec |
                                      fs::perms::others_read | fs::perms::others_exec);
        category(ErrorKind::io, [&] {
          cli({"producer", "1", "--out", (readonly / "out").string()});
        });
        fs::permissions(readonly, fs::perms::owner_all);
      }
      write(out, "keep");
      category(ErrorKind::io, [&] { cli({"producer", "1", "--out", out.string()}); });
      category(ErrorKind::cancelled, [&] {
        cli({"producer", "1", "--out", (work / "stopped").string(), "--stop-file", out.string()});
      });
      category(ErrorKind::io, [&] {
        cli({"producer", "1", "--out", (work / "stopped").string(), "--stop-file", long_path});
      });
      fs::remove(out);
      write(out.string() + ".partial", "partial");
      category(ErrorKind::io, [&] { cli({"producer", "2", "--out", out.string()}); });
      fs::remove(out.string() + ".partial");
      write(out.string() + ".sicx-quarantine", "completed");
      category(ErrorKind::integrity, [&] {
        cli({"producer", "2", "--out", out.string(), "--resume"});
      });
      fs::remove(out.string() + ".sicx-quarantine");
      fs::create_directory(out.string() + ".sicx-quarantine");
      category(ErrorKind::integrity, [&] {
        cli({"producer", "2", "--out", out.string(), "--resume"});
      });
      fs::remove(out.string() + ".sicx-quarantine");
      {
        const std::string path = (work / std::string(248, 'v')).string();
        std::string expected;
        try {
          (void)fs::exists(path + ".partial");
        } catch (const fs::filesystem_error& error) {
          expected = error.what();
        }
        require(!expected.empty(), "filesystem diagnostic fixture");
        try {
          cli({"producer", "1", "--out", path});
          throw std::runtime_error("expected filesystem translation");
        } catch (const sic::Error& error) {
          require(error.kind() == ErrorKind::io && expected == error.what(),
                  "filesystem translation changed kind or message");
        }
      }
      category(ErrorKind::io, [&] { QuarantineOutput q(work / std::string(240, 'q'), false); });
      {
        QuarantineOutput q(out, false);
        write(q.path(), "checked");
        q.completed();
        fs::create_directory(q.path().string() + ".spool.partial");
        write(fs::path(q.path().string() + ".spool.partial") / "keep", "nonempty");
        category(ErrorKind::io, [&] { q.publish(); });
        require(fs::exists(out), "publication order changed");
      }
      fs::remove(out);
      // A true internal forecast contract keeps its standard type.
      resources::Model model(4, 1, 1, 1, 1);
      bool standard = false;
      try {
        (void)model.tower_phase_forecasts(128, 128);
      } catch (const sic::Error&) {
        throw;
      } catch (const std::runtime_error&) {
        standard = true;
      }
      require(standard, "internal forecast contract was categorized");
      sic::support::resources::Budget budget;
      category(ErrorKind::io,
               [&] { resources::report(model, budget, 128, 1, (long_path + "/out").c_str()); });
      model.representatives = uint64_t{1} << 45;
      category(ErrorKind::resource, [&] { resources::report(model, budget, 128, 1, out.c_str()); });
      bool native_overflow = false;
      try {
        (void)sic::support::resources::mul(UINT64_MAX, 2);
      } catch (const std::overflow_error&) {
        native_overflow = true;
      }
      require(native_overflow, "native overflow type changed");
      bool subtype = false;
      try {
        throw TablePrecisionError(2, 3, 128);
      } catch (const TablePrecisionError& error) {
        subtype = error.kind() == ErrorKind::precision;
      }
      require(subtype, "analytic precision subtype/category");
      ComponentRefusal refusal(2048, "arbitrary diagnostic");
      require(refusal.kind() == ErrorKind::precision && refusal.requested_bits == 2048,
              "component refusal contract");
      category(ErrorKind::io, [&] {
        ProducerOptions options(2, {}, {},
                                sic::support::KernelSettings{{"SIC_OUT", out.string()},
                                                             {"SIC_THREADS", "1"},
                                                             {"SIC_ENTRY_ARB", "1"},
                                                             {"SIC_ORDER", "standard"},
                                                             {"SIC_ANALYTIC_CACHE", long_path}});
        (void)run(options, {}, {});
      });
      require(!fs::exists(out), "failed analytic checkpoint read published output");
      {
        const auto log = work / "cache.log";
        const std::string flags = " --group-cache " + (work / "group").string() +
                                  " --analytic-cache " + (work / "analytic").string() +
                                  " --completed-class-cache " + (work / "completed").string() +
                                  " --memory-mib 4096 --threads 2";
        const std::string producer = "\"" + std::string(argv[2]) + "\" 2";
        auto invoke = [&](const std::string& args) {
          return std::system((producer + args + " >" + log.string() + " 2>&1").c_str());
        };
        require(invoke(" --out " + (work / "seed.sicx").string() + flags) == 0, "cache seed");
        fs::remove_all(work / "analytic");
        fs::create_directory(work / "analytic");
        const auto rejected = work / "cache-refused.sicx";
        const int status =
            invoke(" --out " + rejected.string() + flags + " --require-completed-cache");
        std::ifstream input(log);
        const std::string diagnostic((std::istreambuf_iterator<char>(input)), {});
        require(
            WIFEXITED(status) && WEXITSTATUS(status) == 1 &&
                diagnostic.find("[io] cache-only consumer lost its native analytic checkpoint") !=
                    std::string::npos &&
                !fs::exists(rejected),
            "cache-only missing analytic checkpoint contract");
        bool removed_group = false;
        RunServices services;
        services.progress = [&](const std::string& message) {
          if (!removed_group && message.starts_with("d=")) {
            require(fs::remove(work / "group"), "remove group after context validation");
            removed_group = true;
          }
        };
        ProducerOptions options(2, {}, {},
                                sic::support::KernelSettings{
                                    {"SIC_OUT", (work / "missing-group.sicx").string()},
                                    {"SIC_ENTRY_ARB", "1"},
                                    {"SIC_PHASED_STORAGE", "1"},
                                    {"SIC_STREAM_TABLES", "1"},
                                    {"SIC_GROUP_CACHE", (work / "group").string()},
                                    {"SIC_ANALYTIC_CACHE", (work / "analytic").string()},
                                    {"SIC_COMPLETED_CLASS_CACHE", (work / "completed").string()},
                                    {"SIC_REQUIRE_COMPLETED_CACHE", "1"},
                                    {"SIC_THREADS", "1"}});
        category(ErrorKind::io, [&] { (void)run(options, {}, services); });
        require(removed_group && !fs::exists(work / "missing-group.sicx"),
                "missing group was reconstructed or published");
      }
      sic::support::flint::Fmpq ax, ay, bx, by;
      auto read_radicals = [&] { dimension_two::detail::read_radicals(radicals, ax, ay, bx, by); };
      category(ErrorKind::io, read_radicals);
      category(ErrorKind::io,
               [&] { dimension_two::detail::read_radicals(work.string(), ax, ay, bx, by); });
      write(radicals, "bad\n");
      category(ErrorKind::invalid_format, read_radicals);
      write(radicals, std::string(sic::data::kExactD2RadicalsIdentity) + "\nradicand 3\n");
      category(ErrorKind::invalid_format, read_radicals);
      write(radicals, std::string(sic::data::kExactD2RadicalsIdentity) +
                          "\nradicand 3\nalpha_squared bad 1\nend\n");
      category(ErrorKind::invalid_format, read_radicals);
      for (const auto& record : {"\nend\n", "\nradicand bad\nend\n"}) {
        write(radicals, std::string(sic::data::kExactD2RadicalsIdentity) + record);
        category(ErrorKind::invalid_format, read_radicals);
      }
      write(radicals, std::string(sic::data::kExactD2RadicalsIdentity) + "\nradicand 5\nend\n");
      category(ErrorKind::unsupported, read_radicals);
      write(radicals, std::string(sic::data::kExactD2RadicalsIdentity) +
                          "\nradicand 3\nalpha_squared 1 0\nbeta_squared 1 0\nend\n");
      category(ErrorKind::construction, [&] {
        ProducerOptions options(
            1, {}, {},
            sic::support::KernelSettings{{"SIC_OUT", out.string()},
                                         {"SIC_D2_RADICALS", radicals.string()}});
        (void)run(options, {}, {});
      });
      require(!fs::exists(out), "failed radical check published output");
      write(radicals, std::string(sic::data::kExactD2RadicalsIdentity) +
                          "\nradicand 3\nalpha_squared 1 1/3\nbeta_squared 1/2 -1/6\nend\n");
      category(ErrorKind::construction, [&] { dimension_two::make(out, radicals.string(), ""); });
      require(!fs::exists(out), "failed normalization check published output");
      category(ErrorKind::io, [&] { dimension_two::make(out, "", work.string()); });
      require(!fs::exists(out), "failed ball output published SICX");
      sic::sicx::Reader reader(argv[1]);
      category(ErrorKind::invalid_argument, [&] { reencode(reader, out, 0); });
      {
        sic::sicx::Reader oversized(argv[1]);
        auto& meta = const_cast<sic::sicx::Meta&>(oversized.meta());
        meta.rescaling_shifts.at(0) = 8 * sic::sicx::kMaxIntegerBytes + 1;
        category(ErrorKind::resource, [&] { reencode(oversized, work / "oversized.sicx", 1); });
        require(!fs::exists(work / "oversized.sicx"), "inverse allowance refusal published output");
      }
      {
        const auto producer_path = work / "adopted.sicx";
        const fs::path adopted(producer_path.string() + ".spool");
        sic::sicx::StreamOptions stream_options;
        stream_options.durable = false;
        sic::sicx::ProducerWriter writer(producer_path, reader.meta(), stream_options);
        // Supply the adopted metadata snapshot without recanonicalizing this wire fixture.
        const_cast<sic::sicx::Meta&>(writer.meta()).elements = reader.meta().elements;
        size_t representative = 0;
        for (size_t i = 0; i < reader.meta().elements.size(); ++i)
          if (reader.meta().elements[i].kind == "rep") representative = i;
        for (size_t i = 0; i < reader.index().size(); ++i) {
          const auto& entry = reader.index()[i];
          writer.writer().chunk(entry.type_name(), entry.flags, reader.chunk(i), entry.object,
                                entry.first, entry.count);
        }
        writer.writer().finish();
        const auto& info = reader.meta().elements[representative];
        auto resumed = [&] {
          return ResumedRepresentative(adopted, writer, representative, info.position, 1, 1,
                                       reader.meta().rescaling_shifts);
        };
        auto original = resumed();
        require(!original.blocks.empty(), "adopted block fixture");
        const auto entry = original.blocks.front();
        original.file.close();
        category(ErrorKind::io, [&] {
          auto value = resumed();
          value.file.close();
          (void)value.load();
        });
        fs::resize_file(adopted, entry.offset + sic::sicx::kChunkHeaderBytes + entry.length - 1);
        category(ErrorKind::integrity, [&] {
          auto value = resumed();
          (void)value.load();
        });
        fs::resize_file(adopted, entry.offset + sic::sicx::kChunkHeaderBytes + entry.length);
        category(ErrorKind::integrity, [&] {
          auto value = resumed();
          (void)value.load();
        });
        category(ErrorKind::io, [&] {
          auto value = resumed();
          value.file.close();
          value.file.open(work);
          (void)value.load();
        });
      }
#ifdef SIC_ENABLE_TEST_HOOKS
      {
        require(setenv("SIC_TEST_REP_BAND_BITS", "9", 1) == 0, "set hook option");
        std::vector<std::string> args{"producer", "2", "--out", (work / "hook.sicx").string()};
        std::vector<char*> raw;
        for (auto& arg : args) raw.push_back(arg.data());
        category(ErrorKind::invalid_argument, [&] {
          ProducerCommandLine command(static_cast<int>(raw.size()), raw.data());
          command.install_kernel_options(environ);
        });
        require(unsetenv("SIC_TEST_REP_BAND_BITS") == 0, "clear hook option");
      }
#endif
#ifdef SIC_TEST_WRAP_ENV
      // Both environment operations classify their own immediately captured errno.
      for (bool clearing : {false, true}) {
        for (int error : {ENOMEM, EINVAL, EIO}) {
          require(setenv("SIC_INHERITED_FIXTURE", "1", 1) == 0, "set inherited option");
          std::vector<std::string> args{"producer", "2", "--out", (work / "env.sicx").string()};
          std::vector<char*> raw;
          for (auto& arg : args) raw.push_back(arg.data());
          ProducerCommandLine command(static_cast<int>(raw.size()), raw.data());
          environment_fault_seen = false;
          fail_clear = clearing;
          environment_error = error;
          const auto kind = error == ENOMEM   ? ErrorKind::resource
                            : error == EINVAL ? ErrorKind::invalid_argument
                                              : ErrorKind::io;
          category(kind, [&] { command.install_kernel_options(environ); });
          environment_error = 0;
          require(environment_fault_seen, "environment fault not reached");
          require(unsetenv("SIC_INHERITED_FIXTURE") == 0, "clear inherited option");
        }
      }
#endif
    }
#ifdef SIC_ENABLE_TEST_HOOKS
    if (!boundary.empty()) {
      auto construct = [&](const std::string& name, unsigned exponent = 2) {
        sic::support::KernelSettings settings{{"SIC_OUT", (work / (name + ".sicx")).string()},
                                              {"SIC_ENTRY_ARB", "1"},
                                              {"SIC_PHASED_STORAGE", "1"},
                                              {"SIC_DEV_SPILL", "1"},
                                              {"SIC_THREADS", "2"},
                                              {"SIC_ORDER", "standard"}};
        for (const char* key :
             {"SIC_STREAM_ACTIONS", "SIC_STREAM_REPS", "SIC_STREAM_STEPS", "SIC_COMPACT_WORK",
              "SIC_STREAM_TABLES", "SIC_ROOT_BACKING", "SIC_NEWTON_MEMORY_OWNERS",
              "SIC_NEWTON_PHASE_MEMORY", "SIC_SPOOL_LATTICE"})
          settings[key] = "1";
        ProducerOptions options(exponent, {}, {}, settings);
        return run(options, {}, {});
      };
      if (boundary == "output_resume") {
        int hits = 0;
        failure_test_hooks::resume_shifts = [&](auto& shifts) {
          ++hits;
          shifts = {UINT64_MAX};
        };
        category(ErrorKind::integrity, [&] { (void)construct("resume-mismatch"); });
        require(hits == 1, "resume rescaling boundary not reached");
        failure_test_hooks::resume_shifts = {};
      }
      if (boundary == "output_filesystem") {
        int hits = 0;
        failure_test_hooks::write_attempt = [&] {
          ++hits;
          throw fs::filesystem_error("write attempt fixture",
                                     std::make_error_code(std::errc::io_error));
        };
        category(ErrorKind::io, [&] { (void)construct("write-filesystem"); });
        require(hits == 1, "write attempt boundary not reached");
        failure_test_hooks::write_attempt = {};
      }
      if (boundary == "pairing") {
        // Optional class identification recovers only arithmetic identification refusals.
        for (auto kind : {ErrorKind::cancelled, ErrorKind::io, ErrorKind::integrity}) {
          int hits = 0;
          failure_test_hooks::pairing = [&] {
            ++hits;
            throw sic::Error(kind, "arbitrary cause");
          };
          category(kind, [&] { (void)construct("pairing-refused"); });
          require(hits == 1, "pairing boundary not reached");
        }
        failure_test_hooks::pairing = [] { throw std::bad_alloc(); };
        bool allocated = false;
        try {
          (void)construct("pairing-allocation");
        } catch (const std::bad_alloc&) {
          allocated = true;
        }
        require(allocated, "pairing swallowed allocation failure");
        for (bool typed : {false, true}) {
          failure_test_hooks::pairing = [=] {
            if (typed)
              throw sic::Error(ErrorKind::construction, "arithmetic identification refusal");
            throw std::runtime_error("arithmetic identification refusal");
          };
          require(construct(typed ? "pairing-typed" : "pairing-untyped") == 0, "pairing fallback");
        }
        failure_test_hooks::pairing = [] {
          throw sic::Error(ErrorKind::precision, "identification precision ceiling");
        };
        require(construct("pairing-precision") == 0, "pairing precision fallback");
        failure_test_hooks::pairing = {};
      }
      if (boundary == "coarse") {
        int coarse_calls = 0;
        failure_test_hooks::coarse_table = [&] {
          if (++coarse_calls == 1) throw TablePrecisionError(0, 0, 128);
        };
        require(construct("coarse-initial") == 0 && coarse_calls >= 2, "initial coarse retry");
        coarse_calls = 0;
        failure_test_hooks::coarse_table = [&] {
          if (++coarse_calls == 2) throw TablePrecisionError(0, 0, 128);
        };
        bool rebuild_refused = false;
        failure_test_hooks::park_coarse = true;
        try {
          (void)construct("coarse-rebuild", 4);
        } catch (const TablePrecisionError& error) {
          rebuild_refused = error.kind() == ErrorKind::precision;
        }
        failure_test_hooks::park_coarse = false;
        require(rebuild_refused && coarse_calls == 2,
                "final coarse rebuild must escape without retry");
        failure_test_hooks::coarse_table = {};
      }
      for (auto kind : {ErrorKind::cancelled, ErrorKind::invalid_argument, ErrorKind::io,
                        ErrorKind::integrity, ErrorKind::unsupported, ErrorKind::invalid_format,
                        ErrorKind::precision, ErrorKind::resource, ErrorKind::construction}) {
        if (boundary != std::string("direct_") + sic::error_kind_name(kind)) continue;
        int hits = 0;
        failure_test_hooks::direct_eta = [&] {
          ++hits;
          throw sic::Error(kind, "arbitrary wording");
        };
        const bool fallback = kind == ErrorKind::precision || kind == ErrorKind::resource ||
                              kind == ErrorKind::construction;
        const std::string name = "direct-" + std::to_string(static_cast<int>(kind));
        if (fallback)
          require(construct(name, 4) == 0, "direct evaluation fallback");
        else
          category(kind, [&] { (void)construct(name, 4); });
        require(hits > 0, "direct evaluation boundary not reached");
      }
      if (boundary == "direct_untyped") {
        failure_test_hooks::direct_eta = [] {
          throw std::runtime_error("requested stop is just text");
        };
        require(construct("direct-untyped", 4) == 0,
                "untyped runtime fallback depends on diagnostic text");
      }
      for (bool pairing : {false, true}) {
        if (boundary != (pairing ? "pairing" : "direct_native")) continue;
        failure_test_hooks::direct_eta = {};
        auto& hook = pairing ? failure_test_hooks::pairing : failure_test_hooks::direct_eta;
        for (int fault : {0, 1, 2}) {
          int hits = 0;
          hook = [&] {
            ++hits;
            if (fault == 0) throw std::bad_alloc();
            if (fault == 1) throw std::overflow_error("native overflow fixture");
            throw std::system_error(
                std::make_error_code(std::errc::resource_unavailable_try_again));
          };
          bool caught = false;
          try {
            const int result = construct("standard-cause-" + std::to_string(fault), 4);
            if (!pairing && fault != 0) caught = result == 0;
          } catch (const sic::Error&) {
            throw;
          } catch (const std::bad_alloc&) {
            caught = fault == 0;
          } catch (const std::overflow_error&) {
            caught = pairing && fault == 1;
          } catch (const std::system_error&) {
            caught = pairing && fault == 2;
          }
          require(caught && ((pairing || fault == 0) ? hits == 1 : hits >= 1),
                  "optional optimization native cause policy");
        }
        hook = {};
      }
      failure_test_hooks::direct_eta = {};
    }
#endif
    std::cout << "construction failure categories and preserved standard types: PASS\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
