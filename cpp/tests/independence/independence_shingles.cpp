// SPDX-License-Identifier: AGPL-3.0-or-later
// independence_shingles CHECKER [--exclude PATH]... ROOT...: copied-code check for the independence
// tests (cmake/checks/Independence.cmake). CHECKER is the checker's directory or source file; every
// ROOT is a production source directory. CHECKER and every --exclude PATH (the other enrolled
// checkers) are excluded from the production set. Directory symlinks are followed; a symlink inside
// CHECKER that resolves outside it is an error (exit 2). Each C++ source is normalized line by line
// (comments removed, whitespace collapsed; blank lines, include lines, lines shorter than 12
// characters and pure data lines of numbers and punctuation dropped), and every window of 8
// consecutive normalized lines is hashed. The checker passes when none of its windows occurs in the
// production set. Exit 0 pass, 1 shared windows (listed), 2 usage.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kWindow = 8;

bool is_source(const std::filesystem::path& p) {
  const std::string e = p.extension().string();
  return e == ".cpp" || e == ".hpp" || e == ".h" || e == ".cc";
}

bool data_line(const std::string& s) {
  return std::all_of(s.begin(), s.end(), [](unsigned char c) {
    return std::isxdigit(c) || std::isspace(c) || c == ',' || c == 'x' || c == 'X' || c == 'u' ||
           c == 'U' || c == 'l' || c == 'L' || c == '{' || c == '}' || c == '(' || c == ')' ||
           c == ';' || c == '-';
  });
}

// Normalized lines with their 1-based source line numbers.
std::vector<std::pair<std::string, int>> normalize(const std::filesystem::path& p) {
  std::ifstream in(p);
  std::vector<std::pair<std::string, int>> out;
  std::string line;
  bool block = false;
  int no = 0;
  while (std::getline(in, line)) {
    ++no;
    std::string s;
    for (std::size_t i = 0; i < line.size(); ++i) {
      if (block) {
        if (line.compare(i, 2, "*/") == 0) {
          block = false;
          ++i;
        }
        continue;
      }
      if (line.compare(i, 2, "/*") == 0) {
        block = true;
        ++i;
        continue;
      }
      if (line.compare(i, 2, "//") == 0) break;
      const char c = line[i];
      if (std::isspace(static_cast<unsigned char>(c))) {
        if (!s.empty() && s.back() != ' ') s.push_back(' ');
      } else {
        s.push_back(c);
      }
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.size() < 12 || s.rfind("#include", 0) == 0 || data_line(s)) continue;
    out.emplace_back(s, no);
  }
  return out;
}

// TRUE if path is base itself or lies inside the directory base (both canonical).
bool inside(const std::string& path, const std::string& base) {
  return path == base || path.rfind(base + "/", 0) == 0;
}

std::vector<std::filesystem::path> sources(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> v;
  if (std::filesystem::is_regular_file(root)) {
    if (is_source(root)) v.push_back(root);
  } else if (std::filesystem::is_directory(root)) {
    for (const auto& e : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::follow_directory_symlink))
      if (e.is_regular_file() && is_source(e.path())) v.push_back(e.path());
  }
  std::sort(v.begin(), v.end());
  return v;
}

std::string window(const std::vector<std::pair<std::string, int>>& l, std::size_t i) {
  std::string w;
  for (std::size_t k = 0; k < kWindow; ++k) w += l[i + k].first + '\n';
  return w;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: independence_shingles CHECKER [--exclude PATH]... ROOT...\n";
    return 2;
  }
  const std::filesystem::path checker = std::filesystem::weakly_canonical(argv[1]);
  const std::string prefix = checker.string();
  std::vector<std::string> excluded{prefix};
  std::vector<std::string> roots;
  for (int a = 2; a < argc; ++a) {
    if (std::string(argv[a]) == "--exclude" && a + 1 < argc)
      excluded.push_back(std::filesystem::weakly_canonical(argv[++a]).string());
    else
      roots.emplace_back(argv[a]);
  }
  if (roots.empty()) {
    std::cerr << "independence_shingles: no production root\n";
    return 2;
  }
  if (std::filesystem::is_directory(checker))
    for (const auto& e : std::filesystem::recursive_directory_iterator(
             checker, std::filesystem::directory_options::follow_directory_symlink))
      if (std::filesystem::is_symlink(e.symlink_status()) &&
          !inside(std::filesystem::weakly_canonical(e.path()).string(), prefix)) {
        std::cerr << "independence_shingles: symlink " << e.path() << " leaves the checker\n";
        return 2;
      }
  std::map<std::string, std::string> production;  // window -> first location
  for (const auto& root : roots) {
    for (const auto& f : sources(root)) {
      const std::string fp = std::filesystem::weakly_canonical(f).string();
      if (std::any_of(excluded.begin(), excluded.end(),
                      [&](const std::string& x) { return inside(fp, x); }))
        continue;
      const auto l = normalize(f);
      for (std::size_t i = 0; i + kWindow <= l.size(); ++i)
        production.emplace(window(l, i), fp + ":" + std::to_string(l[i].second));
    }
  }
  std::size_t windows = 0, shared = 0;
  for (const auto& f : sources(checker)) {
    const auto l = normalize(f);
    for (std::size_t i = 0; i + kWindow <= l.size(); ++i) {
      ++windows;
      const auto it = production.find(window(l, i));
      if (it != production.end()) {
        if (++shared <= 20)
          std::cout << "shared: " << f.string() << ":" << l[i].second << " = " << it->second
                    << '\n';
      }
    }
  }
  std::cout << "independence_shingles: " << windows << " checker windows, " << production.size()
            << " production windows, " << shared << " shared\n";
  return shared == 0 ? 0 : 1;
}
