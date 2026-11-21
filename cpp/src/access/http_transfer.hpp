// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <exception>

#include "sic/access/remote.hpp"
#include "sicx/container/directory.hpp"
#include "support/sha256.hpp"
#ifdef SIC_HAVE_CURL
#include <curl/curl.h>
#include <dlfcn.h>
#endif
namespace sic::access {
#ifdef SIC_HAVE_CURL
struct CurlApi {
  void* library = nullptr;
  decltype(&curl_global_init) global_init = nullptr;
  decltype(&curl_global_cleanup) global_cleanup = nullptr;
  decltype(&curl_easy_init) easy_init = nullptr;
  decltype(&curl_easy_cleanup) easy_cleanup = nullptr;
  decltype(&curl_easy_setopt) setopt = nullptr;
  decltype(&curl_easy_perform) perform = nullptr;
  decltype(&curl_easy_strerror) strerror = nullptr;
  template <class T>
  void symbol(T& function, const char* name);
  CurlApi();
  // Keep initialized libcurl for process lifetime. Resolver threads can outlive
  // easy/global cleanup, so successful loads must never be dlclose'd here.
  ~CurlApi() = default;
};
CurlApi& curl_api();
struct Curl {
  CURL* handle;
  Curl();
  ~Curl();
};
// The callbacks refuse invalid responses before consuming any response body.
// Received prefixes survive retries; the next request starts at the next byte.
struct Transfer {
  uint64_t first = 0, last = 0, part_size = 0, got = 0;
  std::span<uint8_t> output;
  int fd = -1;
  uint64_t file_offset = 0;
  sic::support::sha256::Hasher* hash = nullptr;
  long status = 0;
  bool full = false, range_seen = false, length_seen = false, headers_ok = false;
  bool no_range = false, transient = false, location_seen = false;
  std::string error;
  std::exception_ptr exception;
  uint64_t wire_bytes = 0;
  const std::atomic<bool>* stopping = nullptr;
  static int progress(void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept;

  static size_t header(char* p, size_t size, size_t nmemb, void* opaque) noexcept;
  static size_t body(char* p, size_t size, size_t nmemb, void* opaque) noexcept;
};
void configure(CURL* c, const std::string& url, const RemoteOptions& o, Transfer& t);
#endif
}  // namespace sic::access
