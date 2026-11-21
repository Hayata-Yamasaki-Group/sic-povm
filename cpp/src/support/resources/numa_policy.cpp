// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/resources/numa_policy.hpp"

#include "sic/error.hpp"

#ifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace sic::support::resources {
void apply_numa_policy(NumaPolicy policy, const std::string& spec) {
#if defined(__linux__) && defined(SYS_set_mempolicy)
  const auto mode = policy.mode;
  const auto mask = policy.mask;
  if (syscall(SYS_set_mempolicy, mode, &mask, 64)) {
    const int error = errno;
    const auto kind = error == ENOSYS || error == EINVAL ? ErrorKind::unsupported
                      : error == ENOMEM                  ? ErrorKind::resource
                                                         : ErrorKind::io;
    throw sic::Error(kind, "set_mempolicy failed for " + spec);
  }
#else
  (void)policy;
  throw sic::Error(sic::ErrorKind::unsupported, "NUMA memory policy unavailable for " + spec);
#endif
}

std::string numa_pages() {
  std::ifstream in("/proc/self/numa_maps");
  std::map<int, uint64_t> pages;
  for (std::string line; std::getline(in, line);) {
    std::stringstream words(line);
    for (std::string w; words >> w;)
      if (w.size() > 2 && w[0] == 'N' && std::isdigit(static_cast<unsigned char>(w[1]))) {
        const auto eq = w.find('=');
        if (eq != std::string::npos)
          pages[std::stoi(w.substr(1, eq - 1))] += std::stoull(w.substr(eq + 1));
      }
  }
  std::string out;
  for (auto [node, n] : pages) out += " N" + std::to_string(node) + "=" + std::to_string(n);
  return out.empty() ? " unavailable" : out;
}
}  // namespace sic::support::resources
