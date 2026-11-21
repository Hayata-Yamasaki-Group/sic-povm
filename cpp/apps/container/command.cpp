// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command_line.hpp"
using sic::apps::container::Invocation;
#include <map>

#include "access/source_arguments.hpp"
#include "construct/output/reencode.hpp"
#include "sic/access/remote.hpp"
#include "sic/sicx/text.hpp"
#include "sic/sicx/verify.hpp"
#include "sic/support/cli.hpp"
#include "sic/support/tool_version.hpp"
#include "sic/transport/parts.hpp"
#include "sicx/container/producer_writer.hpp"
#include "support/sha256.hpp"
#include "transport/fetch.hpp"
#include "transport/repair.hpp"

namespace filesystem = std::filesystem;

namespace {

using Args = sic::support::cli::Arguments;

int cmd_split(sic::transport::SplitOptions o) {
  if (filesystem::exists(o.parts_path))
    throw sic::Error(sic::ErrorKind::io, "refusing to overwrite " + o.parts_path.string());
  const sic::transport::Parts p = sic::transport::split(o);
  std::cout << "split " << o.file.string() << " (" << p.size << " bytes, sha256 " << p.sha256
            << ") into " << p.parts.size() << " part(s); " << o.parts_path.string() << "\n";
  return 0;
}

int cmd_from_text(const std::vector<std::string>& paths) {
  const std::string sha = sic::sicx::text_json_to_binary(paths[0], paths[1]);
  std::cout << "WROTE " << paths[1] << " sha256=" << sha
            << " (byte-identical to the recorded .sicx)" << std::endl;
  return 0;
}

int cmd_merge_parts(const std::vector<std::string>& paths) {
  {
    sic::sicx::StreamWriter writer(paths[0], sic::sicx::StreamOptions{false, false, {}, false});
    for (size_t i = 1; i < paths.size(); ++i) sic::sicx::append_part(writer, paths[i]);
    writer.finish();
  }
  sic::sicx::Reader reader(paths[0]);
  (void)sic::sicx::verify_all(reader, 8);
  std::cout << "MERGED " << paths[0] << " parts=" << paths.size() - 1
            << " sha256=" << sic::support::sha256::file_hex(paths[0])
            << " merkle=" << sic::sicx::hex(reader.root())
            << " exact_root=" << sic::sicx::hex(reader.exact_root()) << std::endl;
  return 0;
}

int cmd_merge_element_parts(const std::vector<std::string>& paths) {
  std::vector<std::filesystem::path> parts(paths.begin() + 1, paths.end());
  const auto root = sic::sicx::merge_element_parts(paths[0], parts);
  sic::sicx::Reader reader(paths[0]);
  (void)sic::sicx::verify_all(reader, 8);
  std::cout << "MERGED " << paths[0] << " parts=" << parts.size()
            << " sha256=" << sic::support::sha256::file_hex(paths[0])
            << " merkle=" << sic::sicx::hex(root) << std::endl;
  return 0;
}

// Writes bytes only to stdout, so this can be piped to cmp, od or a checksum tool.
int cmd_cat_range(Invocation& invocation) {
  auto parsed = invocation.range_input();
  auto& input = parsed.input;
  const auto source = input.open(parsed.path);
  // Opening a Reader preserves the same metadata checks as the decoder.
  sic::sicx::Reader reader(source);
  const auto range = invocation.range();
  const auto offset = range.offset, length = range.size;
  source->plan({&range, 1});
  sic::sicx::ByteBuffer buffer(1u << 20);
  for (uint64_t at = 0; at < length;) {
    const size_t n = static_cast<size_t>(std::min<uint64_t>(buffer.size(), length - at));
    source->read(offset + at, std::span<uint8_t>(buffer).first(n));
    std::cout.write(reinterpret_cast<const char*>(buffer.data()), n);
    if (!std::cout) throw sic::Error(sic::ErrorKind::io, "read-range: output failed");
    at += n;
  }
  if (const auto remote = std::dynamic_pointer_cast<sic::access::RemoteByteSource>(source)) {
    const auto s = remote->stats();
    std::cerr << "REMOTE requests=" << s.requests << " transferred=" << s.transferred
              << " cache_bytes=" << s.cache_bytes << " peak_active=" << s.peak_active
              << " buffer_bound=" << s.buffer_bound << " full_download=" << s.full_download << '\n';
  }
  return 0;
}

}  // namespace

#include "command.hpp"
namespace sic::apps::container {
int command(Invocation invocation) try {
  switch (invocation.mode) {
    case Mode::read_range:
      return cmd_cat_range(invocation);
    case Mode::split:
      return cmd_split(invocation.split_options());
    case Mode::fetch:
      return sic::transport::fetch(invocation.fetch_options());
    case Mode::repair:
      return sic::transport::repair(invocation.repair_job());
    case Mode::from_text:
      return cmd_from_text(invocation.from_text_paths());
    case Mode::merge_parts:
      return cmd_merge_parts(invocation.merge_paths());
    case Mode::merge_element_parts:
      return cmd_merge_element_parts(invocation.merge_paths());
    case Mode::reencode: {
      const auto parsed = invocation.reencode_options();
      const auto jobs = parsed.jobs;
      flint_set_num_threads(jobs);
      sic::sicx::Reader reader(parsed.input);
      const auto policy = invocation.reencode_policy();
      sic::construct::reencode(reader, parsed.output, jobs, policy);
      return 0;
    }
  }
  throw std::logic_error("invalid container command");
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::apps::container
