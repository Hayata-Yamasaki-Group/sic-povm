// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "construct/output/action_elements.hpp"
#include "construct/output/transported_values.hpp"
#include "exact/storage/leaf_store.hpp"
#include "sic/sicx/io.hpp"
#include "sicx/codec/block.hpp"
namespace sic::construct {

struct KeptRepresentative {
  std::unique_ptr<sic::exact::storage::Private> spool;
  ExactSource source;
  std::vector<Leaf> lv;
  long ne = 0;
  long bits = 0;  // bit bound of the original pairs (a lattice-coded spool records the coded ones)
};

}  // namespace sic::construct

namespace sic::construct {
struct Construction::RepresentativeLeaves {
  const int& representative_count;
  slong& wp;
  arb_srcptr sq0;
  sic::support::resources::Bytes& sicx_meta_reserve;
  sic::support::resources::Bytes& sicx_tower_bound;
  size_t& tower_adopted;
  size_t& tower_elements;
  std::vector<char>& rep_adopted;
  std::vector<sic::support::resources::Bytes>& sicx_rep_bounds;
  const std::filesystem::path& sicx_dir;
  const std::filesystem::path& sicx_partial;
  TransportedValues::Dec& dec;
  TransportedValues& transported;
  EntryCtx& Ectx;
  arb_ptr& sqd1;
  sic::support::ComplexBallVector& z2d;
  sic::support::ComplexBallVector& v;
  sic::support::ComplexBallVector& vb;
  std::vector<std::vector<long>>& group;
  sic::support::RealBallVector& y;
  sic::support::RealBallVector& yb;
  std::vector<Leaf>& lv;
  int& st;
  std::vector<KeptRepresentative>& kept;
  const bool& rep_lattice;
  sic::sicx::RescalingCollector& collector;
  double& tm_reps;
  double& tm_fft;
  double& tm_rec;
  double& tm_inv;
  long& stored_rep_leaves;
  std::vector<uint64_t>& journaled_shifts;
  std::unique_ptr<sic::sicx::ProducerWriter>& sicx_out;
};
}  // namespace sic::construct

namespace sic::construct {
// Borrowed forward/recognition inputs for one fresh representative. No journal or writer access.
struct Construction::FreshRepresentative {
  int r, band_bits;
  slong wp;
  arb_srcptr sq0;
  EntryCtx& context;
  arb_srcptr sqrt_d1;
  const sic::support::ComplexBallVector& roots;
  sic::support::RealBallVector& channels;
  sic::support::RealBallVector& scratch;
  std::vector<Leaf>& leaves;
  std::unique_ptr<PrivateLeafStore>& spool;
  ExactSource& source;
  long& bits;
  bool lattice;
  sic::sicx::RescalingCollector& collector;
  long ne = 0;
  double values_time = 0, fft_time = 0, recognition_time = 0;
#ifdef SIC_ENABLE_TEST_HOOKS
  const RepresentativeLeaves* census = nullptr;
#endif
};
}  // namespace sic::construct
