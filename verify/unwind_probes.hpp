// SPDX-License-Identifier: MIT
// The harness's view of the experimental non-local unwind arm.
//
// The arm is the one capability whose correctness depends on the platform runtime rather
// than on the library's own control flow, so its observation surface is larger than a
// single probe and lives apart from the rest of the harness: what a deep escape costs,
// what each C++ runtime does with a handler on the escape path, whether a long randomized
// campaign leaks state, and whether escaping stays as parallel as the platform's own
// unwinder.
#ifndef JMPXX_VERIFY_UNWIND_PROBES_HPP
#define JMPXX_VERIFY_UNWIND_PROBES_HPP

#include "reporter.hpp"

#include <string>
#include <vector>

namespace jv {

// Destructor counts over a deep escape and the sad-path latency distribution beside a
// C++ throw at the same depth. Accepts --iters, --bound-factor, and --inject-jitter.
int probe_unwind(Fmt fmt, const std::vector<std::string>& args);

// Each case of the per-runtime behaviour matrix, run as its own process through the
// fixture named by --fixture. Accepts --inject-silent-loss.
int probe_unwind_matrix(Fmt fmt, const std::vector<std::string>& args);

// A long randomized campaign of escapes across threads, checked for payload identity,
// cleanup balance, and leaked landing state. Accepts --iterations, --threads, --seeds,
// and --inject-imbalance.
int probe_unwind_stress(Fmt fmt, const std::vector<std::string>& args);

// Per-escape latency as threads are added, held against a C++ throw measured in the same
// loop. Accepts --max-threads, --iters, --bound-factor, and --inject-serialization.
int probe_unwind_scale(Fmt fmt, const std::vector<std::string>& args);

}  // namespace jv

#endif  // JMPXX_VERIFY_UNWIND_PROBES_HPP
