// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/completed_class_cache.hpp"

#include "sic/error.hpp"
#include "sic/support/parse_number.hpp"

namespace sic::exact {
namespace {
constexpr const char* kCompletedClassIdentity = "sic-completed-class/1.0";
}
namespace {
uint64_t mapping_unsigned(std::string_view text, int base) {
  try {
    return sic::support::parse_number<uint64_t>(
        text, "arithmetic object mapping", sic::ErrorKind::invalid_format, 0, UINT64_MAX, base);
  } catch (const sic::Error&) {
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid arithmetic object mapping integer");
  }
}
}  // namespace

std::string completed_loaded_object_digest(const void* address) try {
#if defined(__linux__)
  Dl_info info{};
  if (!address || !dladdr(address, &info) || !info.dli_fname || !*info.dli_fname)
    throw sic::Error(sic::ErrorKind::io,
                     "cannot identify loaded completed-class arithmetic object");
  struct Descriptor {
    int value;
    ~Descriptor() {
      if (value >= 0) close(value);
    }
  } file{::open(std::filesystem::canonical(info.dli_fname).c_str(), O_RDONLY | O_CLOEXEC)};
  if (file.value < 0) throw sic::Error(sic::ErrorKind::io, "cannot open loaded arithmetic object");
  if (file.value < 3) {
    const int duplicate = fcntl(file.value, F_DUPFD_CLOEXEC, 3);
    close(file.value);
    file.value = duplicate;
    if (file.value < 0)
      throw sic::Error(sic::ErrorKind::io, "cannot retain arithmetic object descriptor");
  }
  struct stat before {
  }, after{};
  if (fstat(file.value, &before))
    throw sic::Error(sic::ErrorKind::io, "cannot inspect arithmetic object");
  const auto pointer = reinterpret_cast<std::uintptr_t>(address);
  bool matched = false;
  std::ifstream maps("/proc/self/maps");
  if (!maps) throw sic::Error(sic::ErrorKind::io, "cannot open arithmetic object mappings");
  maps.exceptions(std::ios::badbit);
  std::string line;
  while (std::getline(maps, line)) {
    std::istringstream row(line);
    std::string range, permissions, offset, device;
    std::string inode_text;
    if (!(row >> range >> permissions >> offset >> device >> inode_text)) continue;
    const auto dash = range.find('-'), colon = device.find(':');
    if (dash == std::string::npos || colon == std::string::npos) continue;
    const auto first = mapping_unsigned(std::string_view(range).substr(0, dash), 16);
    const auto end = mapping_unsigned(std::string_view(range).substr(dash + 1), 16);
    if (pointer < first || pointer >= end) continue;
    matched = mapping_unsigned(inode_text, 10) == (unsigned long long)before.st_ino &&
              mapping_unsigned(std::string_view(device).substr(0, colon), 16) ==
                  (unsigned long long)major(before.st_dev) &&
              mapping_unsigned(std::string_view(device).substr(colon + 1), 16) ==
                  (unsigned long long)minor(before.st_dev);
    break;
  }
  if (!matched)
    throw sic::Error(sic::ErrorKind::integrity,
                     "resolved arithmetic object was replaced or is not file-backed");
  // Hash through the retained descriptor so the mapped inode stays pinned
  // even if its pathname is replaced.
  const auto digest = checkpoint_digest("/proc/self/fd/" + std::to_string(file.value));
  if (fstat(file.value, &after))
    throw sic::Error(sic::ErrorKind::io, "cannot reinspect arithmetic object");
  if (before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
      before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
      before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
      before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
    throw sic::Error(sic::ErrorKind::integrity, "arithmetic object changed while hashing");
  return digest;
#else
  (void)address;
  throw sic::Error(sic::ErrorKind::unsupported,
                   "mapped arithmetic identity currently requires Linux");
#endif
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
} catch (const std::ios_base::failure& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

const std::string& completed_class_dependencies() {
  static const std::string identity = [] {
    std::ostringstream out;
    for (const char* name : {"acb_mat_mul", "__gmpz_init", "mpfr_init2"}) {
      // Resolve the exported implementation, not a possible executable PLT
      // stub. Unexported static symbols refuse this optional cache feature.
      const void* address = dlsym(RTLD_DEFAULT, name);
      if (!address)
        throw sic::Error(sic::ErrorKind::unsupported,
                         "cannot resolve arithmetic dependency symbol");
      out << name << ' ' << completed_loaded_object_digest(address) << '\n';
    }
    return out.str();
  }();
  return identity;
}

std::string cache_checksum_file(const std::filesystem::path& path) try {
  if (std::filesystem::file_size(path) > 256)
    throw sic::Error(sic::ErrorKind::invalid_format, "oversized cache checksum file");
  std::ifstream input(path);
  if (!input) throw sic::Error(sic::ErrorKind::io, "cannot open cache checksum file");
  input.exceptions(std::ios::badbit);
  std::string digest, extra;
  if (!(input >> digest) || (input >> extra) || digest.size() != 64 ||
      digest.find_first_not_of("0123456789abcdef") != std::string::npos)
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid cache checksum file");
  return digest;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
} catch (const std::ios_base::failure& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

void verify_cached_ball_file(const BallFile& file, size_t count) try {
  if (file.count() != count || std::filesystem::file_size(file.path()) != file.bytes() ||
      checkpoint_digest(file.path()) != file.digest())
    throw sic::Error(sic::ErrorKind::integrity, "completed-class payload checksum failed");
  std::unique_ptr<FILE, decltype(&fclose)> input(fopen(file.path().c_str(), "rb"), fclose);
  if (!input) throw sic::Error(sic::ErrorKind::io, "cannot open completed-class payload");
  std::uint64_t header[6];
  if (fread(header, sizeof header, 1, input.get()) != 1)
    throw sic::Error(ferror(input.get()) ? sic::ErrorKind::io : sic::ErrorKind::invalid_format,
                     "truncated/unreadable completed-class payload header");
  // This is the BallFile header. Keeping this verifier separate
  // avoids changing the established coarse-table source identity. Complete
  // decoding still verifies finite balls and the exact end of the payload.
  if (header[0] != 0x53494342414c4c31ULL || header[1] != 1 || header[2] != MAG_BITS ||
      header[3] != sizeof(ulong) || header[4] != count || header[5] != 2)
    throw sic::Error(sic::ErrorKind::invalid_format, "completed-class payload header mismatch");
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

std::string CompletedClassIdentity::text() const {
#ifdef SIC_COMPLETED_CLASS_SOURCE_ID
  std::ostringstream out;
  out << s << ' ' << d << ' ' << delta << ' ' << order << ' ' << degree << ' ' << slots << ' ' << cc
      << ' ' << precision << ' ' << paired << '\n'
      << producer << '\n'
      << group_digest << '\n'
      << coarse_digest << '\n'
      << GroupCheckpoint::source_id() << '\n'
      << (coarse_source.empty() ? TableCheckpoint::source_id() : coarse_source) << '\n'
      << SIC_COMPLETED_CLASS_SOURCE_ID << '\n'
      << FLINT_VERSION << ' ' << gmp_version << ' ' << mpfr_get_version() << ' ' << sizeof(long)
      << ' ' << sizeof(slong) << ' ' << sizeof(ulong) << ' ' << sizeof(acb_struct) << ' '
      << MAG_BITS << '\n';
  out << completed_class_dependencies();
  for (char value : derived) out << int(value) << ' ';
  out << '\n';
  for (long value : elements) out << value << ' ';
  return out.str();
#else
  throw std::runtime_error("completed-class cache requires CMake source identity");
#endif
}

void CompletedClassCache::validate(sic::support::Index cls, const TableClassBacking& backing,
                                   const std::string& method) const {
  const auto& identity = identity_;
  if (cls < 0 || (size_t)cls >= identity.derived.size() || identity.derived[cls] || !backing ||
      bool(backing.values) == bool(backing.ghost) || (method != "newton" && method != "quadrature"))
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid completed-class record");
  if (backing.ghost) {
    const auto& root = *backing.ghost;
    const auto& m = root.metadata;
    const long g = identity.elements[cls];
    if (method != "newton" || !root.state || m.d != identity.d || root.group_element != g ||
        g < 0 || g >= group_.N || m.chi != group_.chi[g] ||
        m.n != (identity.order * group_.nn[g]) % (2 * m.d) || m.k < 0 || m.k >= m.d ||
        m.precision < identity.precision || m.constants_precision < m.precision ||
        m.constants_precision > WORD_MAX / 16 || root.packing_precision != identity.precision ||
        root.state->count() != (size_t)(2 * m.d - 2))
      throw sic::Error(sic::ErrorKind::invalid_format, "completed root metadata mismatch");
  } else if (backing.values->count() != (size_t)identity.slots)
    throw sic::Error(sic::ErrorKind::invalid_format, "completed full-class length mismatch");
}

CompletedClassCache::CompletedClassCache(std::filesystem::path root,
                                         CompletedClassIdentity identity, const Group& group) try
    : identity_(std::move(identity)), identity_text_(identity_.text()), group_(group) {
  if (!identity_.coarse_source.empty() && identity_.coarse_source != TableCheckpoint::source_id())
    throw sic::Error(sic::ErrorKind::integrity, "native coarse source identity mismatch");
  for (const auto* digest :
       {&identity_.producer, &identity_.group_digest, &identity_.coarse_digest})
    if (digest->size() != 64 || digest->find_first_not_of("0123456789abcdef") != std::string::npos)
      throw std::runtime_error("invalid completed-class identity digest");
  if (identity_.d != group.d || identity_.s != group.ar.s || identity_.delta != group.ar.Delta ||
      identity_.order != group.ar.order_index || identity_.degree != group.N ||
      identity_.slots != (group.d * group.d - 1) / 3 || identity_.precision < 2 ||
      identity_.precision > WORD_MAX / 16 || identity_.derived.size() != (size_t)group.ar.h() ||
      identity_.elements.size() != identity_.derived.size())
    throw std::runtime_error("inconsistent completed-class cache identity");
  if (identity_text_.size() > 512 * 1024 ||
      (identity_.paired &&
       (identity_.cc < 0 || identity_.cc >= group.N || group.mul(identity_.cc, identity_.cc) != 0 ||
        group.nn[identity_.cc] != 2 * group.d - 1 || group.chi[identity_.cc] < 0)) ||
      (!identity_.paired && identity_.cc != -1))
    throw std::runtime_error("invalid completed-class conjugation identity");
  const sic::support::Index anchor = group.orbit[group.pidx(0, 0, 1)];
  for (size_t c = 0; c < identity_.derived.size(); ++c) {
    const long g = identity_.elements[c];
    if ((identity_.derived[c] != 0 && identity_.derived[c] != 1) || g < 0 || g >= group.N ||
        group.act(g, anchor) / (group.d * group.d) != (long)c)
      throw std::runtime_error("invalid completed-class element/coverage identity");
    if (identity_.derived[c] &&
        (!identity_.paired ||
         identity_.derived[group.act(identity_.cc, group.orbit[c * group.d * group.d + 1]) /
                           (group.d * group.d)]))
      throw std::runtime_error("invalid completed-class derived partner");
  }
  if (std::count(identity_.derived.begin(), identity_.derived.end(), 0) == 0)
    throw std::runtime_error("completed-class identity has no physical classes");
  generation_ = std::filesystem::absolute(root).lexically_normal() /
                ("s" + std::to_string(identity_.s)) /
                ("p" + std::to_string(identity_.precision) + "-" + identity_.producer + "-" +
                 identity_.group_digest + "-" + identity_.coarse_digest);
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

TableClassBacking CompletedClassCache::load(sic::support::Index cls) const try {
  const auto directory = path(cls);
  const auto& expected = identity_;
  if (cls < 0 || (size_t)cls >= identity_.derived.size() || identity_.derived[cls])
    throw std::runtime_error("invalid owned completed-class index");
  if (!std::filesystem::exists(directory)) return {};
  const auto manifest_path = directory / "manifest";
  if (std::filesystem::file_size(manifest_path) > sic::support::resources::kMib)
    throw sic::Error(sic::ErrorKind::invalid_format, "oversized completed-class manifest");
  if (checkpoint_digest(manifest_path) != cache_checksum_file(directory / "manifest.sha256"))
    throw sic::Error(sic::ErrorKind::integrity, "completed-class manifest checksum mismatch");
  std::ifstream input(manifest_path);
  if (!input) throw sic::Error(sic::ErrorKind::io, "cannot open completed-class manifest");
  input.exceptions(std::ios::badbit);
  std::string tag, identity, kind, method, digest, options, extra;
  sic::support::Index index;
  size_t count;
  sic::support::resources::Bytes bytes;
  if (!(input >> tag >> std::quoted(identity) >> index >> kind >> method) ||
      tag != kCompletedClassIdentity)
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid completed-class manifest header");
  if (identity != identity_text_ || index != cls)
    throw sic::Error(sic::ErrorKind::integrity,
                     "completed-class source/arithmetic/precision identity mismatch");
  TableClassBacking result;
  GhostRootMetadata metadata;
  long element;
  slong packing;
  if (!(input >> metadata.d >> metadata.n >> metadata.k >> metadata.chi >> metadata.precision >>
        metadata.constants_precision >> element >> packing >> count >> bytes >> digest >>
        std::quoted(options) >> tag) ||
      tag != "end" || (input >> extra) || digest.size() != 64 ||
      digest.find_first_not_of("0123456789abcdef") != std::string::npos ||
      (kind != "root" && kind != "full"))
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid completed-class payload record");
  if (kind == "root") {
    result.ghost = std::make_shared<GhostClassBacking>();
    result.ghost->metadata = metadata;
    result.ghost->group_element = element;
    result.ghost->packing_precision = packing;
    result.ghost->state = BallFile::open(directory / "payload.balls", count, 2, bytes, digest);
  } else {
    if (metadata.d != expected.d || element != expected.elements[cls] ||
        packing != expected.precision || metadata.n != 0 || metadata.k != -1 || metadata.chi != 0 ||
        metadata.precision != packing || metadata.constants_precision != packing)
      throw sic::Error(sic::ErrorKind::invalid_format, "completed full-class metadata mismatch");
    result.values = BallFile::open(directory / "payload.balls", count, 2, bytes, digest);
  }
  validate(cls, result, method);
  const auto& file = result.ghost ? result.ghost->state : result.values;
  verify_cached_ball_file(*file, result.ghost ? 2 * expected.d - 2 : expected.slots);
  return result;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
} catch (const std::ios_base::failure& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

CompletedClassCache::Inventory CompletedClassCache::inventory(int replay_workers) const {
  Inventory result;
  result.backing.resize(identity_.derived.size());
  for (size_t c = 0; c < result.backing.size(); ++c)
    if (!identity_.derived[c]) {
      result.backing[c] = load(c);
      if (result.backing[c]) {
        ++result.hits;
        result.replay_workspace =
            std::max(result.replay_workspace, result.backing[c].workspace(replay_workers));
      } else
        ++result.missing;
    }
  return result;
}

TableClassBacking CompletedClassCache::publish(sic::support::Index cls, TableClassBacking&& source,
                                               const std::string& method,
                                               const std::string& options) try {
  std::lock_guard<std::mutex> lock(publication_);
  // Move caller ownership under the lock. This local and its file aliases
  // are destroyed BEFORE the lock, including on exceptions and cache-hit
  // returns. A following publisher therefore cannot overlap another copy
  // while our old private owner still exists in a caller assignment.
  TableClassBacking backing = std::move(source);
  validate(cls, backing, method);
  if (options.size() > 64 * 1024)
    throw sic::Error(sic::ErrorKind::resource, "oversized completed-class provenance");
  if (auto existing = load(cls)) return existing;
  std::vector<std::filesystem::path> new_directories;
  for (auto directory = generation_; !std::filesystem::exists(directory);
       directory = directory.parent_path()) {
    if (directory.empty()) throw std::runtime_error("invalid completed-class cache directory");
    new_directories.push_back(directory);
  }
  std::filesystem::create_directories(generation_);
  for (const auto& directory : new_directories) {
    TableCheckpoint::sync_file(directory);
    TableCheckpoint::sync_file(directory.parent_path().empty() ? "." : directory.parent_path());
  }
  std::string pattern =
      (generation_ / (".class-" + std::to_string(cls) + ".partial-XXXXXX")).string();
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back(0);
  if (!mkdtemp(name.data()))
    throw sic::Error(sic::ErrorKind::io, "cannot create completed-class staging directory");
  const std::filesystem::path staging(name.data());
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  } cleanup{staging};
  const auto file = backing.ghost ? backing.ghost->state : backing.values;
  file->publish(staging / "payload.balls");
  GhostRootMetadata m;
  long element = identity_.elements[cls];
  slong packing = identity_.precision;
  if (backing.ghost)
    m = backing.ghost->metadata;
  else {
    m.d = identity_.d;
    m.n = 0;
    m.k = -1;
    m.chi = 0;
    m.precision = packing;
    m.constants_precision = packing;
  }
  std::ostringstream manifest;
  manifest << kCompletedClassIdentity << '\n'
           << std::quoted(identity_text_) << '\n'
           << cls << ' ' << (backing.ghost ? "root" : "full") << ' ' << method << '\n'
           << m.d << ' ' << m.n << ' ' << m.k << ' ' << m.chi << ' ' << m.precision << ' '
           << m.constants_precision << ' ' << element << ' ' << packing << '\n'
           << file->count() << ' ' << file->bytes() << ' ' << file->digest() << '\n'
           << std::quoted(options) << "\nend\n";
  sic::support::atomic_write(staging / "manifest", manifest.str(), false, 0666);
  // Checksum atomic_write directory fsync durably publishes BallFile payloads; no staging writes
  // may follow.
  sic::support::atomic_write(staging / "manifest.sha256",
                             checkpoint_digest(staging / "manifest") + '\n', false, 0666);
  // A completed directory is nonempty; rename cannot replace another
  // published generation. Preserve a competing valid producer if it won.
  try {
    sic::support::publish_replace(staging, path(cls));
  } catch (const sic::Error& error) {
    if (error.kind() != sic::ErrorKind::io) throw;
    if (auto existing = load(cls)) return existing;
    throw;
  }
  TableCheckpoint::sync_file(generation_);
  return load(cls);
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

sic::support::resources::Bytes completed_class_new_disk(
    size_t missing, sic::support::resources::Bytes class_bytes,
    sic::support::resources::Bytes generator_bytes,
    sic::support::resources::Bytes temporary_bytes) {
  // Existing persistent files are already charged by actual filesystem free
  // space. Only missing future payloads plus ONE serialized copy overlap are
  // new allocations; root and full hits both eliminate a whole fallback.
  return sic::support::resources::add(
      temporary_bytes,
      sic::support::resources::add(
          generator_bytes,
          sic::support::resources::mul(missing ? sic::support::resources::add(missing, 1) : 0,
                                       class_bytes)));
}

CompletedCacheRequirement CompletedCacheRequirement::parse(const char* value) {
  if (!value || std::strcmp(value, "0") == 0) return {};
  if (std::strcmp(value, "1") == 0) return {true};
  throw sic::Error(sic::ErrorKind::invalid_argument, "SIC_REQUIRE_COMPLETED_CACHE must be 0 or 1");
}

void CompletedCacheRequirement::require_context(bool supported_mode, bool streamed_arb,
                                                const char* completed_root, const char* group,
                                                const char* coarse_root) const try {
  if (!enabled) return;
  const auto present = [](const char* value) { return value && *value; };
  if (!supported_mode || !streamed_arb || !present(completed_root) || !present(group) ||
      !present(coarse_root))
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "cache-only consumer requires streamed Arb final/count/table-only mode, "
                     "explicit completed cache, existing Group and native analytic checkpoints");
  if (!std::filesystem::is_regular_file(group) ||
      !std::filesystem::is_regular_file(std::string(group) + ".sha256") ||
      !std::filesystem::is_directory(coarse_root))
    throw sic::Error(sic::ErrorKind::invalid_argument,
                     "cache-only consumer requires existing Group/coarse input files; "
                     "input construction is forbidden");
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

void CompletedCacheRequirement::enforce(long precision, std::size_t reused, std::size_t missing,
                                        std::size_t physical) const {
  if (!enabled) return;
  if (!physical || reused > physical || missing != physical - reused)
    throw std::runtime_error("cache-only consumer inventory accounting mismatch");
  if (missing)
    throw sic::Error(sic::ErrorKind::io,
                     "cache-only consumer refused: precision=" + std::to_string(precision) +
                         " reused=" + std::to_string(reused) + " missing=" +
                         std::to_string(missing) + "; target recomputation is forbidden");
}

}  // namespace sic::exact
