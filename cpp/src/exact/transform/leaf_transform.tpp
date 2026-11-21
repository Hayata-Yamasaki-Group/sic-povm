// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
template <class Read, class Term>
sic::support::ComplexBallVector inverse_leaf_tree(const std::vector<long>& degrees, int n,
                                                  const std::vector<long>& labels, Read&& read,
                                                  Term&& term) {
  if (n < 0 || static_cast<size_t>(n) > degrees.size())
    throw std::invalid_argument("invalid inverse leaf depth");
  std::vector<long> counts(n + 1, 1);
  std::vector<std::vector<long>> need(n + 1), parent(n);
  need[n] = labels;
  std::sort(need[n].begin(), need[n].end());
  need[n].erase(std::unique(need[n].begin(), need[n].end()), need[n].end());
  for (int i = 0; i < n; ++i) {
    if (degrees[i] < 2 || counts[i] > WORD_MAX / degrees[i])
      throw std::invalid_argument("invalid inverse leaf degree");
    counts[i + 1] = counts[i] * degrees[i];
  }
  for (long label : need[n])
    if (label < 0 || label >= counts[n]) throw std::invalid_argument("invalid inverse leaf label");
  if (need[n].empty()) return sic::support::ComplexBallVector();
  for (int i = n - 1; i >= 0; --i) {
    for (long label : need[i + 1]) need[i].push_back(label / degrees[i]);
    need[i].erase(std::unique(need[i].begin(), need[i].end()), need[i].end());
    for (long label : need[i + 1])
      parent[i].push_back(std::lower_bound(need[i].begin(), need[i].end(), label / degrees[i]) -
                          need[i].begin());
  }
  std::function<sic::support::ComplexBallVector(int, long)> visit =
      [&](int level, long offset) -> sic::support::ComplexBallVector {
    if (level == 0) {
      sic::support::ComplexBallVector result(1);
      read(offset, result[0]);
      return result;
    }
    // Amortize recursion/allocation overhead over small sequential blocks.
    // This cutoff bounds storage independently of the full field degree.
    if (counts[level] <= 1024) {
      sic::support::ComplexBallVector values(counts[level]);
      for (long e = 0; e < counts[level]; ++e) read(offset + e, values[e]);
      for (int i = 0; i < level; ++i) {
        const long blocks = counts[level] / counts[i + 1];
        sic::support::ComplexBallVector next(blocks * need[i + 1].size());
        for (long block = 0; block < blocks; ++block)
          for (size_t q = 0; q < need[i + 1].size(); ++q)
            for (long k = 0; k < degrees[i]; ++k)
              term(next[block * need[i + 1].size() + q],
                   values[(block * degrees[i] + k) * need[i].size() + parent[i][q]], i,
                   need[i + 1][q], k);
        values = std::move(next);
      }
      return values;
    }
    const int i = level - 1;
    sic::support::ComplexBallVector result(need[level].size());
    for (long k = 0; k < degrees[i]; ++k) {
      sic::support::ComplexBallVector child = visit(i, offset + k * counts[i]);
      for (size_t q = 0; q < result.size(); ++q)
        term(result[q], child[parent[i][q]], i, need[level][q], k);
    }
    return result;
  };
  return visit(n, 0);
}

}  // namespace sic::exact
