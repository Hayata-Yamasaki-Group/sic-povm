// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/output/reencode.hpp"

#include <iostream>

#include "sicx/container/rescaling_input.hpp"
#include "support/sha256.hpp"

namespace filesystem = std::filesystem;
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  int status = 0;
  try {
    const filesystem::path fixture(argv[1]), work(argv[2]);
    filesystem::remove_all(work);
    filesystem::create_directories(work);
    sic::sicx::Reader input(fixture);
    sic::construct::reencode(input, work / "identity.sicx", 2);
    sic::sicx::Reader output(work / "identity.sicx");
    if (input.root() != output.root() || input.exact_root() != output.exact_root() ||
        input.size() != output.size())
      throw std::runtime_error("identity route changed the file");
    // A caller's Reader policy must survive the whole file route. Exercise
    // disabled and raised cache policies without allocating large test inputs.
    for (const uint64_t cache : {uint64_t{0}, uint64_t{512} << 20}) {
      sic::sicx::ResourceLimits limits;
      limits.cache_bytes = cache;
      limits.text_chunk_bytes = uint64_t{1} << 20;
      limits.block_bytes = uint64_t{1} << 20;
      limits.directory_entries = 1024;
      sic::sicx::Reader configured(fixture, limits);
      if (configured.resource_limits().cache_bytes != cache ||
          configured.resource_limits().block_bytes != limits.block_bytes ||
          configured.resource_limits().directory_entries != limits.directory_entries)
        throw std::runtime_error("Reader lost its configured resource policy");
      const auto destination = work / (cache ? "raised-cache.sicx" : "zero-cache.sicx");
      sic::construct::reencode(configured, destination, 2);
      sic::sicx::Reader rebuilt(destination, limits);
      if (rebuilt.root() != input.root() || rebuilt.exact_root() != input.exact_root() ||
          sic::support::sha256::file_hex(destination) != sic::support::sha256::file_hex(fixture))
        throw std::runtime_error("resource policy changed file-route identity");
    }
    bool refused = false;
    sic::sicx::VerificationPolicy invalid_policy;
    invalid_policy.precision.maximum_bits = 128;
    try {
      sic::construct::reencode(input, work / "policy-refused.sicx", 2, invalid_policy);
    } catch (const sic::Error& error) {
      refused = error.kind() == sic::ErrorKind::precision;
    }
    if (!refused || filesystem::exists(work / "policy-refused.sicx"))
      throw std::runtime_error("invalid file-route precision policy was accepted");
    refused = false;
    try {
      sic::construct::reencode(input, work / "workers-refused.sicx", 0);
    } catch (const sic::Error& error) {
      refused = error.kind() == sic::ErrorKind::invalid_argument;
    }
    if (!refused || filesystem::exists(work / "workers-refused.sicx"))
      throw std::runtime_error("zero file-route workers were accepted");
    refused = false;
    setenv("SIC_MEMORY_MB", "1", 1);
    try {
      sic::construct::reencode(input, work / "budget-refused.sicx", 2);
    } catch (const sic::support::resources::AdmissionError&) {
      refused = true;
    }
    unsetenv("SIC_MEMORY_MB");
    if (!refused || filesystem::exists(work / "budget-refused.sicx"))
      throw std::runtime_error("file-route admission did not refuse");
    refused = false;
    try {
      sic::construct::reencode(input, work / "identity.sicx", 2);
    } catch (const std::exception&) {
      refused = true;
    }
    if (!refused) throw std::runtime_error("existing output accepted");
    // Change the exact presentation to have an available dyadic generator reduction,
    // while declaring zero shifts. The shared canonical-C verifier must refuse it.
    auto meta = input.meta();
    std::vector<uint64_t> extra(meta.degrees.size(), 0);
    extra.at(0) = 1;
    meta.rescaling_shifts.assign(meta.degrees.size(), 0);
    for (auto& part : meta.branches.at(0)) {
      sic::support::flint::Arb value;
      if (arb_load_str(value, part.c_str())) throw std::runtime_error("branch load");
      arb_mul_2exp_si(value, value, 1);
      char* text = arb_dump_str(value);
      part = text;
      flint_free(text);
    }
    const auto bad_path = work / "noncanonical.sicx";
    {
      sic::sicx::StreamOptions options;
      options.durable = false;
      sic::sicx::ProducerWriter writer(bad_path, meta, options);
      for (size_t i = 0; i < meta.elements.size(); ++i) {
        const auto id = sic::sicx::element_id_of(meta, meta.elements[i]);
        writer.add(id, sic::sicx::unrescaled_source(
                           meta, id, sic::sicx::stored_element_source(input, i), extra));
      }
      writer.write_meta();
      writer.writer().finish();
    }
    sic::sicx::Reader bad(bad_path);
    const auto root = bad.root();
    refused = false;
    try {
      sic::construct::reencode(bad, work / "refused.sicx", 2);
    } catch (const std::exception& error) {
      if (std::string(error.what()).find("residual generator rescaling is not canonical") ==
          std::string::npos)
        throw;
      refused = true;
    }
    if (!refused || filesystem::exists(work / "refused.sicx") ||
        filesystem::exists(work / "refused.sicx.sicx-quarantine") ||
        sic::sicx::Reader(bad_path).root() != root)
      throw std::runtime_error("noncanonical refusal changed or published a file");
    filesystem::remove_all(work);
    std::cout << "inverse/default-C identity, no-clobber and noncanonical refusal: PASS\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    status = 1;
  }
  flint_cleanup_master();
  return status;
}
