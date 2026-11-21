// SPDX-License-Identifier: AGPL-3.0-or-later
#include "guard_tests.hpp"

#include "test_context.hpp"
namespace sic_check::verifier::test {
using namespace sic_check::verifier;
using namespace fixture;
void guard_tests(size_t& rejects) {
  using namespace fixture;
  auto no = [&](const string& name, auto operation) {
    bool rejected = false;
    try {
      operation();
    } catch (const Reject&) {
      rejected = true;
    }
    require(rejected, name);
  };
  auto refuse = [&](const string& name, auto operation) {
    bool refused = false;
    try {
      operation();
    } catch (const PrecErr&) {
      refused = true;
    }
    require(refused, name);
  };
  admit_physical_payload(256ull << 20);
  refuse("physical payload admission before allocation",
         [&] { admit_physical_payload((256ull << 20) + 1); });
  admit_rescaling_key((1u << 24) - 1);
  refuse("rescaling table admission before insertion", [&] { admit_rescaling_key(1u << 24); });
  string path = fixtures_dir + "/guard-source.sicx";
  {
    std::ofstream f(path);
    f << "guard fixture";
  }
  no("source fopen failure", [&] {
    File f;
    f.open(path + ".absent");
  });
  for (int fault : {0, 1, 2, 3}) {
    no("source libc failure " + std::to_string(fault), [&] {
      stat_fault = fault == 0;
      seek_fault = fault == 1;
      tell_fault = fault == 2 ? 1 : fault == 3 ? 2 : 0;
      File f;
      f.open(path);
    });
    stat_fault = seek_fault = tell_fault = 0;
  }
  // No trailing NUL or allocator slack can conceal an out-of-span read.
  long page = sysconf(_SC_PAGESIZE);
  void* region =
      mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (region == MAP_FAILED) throw std::runtime_error("guard-page allocation failed");
  if (mprotect(static_cast<char*>(region) + page, page, PROT_NONE))
    throw std::runtime_error("guard-page protection failed");
  for (string s : {"", "[", "[\"x\"", "{", "{\"x\"", "{\"x\":\"y\"", "\"abc", "\"abc\\", "\"\\u",
                   "\"\\u0", "\"\\u00"}) {
    auto* start = static_cast<Uint8*>(region) + page - s.size();
    memcpy(start, s.data(), s.size());
    no("guard-page truncated JSON " + s, [&] {
      JParser parser;
      parser.parse_top(start, s.size());
    });
  }
  munmap(region, 2 * page);
  for (string s : {"{:\"x\"}", "{x\":\"y\"}", "{\"x\",\"y\"}", "\"\\u01ff\""})
    no("JSON punctuation canonical contract",
       [&] { parse_canonical((const Uint8*)s.data(), s.size(), "test"); });
  Jv nonobject;
  nonobject.t = 's';
  nonobject.s = "0";
  no("J_keys object shape", [&] { j_keys(nonobject, {}, "test"); });
  no("J_uvec array shape", [&] { j_uvec(nonobject, "test"); });
  Jv array, item;
  array.t = 'a';
  item.t = 'o';
  item.s = "0";
  array.a.push_back(item);
  no("J_uvec item shape", [&] { j_uvec(array, "test"); });
  for (string value : vector<string>{"", "1", string(64, 'G')}) {
    bool usage = false;
    try {
      expected_hash(value.empty() ? "A" : value, Hash{}, "test");
    } catch (const UsageErr&) {
      usage = true;
    }
    require(usage, "trusted pin spelling");
  }
  string huge((1u << 20) + 1, '1');
  huge += " 0 0 0";
  refuse("Arb string resource cap", [&] {
    Integer a, b, c, d;
    parse_arbdump(huge, a, b, c, d);
  });
  no("original zero pair forbidden in data run", [&] {
    ByteBuffer b{0, 0, 1, 3, 0, 0};
    Integer d0;
    fmpz_set_ui(d0, 5);
    parse_block(b.data(), b.size(), 0, 0, 1, d0, true, [](Uint64, const fmpz_t, const fmpz_t) {});
  });
  auto model = small(4);
  string tower = model.meta["tower"];
  tower.replace(tower.find("128"), 3, "2097152");
  model.meta["tower"] = tower;
  auto out = write(model);
  refuse("branch precision resource cap", [&] {
    Meta m;
    parse_exmt(m, out.file.data() + 32, le64(out.file.data() + 24));
  });
  model = small(4);
  model.change_file = [](ByteBuffer& b) {
    size_t footer = b.size() - 112;
    Uint64 ld = le64(b.data() + footer), pd = le64(b.data() + footer + 16);
    b.insert(b.begin() + ld, 0);
    set64(b, footer + 1, ld + 1);
    set64(b, footer + 17, pd + 1);
    set64(b, footer + 97, b.size());
  };
  require(run(write(model), "guard-unclaimed-byte-before-LDIR") == 2,
          "physical coverage forbids unclaimed byte before directories");
  ++rejects;
  auto full = write(small(4));
  ByteBuffer original_meta(full.file.begin() + 32,
                           full.file.begin() + 32 + le64(full.file.data() + 24));
  model = small(4);
  model.elements.resize(3);
  model.change_exmt = [=](ByteBuffer& b) { b = original_meta; };
  require(run(write(model), "guard-short-LDIR-at-vector-capacity") == 2,
          "missing leaves bounds checked");
  ++rejects;
  model = small(4);
  model.elements.back().leaves.push_back(leaf(model.elements.size() - 1, 1, 1, 0));
  require(run(write(model), "guard-extra-logical-zero-leaf") == 2, "extra logical leaf rejected");
  ++rejects;
  model = small(4, true);
  model.extras[1].payload.push_back(0);
  require(run(write(model), "guard-BALL-surplus-byte") == 2, "BALL rejects surplus payload bytes");
  ++rejects;
  model = small(4);
  model.change_plan = [](auto& plan) { plan.insert(plan.begin(), Physical{"XXXX", 0, {}}); };
  require(run(write(model), "guard-undefined-first-chunk") == 2,
          "undefined type before rank indexing");
  ++rejects;
  model = small((1u << 20) + 1);
  auto large_dimension = write(model);
  no("dimension upper bound with otherwise coherent metadata", [&] {
    Meta parsed;
    parse_exmt(parsed, large_dimension.file.data() + 32, le64(large_dimension.file.data() + 24));
  });
  Real invalid;
  arb_indeterminate(invalid.v);
  refuse("binary64 nonfinite", [&] { rounded_binary64(invalid.v, 512); });
  // A directory opens as a stream on Linux, but reading it fails with EISDIR.
  std::unique_ptr<FILE, decltype(&fclose)> directory(fopen(fixtures_dir.c_str(), "rb"), &fclose);
  if (!directory) throw std::runtime_error("directory stream open failed");
  no("text read I/O error", [&] {
    string line;
    bool nl;
    bounded_text_line(directory.get(), line, nl);
  });
  Container c;
  no("text fopen failure", [&] { text_check(c, path + ".absent"); });
  model = small(4);
  out = write(model);
  run(out, "guard-newton-base");
  Opts options;
  options.path = fixtures_dir + "/guard-newton-base.sicx";
  open_container(c, options);
  verify_chunks(c, options);
  root_fault = 1;
  refuse("exact root isolation API failure", [&] { branch_check_exact(c, options); });
  root_fault = 0;
  Decoder decoder(c);
  decoder.set_prec(512);
  vector<ACB> constant(1);
  acb_one(constant[0].v);
  ACB z, result;
  acb_one(z.v);
  refuse("Newton constant polynomial cannot certify a root",
         [&] { decoder.newton(constant, z.v, result.v); });
  {
    Container irrational;
    Opts low;
    low.path = fixtures_dir + "/general-base-d5.sicx";
    open_container(irrational, low);
    verify_chunks(irrational, low);
    branch_check_exact(irrational, low);
    Decoder base(irrational);
    base.set_prec(64);
    refuse("exact base must fit stored rectangle at decode precision", [&] { base.base(); });
  }
  {
    // Each attempt stops after coordinate0; later default balls are uncertified.
    auto incomplete = small(4, true);
    incomplete.elements[3].fields["scale"] = "\"2\"";
    incomplete.elements[3].fields["max_bits"] = "\"2\"";
    incomplete.elements[3].leaves[0] = leaf(3, 0, 1, 1);
    require(run(write(incomplete), "guard-incomplete-source") == 0,
            "incomplete decode source container");
    Container input;
    Opts check;
    check.decode = "full";
    check.path = fixtures_dir + "/guard-incomplete-source.sicx";
    open_container(input, check);
    verify_chunks(input, check);
    branch_check(input, check);
    finite_fault = 6;
    refuse("retry exhaustion cannot publish incomplete coordinate array",
           [&] { decode_check(input, check); });
    require(finite_fault == 0, "all six failed attempts exercised");
  }
  {
    // Valid 17-degree action weights include128. Two earlier-weight pairs
    // give gain257 versus128. Reconstructed source valuations are128*2^57
    // and511: the earlier greedy shift must be min(2^64, floor(511/128))=3.
    // Recording0 must reject; wrapping the product to0 would invent tightness.
    Container wide;
    wide.M.deg = {17, 17};
    wide.M.shifts = {0, 1ull << 57};
    wide.rs_N = {1, 17};
    wide.rs_gain = {257, 128};
    wide.rs_minv[1ull | (2ull << 40) | (128ull << 46)] = 0;
    wide.rs_minv[(1ull << 40) | (128ull << 46)] = 511;
    no("inverse rescaling refuses wraparound-created tightness",
       [&] { rescaling_check(wide, {}); });
  }
}
}  // namespace sic_check::verifier::test
