// SPDX-License-Identifier: AGPL-3.0-or-later
#include "transport/parts.hpp"

#include "sic/support/durable_file.hpp"
#include "sic/support/parse_number.hpp"
#include "sic/transport/parts.hpp"
#include "sicx/codec/integer.hpp"
#include "sicx/container/verify.tpp"
#include "sicx/io/io.hpp"
#include "support/io/write_all.hpp"
#include "support/sha256.hpp"

extern char** environ;

namespace sic::transport {
namespace {
constexpr const char* kChunkAlignment = "sicx-chunks/1.0";
}
namespace {
class PartDescriptor {
 public:
  explicit PartDescriptor(int fd) noexcept : fd_(fd) {}
  ~PartDescriptor() { close(); }
  PartDescriptor(const PartDescriptor&) = delete;
  PartDescriptor& operator=(const PartDescriptor&) = delete;
  int get() const noexcept { return fd_; }
  void close() noexcept {
    if (fd_ >= 0) ::close(std::exchange(fd_, -1));
  }

 private:
  int fd_;
};
}  // namespace

bool is_hex64(const std::string& s) {
  return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}
}  // namespace sic::transport

namespace sic::transport {
bool word(const std::string& s, const char* extra) {
  return !s.empty() &&
         s.find_first_not_of(
             std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") +
             extra) == std::string::npos;
}
}  // namespace sic::transport

namespace sic::transport {
bool valid_prefix(const std::string& p) {
  const size_t m = p.find("-m");
  auto digits = [](const std::string& t) {
    return !t.empty() && t.size() <= 18 && t.find_first_not_of("0123456789") == std::string::npos &&
           t[0] != '0';
  };
  return p.size() > 3 && p[0] == 'd' && m != std::string::npos && digits(p.substr(1, m - 1)) &&
         digits(p.substr(m + 2));
}
}  // namespace sic::transport

namespace sic::transport {
std::string asset_name(const std::string& prefix, uint64_t i) {
  char suffix[32];
  std::snprintf(suffix, sizeof suffix, ".sicx.part-%04llu", static_cast<unsigned long long>(i));
  return prefix + suffix;
}
}  // namespace sic::transport

namespace sic::transport {
uint64_t parse_u64(const std::string& s) { return parse_u64(s, ErrorKind::invalid_argument); }

uint64_t parse_u64(const std::string& s, ErrorKind kind) {
  if (s.empty() || s.size() > 19 || s.find_first_not_of("0123456789") != std::string::npos)
    sic::sicx::fail(kind, "bad number: " + s);
  return support::parse_number<uint64_t>(s, "transport number", kind);
}
}  // namespace sic::transport

namespace sic::transport {
uint64_t number(const sic::support::Json& j) {
  const auto& s = j.as_str();
  if (s.empty() || s.size() > 20 || (s.size() > 1 && s[0] == '0'))
    sic::sicx::fail(sic::ErrorKind::invalid_format, "parts noncanonical number");
  return support::parse_number<uint64_t>(s, "parts integer", ErrorKind::invalid_format);
}
}  // namespace sic::transport

namespace sic::transport {
sic::support::Json parts_to_json(const Parts& p) {
  sic::support::Json j = sic::support::Json::object_value();
  j["schema"] = sic::support::Json::string_value(kSchema);
  j["alignment"] = sic::support::Json::string_value(kChunkAlignment);
  j["exact_root"] = sic::support::Json::string_value(p.exact_root);
  j["file_root"] = sic::support::Json::string_value(p.file_root);
  j["file"] = sic::support::Json::string_value(kLogicalName);
  j["size_bytes"] = sic::support::Json::string_value(std::to_string(p.size));
  j["sha256"] = sic::support::Json::string_value(p.sha256);
  j["part_size_bytes"] = sic::support::Json::string_value(std::to_string(p.part_size));
  sic::support::Json rel = sic::support::Json::object_value();
  rel["repository"] = sic::support::Json::string_value(p.repository);
  rel["tag"] = sic::support::Json::string_value(p.tag);
  j["release"] = rel;
  sic::support::Json ps = sic::support::Json::array_value();
  for (const auto& x : p.parts) {
    sic::support::Json e = sic::support::Json::object_value();
    e["index"] = sic::support::Json::string_value(std::to_string(x.index));
    e["asset"] = sic::support::Json::string_value(x.asset);
    e["offset"] = sic::support::Json::string_value(std::to_string(x.offset));
    e["size_bytes"] = sic::support::Json::string_value(std::to_string(x.size));
    e["sha256"] = sic::support::Json::string_value(x.sha256);
    ps.array.push_back(e);
  }
  j["parts"] = ps;
  return j;
}
}  // namespace sic::transport

namespace sic::transport {
Parts parse_parts(std::string text) {
  if (text.size() > sic::sicx::kMaxTextChunk)
    sic::sicx::fail(sic::ErrorKind::resource, "transport manifest resource refusal");
  while (!text.empty() && text.back() == '\n') text.pop_back();
  const sic::support::Json j =
      sic::support::JsonParser::parse(text);  // canonical JSON: sorted keys, no whitespace
  j.keys({"schema", "alignment", "exact_root", "file_root", "file", "size_bytes", "sha256",
          "part_size_bytes", "release", "parts"});
  if (j.at("alignment").as_str() != kChunkAlignment)
    sic::sicx::fail(sic::ErrorKind::invalid_format, "parts alignment");
  if (j.at("schema").as_str() != kSchema)
    sic::sicx::fail(sic::ErrorKind::unsupported, "unsupported parts schema");
  if (j.at("file").as_str() != kLogicalName)
    sic::sicx::fail(sic::ErrorKind::invalid_format, "parts file: file must be multiplet.sicx");
  Parts p;
  p.size = number(j.at("size_bytes"));
  p.part_size = number(j.at("part_size_bytes"));
  p.sha256 = j.at("sha256").as_str();
  p.exact_root = j.at("exact_root").as_str();
  p.file_root = j.at("file_root").as_str();
  const auto& rel = j.at("release");
  rel.keys({"repository", "tag"});
  p.repository = rel.at("repository").as_str();
  p.tag = rel.at("tag").as_str();
  if (!is_hex64(p.sha256) || !is_hex64(p.exact_root) || !is_hex64(p.file_root) || p.size == 0 ||
      p.part_size != kPartSize)
    sic::sicx::fail(sic::ErrorKind::invalid_format, "parts file: bad size or hash");
  const size_t slash = p.repository.find('/');
  if (slash == std::string::npos || !word(p.repository.substr(0, slash), "._-") ||
      !word(p.repository.substr(slash + 1), "._-") || !word(p.tag, "._-"))
    sic::sicx::fail(sic::ErrorKind::invalid_format, "parts file: bad release repository or tag");
  const auto& ps = j.at("parts").as_array();
  if (ps.empty()) sic::sicx::fail(ErrorKind::invalid_format, "empty parts list");
  if (ps.size() > kMaxParts)
    sic::sicx::fail(sic::ErrorKind::resource, "parts file: wrong number of parts");
  std::string prefix;
  uint64_t at = 0;
  for (size_t i = 0; i < ps.size(); ++i) {
    ps[i].keys({"index", "asset", "offset", "size_bytes", "sha256"});
    Part x;
    x.index = number(ps[i].at("index"));
    x.offset = number(ps[i].at("offset"));
    x.size = number(ps[i].at("size_bytes"));
    x.sha256 = ps[i].at("sha256").as_str();
    x.asset = ps[i].at("asset").as_str();
    if (x.index != i)
      sic::sicx::fail(sic::ErrorKind::invalid_format,
                      "parts file: parts are not indexed 0..n-1 in order");
    if (x.offset != at)
      sic::sicx::fail(sic::ErrorKind::invalid_format, "parts offsets have gaps or overlaps");
    if (!x.size || x.size > p.part_size || at > p.size || x.size > p.size - at)
      sic::sicx::fail(sic::ErrorKind::invalid_format, "part size out of bounds");
    if (!is_hex64(x.sha256))
      sic::sicx::fail(sic::ErrorKind::invalid_format, "parts file: bad part hash");
    if (i == 0) {
      const size_t cut = x.asset.find(".sicx.part-");
      if (cut == std::string::npos)
        sic::sicx::fail(sic::ErrorKind::invalid_format, "parts file: bad asset name");
      prefix = x.asset.substr(0, cut);
      if (!valid_prefix(prefix))
        sic::sicx::fail(sic::ErrorKind::invalid_format,
                        "parts file: asset prefix must be d<d>-m<N>");
    }
    if (x.asset != asset_name(prefix, i))
      sic::sicx::fail(sic::ErrorKind::invalid_format,
                      "parts file: asset name is not d<d>-m<N>.sicx.part-NNNN");
    at += x.size;
    p.parts.push_back(std::move(x));
  }
  if (at != p.size)
    sic::sicx::fail(sic::ErrorKind::invalid_format,
                    "parts file: part sizes do not sum to size_bytes");
  return p;
}
}  // namespace sic::transport

namespace sic::transport {
const Part& part_at(const Parts& p, uint64_t offset) {
  if (offset >= p.size)
    sic::sicx::fail(sic::ErrorKind::invalid_argument, "part lookup outside file");
  auto it = std::upper_bound(p.parts.begin(), p.parts.end(), offset,
                             [](uint64_t n, const Part& part) { return n < part.offset; });
  if (it == p.parts.begin())
    sic::sicx::fail(sic::ErrorKind::invalid_format, "missing transport range");
  --it;
  if (offset - it->offset >= it->size)
    sic::sicx::fail(sic::ErrorKind::invalid_format, "transport range gap");
  return *it;
}
}  // namespace sic::transport

namespace sic::transport {
uint64_t suffix_offset(const sic::sicx::Reader& r) {
  return r.finalized() ? r.index().at(*r.rays_chunk()).offset : r.ldir_offset();
}
}  // namespace sic::transport

namespace sic::transport {
void validate_layout(const Parts& p, const sic::sicx::Reader& r) {
  if (sic::sicx::hex(r.exact_root()) != p.exact_root || sic::sicx::hex(r.root()) != p.file_root ||
      r.size() != p.size)
    sic::sicx::fail(sic::ErrorKind::integrity, "manifest/container snapshot mismatch");
  const uint64_t suffix = suffix_offset(r);
  if (!p.parts.front().asset.starts_with("d" + std::to_string(r.meta().d) + "-m"))
    sic::sicx::fail(sic::ErrorKind::integrity, "transport asset dimension differs from EXMT");
  std::set<uint64_t> boundaries{r.ldir_offset()};
  for (const auto& e : r.index())
    boundaries.insert(sic::sicx::checked_add(sic::sicx::checked_add(e.offset, 16), e.length));
  for (size_t i = 1; i < p.parts.size(); ++i) {
    if (!boundaries.contains(p.parts[i].offset) || p.parts[i].offset > suffix)
      sic::sicx::fail(sic::ErrorKind::invalid_format, "part cut splits a chunk or suffix");
  }
  if (p.parts.back().offset > suffix)
    sic::sicx::fail(sic::ErrorKind::invalid_format, "suffix not wholly in final part");
}
}  // namespace sic::transport

namespace sic::transport {
std::vector<sic::support::ByteRange> split_ranges(const sic::sicx::Reader& r, uint64_t ceiling) {
  if (!ceiling) sic::sicx::fail(sic::ErrorKind::invalid_argument, "zero transport ceiling");
  if (r.size() <= ceiling) return {{0, r.size()}};
  const uint64_t suffix = suffix_offset(r);
  if (r.size() - suffix > ceiling)
    sic::sicx::fail(sic::ErrorKind::unsupported, "transport suffix exceeds ceiling");
  std::vector<sic::support::ByteRange> ranges;
  uint64_t first = 0, end = 16;
  for (const auto& e : r.index()) {
    if (e.offset >= suffix) break;
    const auto next = sic::sicx::checked_add(sic::sicx::checked_add(e.offset, 16), e.length);
    if (next - first > ceiling) {
      if (end <= first || end == 16)
        sic::sicx::fail(sic::ErrorKind::unsupported, "indivisible transport chunk exceeds ceiling");
      ranges.push_back({first, end - first});
      first = end;
      if (next - first > ceiling)
        sic::sicx::fail(ErrorKind::unsupported, "indivisible transport chunk exceeds ceiling");
    }
    end = next;
  }
  if (end != suffix) sic::sicx::fail(sic::ErrorKind::invalid_format, "prefix/suffix geometry");
  if (end > first) ranges.push_back({first, end - first});
  ranges.push_back({suffix, r.size() - suffix});
  if (ranges.size() > kMaxParts)
    sic::sicx::fail(sic::ErrorKind::resource, "transport part count resource refusal");
  return ranges;
}
}  // namespace sic::transport

namespace sic::transport {
std::string read_text(const std::filesystem::path& p) { return sic::sicx::read_bounded_text(p); }
}  // namespace sic::transport

namespace sic::transport {
void write_atomic(const std::filesystem::path& path, const std::string& text) {
  support::atomic_write(path, text);
}
}  // namespace sic::transport

namespace sic::transport {
int run(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);
  pid_t pid;
  if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ)) return -1;
  int status = 0;
  while (waitpid(pid, &status, 0) < 0)
    if (errno != EINTR) return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
}  // namespace sic::transport

namespace sic::transport {
int run_sh(const std::string& cmd, const std::string& arg) {
  return run({"/bin/sh", "-c", cmd + " \"$0\"", arg});
}
}  // namespace sic::transport

namespace sic::transport {
std::string capture_sh(const std::string& cmd) {
  FILE* f = ::popen(cmd.c_str(), "r");
  if (!f) throw sic::Error(sic::ErrorKind::io, "cannot run " + cmd);
  std::unique_ptr<FILE, decltype(&::pclose)> owner(f, ::pclose);
  std::string out;
  char buf[256];
  while (size_t n = std::fread(buf, 1, sizeof buf, f)) out.append(buf, n);
  if (::pclose(owner.release()) != 0) sic::sicx::fail(sic::ErrorKind::io, "command failed: " + cmd);
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
  return out;
}
}  // namespace sic::transport

namespace sic::transport {
std::string hash_range(int fd, uint64_t offset, uint64_t size, std::vector<unsigned char>& buf) {
  try {
    return support::sha256::range_hash(fd, offset, size, buf);
  } catch (const sic::Error& error) {
    if (error.kind() != sic::ErrorKind::io) throw;
    return "";  // Short/unreadable work files are repairable misses.
  }
}
}  // namespace sic::transport

namespace sic::transport {
void pwrite_all(int fd, const unsigned char* data, size_t n, uint64_t offset) {
  support::write_all(fd, {data, n}, offset);
}
}  // namespace sic::transport

namespace sic::transport {
uint64_t free_bytes(const std::filesystem::path& dir) {
  struct statvfs sv {};
  if (::statvfs(dir.c_str(), &sv))
    throw sic::Error(sic::ErrorKind::io, "cannot stat the file system of " + dir.string());
  return static_cast<uint64_t>(sv.f_bavail) * sv.f_frsize;
}
}  // namespace sic::transport

namespace sic::transport {
void check_disk(const std::filesystem::path& dir, uint64_t need, const char* what) {
  const uint64_t have = free_bytes(dir);
  if (have < need)
    throw sic::Error(sic::ErrorKind::resource, std::string("not enough disk space for ") + what +
                                                   ": need " + std::to_string(need) +
                                                   " bytes, have " + std::to_string(have));
}
}  // namespace sic::transport

namespace sic::transport {
Parts split(const SplitOptions& o) try {
  if (!valid_prefix(o.prefix))
    sic::sicx::fail(sic::ErrorKind::invalid_argument, "asset prefix must look like d<d>-m<N>");
  if (o.part_size != kPartSize)
    sic::sicx::fail(sic::ErrorKind::invalid_argument, "SICX 1.0 part ceiling is 1900000000");
  sic::sicx::Reader reader(o.file);
  (void)sic::sicx::verify_all(reader, 1);
  if (!o.prefix.starts_with("d" + std::to_string(reader.meta().d) + "-m"))
    sic::sicx::fail(sic::ErrorKind::integrity, "asset prefix dimension differs from EXMT");
  const auto ranges = split_ranges(reader, kPartSize);  // the input must be a valid SICX
  const uint64_t size =
      sic::sicx::filesystem_io([&] { return std::filesystem::file_size(o.file); });
  Parts p;
  p.size = size;
  p.exact_root = sic::sicx::hex(reader.exact_root());
  p.file_root = sic::sicx::hex(reader.root());
  p.part_size = o.part_size;
  p.repository = o.repository;
  p.tag = o.tag;
  const uint64_t n = ranges.size();
  if (n == 0 || n > kMaxParts) sic::sicx::fail(sic::ErrorKind::resource, "too many parts");
  const bool stream = !o.upload_cmd.empty();
  if (stream && o.assets_dir.empty())
    sic::sicx::fail(sic::ErrorKind::invalid_argument,
                    "--emit-and-upload needs a staging directory");
  if (stream && o.confirm_cmd.empty())
    sic::sicx::fail(
        sic::ErrorKind::invalid_argument,
        "--emit-and-upload requires --confirm-size-cmd (the remote size must be confirmed)");
  if (!o.assets_dir.empty())
    sic::sicx::filesystem_io([&] { return std::filesystem::create_directories(o.assets_dir); });
  const int fd = ::open(o.file.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) throw sic::Error(sic::ErrorKind::io, "cannot open " + o.file.string());
  struct Closer {
    int fd;
    ~Closer() { ::close(fd); }
  } closer{fd};
  const std::filesystem::path state_path =
      o.assets_dir.empty() ? std::filesystem::path() : o.assets_dir / ".sicx-split-state";
  std::set<std::string> done;  // "index size sha256" of uploaded and confirmed parts
  if (stream && sic::sicx::filesystem_io([&] { return std::filesystem::exists(state_path); })) {
    std::istringstream in(read_text(state_path));
    for (std::string line; std::getline(in, line);) done.insert(line);
  }
  support::sha256::Hasher whole;
  std::vector<unsigned char> buf(8u << 20);
  auto copy_range = [&](uint64_t offset, uint64_t len, std::ofstream* out,
                        support::sha256::Hasher& h) {
    for (uint64_t d = 0; d < len;) {
      const size_t k = static_cast<size_t>(std::min<uint64_t>(buf.size(), len - d));
      const ssize_t got = ::pread(fd, buf.data(), k, static_cast<off_t>(offset + d));
      if (got < 0 && errno == EINTR) continue;
      if (got < 0) throw sic::Error(ErrorKind::io, "read of the source failed");
      if (got == 0) throw sic::Error(ErrorKind::invalid_format, "short read of the source");
      h.update(buf.data(), static_cast<size_t>(got));
      if (out) out->write(reinterpret_cast<const char*>(buf.data()), got);
      d += static_cast<uint64_t>(got);
    }
  };
  auto write_part = [&](const Part& x, support::sha256::Hasher* h) {
    const std::filesystem::path target = o.assets_dir / x.asset,
                                tmp = o.assets_dir / ("." + x.asset + ".tmp");
    if (sic::sicx::filesystem_io([&] { return std::filesystem::exists(target); })) {
      if (!stream) sic::sicx::fail(sic::ErrorKind::io, "refusing to overwrite " + target.string());
      sic::sicx::filesystem_io([&] {
        return std::filesystem::remove(target);
      });  // a staged part left by an interrupted upload is scratch
    }
    std::ofstream out(tmp, std::ios::binary);
    if (!out) throw sic::Error(sic::ErrorKind::io, "cannot create " + tmp.string());
    support::sha256::Hasher local;
    copy_range(x.offset, x.size, &out, h ? *h : local);
    out.close();
    if (!out) throw sic::Error(sic::ErrorKind::io, "write failed: " + tmp.string());
    sic::sicx::rename_file(tmp, target, "part_asset");
    return target;
  };
  for (uint64_t i = 0; i < n; ++i) {
    Part x;
    x.index = i;
    x.offset = ranges[i].offset;
    x.size = ranges[i].size;
    x.asset = asset_name(o.prefix, i);
    support::sha256::Hasher h;
    if (stream || o.assets_dir.empty()) {
      copy_range(x.offset, x.size, nullptr, h);
    } else {
      write_part(x, &h);  // one pass: write and hash
    }
    x.sha256 = h.finish_hex();
    // The whole-file hash continues over the same bytes (a second hash of the range is cheap next
    // to I/O).
    {
      for (uint64_t d = 0; d < x.size;) {
        const size_t k = static_cast<size_t>(std::min<uint64_t>(buf.size(), x.size - d));
        const ssize_t got = ::pread(fd, buf.data(), k, static_cast<off_t>(x.offset + d));
        if (got < 0) throw sic::Error(ErrorKind::io, "read of the source failed");
        if (got == 0) throw sic::Error(ErrorKind::invalid_format, "short read of the source");
        whole.update(buf.data(), static_cast<size_t>(got));
        d += static_cast<uint64_t>(got);
      }
    }
    if (stream) {
      const std::string key = o.repository + " " + o.tag + " " + x.asset + " " +
                              std::to_string(x.size) + " " + x.sha256;  // tied to the release
      if (!done.contains(key)) {
        const std::filesystem::path path = write_part(x, nullptr);
        ::setenv("SIC_PART_ASSET", x.asset.c_str(), 1);
        if (run_sh(o.upload_cmd, path.string()) != 0)
          sic::sicx::fail(sic::ErrorKind::io, "upload command failed for " + x.asset);
        if (!o.confirm_cmd.empty()) {
          const std::string remote = capture_sh(o.confirm_cmd);
          if (remote != std::to_string(x.size))
            sic::sicx::fail(sic::ErrorKind::integrity, "remote size of " + x.asset + " is " +
                                                           remote + ", expected " +
                                                           std::to_string(x.size));
        }
        sic::sicx::filesystem_io([&] { return std::filesystem::remove(path); });
        std::ofstream st(state_path, std::ios::app | std::ios::binary);
        st << key << '\n';
        st.close();
        if (!st) throw sic::Error(sic::ErrorKind::io, "cannot record the upload state");
        std::cerr << "uploaded " << x.asset << '\n';
      } else {
        std::cerr << "skipped (already uploaded) " << x.asset << '\n';
      }
    }
    p.parts.push_back(std::move(x));
  }
  if (sic::sicx::filesystem_io([&] { return std::filesystem::file_size(o.file); }) != size)
    sic::sicx::fail(sic::ErrorKind::integrity, "source changed while splitting");
  p.sha256 = whole.finish_hex();
  write_atomic(o.parts_path, sic::support::dump_json(parts_to_json(p)) + "\n");
  if (stream) sic::sicx::filesystem_io([&] { return std::filesystem::remove(state_path); });
  return p;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::transport

namespace sic::transport {
std::string source_base(const Parts& p, const std::string& source) {
  if (!source.empty()) return source;
  return "https://github.com/" + p.repository + "/releases/download/" + p.tag;
}
}  // namespace sic::transport

namespace sic::transport {
std::vector<uint64_t> sync_parts(int fd, const Parts& p, const std::string& source, unsigned jobs,
                                 bool hash_existing, const std::filesystem::path& tmp_dir) try {
  const std::string base = source_base(p, source);
  const bool is_url = base.find("://") != std::string::npos;
#ifdef SIC_ENABLE_TEST_HOOKS
  const char* abort_env = std::getenv("SIC_TEST_ABORT_AFTER_PARTS");
  const long kAbortAfter =
      abort_env ? support::parse_number<long>(abort_env, "SIC_TEST_ABORT_AFTER_PARTS",
                                              ErrorKind::invalid_argument)
                : -1;
  std::atomic<long> written_count{0};
#endif
  std::atomic<uint64_t> next{0};
  std::atomic<bool> failed{false};
  std::mutex mu;
  std::exception_ptr error;
  std::vector<uint64_t> repaired;
  constexpr size_t kBufferSizeBytes = 8u << 20;
  constexpr unsigned kRemoteAttempts = 4;
  constexpr unsigned kLocalAttempts = 1;
  constexpr unsigned kRetryDelaySeconds = 2;
  auto worker = [&] {
    try {
      std::vector<unsigned char> buf(kBufferSizeBytes);
      for (;;) {
        const uint64_t i = next.fetch_add(1);
        if (i >= p.parts.size() || failed) return;
        const Part& x = p.parts[i];
        try {
          sic::support::check_stop();
          if (hash_existing && hash_range(fd, x.offset, x.size, buf) == x.sha256) continue;
          const std::filesystem::path tmp = tmp_dir / ("." + x.asset + ".tmp");
          struct Rm {
            std::filesystem::path p;
            ~Rm() {
              std::error_code ec;
              std::filesystem::remove(p, ec);
            }
          } rm{tmp};
          const unsigned attempts = is_url ? kRemoteAttempts : kLocalAttempts;
          for (unsigned a = 1;; ++a) {
            std::string why;
            sic::ErrorKind kind = sic::ErrorKind::integrity;
            if (is_url) {
              sic::sicx::filesystem_io([&] { return std::filesystem::remove(tmp); });
              const int rc = run({"curl", "--proto", "=https,file", "-fL", "-sS", "-o",
                                  tmp.string(), "--", base + "/" + x.asset});
              if (rc != 0) {
                kind = sic::ErrorKind::io;
                why = "download failed (curl exit " + std::to_string(rc) + ")";
              }
            } else {
              const std::filesystem::path src = std::filesystem::path(base) / x.asset;
              std::error_code ec;
              if (!sic::sicx::filesystem_io([&] { return std::filesystem::is_regular_file(src); }))
                throw sic::Error(sic::ErrorKind::io, "missing part " + x.asset);
              sic::sicx::filesystem_io([&] { return std::filesystem::remove(tmp); });
              std::filesystem::copy_file(src, tmp, ec);
              if (ec) throw sic::Error(sic::ErrorKind::io, "cannot copy " + src.string());
            }
            if (why.empty()) {
              if (sic::sicx::filesystem_io([&] { return std::filesystem::file_size(tmp); }) !=
                  x.size)
                why = "wrong size";
              else {
                PartDescriptor tf(::open(tmp.c_str(), O_RDONLY | O_CLOEXEC));
                if (tf.get() < 0)
                  throw sic::Error(sic::ErrorKind::io, "cannot reopen the temporary file");
                const std::string got = hash_range(tf.get(), 0, x.size, buf);
                if (got.empty()) kind = sic::ErrorKind::io;
                if (got == x.sha256) {
                  for (uint64_t d = 0; d < x.size;) {
                    const ssize_t r =
                        ::pread(tf.get(), buf.data(),
                                static_cast<size_t>(std::min<uint64_t>(buf.size(), x.size - d)),
                                static_cast<off_t>(d));
                    if (r <= 0) {
                      tf.close();
                      throw sic::Error(r < 0 ? ErrorKind::io : ErrorKind::invalid_format,
                                       "short read of the temporary file");
                    }
                    pwrite_all(fd, buf.data(), static_cast<size_t>(r), x.offset + d);
                    d += static_cast<uint64_t>(r);
                  }
                  tf.close();
                  // Durability per part for real parts; tiny test parts rely on the final fsync
                  // (resume re-hashes ranges anyway).
                  if (x.size >= (1u << 20) && ::fdatasync(fd))
                    throw sic::Error(sic::ErrorKind::io, "fsync failed");
                  break;
                }
                tf.close();
                why = "SHA-256 mismatch";
              }
            }
            if (a >= attempts) throw sic::Error(kind, "part " + x.asset + ": " + why);
            std::this_thread::sleep_for(std::chrono::seconds(kRetryDelaySeconds * a));  // backoff
          }
          {
            std::lock_guard lock(mu);
            repaired.push_back(i);
          }
#ifdef SIC_ENABLE_TEST_HOOKS
          if (kAbortAfter >= 0 && written_count.fetch_add(1) + 1 >= kAbortAfter) ::_exit(137);
#endif
        } catch (const std::exception&) {
          std::lock_guard lock(mu);
          if (!failed.exchange(true)) error = std::current_exception();
        }
      }
    } catch (...) {
      std::lock_guard lock(mu);
      if (!failed.exchange(true)) error = std::current_exception();
    }
  };
  sic::support::ThreadGroup threads;
  for (unsigned t = 0; t < std::max(1u, jobs); ++t)
    threads.start([&] {
      sic::support::flint::WorkerCleanup cleanup;
      worker();
    });
  threads.join();
  if (error) std::rethrow_exception(error);
  std::sort(repaired.begin(), repaired.end());
  return repaired;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::transport

namespace sic::transport {
void final_checks(const std::filesystem::path& file, const Parts& p) try {
  if (sic::sicx::filesystem_io([&] { return std::filesystem::file_size(file); }) != p.size)
    sic::sicx::fail(sic::ErrorKind::integrity, "assembled size differs from the parts file");
  if (support::sha256::file_hex(file) != p.sha256)
    sic::sicx::fail(sic::ErrorKind::integrity, "whole-file SHA-256 differs from the parts file");
  sic::sicx::Reader r(file);
  validate_layout(p, r);
  (void)sic::sicx::verify_all(r, 1);
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::transport
