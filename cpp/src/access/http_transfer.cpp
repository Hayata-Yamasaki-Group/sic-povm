// SPDX-License-Identifier: AGPL-3.0-or-later
#include "access/http_transfer.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <climits>
#include <cstring>

#include "sic/support/parse_number.hpp"
#include "sic/support/stop.hpp"
#include "sicx/io/io.hpp"
namespace sic::access {
namespace {
constexpr const char* kRemoteClient = "sicx-remote/1.0";
}
using namespace sic::sicx;

#ifdef SIC_HAVE_CURL
template <class T>
void CurlApi::symbol(T& function, const char* name) {
  function = reinterpret_cast<T>(::dlsym(library, name));
  if (!function)
    fail(sic::ErrorKind::unsupported, std::string("remote: libcurl symbol unavailable: ") + name);
}

CurlApi::CurlApi() {
  library = ::dlopen(SIC_CURL_LIBRARY, RTLD_NOW | RTLD_LOCAL);
  if (!library)
    fail(sic::ErrorKind::unsupported, std::string("remote: cannot load libcurl: ") + ::dlerror());
  try {
    symbol(global_init, "curl_global_init");
    symbol(global_cleanup, "curl_global_cleanup");
    symbol(easy_init, "curl_easy_init");
    symbol(easy_cleanup, "curl_easy_cleanup");
    symbol(setopt, "curl_easy_setopt");
    symbol(perform, "curl_easy_perform");
    symbol(strerror, "curl_easy_strerror");
    if (global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
      fail(sic::ErrorKind::io, "remote: curl initialization failed");
  } catch (...) {
    ::dlclose(library);
    library = nullptr;
    throw;
  }
}

CurlApi& curl_api() {
  static CurlApi api;
  return api;
}

Curl::Curl() : handle(curl_api().easy_init()) {
  if (!handle) fail(sic::ErrorKind::resource, "remote: curl handle allocation failed");
}

Curl::~Curl() { curl_api().easy_cleanup(handle); }

int Transfer::progress(void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
  auto& t = *static_cast<Transfer*>(opaque);
  try {
    sic::support::check_stop();
    return t.stopping && t.stopping->load();
  } catch (...) {
    t.exception = std::current_exception();
    return 1;
  }
}

size_t Transfer::header(char* p, size_t size, size_t nmemb, void* opaque) noexcept {
  auto& t = *static_cast<Transfer*>(opaque);
  const size_t n = size * nmemb;
  try {
    std::string line(p, n);
    if (line.rfind("HTTP/", 0) == 0) {
      const auto space = line.find(' ');
      const auto end = line.find_first_of(" \r\n", space == std::string::npos ? 0 : space + 1);
      t.status = space == std::string::npos
                     ? 0
                     : support::parse_number<long>(
                           std::string_view(line).substr(space + 1, end - space - 1), "HTTP status",
                           ErrorKind::invalid_format, 100, 999);
      t.range_seen = t.length_seen = t.headers_ok = t.location_seen = false;
      t.error.clear();
      if (!t.full && t.status == 200) {
        t.no_range = true;
        t.error = "server returned 200 instead of 206";
        return 0;
      }
      return n;
    }
    if (t.status >= 300 && t.status < 400) {
      std::string lower = line;
      for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (lower.rfind("location:", 0) == 0 &&
          lower.find_first_not_of(" \t\r\n", 9) != std::string::npos)
        t.location_seen = true;
      if ((line == "\r\n" || line == "\n") && !t.location_seen) {
        t.error = "redirect response has no Location";
        return 0;
      }
      return n;  // followed redirects preserve CURLOPT_RANGE and skip their bodies
    }
    if (t.status >= 100 && t.status < 200) return n;
    if (line == "\r\n" || line == "\n") {
      const long want = t.full ? 200 : 206;
      if (t.status != want) {
        t.transient = t.status == 408 || t.status == 429 || t.status >= 500;
        t.error =
            "HTTP status " + std::to_string(t.status) + " (expected " + std::to_string(want) + ")";
        return 0;
      }
      if (!t.full && !t.range_seen) {
        t.error = "missing Content-Range";
        return 0;
      }
      t.headers_ok = true;
      return n;
    }
    // Error bodies have unrelated lengths/encodings. Classify status first;
    // retryable errors are aborted at the header terminator above.
    if (t.status != (t.full ? 200 : 206)) return n;
    const auto colon = line.find(':');
    if (colon == std::string::npos) return n;
    std::string name = line.substr(0, colon);
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string value = line.substr(colon + 1);
    const auto b = value.find_first_not_of(" \t");
    value = b == std::string::npos ? "" : value.substr(b);
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' ||
                              value.back() == '\t'))
      value.pop_back();
    if (name == "content-range" && !t.full) {
      const std::string want = "bytes " + std::to_string(t.first) + "-" + std::to_string(t.last) +
                               "/" + std::to_string(t.part_size);
      if (t.range_seen || value != want) {
        t.error = "Content-Range mismatch: expected " + want + ", got " + value;
        return 0;
      }
      t.range_seen = true;
    } else if (name == "content-length") {
      const uint64_t want = t.full ? t.part_size : t.last - t.first + 1;
      if (t.length_seen || value != std::to_string(want)) {
        t.error = "Content-Length differs from the manifest or range";
        return 0;
      }
      t.length_seen = true;
    } else if (name == "content-encoding" && value != "identity") {
      t.error = "encoded range responses are unsupported";
      return 0;
    }
    return n;
  } catch (...) {
    t.exception = std::current_exception();
    return 0;
  }
}

size_t Transfer::body(char* p, size_t size, size_t nmemb, void* opaque) noexcept {
  auto& t = *static_cast<Transfer*>(opaque);
  const size_t n = size * nmemb;
  try {
    if (!t.headers_ok) {
      t.error = "body arrived without accepted headers";
      return 0;
    }
    const uint64_t limit = t.full ? t.part_size : t.output.size();
    if (n > limit - t.got) {
      t.error = "response exceeds the requested byte range";
      return 0;
    }
    if (t.full) {
      pwrite_all(t.fd, reinterpret_cast<const uint8_t*>(p), n, t.file_offset + t.got);
      t.hash->update(reinterpret_cast<const uint8_t*>(p), n);
    } else
      std::memcpy(t.output.data() + t.got, p, n);
    t.got += n;
    t.wire_bytes += n;
    return n;
  } catch (...) {
    t.exception = std::current_exception();
    return 0;
  }
}

void configure(CURL* c, const std::string& url, const RemoteOptions& o, Transfer& t) {
  curl_api().setopt(c, CURLOPT_URL, url.c_str());
  curl_api().setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS | CURLPROTO_HTTP);
  curl_api().setopt(c, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS | CURLPROTO_HTTP);
  curl_api().setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_api().setopt(c, CURLOPT_MAXREDIRS, 8L);
  curl_api().setopt(c, CURLOPT_NOSIGNAL, 1L);
  curl_api().setopt(c, CURLOPT_CONNECTTIMEOUT_MS, o.connect_timeout_ms);
  curl_api().setopt(c, CURLOPT_TIMEOUT_MS, o.timeout_ms);
  curl_api().setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_api().setopt(c, CURLOPT_LOW_SPEED_TIME, 15L);
  curl_api().setopt(c, CURLOPT_HEADERFUNCTION, Transfer::header);
  curl_api().setopt(c, CURLOPT_HEADERDATA, &t);
  curl_api().setopt(c, CURLOPT_WRITEFUNCTION, Transfer::body);
  curl_api().setopt(c, CURLOPT_WRITEDATA, &t);
  curl_api().setopt(c, CURLOPT_NOPROGRESS, 0L);
  curl_api().setopt(c, CURLOPT_XFERINFOFUNCTION, Transfer::progress);
  curl_api().setopt(c, CURLOPT_XFERINFODATA, &t);
  curl_api().setopt(c, CURLOPT_USERAGENT, kRemoteClient);
  // libcurl's default proxy CONNECT headers must not be mistaken for origin headers.
  curl_api().setopt(c, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
}
#endif
}  // namespace sic::access
