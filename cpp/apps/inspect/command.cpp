// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/sha256.hpp"
// sic-inspect FILE --text-json OUT|- [--part-bytes N] [--threads T]
//     canonical text form (JSON Lines, sicx-text-json/1.0); with --part-bytes the
//     stream is split at block boundaries into OUT.part-NNNNN.jsonl + OUT.parts.json
// sic-inspect FILE --json-rays DIR    DIR/c<N>.json (sic-fiducial-json/1.0), one per ray
// (exclusive) sic-inspect FILE --json-all OUT     optional all-in-one JSON for small d (not a
// release file) sic-inspect --exact-json IN.sicx OUT.json [--multiplet mN] [--no-check]
// exact.json (sic-exact-json/1.0), generated on demand, small d only sic-inspect FILE --block I
// text-form line of chunk I only (verified against PDIR and the Merkle root) sic-inspect FILE
// --entry I          PDIR entry I with its RFC 6962 audit path to the Merkle root sic-inspect
// FILE --release-layout ROOT (--multiplet N | --provenance-conductors LIST --provenance-conductor
// C)
//     [--assets-dir DIR] [--release-repo OWNER/REPO] [--release-tag TAG] [--part-size N]
//     ROOT/d<d>/m<N>/{manifest.json, multiplet.sicx.parts.json, c<N>.json}; no SICX, no conductor.
//     N comes from provenance, not from the file.
// sic-inspect --catalogue ROOT        ROOT/catalogue.json (sic-release-catalogue/1.0) rebuilt
// from the tree sic-inspect FILE --info             header, EXMT and the PDIR table The reverse
// direction (text form -> .sicx) is `sic-container from-text`.
#include <chrono>
#ifdef SIC_ENABLE_TEST_HOOKS
#include <cstdlib>
#endif
#include <fstream>
#include <iostream>

#include "command.hpp"
#include "sic/data/layout.hpp"
#include "sic/sicx/exact.hpp"
#include "sic/sicx/text.hpp"
#include "sic/support/cli.hpp"
#include "sic/support/durable_file.hpp"
namespace sic::apps::inspect {
int command(Invocation invocation) try {
  const auto& file = invocation.file;
  flint_set_num_threads(1);
  if (invocation.mode == Mode::text_json) {
    const auto [part_bytes, threads] = invocation.text_options();
    const auto start = std::chrono::steady_clock::now();
    sic::sicx::Reader reader(file);
    const std::string sha = sic::support::sha256::file_hex(file);
    const std::string out = invocation.text_output();
    if (out == "-") {
      std::ios::sync_with_stdio(false);
      sic::sicx::StreamSink sink(std::cout);
      sic::sicx::dump_text_json(reader, sha, sink, threads);
      return 0;
    }
    if (part_bytes) {
      sic::sicx::PartSink sink(out, part_bytes);
      sic::sicx::dump_text_json(reader, sha, sink, threads);
      sink.finish();
    } else {
      if (std::filesystem::exists(out)) throw sic::Error(sic::ErrorKind::io, "output exists");
      std::ofstream output(out, std::ios::binary);
      sic::sicx::StreamSink sink(output);
      sic::sicx::dump_text_json(reader, sha, sink, threads);
      output.close();
      if (!output) throw sic::Error(sic::ErrorKind::io, "write failed");
    }
    std::cerr << "TEXT-JSON " << out << " seconds="
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << '\n';
    return 0;
  }
  if (invocation.mode == Mode::json_rays || invocation.mode == Mode::json_all) {
    const auto multiplet = invocation.json_multiplet();
    sic::sicx::Reader reader(file);
    (void)sic::sicx::verify_all(reader,
                                1);  // the JSON levels are derived from verified BALL records
    const std::string sha = sic::support::sha256::file_hex(file);
    const std::string name = std::filesystem::path(file).filename().string();
    auto write = [](const std::filesystem::path& path, const std::string& text) {
      if (std::filesystem::exists(path))
        throw sic::Error(sic::ErrorKind::io, "output exists: " + path.string());
      try {
        sic::support::atomic_write(path, text, false, 0666);
      } catch (const sic::Error& error) {
        if (error.kind() != ErrorKind::io) throw;
        throw sic::Error(ErrorKind::io, "write failed: " + path.string());
      }
      std::cout << "WROTE " << path.string() << " bytes=" << text.size() << '\n';
    };
    if (invocation.mode == Mode::json_all) {
      write(
          invocation.all_json_output(),
          sic::support::dump_json(sic::sicx::all_in_one_json(reader, name, sha, multiplet)) + "\n");
      return 0;
    }
    const std::filesystem::path dir = invocation.rays_directory();
    std::filesystem::create_directories(dir);
    const bool exact = std::filesystem::exists(dir / "exact.json");
    for (size_t ri = 0; ri < reader.meta().rays.size(); ++ri)
      write(dir / sic::sicx::ray_file_name(reader.meta(), ri),
            sic::support::dump_json(sic::sicx::ray_json(reader, ri, name, sha, exact, multiplet)) +
                "\n");
    return 0;
  }
  if (invocation.mode == Mode::exact_json) {
    auto exact_options = invocation.exact_options();
    const auto& in_path = exact_options.in_path;
    const auto& out_path = exact_options.out_path;
    const auto& options = exact_options.options;
    const auto start = std::chrono::steady_clock::now();
    sic::sicx::Reader reader(in_path);
    (void)sic::sicx::verify_all(reader, 1);
    sic::sicx::exact::DocumentBuilder builder(reader, options);
    // Memory is bounded by the document (about the output size; 28 MB peak RSS for d = 16),
    // durably staged in OUT.partial and published without replacement.
    const std::string text = sic::support::dump_json(builder.build_json(
                                 std::filesystem::path(in_path).filename().string(),
                                 sic::support::sha256::file_hex(in_path))) +
                             "\n";
    if (std::filesystem::exists(out_path)) throw sic::Error(sic::ErrorKind::io, "output exists");
    const std::string partial = std::string(out_path) + ".partial";
    try {
      sic::support::atomic_write(partial, text, true, 0666);
    } catch (const sic::Error& error) {
      if (error.kind() != ErrorKind::io) throw;
      throw sic::Error(ErrorKind::io, "write failed");
    }
#ifdef SIC_ENABLE_TEST_HOOKS
    if (std::getenv("SIC_TEST_INSPECT_EXACT_RENAME_FAIL"))
      throw std::filesystem::filesystem_error("exact export rename", partial, out_path,
                                              std::make_error_code(std::errc::io_error));
#endif
    sic::support::publish_no_replace(partial, out_path);
    sic::support::sync_parent(out_path);
    std::cout << "EXACT-JSON " << out_path << " bytes=" << text.size()
              << " coordinates=" << builder.report().coordinates
              << " numeric_checked=" << builder.report().checked
              << " max_log2_radius=" << builder.report().max_radius_log2 << " seconds="
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << '\n';
    return 0;
  }
  if (invocation.mode == Mode::block || invocation.mode == Mode::entry) {
    const auto i = invocation.chunk_index();
    sic::sicx::Reader reader(file);
    invocation.validate_chunk_index(i, reader.index().size());
    if (invocation.mode == Mode::block) {
      std::cout << sic::sicx::chunk_line(reader, i) << '\n';
      return 0;
    }
    const auto& e = reader.index()[i];
    (void)reader.chunk(i);  // payload hash against PDIR
    sic::support::Json j = sic::support::Json::object_value();
    j["chunk_index"] = sic::support::Json::string_value(std::to_string(i));
    j["chunks"] = sic::support::Json::string_value(std::to_string(reader.index().size()));
    j["type"] = sic::support::Json::string_value(e.type_name());
    for (const auto& [k, v] :
         std::initializer_list<std::pair<const char*, uint64_t>>{{"flags", e.flags},
                                                                 {"offset", e.offset},
                                                                 {"length", e.length},
                                                                 {"object", e.object},
                                                                 {"first", e.first},
                                                                 {"count", e.count}})
      j[k] = sic::support::Json::string_value(std::to_string(v));
    j["sha256"] = sic::support::Json::string_value(sic::sicx::hex(e.sha256));
    j["merkle_root"] = sic::support::Json::string_value(sic::sicx::hex(reader.root()));
    std::cout << sic::support::dump_json(j) << '\n';
    return 0;
  }
  if (invocation.mode == Mode::release_layout) {
    const auto opt = invocation.release_options();
    const auto dir = sic::data::release_layout(file, invocation.release_root(), opt);
    std::cout << "RELEASE " << dir.string() << '\n';
    return 0;
  }
  if (invocation.mode == Mode::catalogue) {
    const auto root = invocation.catalogue_root();
    const auto out = root / "catalogue.json";
    if (std::filesystem::exists(out)) std::filesystem::remove(out);  // regenerated from the tree
    sic::data::write_exclusive(out,
                               sic::support::dump_json(sic::data::release_catalogue(root)) + "\n");
    std::cout << "WROTE " << out.string() << '\n';
    return 0;
  }
  if (invocation.mode == Mode::info) {
    invocation.validate_info();
    sic::sicx::Reader reader(file);
    const auto& m = reader.meta();
    std::cout << "version " << sic::sicx::kMajor << '.' << reader.minor() << "\nd " << m.d
              << "\nD0 " << m.D0 << "\n[L:K] " << m.D << "\nmerkle "
              << sic::sicx::hex(reader.root()) << "\nexact_root "
              << sic::sicx::hex(reader.exact_root()) << "\nfinalized "
              << (reader.finalized() ? "yes" : "no (exact data only)") << "\nEXMT "
              << sic::support::dump_json(reader.meta_json()) << '\n';
    for (size_t i = 0; i < reader.index().size(); ++i) {
      const auto& e = reader.index()[i];
      std::cout << "chunk " << i << ' ' << e.type_name() << " flags=" << e.flags
                << " offset=" << e.offset << " length=" << e.length << " object=" << e.object
                << " first=" << e.first << " count=" << e.count
                << " sha256=" << sic::sicx::hex(e.sha256) << '\n';
    }
    return 0;
  }
  throw sic::support::cli::UsageError("missing output mode");
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::apps::inspect
