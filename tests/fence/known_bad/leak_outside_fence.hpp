// SPDX-License-Identifier: MIT
// Known-bad fixture for the platform-fence scan's self-test. This header sits
// outside the fenced platform/ and unwind/ directories and reads a raw
// operating-system macro, the exact leak the scan exists to catch. A scan that
// passes this header is not catching it.
#ifndef JMPXX_TEST_LEAK_OUTSIDE_FENCE_HPP
#define JMPXX_TEST_LEAK_OUTSIDE_FENCE_HPP
#if defined(_WIN32)
#define JMPXX_TEST_ON_WINDOWS 1
#endif
#endif
