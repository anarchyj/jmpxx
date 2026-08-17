<!-- SPDX-License-Identifier: MIT -->
# config_validate

A configuration validator built with jmpxx. It reads a TOML file, checks that the
`[server]` section has a string `host`, an integer `port` in 1..65535, and an integer
`workers` in 1..1024, that the `[limits]` section has a `max_connections` and a
`timeout_ms` in range, and that the `[policy]` section's `access` JSON parses into an
access policy, and reports the first problem it finds.

It exists to use jmpxx the way a real program would. Every fallible step returns a
`jmpxx::result` and propagates its failure with `JMPXX_TRY` to a single landing point
in `main`, so the program has one place that handles errors rather than a status
check after every call. It is built with `-fno-exceptions -fno-rtti`, and it parses
with [toml++](https://github.com/marzer/tomlplusplus) in that library's no-exceptions
mode, bridging the `toml::parse_result` into a jmpxx failure at the point where
parsing happens.

## One source, three builds

The `[server]` pipeline is built three times from the same source.
`config_validate_minimal` uses the minimal error policy, `config_validate_rich`
defines `APP_POLICY_RICH` and turns the diagnostic layer on, and
`config_validate_hardened` adds extensive hardening and trace capture. The only
difference is the build flag: every call site is identical source, selected through
the `app::error` alias in `app_policy.hpp`. On a deep validation failure the rich
builds report where the failure began and the path it propagated, while the minimal
build reports only the code and domain. The hardened build checks that diagnostics,
stack traces, type-erased boundaries, and the bridge code still compose under the
strict portable hardening mode.

The `[limits]` section is validated by a separate component that reports type-erased
errors from its own domain. `main` reads the domain name, message, and value at that
boundary without naming the component's error codes, which is the type-erased policy
at a component boundary.

The `[policy]` section holds an access policy as JSON, parsed by
[glaze](https://github.com/stephenberry/glaze), a third-party library whose read API
returns `std::expected`. The expected interop bridge adopts that result into a jmpxx
result carrying glaze's own error type, so the program speaks the standard expected
vocabulary at this boundary while propagating the failure the jmpxx way. The toml++
field and parse boundaries elsewhere use the `from_optional` and `from_condition` adapters
rather than a hand-written check at each site. Both libraries are essential: remove
jmpxx and the result types, propagation, adapters, and bridge are gone; remove toml++
and there is nothing to read; remove glaze and there is no policy to parse.

## Building

The application consumes jmpxx through `find_package`, so install jmpxx first, then
point the application at it:

```sh
cmake --install <jmpxx-build> --prefix <prefix>
cmake -S . -B build -DCMAKE_PREFIX_PATH=<prefix>
cmake --build build
ctest --test-dir build
```

toml++ and glaze are fetched automatically with FetchContent.

## Running

```sh
./build/config_validate_minimal  configs/valid.toml          # prints the validated config
./build/config_validate_rich     configs/missing_host.toml   # reports the failure with its origin and chain
./build/config_validate_hardened configs/missing_host.toml   # same app under extensive hardening
./build/config_validate_rich     configs/invalid_limits.toml # reports the type-erased boundary error
```
