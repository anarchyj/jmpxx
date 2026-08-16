// SPDX-License-Identifier: MIT
// Small text helpers the harness units share. They live here rather than in each unit so
// the trimming and file-reading rules have one definition.
#ifndef JMPXX_VERIFY_SUPPORT_HPP
#define JMPXX_VERIFY_SUPPORT_HPP

#include <fstream>
#include <sstream>
#include <string>

namespace jv {

inline std::string trim(const std::string& s) {
  const std::size_t a = s.find_first_not_of(" \t");
  if (a == std::string::npos) return "";
  const std::size_t b = s.find_last_not_of(" \t");
  return s.substr(a, b - a + 1);
}

inline std::string read_file(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}  // namespace jv

#endif  // JMPXX_VERIFY_SUPPORT_HPP
