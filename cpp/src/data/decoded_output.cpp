// SPDX-License-Identifier: AGPL-3.0-or-later
#include "data/decoded_output.hpp"

#include <sstream>

#include "sic/data/text_formats.hpp"
#include "sic/error.hpp"
#include "sic/sicx/json.hpp"
#include "sic/support/flint_raii.hpp"
#include "support/io/durable_output.hpp"
#include "support/hex.hpp"
namespace sic::data {

void require_new_output(const std::filesystem::path& path) try {
  if (std::filesystem::exists(std::filesystem::symlink_status(path)))
    throw sic::Error(sic::ErrorKind::io, "output exists: " + path.string());
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(ErrorKind::io, error.what());
}

std::string dump_ball(const acb_t z) {
  sic::support::flint::String re(arb_dump_str(acb_realref(z)));
  sic::support::flint::String im(arb_dump_str(acb_imagref(z)));
  std::string s = std::string(re.get()) + " | " + im.get();
  return s;
}

void write_balls(const std::filesystem::path& path, std::span<const acb_struct> v) try {
  require_new_output(path);
  try {
    support::atomic_write_stream(
        path,
        [&](std::ostream& out) {
          out << sic::data::kVectorBallsIdentity << "\nd " << v.size() << '\n';
          for (size_t j = 0; j < v.size(); ++j) out << dump_ball(&v[j]) << '\n';
          out << "end\n";
        },
        false, 0666);
  } catch (const sic::Error& error) {
    if (error.kind() != ErrorKind::io) throw;
    throw sic::Error(ErrorKind::io, "write failed: " + path.string());
  }
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(ErrorKind::io, error.what());
}

std::string json_escape(const std::string& text) {
  std::string out;
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      out += "\\u" + sic::support::hex_unsigned(c, 4);
    } else
      out += static_cast<char>(c);
  }
  return out;
}
void write_decode_provenance(const std::filesystem::path& path, const std::string& text) {
  require_new_output(path);
  try {
    support::atomic_write(path, text, false, 0666);
  } catch (const sic::Error& error) {
    if (error.kind() != ErrorKind::io) throw;
    throw sic::Error(ErrorKind::io, "cannot write decode.provenance.json");
  }
}
DecodedComparison write_decoded_fp64(const std::filesystem::path& fp_path,
                                     const sic::support::ComplexBallVector& w,
                                     const std::vector<sic::sicx::BallPart>& parts,
                                     uint64_t dimension) try {
  sic::support::flint::Arb b;
  uint64_t overlap = 0, same64 = 0;
  int64_t worst = INT64_MIN;
  require_new_output(fp_path);
  try {
    support::atomic_write_stream(
        fp_path,
        [&](std::ostream& fp) {
          for (uint64_t j = 0; j < dimension; ++j) {
            bool all = true, same = true;
            std::string line;
            for (int part = 0; part < 2; ++part) {
              arb_srcptr x = part ? acb_imagref(w[j]) : acb_realref(w[j]);
              sic::sicx::ball_part_arb(b, parts[2 * j + part]);
              all = all && arb_overlaps(x, b);
              const double ours =
                  sic::sicx::certified_round(x, "decoded coordinate " + std::to_string(j));
              const double theirs =
                  sic::sicx::certified_round(b, "BALL coordinate " + std::to_string(j));
              same = same && ours == theirs;
              line += (part ? " " : "") + sic::sicx::double_hex(ours);
              worst = std::max<int64_t>(
                  worst, mag_is_zero(arb_radref(x))
                             ? INT64_MIN
                             : static_cast<int64_t>(fmpz_get_si(MAG_EXPREF(arb_radref(x)))));
            }
            fp << line << '\n';
            overlap += all;
            same64 += same;
          }
        },
        false, 0666);
  } catch (const sic::Error& error) {
    if (error.kind() != ErrorKind::io) throw;
    throw sic::Error(ErrorKind::io, "write failed: " + fp_path.string());
  }

  return {overlap, same64, worst};
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(ErrorKind::io, error.what());
}

}  // namespace sic::data
