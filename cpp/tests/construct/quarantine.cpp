// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/quarantine.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
namespace filesystem = std::filesystem;
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void write(const filesystem::path& path, const std::string& text) {
  std::ofstream stream(path);
  stream << text;
  if (!stream) throw std::runtime_error("write failed");
}
std::string read(const filesystem::path& path) {
  std::ifstream stream(path);
  return {std::istreambuf_iterator<char>(stream), {}};
}
int main() {
  std::string temporary = "/tmp/sic-quarantine-test-XXXXXX";
  if (!::mkdtemp(temporary.data())) return 1;
  struct Cleanup {
    filesystem::path p;
    ~Cleanup() {
      std::error_code e;
      filesystem::remove_all(p, e);
    }
  } cleanup{temporary};
  const filesystem::path out = filesystem::path(temporary) / "out.sicx";
  using sic::construct::QuarantineOutput;
  try {
    {
      QuarantineOutput q(out, false);
      write(q.path(), "own");
      q.completed();
    }
    require(!filesystem::exists(out) && !filesystem::exists(out.string() + ".sicx-quarantine"),
            "failed check leaked output");
    {
      QuarantineOutput q(out, false);
      write(q.path(), "external");
    }
    require(read(out.string() + ".sicx-quarantine") == "external", "guard deleted an unowned file");
    {
      QuarantineOutput q(out, true);
      require(read(q.path()) == "external", "resume mutated file before authentication");
    }
    require(filesystem::exists(out.string() + ".sicx-quarantine"),
            "failed resume removed completed file");
    {
      QuarantineOutput q(out, true);
      bool unauthenticated = false;
      try {
        q.prepare_replay(false);
      } catch (const sic::Error& error) {
        unauthenticated = error.kind() == sic::ErrorKind::integrity;
      }
      require(unauthenticated && filesystem::exists(q.path()),
              "unauthenticated replay removed completed file");
      q.prepare_replay(true);
      write(q.path(), "own");
      q.completed();
      bool refused = false;
      try {
        QuarantineOutput collision(q.path(), false);
      } catch (...) {
        refused = true;
      }
      require(refused, "scratch namespace was not reserved");
      write(out, "keep");
      refused = false;
      try {
        q.publish();
      } catch (const sic::Error& error) {
        refused = error.kind() == sic::ErrorKind::io;
      }
      require(refused && read(out) == "keep", "publication clobbered an existing output");
    }
    filesystem::remove(out);
    {
      QuarantineOutput q(out, false);
      write(q.path(), "own");
      q.completed();
      write(filesystem::path(temporary) / "foreign", "foreign");
      filesystem::rename(filesystem::path(temporary) / "foreign", q.path());
      bool refused = false;
      try {
        q.publish();
      } catch (...) {
        refused = true;
      }
      require(refused && !filesystem::exists(out), "publication accepted a replaced private inode");
    }
    require(read(out.string() + ".sicx-quarantine") == "foreign",
            "cleanup deleted a replaced inode");
    filesystem::remove(out);
    filesystem::remove(out.string() + ".sicx-quarantine");
    {
      QuarantineOutput q(out, false);
      write(q.path(), "own");
      q.completed();
      q.publish();
    }
    require(read(out) == "own", "checked inode publication failed");
    filesystem::remove(out);
    filesystem::create_symlink("missing", out.string() + ".sicx-quarantine");
    bool refused = false;
    try {
      QuarantineOutput q(out, true);
    } catch (...) {
      refused = true;
    }
    require(refused && filesystem::is_symlink(out.string() + ".sicx-quarantine"),
            "dangling quarantine symlink accepted or removed");
    filesystem::remove(out.string() + ".sicx-quarantine");
    write(filesystem::path(temporary) / "from", "new");
    write(out, "keep");
    refused = false;
    try {
      sic::support::publish_no_replace(filesystem::path(temporary) / "from", out);
    } catch (...) {
      refused = true;
    }
    require(refused && read(out) == "keep" && read(filesystem::path(temporary) / "from") == "new",
            "no-clobber move lost an inode");
    filesystem::remove(out);
    sic::support::publish_no_replace(filesystem::path(temporary) / "from", out);
    require(read(out) == "new" && !filesystem::exists(filesystem::path(temporary) / "from"),
            "no-clobber move failed");
    std::cout << "quarantine ownership, refusal, replay and pinned publication: PASS\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
