// SPDX-License-Identifier: AGPL-3.0-or-later
#include <dlfcn.h>
#include <pthread.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <system_error>

#include "sicx/codec/block.hpp"
#include "sicx/codec/rescaling_rule.hpp"

namespace {
bool injecting = false;
unsigned attempts = 0, started = 0, joined = 0;
std::atomic<unsigned> running{0}, completed{0};
struct Start {
  void* (*body)(void*);
  void* argument;
  pthread_t thread;
} first;

void* track(void*) {
  ++running;
  void* result = first.body(first.argument);
  --running;
  ++completed;
  return result;
}
}  // namespace

// Use the existing pthread interposer seam; no production fault hook is needed.
extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attributes,
                              void* (*body)(void*), void* argument) {
  static const auto real =
      reinterpret_cast<decltype(&pthread_create)>(dlsym(RTLD_NEXT, "pthread_create"));
  if (!real) std::abort();
  if (!injecting) return real(thread, attributes, body, argument);
  if (++attempts == 2) return EAGAIN;  // std::thread must throw std::system_error.
  if (attempts != 1) std::abort();
  first.body = body;
  first.argument = argument;
  const int status = real(thread, attributes, track, nullptr);
  if (!status) {
    first.thread = *thread;
    ++started;
  }
  return status;
}

extern "C" int pthread_join(pthread_t thread, void** result) {
  static const auto real =
      reinterpret_cast<decltype(&pthread_join)>(dlsym(RTLD_NEXT, "pthread_join"));
  if (!real) std::abort();
  const int status = real(thread, result);
  if (!status && injecting && started && pthread_equal(thread, first.thread)) ++joined;
  return status;
}

int main() {
  flint_set_num_threads(1);
  sic::sicx::Meta meta;
  const auto size = 4 * sic::sicx::kRescalingParallelGrain;
  meta.degrees = {static_cast<unsigned>(size), 2};
  meta.width = {1, size, 2 * size};
  sic::sicx::RescalingCollector input(meta);
  sic::sicx::FmpzPair pair;
  fmpz_one(pair.x);
  fmpz_mul_2exp(pair.x, pair.x, 4 * size);
  input.add({"wsq", 1}, [&](const sic::sicx::LeafVisitor& visit) {
    visit(0, pair.x, pair.y);
    visit(size - 1, pair.x, pair.y);
  });
  bool propagated = false;
  injecting = true;
  try {
    (void)input.compute(4);
  } catch (const std::system_error& error) {
    propagated = error.code() == std::make_error_code(std::errc::resource_unavailable_try_again);
  } catch (...) {
    std::fputs("thread start exception lost its original type\n", stderr);
  }
  injecting = false;
  if (!propagated || attempts != 2 || started != 1 || joined != 1 || running != 0 || completed != 1)
    return 1;
  std::puts(
      "second start: original std::system_error/EAGAIN; one started, completed and joined: PASS");
  flint_cleanup_master();
}
