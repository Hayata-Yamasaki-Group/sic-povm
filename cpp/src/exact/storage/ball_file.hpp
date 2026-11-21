// SPDX-License-Identifier: AGPL-3.0-or-later
// Immutable, private, checksum-verified backing for exact finite Arb balls.
// This is temporary storage, not the mathematical export format.
#pragma once

#include <array>
#include <memory>

#include "exact/group/group_checkpoint.hpp"
#include "exact_leaf_codec.hpp"
#include "sic/error.hpp"
#include "sic/support/ball_vectors.hpp"

namespace sic::exact {

class BallFile {
  struct Temporary {
    std::string path;
    std::unique_ptr<FILE, decltype(&fclose)> file{nullptr, fclose};
    Temporary();
    ~Temporary();
  };
  struct Integers {
    std::array<fmpz, 4> value;
    Integers();
    ~Integers();
  };
  std::unique_ptr<Temporary> temporary_;
  std::string path_;
  std::string digest_;
  size_t count_ = 0, components_ = 0;
  sic::support::resources::Bytes bytes_ = 0;
  static constexpr std::uint64_t kMagic = 0x53494342414c4c31ULL;

  static void decompose(arb_srcptr ball, Integers& work);
  static void read_ball(FILE* file, arb_ptr ball, Integers& work, leaf_codec::Count& budget);
  template <class Source>
  BallFile(size_t count, size_t components, Source&& source);
  BallFile(std::string path, size_t count, size_t components, sic::support::resources::Bytes bytes,
           std::string digest);
  std::unique_ptr<FILE, decltype(&fclose)> verified_input() const;
  template <class Destination>
  void load(FILE* file, Destination&& destination) const;

 public:
  BallFile(const BallFile&) = delete;
  BallFile& operator=(const BallFile&) = delete;
  static std::shared_ptr<BallFile> save(const sic::support::RealBallVector& values);
  static std::shared_ptr<BallFile> save(const sic::support::ComplexBallVector& values);
  sic::support::RealBallVector real() const;
  sic::support::ComplexBallVector complex() const;
  sic::support::resources::Bytes bytes() const { return bytes_; }
  size_t count() const { return count_; }
  const std::string& digest() const { return digest_; }
  const std::string& path() const { return path_; }
  static std::shared_ptr<BallFile> open(const std::filesystem::path& path, size_t count,
                                        size_t components, sic::support::resources::Bytes bytes,
                                        const std::string& digest);
  void publish(const std::filesystem::path& path) const;
};

}  // namespace sic::exact

#include "exact/storage/ball_file.tpp"
