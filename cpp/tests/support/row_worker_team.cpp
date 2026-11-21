// SPDX-License-Identifier: AGPL-3.0-or-later
#include <unistd.h>

#include <atomic>

#include "assertions.hpp"
#include "exact/certification/reconstruction/ghost.hpp"

void reused_request_contexts() {
  std::unique_ptr<sic::exact::RowWorkerTeam> team;
  const auto* original_settings = sic::support::current_kernel_settings;
  auto* original_token = sic::support::current_stop_token;
  auto* original_progress = sic::support::current_progress_sink;
  const bool original_logging = sic::support::admission::logging;
  for (int pass = 0; pass < 3; ++pass) {
    sic::support::StopToken token;
    sic::support::KernelSettings settings{{"row-request", std::to_string(pass)}};
    sic::support::ProgressSink progress;
    sic::support::StopScope stop_scope(&token);
    sic::support::KernelSettingsScope settings_scope(&settings);
    sic::support::ProgressScope progress_scope(&progress);
    sic::support::admission::LogScope log_scope(pass == 1);
    if (!team) team = std::make_unique<sic::exact::RowWorkerTeam>(4);
    std::atomic<bool> bad{false};
    std::mutex mutex;
    std::vector<std::thread::id> owners(4);
    bool caught = false;
    try {
      team->for_each_indexed(97, [&](long row, int worker) {
        if (worker < 0 || worker >= 4) {
          bad = true;
          return;
        }
        if (sic::support::current_stop_token != &token ||
            sic::support::current_kernel_settings != &settings ||
            sic::support::current_progress_sink != &progress ||
            sic::support::admission::logging != (pass == 1) || flint_get_num_threads() != 1)
          bad = true;
        sic::support::check_stop();
        {
          std::lock_guard lock(mutex);
          auto& owner = owners[worker];
          if (owner != std::thread::id{} && owner != std::this_thread::get_id()) bad = true;
          owner = std::this_thread::get_id();
        }
        if (pass == 0 && row == 5) throw std::runtime_error("request fixture");
      });
    } catch (const std::runtime_error& error) {
      caught = std::string(error.what()) == "request fixture";
    }
    SIC_CHECK(caught == (pass == 0));
    SIC_CHECK(!bad);
    for (size_t a = 0; a < owners.size(); ++a) {
      SIC_CHECK(owners[a] != std::thread::id{});
      for (size_t b = 0; b < a; ++b) SIC_CHECK(owners[a] != owners[b]);
    }
  }  // All borrowed request objects are destroyed while the team remains idle.
  std::atomic<bool> bad{false};
  team->for_each(97, [&](long) {
    if (sic::support::current_stop_token != original_token ||
        sic::support::current_kernel_settings != original_settings ||
        sic::support::current_progress_sink != original_progress ||
        sic::support::admission::logging != original_logging)
      bad = true;
  });
  SIC_CHECK(!bad);
  {
    sic::support::StopToken cancelled;
    cancelled.request_stop();
    sic::support::StopScope stop_scope(&cancelled);
    bool caught = false;
    try {
      team->for_each(97, [](long) { throw std::logic_error("cancel dispatched work"); });
    } catch (const sic::Error& error) {
      caught = error.kind() == sic::ErrorKind::cancelled;
    }
    SIC_CHECK(caught);
  }
  team->for_each(97, [](long) { sic::support::check_stop(); });
  struct RequestFailure : std::runtime_error {
    std::shared_ptr<int> owner;
    explicit RequestFailure(std::shared_ptr<int> value)
        : std::runtime_error("request-owned fixture"), owner(std::move(value)) {}
  };
  auto request = std::make_shared<int>(17);
  std::weak_ptr<int> lifetime = request;
  bool caught = false;
  try {
    team->for_each(97, [&](long row) {
      if (row == 60) throw RequestFailure(request);
    });
  } catch (const RequestFailure&) {
    caught = true;
  }
  SIC_CHECK(caught);
  request.reset();
  SIC_CHECK(lifetime.expired());
  team->for_each(97, [](long) { sic::support::check_stop(); });
}

void compare_jacobian(long dimension, slong precision, long normalization, int sign) {
  sic::exact::GhostRefiner serial(dimension, sign, 3), parallel(dimension, sign, 3);
  serial.k = parallel.k = normalization;
  serial.kernel_threads = 1;
  parallel.kernel_threads = 4;
  sic::support::ComplexBallVector state(2 * dimension - 2);
  for (size_t q = 0; q < state.size(); ++q) {
    acb_set_si(state[q], (long)(q % 11) - 5);
    arb_set_si(acb_imagref(state[q]), (long)(q % 7) - 3);
    acb_mul_2exp_si(state[q], state[q], -8 - (q % 5));
    arb_add_error_2exp_si(acb_realref(state[q]), -precision + 7);
    arb_add_error_2exp_si(acb_imagref(state[q]), -precision + 5);
  }
  const long rows = std::max(97L, 3 * (2 * dimension - 2));
  std::vector<long> labels(rows);
  for (long r = 0; r < rows; ++r) labels[r] = (r * 17 + r / 7) % (dimension * dimension - 1) + 1;
  sic::exact::GhostRefiner::Ctx context(dimension, sign, precision);
  acb_mat_t expected, actual;
  acb_mat_init(expected, rows, state.size());
  acb_mat_init(actual, rows, state.size());
  serial.jacobian(expected, context, state.v, labels);
  parallel.jacobian(actual, context, state.v, labels);
  SIC_CHECK(acb_mat_equal(expected, actual));
  for (long r = 0; r < rows; ++r)
    for (size_t c = 0; c < state.size(); ++c) SIC_CHECK(acb_is_finite(acb_mat_entry(actual, r, c)));
  acb_mat_clear(expected);
  acb_mat_clear(actual);
}

template <class Real>
void compare_selection(int kind) {
  constexpr long kColumns = 72, rows = 3 * kColumns;
  acb_mat_t matrix;
  acb_mat_init(matrix, rows, kColumns);
  std::mt19937 random(917);
  for (long r = 0; r < rows; ++r)
    for (long c = 0; c < kColumns; ++c) {
      auto value = acb_mat_entry(matrix, r, c);
      const long row = kind == 1 ? r % kColumns : r;
      if (kind == 1)
        acb_set_si(value, row == c);
      else if (kind == 2)
        acb_set_si(value, (r == c && c < kColumns - 1) ? 1 : 0);
      else {
        acb_set_si(value, (int)(random() % 19) - 9);
        arb_set_si(acb_imagref(value), (int)(random() % 17) - 8);
        if (r % kColumns == c) acb_add_si(value, value, 40, 160);
        if (kind == 3) acb_mul_2exp_si(value, value, (c % 7) * 8 - (r % 5) * 9);
        if (kind == 4) acb_mul_2exp_si(value, value, 600 + (c % 7) * 8 - (r % 5) * 9);
        if (kind == 5) acb_mul_2exp_si(value, value, 1200 + (c % 7) * 8 - (r % 5) * 9);
      }
    }
  {
    char path[] = "/tmp/sic-row-stop-XXXXXX";
    const int descriptor = mkstemp(path);
    SIC_CHECK(descriptor >= 0);
    if (descriptor >= 0) {
      close(descriptor);
      sic::support::ScopedStopFile stop_scope(path);
      bool caught = false;
      {
        sic::exact::RowWorkerTeam team(4);
        try {
          team.for_each(37, [](long) { SIC_CHECK(false); });
        } catch (const std::runtime_error&) {
          caught = true;
        }
      }
      SIC_CHECK(caught);
      unlink(path);
    }
  }
  std::vector<long> labels(rows);
  std::iota(labels.begin(), labels.end(), 1);
  sic::exact::GhostRefiner serial(8, 1, 1), parallel(8, 1, 1);
  serial.kernel_threads = 1;
  parallel.kernel_threads = 4;
  const bool a = serial.select_rows<Real>(matrix, rows, kColumns, labels);
  const bool b = parallel.select_rows<Real>(matrix, rows, kColumns, labels);
  SIC_CHECK(a == b);
  SIC_CHECK(serial.S == parallel.S);
  SIC_CHECK(serial.cond_est == parallel.cond_est);
  const bool range_available = kind != 5 || sic::exact::selection_max_exponent<Real>() > 1300;
  SIC_CHECK(a == (kind != 2 && range_available));
  acb_mat_clear(matrix);
}

int main() {
  reused_request_contexts();
  {
    char path[] = "/tmp/sic-jacobian-stop-XXXXXX";
    const int descriptor = mkstemp(path);
    SIC_CHECK(descriptor >= 0);
    if (descriptor >= 0) {
      close(descriptor);
      sic::support::ScopedStopFile stop_scope(path);
      sic::exact::GhostRefiner refiner(8, 1, 3);
      sic::exact::GhostRefiner::Ctx context(8, 1, 80);
      sic::support::ComplexBallVector state(14);
      std::vector<long> labels(97, 1);
      acb_mat_t matrix;
      acb_mat_init(matrix, labels.size(), state.size());
      for (int workers : {1, 4}) {
        refiner.kernel_threads = workers;
        bool caught = false;
        try {
          refiner.jacobian(matrix, context, state.v, labels);
        } catch (const std::runtime_error&) {
          caught = true;
        }
        SIC_CHECK(caught);
        SIC_CHECK(acb_mat_is_zero(matrix));
      }
      acb_mat_clear(matrix);
      unlink(path);
    }
  }
  {
    sic::exact::RowWorkerTeam team(4);
    std::vector<long> counts(37);
    for (int pass = 0; pass < 19; ++pass)
      team.for_each(counts.size(), [&](long row) { ++counts[row]; });
    for (auto count : counts) SIC_CHECK(count == 19);
    bool caught = false;
    try {
      team.for_each(37, [&](long row) {
        if (row == 5) throw std::runtime_error("fixture");
      });
    } catch (const std::runtime_error&) {
      caught = true;
    }
    SIC_CHECK(caught);
    team.for_each(counts.size(), [&](long row) { ++counts[row]; });
    for (auto count : counts) SIC_CHECK(count == 20);
    team.for_each(0, [](long) { SIC_CHECK(false); });
  }
  for (int kind = 0; kind < 6; ++kind) {
    compare_selection<double>(kind);
    compare_selection<long double>(kind);
    compare_selection<sic::exact::SelectionWide>(kind);
  }
  for (long dimension : {8, 16, 32})
    for (slong precision : {80, 512})
      for (long normalization : {0L, dimension / 2, dimension - 1})
        for (int sign : {-1, 1}) compare_jacobian(dimension, precision, normalization, sign);
  return sic_test::report();
}
