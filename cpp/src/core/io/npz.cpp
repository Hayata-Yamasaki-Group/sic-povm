// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/io/npz.hpp"

#include "support/sha256.hpp"

namespace sic::core::interchange {
std::uint32_t crc32(const void* data, std::size_t n) {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  std::uint32_t c = 0xFFFFFFFFu;
  const auto* p = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 255] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}
}  // namespace sic::core::interchange

namespace sic::core::interchange::zip {
void p16(ByteBuffer& b, std::uint32_t x) {
  for (int i = 0; i < 2; ++i) b.push_back(static_cast<unsigned char>(x >> (8 * i)));
}
}  // namespace sic::core::interchange::zip

namespace sic::core::interchange::zip {
void p32(ByteBuffer& b, std::uint32_t x) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(x >> (8 * i)));
}
}  // namespace sic::core::interchange::zip

namespace sic::core::interchange {
void admit_zip_profile(uint64_t members, uint64_t bytes) {
  if (members > kZipProfileMaximumMembers || bytes > kZipProfileMaximumBytes)
    throw sic::Error(ErrorKind::unsupported,
                     "zip: output exceeds non-ZIP64 profile; split bundles or use HDF5");
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
ByteBuffer zip_store_encode(const std::vector<ZipMember>& members, const std::string& comment) {
  admit_zip_profile(members.size(), 0);
  if (comment.size() > 65535)
    throw sic::Error(ErrorKind::resource, "zip: archive comment longer than 65535 bytes");
  uint64_t total = 22 + comment.size();
  for (const auto& m : members)
    total = support::admission::add(total,
                                    support::admission::add(76 + 2 * m.name.size(), m.data.size()));
  admit_zip_profile(members.size(), total);
  ByteBuffer out, central;
  for (const auto& m : members) {
    if (m.name.empty()) throw sic::Error(ErrorKind::invalid_argument, "zip: empty member name");
    if (m.name.size() > 65535) throw sic::Error(ErrorKind::resource, "zip: member name too long");
    const std::uint32_t crc = crc32(m.data.data(), m.data.size()),
                        size = static_cast<std::uint32_t>(m.data.size());
    const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
    using namespace zip;
    p32(out, 0x04034b50);
    p16(out, 20);
    p16(out, 0);
    p16(out, 0);
    p16(out, kDosTime);
    p16(out, kDosDate);
    p32(out, crc);
    p32(out, size);
    p32(out, size);
    p16(out, static_cast<std::uint32_t>(m.name.size()));
    p16(out, 0);
    out.insert(out.end(), m.name.begin(), m.name.end());
    out.insert(out.end(), m.data.begin(), m.data.end());
    p32(central, 0x02014b50);
    p16(central, 20);
    p16(central, 20);
    p16(central, 0);
    p16(central, 0);
    p16(central, kDosTime);
    p16(central, kDosDate);
    p32(central, crc);
    p32(central, size);
    p32(central, size);
    p16(central, static_cast<std::uint32_t>(m.name.size()));
    p16(central, 0);
    p16(central, 0);
    p16(central, 0);
    p16(central, 0);
    p32(central, 0);
    p32(central, offset);
    central.insert(central.end(), m.name.begin(), m.name.end());
  }
  using namespace zip;
  const std::uint32_t cd_offset = static_cast<std::uint32_t>(out.size());
  out.insert(out.end(), central.begin(), central.end());
  p32(out, 0x06054b50);
  p16(out, 0);
  p16(out, 0);
  p16(out, static_cast<std::uint32_t>(members.size()));
  p16(out, static_cast<std::uint32_t>(members.size()));
  p32(out, static_cast<std::uint32_t>(central.size()));
  p32(out, cd_offset);
  p16(out, static_cast<std::uint32_t>(comment.size()));
  out.insert(out.end(), comment.begin(), comment.end());
  return out;
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
std::vector<ZipMember> zip_store_decode(const ByteBuffer& in, std::string* comment,
                                        const std::string& what) {
  auto fail = [&](const std::string& m, ErrorKind kind = ErrorKind::invalid_format) {
    throw sic::Error(kind, what + ": " + m);
  };
  auto u16 = [&](std::size_t at) -> std::uint32_t {
    if (at + 2 > in.size()) fail("truncated");
    return in[at] | (std::uint32_t(in[at + 1]) << 8);
  };
  auto u32 = [&](std::size_t at) -> std::uint32_t { return u16(at) | (u16(at + 2) << 16); };
  if (in.size() < 22) fail("too short for a ZIP archive");
  std::size_t eocd = std::string::npos;
  for (std::size_t back = 22; back <= std::min<std::size_t>(in.size(), 22 + 65535); ++back) {
    const std::size_t at = in.size() - back;
    if (u32(at) == 0x06054b50 && at + 22 + u16(at + 20) == in.size()) {
      eocd = at;
      break;
    }
  }
  if (eocd == std::string::npos) fail("no end-of-central-directory record");
  if (u16(eocd + 4) != 0 || u16(eocd + 6) != 0)
    fail("multi-disk archives are not supported", ErrorKind::unsupported);
  const std::size_t count = u16(eocd + 10);
  if (u16(eocd + 8) != count) fail("entry counts disagree");
  const std::size_t cd_size = u32(eocd + 12), cd_off = u32(eocd + 16);
  if (cd_size == UINT32_MAX || cd_off == UINT32_MAX)
    fail("ZIP64 archives are not supported", ErrorKind::unsupported);
  if (cd_off + cd_size != eocd) {
    if (eocd >= 20 && u32(eocd - 20) == 0x07064b50)
      fail("ZIP64 archives are not supported", ErrorKind::unsupported);
    fail("central directory is not contiguous with the end record");
  }
  if (comment) comment->assign(in.begin() + static_cast<std::ptrdiff_t>(eocd + 22), in.end());
  std::vector<ZipMember> out;
  std::set<std::string> names;
  std::size_t cp = cd_off, expect_local = 0;
  for (std::size_t k = 0; k < count; ++k) {
    if (u32(cp) != 0x02014b50) fail("bad central directory entry");
    const std::uint32_t crc = u32(cp + 16);
    const std::size_t nlen = u16(cp + 28), xlen = u16(cp + 30), clen = u16(cp + 32);
    if (cp + 46 + nlen + xlen + clen > eocd) fail("central directory entry overruns");
    // Local and central extra fields may differ. Resolve ZIP64 size/offset
    // sentinels before checking agreement, even though that profile is refused.
    auto sizes = [&](std::size_t extra, std::size_t length, uint64_t size, uint64_t compressed,
                     uint64_t offset) {
      std::array<uint64_t, 3> values{size, compressed, offset};
      bool zip64 = false;
      const auto end = extra + length;
      if (end > in.size()) fail("extra field overruns");
      while (extra < end) {
        if (end - extra < 4) fail("truncated extra field");
        const auto tag = u16(extra), bytes = u16(extra + 2);
        extra += 4;
        if (bytes > end - extra) fail("extra field payload overruns");
        if (tag == 1) {
          if (zip64) fail("duplicate ZIP64 extra field");
          zip64 = true;
          std::size_t at = extra;
          for (auto& value : values) {
            if (value != UINT32_MAX) continue;
            if (extra + bytes - at < 8) fail("truncated ZIP64 extra field");
            value = uint64_t{u32(at)} | (uint64_t{u32(at + 4)} << 32);
            at += 8;
          }
        }
        extra += bytes;
      }
      for (auto value : values)
        if (value == UINT32_MAX && !zip64) fail("missing ZIP64 extra field");
      return values;
    };
    const auto central = sizes(cp + 46 + nlen, xlen, u32(cp + 24), u32(cp + 20), u32(cp + 42));
    const auto usize = central[0], csize = central[1], loff = central[2];
    std::string name(in.begin() + static_cast<std::ptrdiff_t>(cp + 46),
                     in.begin() + static_cast<std::ptrdiff_t>(cp + 46 + nlen));
    if (loff != expect_local) fail("members are not contiguous");
    if (loff > cd_off || cd_off - loff < 30) fail("local header overruns");
    const std::size_t local_extra = u16(loff + 28);
    if (30 + nlen + local_extra > cd_off - loff) fail("local name/extra fields overrun");
    const auto local = sizes(loff + 30 + nlen, local_extra, u32(loff + 22), u32(loff + 18), 0);
    const bool descriptor = (u16(cp + 8) & 8) != 0;
    if (u32(loff) != 0x04034b50 || u16(loff + 6) != u16(cp + 8) || u16(loff + 8) != u16(cp + 10) ||
        u16(loff + 10) != u16(cp + 12) || u16(loff + 12) != u16(cp + 14) ||
        ((!descriptor || u32(loff + 14) != 0) && u32(loff + 14) != crc) ||
        ((!descriptor || local[0] != 0) && local[0] != usize) ||
        ((!descriptor || local[1] != 0) && local[1] != csize) || u16(loff + 26) != nlen)
      fail("local header disagrees with the central directory (" + name + ")");
    const std::size_t dat = loff + 30 + nlen + local_extra;
    if (csize > cd_off - dat) fail("member data overruns the central directory");
    if (!std::equal(name.begin(), name.end(), in.begin() + static_cast<std::ptrdiff_t>(loff + 30)))
      fail("local and central names differ");
    if (u16(cp + 8) != 0 || u16(cp + 10) != 0)
      fail("only stored, flag-free entries are supported", ErrorKind::unsupported);
    if (xlen || local_extra || clen || u16(cp + 34) != 0)
      fail("extra fields, comments and multi-disk entries are not supported",
           ErrorKind::unsupported);
    if (u16(cp + 12) != zip::kDosTime || u16(cp + 14) != zip::kDosDate)
      fail("noncanonical timestamps are not supported", ErrorKind::unsupported);
    if (csize != usize) fail("compressed size differs from size");
    ZipMember m{name, ByteBuffer(in.begin() + static_cast<std::ptrdiff_t>(dat),
                                 in.begin() + static_cast<std::ptrdiff_t>(dat + csize))};
    if (crc32(m.data.data(), m.data.size()) != crc)
      fail("CRC-32 mismatch in member " + name, ErrorKind::integrity);
    if (!names.insert(name).second) fail("duplicate member name " + name);
    out.push_back(std::move(m));
    expect_local = dat + csize;
    cp += 46 + nlen;
  }
  if (expect_local != cd_off || cp != eocd) fail("trailing or missing data");
  return out;
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
std::string npz_comment(std::size_t d, const std::vector<NpzRay>& sorted) {
  std::string rays, exceptional;
  std::size_t wh = 0;
  for (const auto& r : sorted) {
    if (r.exceptional_group.empty()) {
      rays += std::string(wh++ ? "," : "") + "\"" + r.id + "\":\"" + r.source + "\"";
    } else {
      // Escape the free-form group name; IDs are validated by npz_encode.
      std::string group;
      for (unsigned char c : r.exceptional_group) {
        if (c < 32)
          throw sic::Error(ErrorKind::invalid_argument, "npz: control character in group name");
        if (c == '"' || c == '\\') group += '\\';
        group += static_cast<char>(c);
      }
      exceptional += std::string(exceptional.empty() ? "" : ",") + "\"" + r.id +
                     "\":{\"group\":\"" + group + "\",\"wh_covariant\":false}";
    }
  }
  std::string comment =
      "{\"schema\":\"sic-npz-bundle/1.0\",\"dimension\":" + std::to_string(d) +
      ",\"dtype\":\"<c16\",\"member_suffix\":\".npy\",\"ray_count\":" + std::to_string(wh) +
      ",\"source\":\"certified binary64 values (fp64.values) of c<N>.json; sha256 of that "
      "file\",\"rays\":{" +
      rays + "}";
  if (!exceptional.empty()) comment += ",\"exceptional\":{" + exceptional + "}";
  comment += "}";
  if (comment.size() > 65535)
    throw sic::Error(ErrorKind::resource, "npz: archive comment too long");
  return comment;
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
ByteBuffer npz_encode(std::size_t d, std::vector<NpzRay> rays) {
  uint64_t estimate = 22 + npz_comment(d, rays).size();
  for (const auto& ray : rays)
    estimate = support::admission::add(estimate,
                                       support::admission::add(76 + 2 * (ray.id.size() + 4) + 256,
                                                               support::admission::mul(d, 16)));
  admit_zip_profile(rays.size(), estimate);
  if (rays.empty()) throw sic::Error(ErrorKind::invalid_argument, "npz: no rays");
  std::sort(rays.begin(), rays.end(), [](const NpzRay& a, const NpzRay& b) { return a.id < b.id; });
  std::vector<ZipMember> members;
  for (std::size_t i = 0; i < rays.size(); ++i) {
    const auto& r = rays[i];
    if (r.id.empty() || !std::all_of(r.id.begin(), r.id.end(), [](unsigned char c) {
          return std::isalnum(c) || c == '.' || c == '_' || c == '-';
        }))
      throw sic::Error(ErrorKind::invalid_argument, "npz: unsafe ray id '" + r.id + "'");
    if (i && rays[i - 1].id == r.id)
      throw sic::Error(ErrorKind::invalid_argument, "npz: duplicate ray id " + r.id);
    if (r.psi.size() != d)
      throw sic::Error(ErrorKind::invalid_argument,
                       "npz: ray " + r.id + " has the wrong dimension");
    require_finite(r.psi, "npz ray " + r.id);
    members.push_back({r.id + ".npy", npy_encode(r.psi)});
  }
  return zip_store_encode(members, npz_comment(d, rays));
}
}  // namespace sic::core::interchange

namespace sic::core::interchange {
void require_npz_comment_identity(const std::string& comment, const std::string& what) {
  if (!comment.starts_with("{\"schema\":\"sic-npz-bundle/1.0\","))
    throw sic::Error(ErrorKind::unsupported, what + ": unsupported NPZ comment schema");
}

NpzContents npz_decode(const ByteBuffer& in, const std::string& what) {
  NpzContents c;
  auto members = zip_store_decode(in, &c.comment, what);
  require_npz_comment_identity(c.comment, what);
  for (auto& m : members) {
    if (m.name.size() <= 4 || m.name.compare(m.name.size() - 4, 4, ".npy") != 0)
      throw sic::Error(ErrorKind::invalid_format, what + ": member " + m.name + " is not .npy");
    c.arrays.emplace_back(m.name.substr(0, m.name.size() - 4),
                          npy_decode(m.data, what + ":" + m.name));
  }
  return c;
}
}  // namespace sic::core::interchange
