// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/ball_file.hpp"

#include "support/io/write_all.hpp"

namespace sic::exact {
BallFile::Temporary::Temporary() {
  char name[] = "/tmp/sic-balls-XXXXXX";
  const int fd = mkstemp(name);
  if (fd < 0) throw sic::Error(sic::ErrorKind::io, "cannot create private ball backing");
  try {
    path = name;
  } catch (...) {
    close(fd);
    unlink(name);
    throw;
  }
  FILE* stream = fdopen(fd, "w+b");
  if (!stream) {
    close(fd);
    unlink(path.c_str());
    throw sic::Error(sic::ErrorKind::io, "ball backing fdopen failed");
  }
  file.reset(stream);
}

BallFile::Temporary::~Temporary() {
  file.reset();
  if (!path.empty()) unlink(path.c_str());
}

BallFile::Integers::Integers() {
  for (auto& x : value) fmpz_init(&x);
}

BallFile::Integers::~Integers() {
  for (auto& x : value) fmpz_clear(&x);
}

void BallFile::decompose(arb_srcptr ball, Integers& work) {
  if (!arb_is_finite(ball)) throw std::runtime_error("only finite balls may be backed");
  arf_get_fmpz_2exp(&work.value[0], &work.value[1], arb_midref(ball));
  fmpz_set_ui(&work.value[2], MAG_MAN(arb_radref(ball)));
  fmpz_set(&work.value[3], MAG_EXPREF(arb_radref(ball)));
}

void BallFile::read_ball(FILE* file, arb_ptr ball, Integers& work, leaf_codec::Count& budget) {
  for (auto& integer : work.value) leaf_codec::read_integer(file, &integer, budget);
  if (fmpz_sgn(&work.value[2]) < 0 || !fmpz_abs_fits_ui(&work.value[2]))
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid backed radius mantissa");
  const ulong radius = fmpz_get_ui(&work.value[2]);
  if ((radius == 0 && !fmpz_is_zero(&work.value[3])) ||
      (radius != 0 && (radius < (UWORD(1) << (MAG_BITS - 1)) || radius >= (UWORD(1) << MAG_BITS))))
    throw sic::Error(sic::ErrorKind::invalid_format, "unnormalized backed radius");
  arf_set_fmpz_2exp(arb_midref(ball), &work.value[0], &work.value[1]);
  fmpz_set(MAG_EXPREF(arb_radref(ball)), &work.value[3]);
  MAG_MAN(arb_radref(ball)) = radius;
  if (!arb_is_finite(ball))
    throw sic::Error(sic::ErrorKind::invalid_format, "nonfinite backed ball");
}

BallFile::BallFile(std::string path, size_t count, size_t components,
                   sic::support::resources::Bytes bytes, std::string digest)
    : path_(std::move(path)),
      digest_(std::move(digest)),
      count_(count),
      components_(components),
      bytes_(bytes) {
  if (!count_ || (components_ != 1 && components_ != 2) || digest_.size() != 64 ||
      digest_.find_first_not_of("0123456789abcdef") != std::string::npos ||
      std::filesystem::file_size(path_) != bytes_)
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid persistent ball backing metadata");
}

std::unique_ptr<FILE, decltype(&fclose)> BallFile::verified_input() const try {
  if (std::filesystem::file_size(path_) != bytes_ || checkpoint_digest(path_) != digest_)
    throw sic::Error(sic::ErrorKind::integrity, "ball backing checksum verification failed");
  std::unique_ptr<FILE, decltype(&fclose)> file(fopen(path_.c_str(), "rb"), fclose);
  if (!file) throw sic::Error(sic::ErrorKind::io, "cannot reopen ball backing");
  std::uint64_t header[6];
  if (fread(header, sizeof header, 1, file.get()) != 1)
    throw sic::Error(ferror(file.get()) ? sic::ErrorKind::io : sic::ErrorKind::invalid_format,
                     "truncated/unreadable ball backing header");
  if (header[0] != kMagic || header[1] != 1 || header[2] != MAG_BITS ||
      header[3] != sizeof(ulong) || header[4] != count_ || header[5] != components_)
    throw sic::Error(sic::ErrorKind::invalid_format, "ball backing metadata mismatch");
  return file;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

std::shared_ptr<BallFile> BallFile::save(const sic::support::RealBallVector& values) {
  return std::shared_ptr<BallFile>(
      new BallFile(values.size(), 1, [&](size_t q, size_t) { return values[q]; }));
}

std::shared_ptr<BallFile> BallFile::save(const sic::support::ComplexBallVector& values) {
  return std::shared_ptr<BallFile>(new BallFile(values.size(), 2, [&](size_t q, size_t part) {
    return part ? acb_imagref(values[q]) : acb_realref(values[q]);
  }));
}

sic::support::RealBallVector BallFile::real() const {
  if (components_ != 1) throw std::logic_error("complex ball backing requested as real");
  auto file = verified_input();  // validate before allocating the target vector
  sic::support::RealBallVector result(count_);
  load(file.get(), [&](size_t q, size_t) { return result[q]; });
  return result;
}

sic::support::ComplexBallVector BallFile::complex() const {
  if (components_ != 2) throw std::logic_error("real ball backing requested as complex");
  auto file = verified_input();
  sic::support::ComplexBallVector result(count_);
  load(file.get(), [&](size_t q, size_t part) {
    return part ? acb_imagref(result[q]) : acb_realref(result[q]);
  });
  return result;
}

std::shared_ptr<BallFile> BallFile::open(const std::filesystem::path& path, size_t count,
                                         size_t components, sic::support::resources::Bytes bytes,
                                         const std::string& digest) try {
  return std::shared_ptr<BallFile>(new BallFile(path.string(), count, components, bytes, digest));
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

void BallFile::publish(const std::filesystem::path& path) const try {
  std::unique_ptr<FILE, decltype(&fclose)> source(fopen(path_.c_str(), "rb"), fclose);
  std::unique_ptr<FILE, decltype(&fclose)> target(fopen(path.c_str(), "wbx"), fclose);
  if (!source || !target)
    throw sic::Error(sic::ErrorKind::io, "cannot copy to exclusive persistent ball backing");
  require_leaf_disk(target.get(), bytes_);
  std::vector<char> block(1024 * 1024);
  for (;;) {
    const size_t size = fread(block.data(), 1, block.size(), source.get());
    sic::support::write_all(fileno(target.get()),
                            {reinterpret_cast<const uint8_t*>(block.data()), size});
    if (size < block.size()) break;
  }
  if (ferror(source.get())) throw sic::Error(sic::ErrorKind::io, "ball backing copy read failed");
  sic::support::sync_file(fileno(target.get()));
  if (fclose(target.release()))
    throw sic::Error(sic::ErrorKind::io, "persistent ball backing close failed");
  if (std::filesystem::file_size(path) != bytes_ || checkpoint_digest(path) != digest_)
    throw sic::Error(sic::ErrorKind::integrity, "persistent ball backing copy checksum failed");
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

}  // namespace sic::exact
