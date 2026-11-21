// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_context.hpp"
namespace sic_check::verifier::test {
using namespace sic_check::verifier;
using namespace fixture;
size_t checks = 0;

string fixtures_dir;

void require(bool ok, const string& what) {
  ++checks;
  if (!ok) throw std::runtime_error("TEST FAILED: " + what);
}

int run(const fixture::Output& out, const string& name, Opts opt, bool numerical) {
  string p = fixtures_dir + "/" + name + ".sicx";
  std::ofstream f(p, std::ios::binary);
  f.write((const char*)out.file.data(), out.file.size());
  f.close();
  opt.path = p;
  try {
    Container c;
    open_container(c, opt);
    verify_chunks(c, opt);
    if (c.all_exact) {
      rescaling_check(c, opt);
      branch_check(c, opt);
    }
    if (opt.partial_elements.empty()) {
      if (numerical) decode_check(c, opt);
      if (!opt.text_path.empty()) text_check(c, opt.text_path);
    }
    return 0;
  } catch (const Reject& e) {
    say("EXPECTED/OBSERVED REJECT %s: %s\n", name.c_str(), e.what());
    return 2;
  } catch (const PrecErr& e) {
    say("OBSERVED REFUSAL %s: %s\n", name.c_str(), e.what());
    return 3;
  }
}

void bad(size_t& rejects, const string& name, fixture::Model m) {
  require(run(write(m), name) == 2, name);
  ++rejects;
}

void setup_inverse(Model& m) {
  m.elements.clear();
  element(m, "wsq", 0, 1, 4);
  element(m, "wsq", 1, 1, 6);
  element(m, "rep", 0, 1, 1, {"0", "1"}, 4);
  m.elements[2].leaves[0] = {2, 0, 4, 6, 3, 2, 1, 0};
  for (unsigned r = 1; r <= m.d / 2; r *= 2) element(m, "rep", r, 0);
  m.meta["tower"] =
      "{\"branch_bits\":\"128\",\"branches\":[[\"1 1 0 0\",\"0 0 0 "
      "0\"],[\"1bb67ae8584caa73b25742d7078b83b89 -80 1 -80\",\"0 0 0 "
      "0\"]],\"degrees\":[\"2\",\"2\"]}";
  m.meta["galois"] = galois(m.d, {"2", "2"});
  m.meta["generator_rescaling"] = "{\"rule\":\"sicx-2adic-greedy/1.0\",\"shifts\":[\"0\",\"1\"]}";
}

auto DimensionContext::mutate(string name, std::function<void(Model&)> change, bool final) -> void {
  auto m = small(d, final);
  change(m);
  bad(rejects, pre + name, m);
}

auto DimensionContext::meta(string name, string key, string value) -> void {
  auto m = small(d);
  m.meta[key] = value;
  auto out = write(m);
  bool rejected = false;
  try {
    Meta parsed;
    parse_exmt(parsed, out.file.data() + 32, le64(out.file.data() + 24));
  } catch (const Reject&) {
    rejected = true;
  }
  if (name != "branch-bits-256" && !name.starts_with("branch-dump-"))
    require(rejected, "EXMT parser contract " + name);
  bad(rejects, pre + name, m);
}

auto DimensionContext::descriptor_reject(string name, Model m) -> void {
  auto data = write(m);
  bool rejected = false;
  try {
    Meta parsed;
    parse_exmt(parsed, data.file.data() + 32, le64(data.file.data() + 24));
  } catch (const Reject&) {
    rejected = true;
  }
  require(rejected, "element metadata parser contract " + name);
}

auto DimensionContext::raysbad(string name, string from, string to) -> void {
  mutate(
      name,
      [=](Model& m) {
        string json(m.extras[0].payload.begin(), m.extras[0].payload.end());
        size_t at = json.find(from);
        if (at == string::npos) throw std::runtime_error("bad test replacement");
        json.replace(at, from.size(), to);
        m.extras[0].payload = bytes(json);
      },
      true);
}

auto DimensionContext::resource(string name, Model m) -> void {
  require(run(write(m), pre + name) == 3, name);
  std::filesystem::remove(fixtures_dir + "/" + pre + name + ".sicx");
}

auto DimensionContext::partial_bad(string name, Model m) -> void {
  require(run(write(m), pre + name, Opts{.partial_elements = {2}}) == 2, name);
  ++rejects;
}
}  // namespace sic_check::verifier::test
