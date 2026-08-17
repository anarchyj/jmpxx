<!-- SPDX-License-Identifier: MIT -->
# jmpxx

jmpxx is a header-only C++20 library for non-local, RAII-correct, exception-free
error propagation. A failure returned deep in a call chain travels to a single
typed handling point, every destructor along the way runs, and the success path
compiles to the same machine code as a hand-written branch on a status flag. A
committed codegen golden checks that last property on every build.

It is built for code compiled with `-fno-exceptions`: embedded firmware, game
engines, real-time media, trading, storage, and other systems where binary size,
tail latency, and generated-code review matter.

## Background

With exceptions disabled, returning an error from a deep call chain means
threading a status value back through every function by hand. `longjmp` skips
that boilerplate, but it also skips C++ destructors, which is undefined behavior.
Exceptions keep cleanup correct, at the cost of unwind tables and a failure path
whose latency is hard to bound.

jmpxx removes the manual threading while keeping destructors correct and the
cost bounded. A failure travels back as an ordinary return value and each call
site forwards it with one construct rather than a hand-written branch. What the
failure accumulates on the way, its origin and the path it took, is held out of
band, so the value the chain returns stays the width of a value-or-error union.

## Example

```cpp
#include <cstdio>
#include <jmpxx/core.hpp>
#include <string_view>

struct Config {
  int port;
};

jmpxx::result<int> parse_port(std::string_view text);
int start_server(Config cfg);

jmpxx::result<Config> load_config(std::string_view text) {
  JMPXX_TRY(port, parse_port(text));  // on failure, return it from load_config
  Config cfg;
  cfg.port = port;
  return cfg;
}

int main() {
  jmpxx::result<Config> cfg = load_config("8080");
  if (!cfg.has_value()) {
    std::fprintf(stderr, "config error %d\n", cfg.error().code);
    return 1;
  }
  return start_server(cfg.value());
}
```

`result<T>` holds either a `T` or an error. `JMPXX_TRY` evaluates an expression,
binds its value on success, and returns its error to the caller on failure. The
same call sites serve three error representations selected by policy at the error
type: a minimal freestanding code, a rich error that carries a failure's origin and
causal chain in debug, and a type-erased boundary error. Selecting a policy changes
no call site.

`result` also offers the `std::expected` monadic combinators `and_then`, `or_else`,
`transform`, and `transform_error`, for code that prefers a pipeline to a sequence of
`JMPXX_TRY` statements.

## Use

Install once:

```sh
cmake -S . -B build -DJMPXX_BUILD_TESTS=OFF
cmake --install build --prefix /path/to/prefix
```

Then link the interface target, which carries the include directory and the C++20
requirement:

```cmake
find_package(jmpxx CONFIG REQUIRED)
target_link_libraries(app PRIVATE jmpxx::jmpxx)
```

Pass `/path/to/prefix` through `CMAKE_PREFIX_PATH` if it is not on CMake's default
search path. FetchContent, `add_subdirectory`, CPM.cmake, Conan, vcpkg, and the
single-header amalgamations are covered in
[docs/reference/packaging.md](docs/reference/packaging.md).

## Guarantees

Every property below has a build gate, and each gate has an inverted case that must
fail on known-bad input. [docs/reference/verification.md](docs/reference/verification.md)
names the commands and fixtures behind them.

| Guarantee | Backed by |
|-----------|-----------|
| Zero overhead versus a hand-written branch on the happy path | committed codegen golden, a 0-byte binary-size delta, and a deterministic callgrind instruction count |
| Rich diagnostics cost nothing in a release build | a release codegen diff requiring the rich policy to equal the minimal policy |
| Every destructor on a failure path runs exactly once | instrumented destructor-count tiers, on the portable surface and the unwind arm |
| A produced failure cannot be discarded silently | a compile-fail tier at the type and at the propagation site |
| Freestanding: no heap, no exceptions, no RTTI, no hosted header | a freestanding cell, an include-graph scan, and a no-allocation gate |
| Hardened contract violations fail fast when enabled and vanish when disabled | live fail-fast probes and optimized codegen absence checks |
| No undefined behavior on any exercised path | sanitizers, static analysis, and boundary-input fuzzing |
| The transport layout is frozen within a major version | an ABI layout-descriptor gate |

The `jmpxx-verify` tool reproduces each measurement. `python3 verify/acceptance.py
--build-dir build` runs every tier and gate and prints one release verdict.

## Supported toolchains and platforms

| | |
|---|---|
| Standard | C++20 baseline; C++23 and C++26 as progressive enhancements, never preconditions |
| Compilers | GCC ≥ 13, Clang ≥ 16, MSVC ≥ 19.31 (Visual Studio 2022 17.1) |
| Operating systems | Linux, macOS, Windows, FreeBSD, and bare-metal freestanding |
| Architectures | x86-64, ARM64, ARM32, RISC-V, and WebAssembly |

Every reachable cell builds and runs in continuous integration, the cross
architectures under emulation. The optional C++26 reflection layer is validated on
a reflection-capable toolchain and degrades to a hand-written C++20 path that
produces identical results. `JMPXX_VERSION` (and `JMPXX_VERSION_STRING`) expose the
version for a one-line consumer check: `#if JMPXX_VERSION >= 100`.

## When to reach for jmpxx

Reach for it when you compile with exceptions disabled, when binary size or tail
latency is a first-order constraint, when you need to see the generated code, or
when a recoverable failure has to travel up a deep call chain without every frame
carrying an error type.

It is not the tool for a programming bug: a precondition violation or a broken
invariant is routed to a contract or a termination, not propagated as a recoverable
failure. It is not a general result-and-monad library competing on breadth, not an
async runtime, not a logging framework, and not a drop-in `longjmp`.

## Repository map

| Path | What it holds |
|------|---------------|
| `include/jmpxx/` | the header library, the only thing a consumer compiles against |
| `docs/` | the guides, the API reference per capability, and the comparison |
| `examples/` | small programs, one per capability, that build and run as tests |
| `reference_app/` | the worked example consuming jmpxx through `find_package` |
| `tests/` | the verification tiers, each independently runnable |
| `benchmarks/` | the distribution benchmark suite and the perf gates |
| `verify/` | the `jmpxx-verify` harness and the acceptance sweep |
| `lint/` | the `jmpxx-lint` companion check set |
| `packaging/` | the integration-channel metadata and the amalgamation generator |
| `goldens/` | the committed codegen and ABI-layout goldens |

## Benchmarks

Every statement about size, speed, or generated code is measured and gated in
CI, and a regression fails the build. The `jmpxx-verify` tool compiles a fixture,
emits its optimized machine code, compares it against a committed reference, and
reports transport size, the binary-size delta over a hand-written branch, and the
compile-cost as a deterministic instantiation count. The `jmpxx-bench` tool reports
the per-call latency distribution against every incumbent, and a callgrind run counts
the instructions each executes, deterministically. [docs/comparison.md](docs/comparison.md)
states where jmpxx wins and where it does not. Claims without a backing artifact are
not made.

## jmpxx-lint

`jmpxx-lint` is an out-of-tree Clang tool that enforces the usage discipline in
consumer code: it flags a discarded failure, a value taken through a narrow accessor
with no success check, and a hand-written failure forward that `JMPXX_TRY` expresses.
It is a developer tool, never a runtime or include dependency.

## Documentation

[docs/](docs/README.md) holds the guides, the API reference for each capability, the
performance comparison, and the reference for the verification and lint tooling.
[Why exceptions get disabled](docs/why-no-exceptions.md) is the orientation,
the [cookbook](docs/cookbook.md) is task-oriented recipes, and there are migration
guides from [std::expected](docs/migration/from-expected.md),
[std::error_code](docs/migration/from-error-code.md), and
[exceptions](docs/migration/from-exceptions.md). [Installing and depending on
jmpxx](docs/reference/packaging.md) covers every integration channel. The
[reference application](reference_app/README.md) is the worked example of consuming
jmpxx through `find_package` alongside two third-party libraries, and the
[examples](examples/README.md) are small programs showing each capability.

## Versioning and stability

jmpxx follows semantic versioning. While jmpxx is at 0.x the public surface may
change between minor versions, so `find_package(jmpxx 0.1)` accepts a 0.1.x release and
rejects 0.2.0. The observable layout of the transport under a fixed policy is held
frozen from 0.1.0 by the ABI gate, with the experimental unwind arm exempt until it
graduates. Full surface and ABI stability is promised at 1.0.0. Each change is recorded
in [CHANGELOG.md](CHANGELOG.md).

## License

MIT. See [LICENSE](LICENSE).
