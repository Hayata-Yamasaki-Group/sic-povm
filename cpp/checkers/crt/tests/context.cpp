// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"

#include "test_general.hpp"
namespace sic_check::crt::test {
namespace filesystem = std::filesystem;
using namespace sic_check::crt::sicx;
size_t passed = 0;

filesystem::path scratch;

void require(bool p, const string& msg) {
  if (!p) throw std::runtime_error(msg);
}

string save(const fixture::ByteBuffer& bytes) {
  auto p = scratch / "input.sicx";
  std::ofstream f(p, std::ios::binary);
  f.write((const char*)bytes.data(), bytes.size());
  f.close();
  require(bool(f), "fixture write failed");
  return p.string();
}

void positive(const string& name, const fixture::ByteBuffer& bytes) {
  Input in;
  try {
    in.open(save(bytes));
    require(in.scope.full, "full scope missing");
    require(in.file_sha == fixture::digest(bytes), "whole-file OpenSSL oracle");
    ++passed;
  } catch (const std::exception& e) {
    throw std::runtime_error(name + ": " + e.what());
  }
}

void negative(const string& name, const fixture::ByteBuffer& bytes, const string& diagnostic) {
  try {
    Input in;
    in.open(save(bytes));
  } catch (const Reject& e) {
    static std::map<string, size_t> occurrence;
    const auto& expected = kRejectionDiagnostics.at(name);
    size_t index = occurrence[name]++;
    require(index < expected.size() && expected[index] == e.what(),
            name + ": intended rejection category changed: " + e.what());
    require(diagnostic.empty() || string(e.what()).find(diagnostic) != string::npos,
            name + ": wrong rejection: " + e.what());
    ++passed;
    return;
  }
  throw std::runtime_error(name + ": invalid file accepted or wrong exception class");
}

void refusal(const string& name, const fixture::ByteBuffer& bytes,
             const std::function<void(Input&)>& change, const string& diagnostic) {
  try {
    Input in;
    change(in);
    in.open(save(bytes));
  } catch (const Refuse& e) {
    require(diagnostic.empty() || string(e.what()).find(diagnostic) != string::npos,
            name + ": wrong refusal: " + e.what());
    ++passed;
    return;
  }
  throw std::runtime_error(name + ": missing resource refusal");
}

void mutation(const string& name, const std::function<void(fixture::File&)>& change, bool finalized,
              const string& diagnostic) {
  fixture::File f(4, finalized);
  change(f);
  negative(name, f.encode(), diagnostic);
}

void meta(const string& name, const string& a, const string& b) {
  mutation(name, [&](auto& f) { fixture::replace(f.meta, a, b); });
}

void raymeta(const string& name, const string& a, const string& b) {
  mutation(
      name,
      [&](auto& f) {
        auto s = string(f.physical[f.physical.size() - 2].bytes.begin(),
                        f.physical[f.physical.size() - 2].bytes.end());
        fixture::replace(s, a, b);
        f.physical[f.physical.size() - 2].bytes = fixture::text(s);
      },
      true);
}
}  // namespace sic_check::crt::test
