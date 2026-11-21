// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/guarded_writer.hpp"

#include <fstream>
#include <iostream>
namespace filesystem = std::filesystem;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void write(const filesystem::path& path, const char* text) {
  std::ofstream stream(path);
  stream << text;
  if (!stream) throw std::runtime_error("fixture write failed");
}
std::string read(const filesystem::path& path) {
  std::ifstream stream(path);
  return {std::istreambuf_iterator<char>(stream), {}};
}
int main(int argc, char** argv) {
  if (argc != 2) return 1;
  std::string temporary = "/tmp/sic-guarded-writer-test-XXXXXX";
  if (!::mkdtemp(temporary.data())) return 1;
  struct Cleanup {
    filesystem::path path;
    ~Cleanup() {
      std::error_code error;
      filesystem::remove_all(path, error);
    }
  } cleanup{temporary};
  try {
    using namespace sic::construct;
    sic::sicx::Reader fixture(argv[1]);
    Components first_family, second_family;
    CheckpointStore checkpoints;
    second_family.evaluator_family = "sic-test-custom-family/1.0";
    const auto wanted = component_record(first_family, checkpoints);
    const auto wrong = component_record(second_family, checkpoints);
    require(wanted != wrong, "evaluator families have the same resume identity");
    for (int control = 0; control < 4; ++control) {
      const auto out = filesystem::path(temporary) / ("case-" + std::to_string(control) + ".sicx");
      const auto private_path = filesystem::path(out.string() + ".sicx-quarantine");
      sic::sicx::StreamOptions options;
      options.durable = true;
      {
        QuarantineOutput guard(out, false);
        sic::sicx::ProducerWriter writer(private_path, fixture.meta(), options);
        writer.preserve_spool_for_resume();
        if (control) {
          const std::string record = control == 1 ? wrong : wanted;
          writer.writer().put_note("producer components",
                                   sic::sicx::ByteBuffer(record.begin(), record.end()));
        }
        // Simulate completion before guard.completed(), with zero replay chunks.
        write(private_path, "preserve completed file");
      }
      if (control == 3) filesystem::remove(private_path.string() + ".spool.partial");
      QuarantineOutput guard(out, true);
      options.resume = true;
      ProducerWriter component;
      bool refused = false;
      try {
        auto writer = open_guarded_writer(component, guard, fixture.meta(), options, 1, wanted);
      } catch (const std::exception&) {
        refused = true;
      }
      if (control == 2) {
        require(!refused && !filesystem::exists(private_path),
                "authenticated zero-chunk replay did not prepare output");
      } else {
        require(refused && read(private_path) == "preserve completed file",
                "unauthenticated replay removed completed file");
        if (control == 3)
          require(!filesystem::exists(private_path.string() + ".spool.partial"),
                  "missing spool was recreated before refusal");
      }
      require(!filesystem::exists(out), "recovery control published a final output");
    }
    std::cout << "completed replay requires prior spool, journal, plan and component note: PASS\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
