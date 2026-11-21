// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <atomic>
#include <map>
#include <mutex>

#include "exact/arithmetic/arith.hpp"
#include "sic/support/ball_vectors.hpp"
#include "support/resources.hpp"
namespace sic::exact {
struct TableStateData {
  std::vector<acb_struct> L, S;
  long nodes = 0;
  double log2err = 0;
  double t_nodes = 0, t_setup = 0, t_main = 0;
};
// Synchronous borrowed kernel state; the Table outlives every call.
struct QuadratureNode {
  arb_struct abscissa, weight;
};
struct QuadratureRuleKey {
  long node_count;
  slong precision_bits;
  auto operator<=>(const QuadratureRuleKey&) const = default;
};
using TableRuleCache = std::map<QuadratureRuleKey, std::vector<QuadratureNode>>;
sic::support::resources::Bytes table_kernel_bytes(long d, long node_slab, std::mutex& gl_mutex,
                                                  const TableRuleCache& gl_cache, size_t nodes,
                                                  long rows, size_t differences, slong wp);
struct TableView {
  using SData = TableStateData;
  const Arithmetic& ar;
  slong& prec;
  long& d;
  int& nthreads;
  long& node_slab;
  std::vector<TableStateData>& sd;
  std::vector<sic::support::Index>& s_c;
  std::vector<sic::support::Index>& s_l;
  TableRuleCache& gl_cache;
  std::mutex& gl_mutex;
  std::function<void(sic::support::resources::Bytes)>& admit_workspace;
  std::atomic<sic::support::resources::Bytes>& kernel_peak;
  std::atomic<long>& maximum_nodes;
  const std::vector<QuadratureNode>& gl_rule(long n, slong wp);
  static double log_sinh(double x);
  double gl_nodes(std::vector<QuadratureNode>& nodes, const arb_t b, const arb_t eta, slong wp,
                  double umax, mag_t error, const std::function<void(size_t)>& admit_nodes = {});
  sic::support::resources::Bytes kernel_bytes(size_t nodes, long rows, size_t differences,
                                              slong wp) const;
  static void eighth_root(acb_t r, long k, slong p);
  void batch_state(sic::support::Index sid);
  void block_value(acb_t res, sic::support::Index c, long i, long j, slong p);
};
}  // namespace sic::exact
