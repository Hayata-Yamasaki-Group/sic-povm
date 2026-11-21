// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>

#include "construct/components/components.hpp"
#include "construct/model/entry_order.hpp"
#include "construct/model/memory_plan.hpp"
#include "construct/model/options.hpp"
#include "construct/model/run_services.hpp"
#include "exact/analytic/direct_eta_rows.hpp"
#include "exact/analytic/product_candidate.hpp"
#include "exact/analytic/table.hpp"
#include "exact/arithmetic/maximal_order.hpp"
#include "exact/certification/reconstruction/ghost.hpp"
#include "exact/group/group.hpp"
#include "exact/storage/completed_class_cache.hpp"
#include "exact/storage/ghost_class_backing.hpp"
#include "exact/storage/leaf_source.hpp"
#include "exact/transform/fixed_field_tower_general.hpp"
#include "support/periodic_grid.hpp"
namespace sic::exact::phase_memory {
template <class Seed>
class PreparedSeed;
}
namespace sic::construct::resources {
struct Model;
struct ResidentCensus;
}  // namespace sic::construct::resources
namespace sic::construct {
struct NewtonSeedState;
struct NewtonCachePlan;
struct NewtonResidentPlan;
class QuarantineOutput;
using namespace sic::exact;
using sic::exact::certified::HeightBound;
using ExactSource = sic::exact::storage::Source;
using PrivateLeafStore = sic::exact::storage::Private;

struct NativeAnalyticIdentity {
  std::filesystem::path manifest;
  std::string digest, source;
};

// Restore captured local-owner hooks before the referenced owners leave scope, including retries.
template <class T>
struct ScopedMemoryHook {
  T& slot;
  T previous;
  ScopedMemoryHook(T& target, T value) : slot(target), previous(std::move(target)) {
    slot = std::move(value);
  }
  ~ScopedMemoryHook() { slot = std::move(previous); }
  ScopedMemoryHook(const ScopedMemoryHook&) = delete;
};

// Fresh usable headroom, constrained by cgroup/job limits and the user ceiling.

// Recognition of beta = x + y omega in O_K = Z[omega] from a real ball bt ~ beta (at the embedding
// sqrt(D0) > 0) and the bound |beta'| <= 2^log2Bp at the other embedding. The planar lattice
//   { x (S, 1) + y (S omega, omega') },  S = 2^k,  k = ceil(2 log2Bp) + 8,
// is reduced once; each call is a Babai rounding plus a certified check. The basis is stored scaled
// by 2^r and rounded; the rounding perturbs the lattice point of beta by about |y|, which must stay
// far below the first minimum 2^{r + k/2}. So r >= log2|beta| - k/2 + margin, with log2|beta| < wp
// (larger leaves are not resolved at wp anyway).

// One run owns these values for precisely the lifetime of the construction. Stages exchange
// references to these owners; splitting does not copy ball arrays or change reduction order.
struct ActionElement;
struct ClassRefinement;
class Construction {
  std::function<void(sic::construct::resources::ResidentCensus&)> with_local(
      std::function<void(sic::construct::resources::ResidentCensus&)> extra);
  void refine_class_data(ClassRefinement& state, long c, const Table& Ts, bool check_seed,
                         sic::support::resources::Bytes private_slot,
                         sic::support::resources::Bytes callback_slot);
  template <class F>
  void for_each_tower_element(const std::vector<std::unique_ptr<ActionElement>>& el, F&& f);
  void restore_output_prefix(const sic::sicx::Meta& sicx_header,
                             const sic::sicx::StreamOptions& sicx_options,
                             const std::vector<std::unique_ptr<ActionElement>>& el,
                             std::unique_ptr<sic::sicx::ProducerWriter>& sicx_out,
                             const std::string& rescaling_note, size_t tower_elements,
                             size_t& tower_adopted, int representative_count,
                             std::vector<char>& rep_adopted,
                             std::vector<uint64_t>& journaled_shifts);
  struct TransportedValues;
  struct RepresentativeLeaves;
  struct FreshRepresentative;
  int recognize_fresh_representative(FreshRepresentative& operation);
  void collect_representative_block(FreshRepresentative& operation, size_t first,
                                    std::span<const Leaf> block);
  void spool_representative_block(FreshRepresentative& operation, size_t first,
                                  std::span<Leaf> block);
  void release_representative_forward(int r);
  void process_representatives(RepresentativeLeaves& state);
#ifdef SIC_ENABLE_TEST_HOOKS
  void trace_representative_owners(const char* stage, int r, const RepresentativeLeaves& state);
#endif
  void initialize_actions(size_t nl, std::vector<std::vector<sic::support::Index>>& gens,
                          std::vector<std::unique_ptr<ActionElement>>& el,
                          std::vector<std::vector<long>>& el_of);
  int recognize_actions(slong wp, arb_srcptr sq0, PrivateLeafStore& spool,
                        std::vector<std::vector<sic::support::Index>>& gens,
                        std::vector<std::unique_ptr<ActionElement>>& el,
                        std::vector<std::vector<long>>& el_of, long& n_act);
  void plan_output(slong wp, int representative_count,
                   const std::vector<std::unique_ptr<ActionElement>>& el,
                   const std::filesystem::path& sicx_dir, sic::sicx::Meta& sicx_header,
                   sic::sicx::StreamOptions& sicx_options,
                   sic::support::resources::Bytes& sicx_tower_bound,
                   std::vector<sic::support::resources::Bytes>& sicx_rep_bounds,
                   sic::support::resources::Bytes sicx_meta_reserve);

  const ProducerOptions& producer_options;
  sic::support::resources::Budget budget;
  MemoryPlanner memory{budget};
  void plan_final(slong precision);
  void adjust_memory(const char* phase, slong precision);
  sic::support::resources::Bytes inspect_final_owners() const;
  std::array<MemoryPlanner::PhaseReservation, 5> project_final(
      const sic::construct::resources::Model& model, slong precision, slong decoding,
      sic::support::resources::Bytes rss, sic::support::resources::Bytes owned) const;
  void reserve_final(const char* phase, unsigned first_phase, slong precision, slong decoding);
  slong final_generator_precision = 0;
  bool final_table_immutable = false;
  mutable std::optional<sic::support::resources::Bytes> immutable_table_owned;
  std::function<void(sic::construct::resources::ResidentCensus&)> inspect_final_local;
  std::function<sic::support::resources::Bytes(unsigned, sic::support::resources::BitCount)>
      final_local_credit;
  std::unique_ptr<Arithmetic> ar_;
  std::unique_ptr<sic::construct::resources::Model> model_;
  std::unique_ptr<Group> group_;
  std::unique_ptr<ProductCandidate> product_;
  Context component_context{budget, 1, {}, &producer_options.policy};
  using StepData = sic::construct::EvaluatedTower;
  struct RelativeCertificate {
    int level;
    long character, tau, anchor;
    slong precision;
    std::vector<long> kernel_generators, target_generators;
    std::string real_ball, imaginary_ball;
  };
  struct Cand {
    const Table* tb = nullptr;
    long tj = 0;
    long zpow = 0;
    bool sq = false, product = false;
    int highest_needed = -1;
    double C = 1;
    HeightBound certified_C;
    std::string name;
  };
  struct EntryCtx {
    sic::support::ComplexBallVector Tord;
    std::vector<acb_srcptr> Tptr;
    std::vector<sic::support::Index> oi;
    const Table* direct = nullptr;
    std::function<void(sic::support::Index)> load_class, drop_class;
    acb_srcptr at(long q) const { return Tptr.empty() ? Tord[q] : Tptr[q]; }
  };
  struct Item {
    int i;
    long k;
  };
  struct Path {
    std::vector<sic::support::ComplexBallVector> B;
    sic::support::ComplexBallVector root{0};
    slong wpd = 0;
    double score = 0;
  };
  std::function<void(sic::construct::resources::ResidentCensus&)> inspect_newton_caller;
  int s{};
  slong prec_user{};
  unsigned seed{};
  int requested_threads{};
  int nth{};
  static constexpr int kCorrectionRequested = 2;
  const char* completed_cache_root{};
  bool pinned_inputs{};
  double T0{};
  bool maximal_order{};
  reduced_order::CycleUnit cycle_unit{};
  bool unit_power{};
  long d{};
  bool phase_planned{};
  bool stream_actions{};
  bool stream_reps{};
  bool stream_steps{};
  bool compact_work{};
  sic::support::resources::Count states{};
  sic::support::resources::Count max_cycle{};
  slong initial_precision{};
  CompletedCacheRequirement require_completed_cache{};
  sic::support::resources::Count generator_block{};
  sic::support::resources::Count first_pass{};
  long D{};
  const char* group_cache{};
  std::string verified_group_digest{};
  bool cached_group{};
  std::vector<long> sig_odd{};
  sic::sicx::Meta entry_metadata;
  EntryOrder entry_translations;
  std::vector<long> entry_actions;
  long N{};
  std::shared_ptr<OrbitTableLayout> analytic_layout{};
  long Lc{};
  int m{};
  static constexpr int kProductFactors = 2;
  int relative_highest{};
  std::vector<RelativeCertificate> relative_certificates{};
  long cc_pre{};
  std::vector<char> derived{};
  std::vector<TableClassBacking> pending_table_backing{};
  std::unique_ptr<CompletedClassCache> completed_cache{};
  CompletedClassCache::Inventory completed_inventory{};
  slong completed_precision{};
  std::vector<long> completed_elements{};
  NativeAnalyticIdentity verified_coarse{};
  slong first_pass_bits{};
  std::unique_ptr<Table> T{};
  std::vector<char> inSupp{};
  std::vector<char> inA{};
  double B1{};
  long supp_count{};
  long Dstar{};
  long Ssz{};
  long cc{};
  std::string completed_producer{};
  std::unique_ptr<ScalarFixedFieldsGeneral> scalar_fields{};
  std::vector<long> phis, ps{};
  std::vector<char> X{};
  int t{};
  std::vector<long> Ncos{};
  std::vector<sic::support::Index> cos_top{};
  std::vector<std::vector<long>> rep{};
  std::vector<long> cos_scale{};
  std::vector<sic::support::Index> cos_shift{};
  bool phased_storage{};
  bool table_parked{};
  std::vector<TableClassBacking> table_backing{};
  std::vector<std::vector<sic::support::Index>> analytic_by_class{};
  sic::support::Index analytic_anchor{};
  std::vector<sic::support::Index> step_sel{};
  std::vector<HeightBound> selected_bounds{};
  sic::construct::Components components{};
  sic::construct::PrecisionManager component_precision{};
  sic::construct::CheckpointStore component_checkpoints{};
  sic::construct::TowerShape tower_shape{};
  sic::construct::TowerContext tower_context{};
  std::unique_ptr<GeneratorEvaluator> generator_evaluator{};
  std::vector<GeneratorChoice> generator_choices{};
  long D0{};
  HeightBound Ceta{};
  bool direct_eta_check{};
#ifdef SIC_ENABLE_TEST_HOOKS
  std::string direct_eta_fault{};
#endif
  std::unique_ptr<GhostRefiner::Ctx> direct_eta_roots{};
  std::unique_ptr<DirectEtaRows> direct_eta_rows{};
  bool direct_eta_rows_planned = false;
  void prepare_direct_rows(sic::support::resources::Bytes workspace, slong precision);
  int requested_band_bits{};
  std::vector<Item> items{};
  std::vector<std::vector<sic::support::Index>> item_idx{};
  slong P{};
  std::vector<std::vector<Leaf>> ilv{};
  std::vector<ExactSource> item_sources{};
  std::vector<char> item_constant{};
  std::unique_ptr<PrivateLeafStore> item_spool{};
  StepData sdt{};
  sic::support::ComplexBallVector vec{0};
  Path path{};
  long maxbits{};
  const char* outp{};
  bool stream_tables{};
  double t_exact{};
  double stamp();
  long chiexp(long m, long g);
  void configure_table(Table& table, bool early_finite = false);
  void derive_classes(Table& Tb);
  void initialize_pending(bool stream, slong precision);
  void park_completed_class(Table& table, sic::support::Index c,
                            std::unique_ptr<CertifiedGhostRoot> root = {}, long element = -1,
                            const std::string& method = "quadrature");
  std::unique_ptr<Table> build_table(slong P, bool early_finite = false, bool stream = false);
  inline acb_srcptr tval(const Table& T, long x);
  void conj_f_at(const Table& T, long i, long j, long g, acb_ptr out);
  void root_2d(long k, slong wp, acb_ptr out);
  void product_at(const Table& Tb, long g, slong wp, const sic::support::ComplexBallVector& roots,
                  const arb_t sqrt_eps, acb_ptr out);
  void generator_at(const Table& Tb, int j, long g, slong wp, acb_ptr out);
  sic::support::ComplexBallVector gen_vector(const Table& Tb, int j, slong wp,
                                             const sic::support::PeriodicGrid* grid = nullptr);
  NativeAnalyticIdentity inspect_coarse_identity(const std::filesystem::path& checkpoint,
                                                 const std::string& source);
  std::unique_ptr<Table> build_pass1(slong P);
  void prepare_completed_cache(slong precision, bool stream);
  bool build_newton_seed(NewtonSeedState& state, long c,
                         phase_memory::PreparedSeed<Table>& pending_seed);
  NewtonCachePlan newton_cache_plan(slong precision, bool stream);
  NewtonResidentPlan newton_resident_plan(slong precision,
                                          sic::support::resources::Bytes retained_labels,
                                          bool stream);
  std::unique_ptr<Table> newton_table(slong P, sic::support::resources::Bytes retained_labels = 0,
                                      bool stream = false);
  inline sic::support::Index coset(int i, long g);
  long source_class(long g);
  void load_table_class(sic::support::Index c);
  void drop_table_class(sic::support::Index c);
  void park_table();
  StepData step_data_multi(const std::vector<Cand>& cands, slong wp, bool verbose,
                           const std::vector<sic::support::Index>* sel_in,
                           bool back_generators = false);
  std::vector<Cand> cand_list();
  void make_entry_ctx(const Table& Tb, EntryCtx& E);
  void eta_entry(const EntryCtx& E, long mm, slong wp, const sic::support::ComplexBallVector& z2d,
                 const arb_t sqd1, sic::support::ComplexBallVector& out, int nthr,
                 const std::vector<long>* requested = nullptr,
                 const std::function<void(long, long, acb_srcptr)>* consumer = nullptr,
                 long requested_group = 1);
  int rep_of(long mm, long& sig);
  long rep_entry(int r);
  void leaves_fft_real(sic::support::RealBallVector& a, int n, const StepData& sd, slong wp,
                       int nthr, sic::support::RealBallVector* buf = nullptr);
  slong log2_bp_of(const StepData& sd, int n, const HeightBound& Ca);
  slong representative_height(const StepData& sd, int r);
  sic::support::resources::BitCount recognition_bits(const sic::support::RealBallVector& values,
                                                     slong conjugate_height);
  void admit_recognition_block(const sic::support::RealBallVector& values, slong conjugate_height);
  void trim_heap();
  void release_band_heap(const char* where, long eta, long band, long bands);
  bool direct_eta_class(const EntryCtx& context, sic::support::Index source, long mm, slong wp,
                        const sic::support::ComplexBallVector& roots, const arb_t sqrt_d1,
                        const std::vector<long>& requested,
                        const std::function<void(long, long, acb_srcptr)>& consume, long group);
  void class_entries(const EntryCtx& context, sic::support::Index source, long mm, slong wp,
                     const sic::support::ComplexBallVector& roots, const arb_t sqrt_d1,
                     const std::vector<long>& requested,
                     const std::function<void(long, long, acb_srcptr)>& consume, long group);
  void representative_channels(const EntryCtx& context, int r, slong wp,
                               const sic::support::ComplexBallVector& roots, const arb_t sqrt_d1,
                               const StepData& sd, sic::support::RealBallVector& channels);
  void transform_channels(sic::support::RealBallVector& channels, int r, const StepData& sd,
                          slong wp, sic::support::RealBallVector* scratch = nullptr,
                          int band_bits = 0);
  int representative_band_bits(int r);
  void representative_band_channels(const EntryCtx& context, int r, slong wp,
                                    const sic::support::ComplexBallVector& roots,
                                    const arb_t sqrt_d1, const StepData& sd, int bits, long band,
                                    sic::support::RealBallVector& channels);
  void item_vec(const StepData& sd, const Item& it, slong wp, sic::support::ComplexBallVector& a);
  void item_leaves(const StepData& sd, const Item& it, slong wp, sic::support::RealBallVector& y,
                   int nthr);
  HeightBound item_height(const StepData& sd, const Item& it);
  bool constant_item(const StepData& sd, const Item& it);
  double max_log2rad(const sic::support::RealBallVector& a);
  sic::support::ComplexBallVector roots_2d(slong wp);
  void fold(acb_t r, const std::vector<Leaf>& lf, int n,
            const std::vector<sic::support::ComplexBallVector>& B, slong wpd, const arb_t omc,
            ExactSource source = {});
  bool candidates(int i, const Path& pt, const arb_t omc, sic::support::ComplexBallVector& cand,
                  std::vector<double>& gs, std::vector<sic::support::ComplexBallVector>& Bc);
  bool compute_path(slong wpd, const Path* prev, Path& pt);
  std::unique_ptr<QuarantineOutput> quarantine;
  std::string quarantine_name;
  sic::sicx::Digest completed_root{};
  std::unique_ptr<sic::sicx::ProducerWriter> open_writer(const sic::sicx::Meta&,
                                                         const sic::sicx::StreamOptions&);
  const std::vector<GeneratorChoice>* evaluation_choices = nullptr;
  void apply_generator_choice(int, AnalyticGenerator&, const GeneratorChoice&, slong);
  int alignment_selection();
  int write_attempt(slong wp, arb_srcptr sq0);
  int initialize();
  int group_stage();
  int structure();
  int tower();
  int step_data();
  int entries();
  int precision();
  int branch_path();
  int final_pass();
  int sic_check();

 public:
  ~Construction();
  Construction(const ProducerOptions& options, Components chosen = {},
               const RunServices& runtime = {});
  Construction(const Construction&) = delete;
  Construction& operator=(const Construction&) = delete;
  Construction(Construction&&) = delete;
  Construction& operator=(Construction&&) = delete;
  int run();
};

}  // namespace sic::construct
