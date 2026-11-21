// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/io/npz.hpp"
#include "support/sha256.hpp"
#include "temporary_directory.hpp"
// Round-trip tests of the dependency-free interchange writers (npy, MAT v5, SHA-256) against the
// released binary64 CSV files of the given publication directories, plus the WH SIC check.
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "sic/core/interchange.hpp"
#include "sic/core/verify.hpp"

namespace interchange = sic::core::interchange;

int main(int argc, char** argv) {
  int failures = 0;
  auto check = [&](bool ok, const std::string& what) {
    if (!ok) {
      std::cerr << "FAIL: " << what << "\n";
      ++failures;
    }
  };
  sic_test::TemporaryDirectory work("sic-csv");
  const auto csv_path = work.path / "input.csv";
  for (const auto* value : {"nan", "inf", "bad"}) {
    {
      std::ofstream out(csv_path);
      out << "index,real,imag\n0," << value << ",0\n";
    }
    bool rejected = false;
    try {
      interchange::read_csv(csv_path.string());
    } catch (const std::exception& error) {
      rejected = std::string(error.what()).find(csv_path.string()) != std::string::npos;
    }
    check(rejected, std::string("CSV rejected with path: ") + value);
  }
  std::filesystem::remove(csv_path);
  auto malformed_npy = interchange::npy_encode({{1, 0}});
  const std::string header(malformed_npy.begin(), malformed_npy.end());
  const auto shape = header.find("(1,)");
  check(shape != std::string::npos, "NPY shape found");
  if (shape != std::string::npos) {
    malformed_npy[shape + 1] = 'x';
    bool rejected = false;
    try {
      interchange::npy_decode(malformed_npy, "example.npy");
    } catch (const std::exception& error) {
      rejected = std::string(error.what()).find("example.npy") != std::string::npos;
    }
    check(rejected, "NPY numeric error includes path");
  }
  // SHA-256 known answers.
  check(sic::support::sha256::digest_hex(std::string("abc")) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256 abc");
  check(sic::support::sha256::digest_hex(std::string()) ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha256 empty");
  check(sic::support::sha256::digest_hex(std::string(1000, 'a')) ==
            "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3",
        "sha256 1000 a");
  // CRC-32 known answer and ZIP/NPZ container.
  check(interchange::crc32("123456789", 9) == 0xCBF43926u, "crc32 check value");
  std::vector<interchange::NpzRay> bundle;
  std::size_t bundle_d = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string dir = argv[i];
    const sic::core::Vector psi = interchange::read_csv(dir + "/fiducial-fp64.csv");
    const interchange::ByteBuffer npy = interchange::npy_encode(psi);
    check(npy.size() % 16 == 0 && (npy.size() - 16 * psi.size()) % 64 == 0,
          dir + ": npy alignment");
    check(npy[npy.size() - 16 * psi.size() - 1] == '\n', dir + ": npy header newline");
    check(interchange::bit_equal(interchange::npy_decode(npy), psi), dir + ": npy round trip");
    interchange::MatFile m("test");
    m.add_complex_column("psi", psi);
    m.add_scalar("d", static_cast<double>(psi.size()));
    m.add_string("wh_convention", "abc");
    const auto vars = interchange::mat_decode(m.bytes());
    check(m.bytes().size() % 8 == 0, dir + ": mat 8-byte alignment");
    check(interchange::bit_equal(interchange::mat_psi(vars), psi), dir + ": mat round trip");
    check(interchange::mat_find(vars, "wh_convention").text == "abc", dir + ": mat string");
    check(sic::core::verify_fiducial(psi).is_fiducial(1e-12), dir + ": SIC check from CSV");
    std::printf("%s: d=%zu ok\n", dir.c_str(), psi.size());
    if (bundle_d == 0 || bundle_d == psi.size()) {
      bundle_d = psi.size();
      bundle.push_back({"d" + std::to_string(psi.size()) + "-test-c" + std::to_string(i), psi,
                        sic::support::sha256::digest_hex(interchange::npy_encode(psi))});
    }
  }
  if (!bundle.empty()) {
    // Round trip, with the members passed in reverse order (the writer sorts by id).
    std::vector<interchange::NpzRay> rev(bundle.rbegin(), bundle.rend());
    rev.push_back({"d" + std::to_string(bundle_d) + "-test-c0", bundle[0].psi, "00"});
    const interchange::ByteBuffer z1 = interchange::npz_encode(bundle_d, rev),
                                  z2 = interchange::npz_encode(bundle_d, rev);
    check(z1 == z2, "npz determinism");
    check(interchange::npz_encode(bundle_d, {rev.rbegin(), rev.rend()}) == z1,
          "npz independent of input order");
    const interchange::NpzContents back = interchange::npz_decode(z1);
    check(back.arrays.size() == rev.size(), "npz member count");
    for (std::size_t k = 1; k < back.arrays.size(); ++k)
      check(back.arrays[k - 1].id < back.arrays[k].id, "npz sorted members");
    for (const auto& [id, v] : back.arrays)
      for (const auto& r : rev)
        if (r.id == id) check(interchange::bit_equal(v, r.psi), "npz round trip " + id);
    check(back.comment.find("\"schema\":\"sic-npz-bundle/1.0\"") != std::string::npos &&
              back.comment.find(rev[0].source) != std::string::npos,
          "npz comment");
    // Preserve a valid ZIP while replacing only the JSON format identity.
    for (const std::string identity :
         {"sic-npz-bundle/1", "sic-npz-bundle/1.0.0"}) {  // identity-negative
      auto old_comment = back.comment;
      old_comment.replace(old_comment.find("sic-npz-bundle/1.0"),
                          std::string("sic-npz-bundle/1.0").size(), identity);
      std::vector<interchange::ZipMember> members;
      for (const auto& [id, values] : back.arrays)
        members.push_back({id + ".npy", interchange::npy_encode(values)});
      bool refused = false;
      try {
        interchange::npz_decode(interchange::zip_store_encode(members, old_comment));
      } catch (const std::runtime_error&) {
        refused = true;
      }
      check(refused, "npz unsupported comment identity rejected");
    }
    // fixed timestamp 1980-01-01 in the first local header, store method
    check(z1[8] == 0 && z1[9] == 0 && z1[10] == 0 && z1[11] == 0 && z1[12] == 0x21 && z1[13] == 0,
          "npz fixed DOS time/date and method");
    // corrupt one data byte (inside the first member's payload) -> CRC mismatch must be rejected
    interchange::ByteBuffer bad = z1;
    bad[30 + back.arrays[0].id.size() + 100] ^= 1;  // inside the first member's npy payload
    bool rejected = false;
    try {
      interchange::npz_decode(bad);
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    check(rejected, "npz corrupt data byte rejected");
    interchange::ByteBuffer trunc(z1.begin(), z1.end() - 5);
    rejected = false;
    try {
      interchange::npz_decode(trunc);
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    check(rejected, "npz truncated archive rejected");
    // compressed method flag is refused
    interchange::ByteBuffer meth = z1;
    meth[8] = 8;
    rejected = false;
    try {
      interchange::npz_decode(meth);
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    check(rejected, "npz non-stored method rejected");
    // duplicate ids and unsafe ids refused by the writer
    rejected = false;
    try {
      interchange::npz_encode(bundle_d, {bundle[0], bundle[0]});
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    check(rejected, "npz duplicate id rejected");
    rejected = false;
    try {
      auto r = bundle[0];
      r.id = "../x";
      interchange::npz_encode(bundle_d, {r});
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    check(rejected, "npz unsafe id rejected");
    if (const char* out = std::getenv("SIC_NPZ_DUMP")) interchange::write_file(out, z1);
  }
  return failures ? 1 : 0;
}
