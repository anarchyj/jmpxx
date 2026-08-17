// SPDX-License-Identifier: MIT
// Known-bad fixture for the codegen gate's self-test. The volatile array forces
// stack traffic, so the spill detector must flag it. If the gate passes this, it
// misses the spill it exists to catch.
extern "C" int jx_spilly(int x) {
  volatile int buf[16];
  for (int i = 0; i < 16; ++i) buf[i] = x + i;
  int s = 0;
  for (int i = 0; i < 16; ++i) s += buf[i];
  return s;
}
