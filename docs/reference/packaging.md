<!-- SPDX-License-Identifier: MIT -->
# Installing and depending on jmpxx

jmpxx is header-only with zero runtime dependencies, so depending on it is including a
header and, for the CMake channels, linking one interface target, `jmpxx::jmpxx`, which
carries the include path and the C++20 requirement. Every channel below builds a clean
consumer, each checked by a consumer program under `packaging/`.

## CMake find_package

Install jmpxx, then find it:

```sh
cmake -S . -B build -DJMPXX_BUILD_TESTS=OFF
cmake --install build --prefix /path/to/prefix
```

```cmake
find_package(jmpxx CONFIG REQUIRED)
target_link_libraries(app PRIVATE jmpxx::jmpxx)
```

The installed package version file declares `SameMinorVersion` compatibility, so while
jmpxx is at 0.x a request for `0.1` is satisfied by a 0.1.x release and rejected by
0.2.0, matching the pre-1.0 contract that the surface may change between minor versions;
see [abi.md](abi.md) for what that promises about type layout.

## FetchContent and add_subdirectory

The same definition works as a subproject. A consumer fetches jmpxx, or finds an
installed copy first, from one call:

```cmake
include(FetchContent)
FetchContent_Declare(jmpxx
  GIT_REPOSITORY https://github.com/anarchyj/jmpxx.git
  GIT_TAG v0.1.3
  FIND_PACKAGE_ARGS NAMES jmpxx)
FetchContent_MakeAvailable(jmpxx)
target_link_libraries(app PRIVATE jmpxx::jmpxx)
```

`add_subdirectory(path/to/jmpxx)` works the same way. The library target is guarded so
a build that pulls jmpxx in through more than one dependency does not redefine it, and
the install rules are generated only for a standalone build, so a parent project does
not inherit them.

## CPM.cmake

```cmake
CPMAddPackage("gh:anarchyj/jmpxx@0.1.3")
target_link_libraries(app PRIVATE jmpxx::jmpxx)
```

## Conan

A recipe ships at the repository root. Build the package and depend on it without
waiting for a central registry:

```sh
conan create . -s compiler.cppstd=20
```

```python
def requirements(self):
    self.requires("jmpxx/0.1.3")
```

The CMake target is `jmpxx::jmpxx`. Conan does not propagate a compile-feature
requirement to the consumer, so a consumer sets C++20 itself. The recipe rejects a
lower standard with a clear message rather than a compiler error from the headers.

## vcpkg

An overlay port ships under `packaging/vcpkg`, so a vcpkg user installs jmpxx without
waiting for the central registry:

```sh
vcpkg install jmpxx --overlay-ports=packaging/vcpkg
```

The port pins the tagged release and its source hash, installs the headers and the CMake
package config through the standard header-only port flow, and exposes the same
`jmpxx::jmpxx` target. When the version changes, the hash is recomputed for the new tag.

## The central registries

The Conan and vcpkg sections above install jmpxx today, from the in-repo recipe and the
overlay port; neither waits on a central index. Listing in the central registries,
ConanCenter and the vcpkg registry, is a separate step: each is a reviewed pull request to
that registry's own repository, merged on its maintainers' schedule rather than instantly.
The recipe and the port here are the artifacts those submissions carry, and the release
tag and its source hash are pinned, so a submission is a matter of opening the pull request
and addressing review. Until each lands, the channels above are the supported way to
depend on jmpxx, and a listing changes the install command, not the library.

## Single-header amalgamation

Two generated single headers ship under `single_include` for a consumer that prefers
to drop one file in rather than add an include directory tree.

- `jmpxx-core.hpp` is the freestanding minimal core, the same surface as
  `jmpxx/core.hpp`, and pulls in nothing outside the freestanding subset.
- `jmpxx.hpp` is the full hosted surface: the core, the diagnostic layer, the
  type-erased policy, the reflection layer, the platform queries, and the interop
  bridges.

```cpp
#include <jmpxx.hpp>        // or <jmpxx-core.hpp> for the freestanding core
```

The experimental unwind arm is not amalgamated. It stays the opt-in
`jmpxx/unwind.hpp` include, because it requires unwind tables and refuses a
no-exceptions build. The single headers are generated from the modular headers by
`packaging/amalgamate.py`, and a gate regenerates and diffs them, so a committed single
header cannot drift from the source it is built from.

## License

jmpxx is MIT-licensed; the full text is in [LICENSE](../../LICENSE). Every source file
also carries an `SPDX-License-Identifier: MIT` tag for per-file clarity.
