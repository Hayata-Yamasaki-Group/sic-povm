// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "construct/model/session_state.hpp"
#include "construct/output/action_elements.hpp"
namespace sic::construct {
struct Construction::TransportedValues {
  Construction& session;
  slong wp;
  arb_ptr omh;
  slong& omh_prec;
  const size_t& nl;
  std::vector<std::vector<sic::support::Index>>& gens;
  std::vector<std::unique_ptr<ActionElement>>& el;
  std::vector<std::vector<long>>& el_of;
  using Elem = ActionElement;
  struct Dec {
    slong wd = 0;
    std::vector<sic::support::ComplexBallVector> h2w;
    std::vector<std::vector<sic::support::ComplexBallVector>> Bo;
    bool symbolic_top = false;
    sic::support::ComplexBallVector top_dual;
    bool pruned = false;
    std::vector<std::vector<long>> labels;
    std::vector<std::vector<long>> odd_labels;
  };
  void lam_vals(const std::vector<Leaf>& lv, slong wd, sic::support::ComplexBallVector& v);
  void dual_at(acb_ptr out, int i, long c, const Dec& decoder);
  void inv_fft(sic::support::ComplexBallVector& a, int n, const Dec& Dc,
               sic::support::ComplexBallVector& b);
  void inv_fft_pruned(const std::vector<Leaf>& leaves, int n, const Dec& Dc,
                      sic::support::ComplexBallVector& out, const std::vector<long>& lab,
                      long offset = 0, ExactSource source = {}, bool lattice = false,
                      long known_bits = 0);
  void conj_of(const std::vector<Leaf>& lv, int n, const Dec& Dc,
               sic::support::ComplexBallVector& out, ExactSource source = {});
  void conj_of_action(const Elem& E, int n, const Dec& Dc, sic::support::ComplexBallVector& out);
  slong min_acc(const sic::support::ComplexBallVector& v);
  bool build_dec(slong wd, Dec& Dc, std::string& why);
};
}  // namespace sic::construct
