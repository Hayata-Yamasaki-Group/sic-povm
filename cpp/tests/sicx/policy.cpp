// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <complex>
#include <iostream>

#include "core/numeric/overlap_deviation.hpp"
#include "sicx/exact/exact.hpp"
#include "sicx/model/branches.hpp"

using C = std::complex<double>;
#include <climits>
#include <type_traits>

#include "decode/dag.hpp"
#include "decode/decoder.hpp"
#include "sic/bounds/overlap_bounds.hpp"
#include "sic/decode/decoder.hpp"
#include "sic/sicx/exact.hpp"
#include "sic/sicx/finalize.hpp"

namespace exact = sic::sicx::exact;
void require(bool b) {
  if (!b) throw std::runtime_error("policy regression assertion");
}
template <class F>
void refuses(sic::ErrorKind kind, F f) {
  try {
    f();
  } catch (const sic::Error& e) {
    require(e.kind() == kind);
    return;
  }
  throw std::runtime_error("expected typed refusal");
}
int main(int argc, char** argv) {
  try {
    require(argc == 4);
    flint_set_num_threads(1);
    const std::string_view name = argv[1];
    sic::sicx::Reader reader(argv[2]);
    sic::decode::Decoder::Options o;
    o.pin = false;
    const auto word = reader.meta().rays.front().word;
    const auto rep = reader.meta().representative.begin()->first;
    const std::vector<sic::decode::Decoder::Request> requests{{rep, word}};
    if (name == "overlap_reference") {
      for (size_t d : {3, 5, 6, 8}) {
        std::vector<C> v(d);
        for (size_t j = 0; j < d; ++j) v[j] = C(double(j + 1), double(j % 2)) / double(d);
        double expected = 0;
        for (size_t a = 0; a < d; ++a)
          for (size_t b = 0; b < d; ++b)
            if (a || b) {
              C z = 0;
              for (size_t j = 0; j < d; ++j)
                z += std::conj(v[(j + a) % d]) * v[j] *
                     std::polar(1., -2 * M_PI * double(j * b) / double(d));
              expected = std::max(expected, std::abs(std::norm(z) - 1. / double(d + 1)));
            }
        require(std::abs(sic::core::overlap_deviation(v) - expected) < 1e-10);
      }
    } else if (name == "overlap_admission") {
      sic::bounds::BallVerificationLimits l;
      l.maximum_dimension = 4096;
      l.maximum_work_score = UINT64_MAX;
      l.maximum_workspace_bytes = 0;
      sic::support::ComplexBallVector one(1);
      acb_one(one[0]);
      // Must reach admission before traversing input, rather than the old d cap.
      refuses(sic::ErrorKind::resource,
              [&] { (void)sic::bounds::bound_wh_overlap_deviations(one[0], 4096, l); });
      l.maximum_dimension = 2;
      l.maximum_working_bits = 32768;
      l.working_bits = 32768;
      refuses(sic::ErrorKind::resource,
              [&] { (void)sic::bounds::bound_wh_overlap_deviations(one[0], 2, l); });
    } else if (name == "branch_precision") {
      auto m = reader.meta();
      for (auto& e : m.elements)
        if (e.kind != "rep") e.max_bits = 65536;
      bool visited = false;
      try {
        sic::sicx::canonical_branches(
            m,
            [&](size_t, auto) {
              visited = true;
              throw std::logic_error("source reached");
            },
            131072);
      } catch (const std::logic_error&) {
      }
      require(visited);
      (void)sic::sicx::verify_all(reader, 1, {{131072}, {}});
      auto parts = reader.ball_parts(0);
      auto final_meta = reader.meta();
      const auto& ray = final_meta.rays.front();
      const auto out = sic::sicx::build_finalization(
          final_meta, {{ray.label, ray.word, static_cast<int64_t>(ray.anchor)}}, {parts}, 131072);
      require(final_meta.ball_limbs == parts.front().mantissa.size() &&
              final_meta.ball_working_bits == 131072 && !out.balls.empty());
    } else if (name == "container_resources") {
      sic::sicx::ResourceLimits l;
      l.text_chunk_bytes = 3;
      refuses(sic::ErrorKind::resource, [&] { sic::sicx::admit_chunk("PROV", 4, l); });
      l.text_chunk_bytes = 4;
      sic::sicx::admit_chunk("PROV", 4, l);
      require(sic::sicx::kMaximumDimension == (1u << 20));
      // Zero/low cache bounds must force re-verification on each request.
      for (const uint64_t cap : {uint64_t{0}, uint64_t{1}}) {
        sic::sicx::ResourceLimits policy;
        policy.cache_bytes = cap;
        sic::sicx::Reader uncached(argv[2], policy);
        const auto owner = uncached.leaf_owner(1);
        { const auto unit = uncached.element_chunk(owner); }
        const auto before = uncached.bytes_read();
        { const auto unit = uncached.element_chunk(owner); }
        require(uncached.bytes_read() > before);
      }
      const auto path = std::filesystem::path(argv[3]) / "writer-limit.sicx";
      std::filesystem::create_directories(path.parent_path());
      std::filesystem::remove(path);
      sic::sicx::StreamOptions opts(false);
      opts.limits.text_chunk_bytes = 3;
      {
        sic::sicx::StreamWriter writer(path, opts);
        refuses(sic::ErrorKind::resource,
                [&] { writer.chunk("PROV", 0, sic::sicx::ByteBuffer(4)); });
      }
      require(!std::filesystem::exists(path));
      {
        sic::sicx::StreamOptions bounded(false);
        bounded.limits.directory_entries = 2;
        sic::sicx::StreamWriter writer(path, bounded);
        const auto& first = reader.index().front();
        writer.chunk(first.type_name(), first.flags, reader.chunk(0), first.object, first.first,
                     first.count);
        const sic::sicx::ByteBuffer two_leaves{0, 0, 1, 2, 0, 1, 1, 2};
        // Physical count 2 fits; aggregate logical count 3 must refuse.
        refuses(sic::ErrorKind::resource, [&] { writer.chunk("TOWR", 1, two_leaves, 0, 0, 2); });
      }
      // Exact review reproducer: default writer must reject a 64 MiB+1 PROV
      // before hashing or publishing it, even though it fits the block bound.
      {
        sic::sicx::StreamWriter writer(path, sic::sicx::StreamOptions(false));
        const sic::sicx::ByteBuffer oversized(sic::sicx::kMaxTextChunk + 1, ' ');
        refuses(sic::ErrorKind::resource, [&] { writer.chunk("PROV", 0, oversized); });
      }
      require(!std::filesystem::exists(path));
    } else if (name == "valuation_admission") {
      sic::sicx::ValuationPolicy p;
      refuses(sic::ErrorKind::resource, [&] { p.admit(uint64_t{1} << 31, 0); });
      p.table_bytes = uint64_t{8} << 30;
      p.total_bytes = uint64_t{16} << 30;
      require(p.admit(uint64_t{1} << 31, 0) == (uint64_t{8} << 30));
      const auto ordinary = sic::sicx::bounds_memory(reader);
      require(ordinary == sic::sicx::bounds_memory(reader, false, p));
      p.table_bytes = 0;
      refuses(sic::ErrorKind::resource, [&] { (void)sic::sicx::bounds_memory(reader, false, p); });
      sic::sicx::RescalingCollector c(reader.meta(), p);
      const auto& e = reader.meta().elements.front();
      bool called = false;
      refuses(sic::ErrorKind::resource, [&] {
        c.add({e.kind, e.level, e.axis, e.k, e.position}, [&](auto) { called = true; });
      });
      require(!called);
    } else if (name == "graph_width" || name == "decode_memory") {
      std::vector<uint64_t> factors{uint64_t{1} << 40}, w{uint64_t{1} << 32};
      std::vector<sic::decode::detail::WordView> words{w};
      sic::decode::detail::PrefixPlan p(words, factors);
      require(p.planned_calls == (uint64_t{1} << 32));
      require(sic::decode::next_graph_depth((uint64_t{1} << 32) - 1) == (uint64_t{1} << 32));
      auto graph_requests = requests;
      graph_requests.front().word.front() = 1;
      o.maximum_graph_nodes = 1;
      sic::decode::Decoder d(reader, o);
      refuses(sic::ErrorKind::resource, [&] { (void)d.decode(graph_requests); });
      if (name == "decode_memory") {
        sic::sicx::ResourceLimits l;
        l.reader_memory_bytes = 1;
        refuses(sic::ErrorKind::resource, [&] { sic::sicx::Reader r(argv[2], l); });
        o.maximum_graph_nodes = 1024;
        o.memory_budget = uint64_t{64} << 20;
        sic::decode::Decoder small(reader, o);
        std::vector<sic::decode::Decoder::Request> repeated(
            10000, {rep, std::vector<uint64_t>(word.size(), 0)});
        // A ceiling is not an allocation request: these actual low-precision
        // outputs fit. Preserve duplicates and their exact returned values.
        {
          const auto values = small.decode(repeated);
          require(values.size() == repeated.size());
          for (size_t i = 1; i < values.size(); ++i) require(acb_equal(values[0], values[i]));
        }
        // This request volume genuinely exceeds the same 64 MiB policy;
        // refuse before beginning another numerical pass.
        const auto passes = small.stats().passes;
        std::vector<sic::decode::Decoder::Request> oversized(
            size_t{1} << 18, {rep, std::vector<uint64_t>(word.size(), 0)});
        refuses(sic::ErrorKind::resource, [&] { (void)small.decode(oversized); });
        require(small.stats().passes == passes);
      }
    } else if (name == "decode_options") {
      o.target = LONG_MAX;
      o.delta = 64;
      refuses(sic::ErrorKind::invalid_argument, [&] { sic::decode::Decoder d(reader, o); });
      o.target = 128;
      o.delta = 0;
      o.threads = 0;
      refuses(sic::ErrorKind::invalid_argument, [&] { sic::decode::Decoder d(reader, o); });
      o.threads = o.maximum_threads = 4096;
      o.memory_budget = 1;
      refuses(sic::ErrorKind::resource, [&] { sic::decode::Decoder d(reader, o); });
      o.memory_budget = uint64_t{16} << 30;
      o.threads = 1;
      o.tower_resident = 2;
      refuses(sic::ErrorKind::invalid_argument, [&] { sic::decode::Decoder d(reader, o); });
    } else if (name == "decode_cancellation") {
      sic::sicx::Reader other(argv[2]);
      sic::decode::Decoder::Options other_options;
      other_options.pin = false;
      const auto stop_file = std::filesystem::path(argv[3]) / "stop";
      std::filesystem::create_directories(stop_file.parent_path());
      std::filesystem::remove(stop_file);
      o.cancellation = std::make_shared<sic::support::StopToken>(stop_file);
      sic::decode::Decoder first(reader, o), second(other, other_options);
      {
        std::ofstream f(stop_file);
        f << "stop";
      }
      refuses(sic::ErrorKind::cancelled, [&] { (void)first.decode(requests); });
      std::filesystem::remove(stop_file);
      o.cancellation->request_stop();
      refuses(sic::ErrorKind::cancelled, [&] { (void)first.decode(requests); });
      auto values = second.decode(requests);
      require(values.size() == 1 && flint_get_num_threads() == 1);
    } else if (name == "decode_thread_ownership") {
      static_assert(!std::is_constructible_v<sic::decode::Decoder, sic::sicx::Reader&&,
                                             sic::decode::Decoder::Options>);
      sic::decode::Decoder d(reader, o);
      bool rejected = false;
      std::thread t([&] {
        try {
          (void)d.stats();
        } catch (const sic::Error& e) {
          rejected = e.kind() == sic::ErrorKind::invalid_argument;
        }
      });
      t.join();
      require(rejected);
    } else if (name == "conjugation_selection") {
      // General d2: K(omega*i,sqrt(2)), not the fixture's generator or shape.
      sic::sicx::Reader d2(std::filesystem::path(argv[2]).parent_path() / "d2.sicx");
      auto& meta = const_cast<sic::sicx::Meta&>(d2.meta());
      meta.degrees = {2, 2};
      meta.width = {1, 2, 4};
      meta.D = 4;
      exact::Builder b(d2, {});
      std::vector<exact::Elt> f0(2, b.T.zero(0));
      fmpq_set_si(f0[0][0].a, -1, 1);
      fmpq_one(f0[0][0].b);
      b.T.set_level(0, std::move(f0));
      std::vector<exact::Elt> f1(2, b.T.zero(1));
      fmpq_set_si(f1[0][0].a, -2, 1);
      b.T.set_level(1, std::move(f1));
      b.roots.resize(2);
      arb_sqrt_ui(acb_imagref(b.omega.v), 3, 768);
      arb_neg(acb_imagref(b.omega.v), acb_imagref(b.omega.v));
      arb_one(acb_realref(b.omega.v));
      acb_mul_2exp_si(b.omega.v, b.omega.v, -1);
      acb_mul_onei(b.roots[0].v, b.omega.v);
      arb_sqrt_ui(acb_realref(b.roots[1].v), 2, 768);
      b.opt.conjugation_images = {b.T.zero(1), b.T.generator(1)};
      fmpq_one(b.opt.conjugation_images[0][1].b);
      b.find_conjugation();
      require(b.conj_k);
      b.opt.conjugation_images.clear();
      refuses(sic::ErrorKind::unsupported, [&] { b.find_conjugation(); });
    } else if (name == "conjugation_word_width") {
      exact::BuildReport r;
      r.conjugation = {uint64_t{1} << 31, uint64_t{1} << 40};
      require(exact::conjugation_word_json(r.conjugation).array[0].as_str() == "2147483648");
      require(exact::conjugation_word_json(r.conjugation).array[1].as_str() == "1099511627776");
    } else
      throw std::runtime_error("unknown test");
    std::cout << "policy-" << name << " PASS\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
