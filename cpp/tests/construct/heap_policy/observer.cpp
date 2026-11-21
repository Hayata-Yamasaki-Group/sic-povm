// SPDX-License-Identifier: AGPL-3.0-or-later
#include <dlfcn.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {
int trace_fd = -1;
bool refuse_mmap = false, refuse_trim = false;
std::atomic<bool> first_thread{true};
void trace(const char* line, int bytes) {
  if (bytes < 0 || (trace_fd >= 0 && write(trace_fd, line, bytes) != bytes)) std::abort();
}
// Capture before the CLI scrubs inherited test settings.
__attribute__((constructor)) void open_trace() {
  if (const char* fault = std::getenv("SIC_TEST_HEAP_FAIL")) {
    refuse_mmap = std::string_view(fault) == "mmap";
    refuse_trim = std::string_view(fault) == "trim";
  }
  if (const char* path = std::getenv("SIC_TEST_HEAP_TRACE")) {
    trace_fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (trace_fd < 0) std::abort();
  }
}
}  // namespace

extern "C" int mallopt(int parameter, int value) noexcept {
  static const auto real = reinterpret_cast<int (*)(int, int)>(dlsym(RTLD_NEXT, "mallopt"));
  if (!real) std::abort();
  const bool refuse = (refuse_mmap && parameter == M_MMAP_THRESHOLD) ||
                      (refuse_trim && parameter == M_TRIM_THRESHOLD);
  const int result = refuse ? 0 : real(parameter, value);
  if (trace_fd >= 0 && (parameter == M_TRIM_THRESHOLD || parameter == M_MMAP_THRESHOLD)) {
    char line[64];
    const int bytes = std::snprintf(line, sizeof line, "%s %d %d\n",
                                    parameter == M_TRIM_THRESHOLD ? "trim" : "mmap", value, result);
    trace(line, bytes);
  }
  return result;
}
extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attributes,
                              void* (*entry)(void*), void* argument) noexcept {
  static const auto real =
      reinterpret_cast<decltype(&pthread_create)>(dlsym(RTLD_NEXT, "pthread_create"));
  if (!real) std::abort();
  const bool first = first_thread.exchange(false);
  if (first) trace("pthread_create\n", 15);
  const int result = real(thread, attributes, entry, argument);
  if (first) {
    char line[64];
    const int bytes = std::snprintf(line, sizeof line, "pthread_result %d\n", result);
    trace(line, bytes);
  }
  return result;
}
