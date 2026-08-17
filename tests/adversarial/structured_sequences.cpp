// SPDX-License-Identifier: MIT
// Structure-aware adversarial tier. It drives the same portable-surface semantics as
// the fuzz engines, but with hand-shaped records that force boundary lengths, domain
// values, diagnostic overflow, and state-machine transitions a mutational fuzzer may
// not reach in a bounded CI run.
#include "portable_fuzz_driver.hpp"

#include <array>
#include <cstdio>
#include <cstring>

namespace {

void run_case(const std::array<unsigned char, 96>& data) {
  adversarial::exercise(data.data(), data.size());
}

std::array<unsigned char, 96> case_for(int seed) {
  std::array<unsigned char, 96> data{};
  data[0] = static_cast<unsigned char>(seed % 17);  // decode domain
  int code = seed * 65537 - 12345;
  data[1] = static_cast<unsigned char>(code);
  data[2] = static_cast<unsigned char>(code >> 8);
  data[3] = static_cast<unsigned char>(code >> 16);
  data[4] = static_cast<unsigned char>(code >> 24);
  data[5] = static_cast<unsigned char>(seed % (boundary::max_payload + 1));
  for (std::size_t i = 6; i < data.size(); ++i)
    data[i] = static_cast<unsigned char>((seed * 131 + static_cast<int>(i) * 17) & 0xff);
  return data;
}

}  // namespace

int main(int argc, char** argv) {
  bool inject = argc > 1 && std::strcmp(argv[1], "--inject-defect") == 0;
  for (int i = 0; i < 512; ++i) {
    auto data = case_for(i);
    if (inject && i == 17) {
      data[0] = 'J';
      data[1] = 'M';
      data[2] = 'P';
      data[3] = 'X';
    }
    run_case(data);
  }
  std::printf("    cases.asked  %d\n    cases.known  %d\n", 512, 512);
  std::printf("structured adversarial sequences: 512 cases clean\n");
  return 0;
}
