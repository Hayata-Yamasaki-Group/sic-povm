// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/contraction.hpp"
#include "decode/decoder.hpp"
#include "decode/input/element_source.hpp"
#include "sic/error.hpp"
#include "sicx/codec/integer.hpp"

namespace sic::decode {
Decoder::Impl::Options Decoder::Impl::validate_options(Options o, const Reader& reader) {
  if (!o.threads || !o.attempts || o.root_iterations < 1 ||
      o.root_iterations > sic::kNativeMaximumPrecision || o.tower_resident < -1 ||
      o.tower_resident > 1 || !o.memory_budget || !o.maximum_graph_nodes || !o.cancellation ||
      o.delta < -1 || o.base_bits < -1 || o.target < kMinimumTargetBits)
    throw sic::Error(ErrorKind::invalid_argument, "invalid decoder options");
  if (o.threads > o.maximum_threads)
    throw sic::Error(ErrorKind::resource, "decoder worker count exceeds policy");
  if (checked_mul(o.threads, 4096) > o.memory_budget)
    throw sic::Error(ErrorKind::resource, "decoder worker metadata exceeds memory policy");
  if (reader.prec()) {
    const sic::support::Json hint = reader.json_chunk(*reader.prec());
    if (o.delta == -1 && hint.has("delta_bits")) o.delta = hint.at("delta_bits").as_int();
    if (o.base_bits == -1 && hint.has("base_path_bits"))
      o.base_bits = hint.at("base_path_bits").as_int();
  }
  if (o.delta == -1) o.delta = kDefaultGuardBits;
  if (o.base_bits == -1)
    o.base_bits = std::min<slong>(kDefaultInitialPrecisionCap, o.maximum_precision);
  const auto initial = sic::checked_precision(o.target, o.delta, o.maximum_precision);
  if (o.base_bits < kMinimumTargetBits)
    throw sic::Error(ErrorKind::invalid_argument, "invalid decoder base precision");
  if (o.base_bits > o.maximum_precision)
    throw sic::Error(ErrorKind::precision, "decoder base precision exceeds policy");
  o.base_bits = std::max(o.base_bits, initial);
  if (reader.source_memory_reservation() >= o.memory_budget)
    throw sic::Error(ErrorKind::resource, "source headroom exceeds decoder memory budget");
  if (flint_get_num_threads() != 1)
    throw sic::Error(ErrorKind::invalid_argument, "decoder requires one native FLINT owner worker");
  return o;
}

Decoder::Impl::Impl(const Reader& reader, Options options)
    : reader_(reader),
      meta_(reader.meta()),
      options_(validate_options(std::move(options), reader)),
      place_(Placement::detect(options_.pin)),
      budget_(options_.memory_budget, options_.threads) {
  sic::support::StopScope stop(options_.cancellation.get());
  sic::support::check_stop();
  reader_.bind_cancellation(options_.cancellation);
  if (reader_.remote()) plan_input({});
  pool_ = std::make_unique<Pool>(options_.threads, place_);
  load_tower();
}

std::vector<size_t> Decoder::Impl::input_chunks(const Reader& reader,
                                                const std::vector<Request>& requests) {
  std::set<size_t> elements, chunks;
  const auto& meta = reader.meta();
  for (size_t id = 0; id < meta.elements.size(); ++id)
    if (meta.elements[id].kind != "rep") elements.insert(id);
  for (const auto& r : requests) {
    if (!meta.representative.contains(r.representative))
      fail(sic::ErrorKind::invalid_argument, "request names a missing representative");
    if (r.word.size() != meta.smith.size())
      fail(sic::ErrorKind::invalid_argument, "request word rank mismatch");
    for (size_t j = 0; j < r.word.size(); ++j)
      if (r.word[j] >= meta.smith[j])
        fail(sic::ErrorKind::invalid_argument, "request word is not a canonical residue");
    elements.insert(meta.representative.at(r.representative));
  }
  for (auto id : elements)
    for (auto leaf : reader.element_blocks(id)) chunks.insert(reader.leaf_owner(leaf));
  if (reader.prec()) chunks.insert(*reader.prec());
  return {chunks.begin(), chunks.end()};
}

void Decoder::Impl::plan_input(const std::vector<Request>& requests) {
  auto chunks = input_chunks(reader_, requests);
  reader_.plan_chunks(chunks);
  reader_.schedule_chunks(chunks);
  for (const auto& r : requests) planned_representatives_.insert(r.representative);
}

Decoder::Impl::Impl(const Reader& reader, Options options, const std::vector<Request>& requests)
    : Impl(
          [&]() -> const Reader& {
            (void)validate_options(options, reader);
            if (reader.remote()) reader.plan_chunks(input_chunks(reader, requests));
            return reader;
          }(),
          options) {
  if (reader_.remote()) plan_input(requests);
}

ComplexBallVector Decoder::Impl::decode(const std::vector<Request>& requests) {
  check_owner();
  sic::support::StopScope stop(options_.cancellation.get());
  const auto metadata_bytes =
      checked_mul(requests.size(), checked_add(256, checked_mul(meta_.smith.size(), 32)));
  ReserveGuard metadata_guard(budget_, metadata_bytes, "request metadata");
  // A precision ceiling does not allocate values at that precision. Keep the
  // existing three-array allowance at the largest precision actually attempted,
  // including successful outputs retained while only the pending entries replay.
  std::vector<std::unique_ptr<ReserveGuard>> numeric_guards;
  uint64_t numeric_reserved = 0;
  auto reserve_numeric = [&](slong bits) {
    const auto bytes = checked_mul(requests.size(), checked_mul(3, acb_bytes(bits)));
    if (bytes > numeric_reserved) {
      auto guard = std::make_unique<ReserveGuard>(budget_, bytes - numeric_reserved,
                                                  "request/output staging");
      numeric_guards.push_back(std::move(guard));
      numeric_reserved = bytes;
    }
  };
  slong wp = options_.target + options_.delta;
  reserve_numeric(wp);
  (void)input_chunks(reader_, requests);  // validate before numerical work
  if (reader_.remote() && std::any_of(requests.begin(), requests.end(), [&](const auto& r) {
        return !planned_representatives_.contains(r.representative);
      }))
    plan_input(requests);
  base_path();
  ComplexBallVector out(requests.size());
  std::vector<size_t> pending(requests.size());
  std::iota(pending.begin(), pending.end(), 0);
  for (unsigned attempt = 0; !pending.empty();) {
    sic::support::check_stop();
    if (attempt == options_.attempts || wp > options_.maximum_precision)
      throw sic::Error(ErrorKind::precision, "decoder precision/attempt ceiling reached");
    reserve_numeric(wp);
    ++attempt;
    std::vector<Request> batch;
    for (auto q : pending) batch.push_back(requests[q]);
    progress() << "SICX-DECODE pass " << attempt << " precision=" << wp
               << " outputs=" << batch.size() << std::endl;
    try {
      ++stats_.passes;
      const ComplexBallVector values = decode_pass(batch, wp);
      std::vector<size_t> failing;
      for (size_t i = 0; i < pending.size(); ++i) {
        if (accurate(values[i]))
          acb_set(out[pending[i]], values[i]);
        else
          failing.push_back(pending[i]);
      }
      if (!failing.empty())
        progress() << "SICX-DECODE radius check: " << failing.size() << " outputs replayed"
                   << std::endl;
      pending = std::move(failing);
    } catch (const PrecisionRefusal& refusal) {
      progress() << "SICX-DECODE precision refusal at " << wp << ": " << refusal.what()
                 << std::endl;
    }
    if (!pending.empty()) {
      ++stats_.replays;
      if (wp > options_.maximum_precision / 2)
        throw sic::Error(ErrorKind::precision, "decoder precision ceiling reached");
      wp *= 2;
      if (wp > base_prec_) {
        base_roots_.clear();
        options_.base_bits =
            wp > options_.maximum_precision / 2 ? options_.maximum_precision : 2 * wp;
        base_path();
      }
    }
  }
  reader_.quiesce();
  for (const auto& r : requests) consumed_representatives_.insert(r.representative);
  if (reader_.remote() && !exact_checked_ &&
      consumed_representatives_.size() == meta_.representative.size()) {
    // Serial canonical checks avoid a second unbudgeted window of expanded
    // integer leaves. Admit their conservative scratch envelope alongside RSS
    // and the source reservation, just like numerical BODY work.
    rebase();
    const auto scratch = bounds_memory(reader_);
    ReserveGuard verification(budget_, scratch, "remote exact canonicality checks");
    (void)verify_exact(reader_, 1, {{options_.maximum_precision}, {}});
    exact_checked_ = true;
  }
  return out;
}

}  // namespace sic::decode

namespace sic::decode {
ComplexBallVector Decoder::Impl::decode_pass(const std::vector<Request>& requests, slong wp) {
  const auto plan_start = std::chrono::steady_clock::now();
  const Graph graph = plan(requests);
  stats_.seconds_plan += seconds(plan_start);
  std::vector<std::vector<uint64_t>> node_classes;
  std::vector<uint64_t> endpoint_class;
  Tree out_tree = [&] {
    Tree full = prepare(graph, wp, false, &node_classes);
    const auto prune_start = std::chrono::steady_clock::now();
    Tree pruned = prune(full, node_classes, graph, endpoint_class);
    release_tree(full);
    stats_.seconds_prune += seconds(prune_start);
    pruned.serial = ++tree_serial_;
    return pruned;
  }();
  node_classes.clear();
  node_classes.shrink_to_fit();
  progress() << "SICX-DECODE DAG nodes=" << graph.nodes.size()
             << " output classes=" << out_tree.count(meta_.levels())
             << " compute_workers=" << pool_->physical_parallelism() << "/" << pool_->size()
             << std::endl;
  ComplexBallVector values(requests.size());
  for (size_t q = 0; q < requests.size(); ++q) {
    if (!meta_.representative.contains(requests[q].representative))
      fail(sic::ErrorKind::invalid_argument, "request names a missing representative");
    acb_indeterminate(values[q]);
  }
  const auto start = std::chrono::steady_clock::now();
  const unsigned height = meta_.levels();
  // Every chunk of every requested representative is one task (parsed and
  // contracted while it streams); then all representatives are lifted together.
  rebase();
  auto body = plan_body(requests, out_tree, wp);
  auto& reps = body.reps;
  auto& parts = body.parts;
  auto& level = body.level;
  const auto& rep_jobs = body.jobs;
  const auto largest = body.largest;
  // Representatives in groups whose block results fit together with one chunk
  // task (all of them at once when the budget allows; one at a time at least).
  std::atomic<uint64_t> body_bytes{0};
  double lift_seconds = 0;
  size_t groups = 0;
  for (size_t first = 0; first < reps.size();) {
    const auto admission = plan_body_admission(body, out_tree, first);
    const size_t last = admission.last;
    ++groups;
    if (last - first < reps.size()) ++stats_.budget_fallbacks;
    ReserveGuard guard(budget_, admission.reserve, "representative block results");
    ReserveGuard scheduling_guard(budget_, admission.scheduling,
                                  "physical BODY scheduling metadata");
    size_t job_count = 0;
    for (size_t r = first; r < last; ++r) job_count += rep_jobs[r].size();
    std::vector<BodyJob> jobs;
    jobs.reserve(job_count);
    for (size_t r = first; r < last; ++r) {
      parts[r].val.resize(parts[r].pos.size());
      jobs.insert(jobs.end(), rep_jobs[r].begin(), rep_jobs[r].end());
    }
    // Scheduling follows physical ownership, while every numerical contraction
    // still follows the canonical logical subtree and writes its original slot.
    // Hold one verified unit for all of its leaves so eviction cannot cause
    // reparsing, and let different owners verify/contract on different workers.
    const auto physical = physical_tasks(jobs);
    auto contract = [&](const BodyJob& job, const Reader::ElementChunk& unit) {
      body_bytes += contract_body(job, unit, body, out_tree, parts);
    };
    const uint64_t carry_window = admission.carry;
    auto stream_physical = [&](size_t first_owner) {
      pool_->run(
          physical.size() - first_owner,
          [&](size_t q) {
            const auto& task = physical[first_owner + q];
            Admission admission(budget_, task.bytes);
            const auto unit = reader_.element_chunk(task.owner, true);
            for (const auto& job : task.leaves) {
              sic::support::check_stop();
              contract(job, unit);
            }
          },
          2, true);
    };
    if (carry_window >= budget_.available() || largest > budget_.available() - carry_window) {
      // A physical owner is already a group of leaves. Refresh live admission
      // between owners because the reader may retain each completed unit.
      stream_physical(0);
    } else {
      // Verify each whole unit before exposing its leaves; schedule their
      // canonical logical subtrees independently. Physical grouping must not
      // reduce numerical parallelism or change a rounding boundary.
      for (size_t begin = 0; begin < physical.size();) {
        sic::support::check_stop();
        size_t end = begin;
        uint64_t held = 0;
        // Between joined batches no decoder task can allocate. Include retained
        // reader-cache pages in this fresh sizing sample; admission samples again
        // before publication, covering all payload and carry growth together.
        const uint64_t rss = resident_bytes();
        const uint64_t live_room = rss < budget_.budget() ? budget_.budget() - rss : 0;
        const uint64_t room = std::min(budget_.available(), live_room);
        const uint64_t payload_room = room > carry_window ? room - carry_window : 0;
        while (end < physical.size() && physical[end].bytes <= payload_room - held)
          held += physical[end++].bytes;
        if (end == begin) {
          ++stats_.budget_fallbacks;
          stream_physical(begin);
          break;
        }
        // Admit the entire bounded carry window before publishing numerical
        // groups. Units stay immutable/alive until the dispatch has joined.
        Admission payload_and_carries(budget_, checked_add(held, carry_window));
        std::vector<Reader::ElementChunk> units(end - begin);
        pool_->run(units.size(), [&](size_t k) {
          units[k] = reader_.element_chunk(physical[begin + k].owner, true);
        });
        std::vector<std::pair<size_t, const BodyJob*>> ready;
        ready.reserve(jobs.size());
        for (size_t k = 0; k < units.size(); ++k)
          for (const auto& leaf : physical[begin + k].leaves) ready.push_back({k, &leaf});
        // The pool already claims bounded chunks dynamically. An outer group
        // per worker reduces the participating team when the leaf count is not
        // divisible by its width and prevents stealing within a slow group.
        // The admitted carry window covers every physical worker until join.
        pool_->run(
            ready.size(),
            [&](size_t k) {
              sic::support::check_stop();
              contract(*ready[k].second, units[ready[k].first]);
            },
            2, true);
        begin = end;
      }
    }
    const auto lift_start = std::chrono::steady_clock::now();
    std::vector<Sparse> group(std::make_move_iterator(parts.begin() + static_cast<long>(first)),
                              std::make_move_iterator(parts.begin() + static_cast<long>(last)));
    std::vector<unsigned> group_level(level.begin() + static_cast<long>(first),
                                      level.begin() + static_cast<long>(last));
    lift(out_tree, group, group_level, height);
    for (size_t r = first; r < last; ++r) {
      const ComplexBallVector all = top_value(group[r - first], out_tree.count(height));
      for (size_t q = 0; q < requests.size(); ++q)
        if (requests[q].representative == reps[r]) acb_set(values[q], all[endpoint_class[q]]);
    }
    lift_seconds += seconds(lift_start);
    first = last;
  }
  stats_.body_bytes_read += body_bytes.load();
  stats_.seconds_body += seconds(start);
  progress() << "SICX-DECODE representatives=" << reps.size() << " groups=" << groups
             << " chunks=" << body.chunks << " t=" << seconds(start) << " lift=" << lift_seconds
             << std::endl;
  return values;
}
}  // namespace sic::decode
