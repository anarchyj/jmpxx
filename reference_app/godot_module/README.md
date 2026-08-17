<!-- SPDX-License-Identifier: MIT -->
# Scene audit, a Godot engine module

A Godot module whose error spine is jmpxx, built inside the engine by the engine's own
build and run headless. It is the field-use exercise for a consumer whose build system,
language standard, macro conventions and runtime were all decided without reference to
this library.

The module registers `JmpxxSceneAudit`, a class script can call, which walks a scene tree
and validates each node: a name that is present, not reserved, and not already used, and a
tree no deeper than a stated limit. Validation is recursive descent, so a fault found six
levels down returns through every level with no check written at any of them. One landing
in `audit()` turns the outcome into the `Dictionary` script receives.

## Why an engine

The audience jmpxx is written for includes game engines, and this one is a fair
representative: it compiles with exceptions off, it takes `likely`, `unlikely` and `SWAP`
as macros, it defines `NDEBUG` even for an editor build, and it builds with SCons rather
than CMake. None of that was chosen to suit a library.

## Building

The module drops into an engine checkout and the engine builds it:

```sh
cp -r reference_app/godot_module <godot>/modules/jmpxx_audit
cp single_include/jmpxx.hpp /tmp/jmpxx-release-drop/     # the channel this build uses
cd <godot> && scons platform=linuxbsd target=editor -j "$(nproc)"
./bin/godot.linuxbsd.editor.x86_64 --headless --path <this dir>/project --script main.gd
```

The library arrives as the released single header dropped into a directory, which is what
a build that is not CMake reaches for. `SCsub` names that directory and the two flags the
integration needs.

## What the engine imposed

Three things came from the host rather than from jmpxx, and each is handled at the site
that meets it.

The engine builds at C++17 and prepends its own `-std=gnu++17` to every module's flags. A
module that raises the standard has to **append**, because the last `-std` on the command
line is the one that takes effect; prepending produced `-std=gnu++20 -std=gnu++17`, which
built the module at C++17 and left the library refusing it by name.

The engine defines `NDEBUG` even for an editor build, which turns the diagnostic layer off
by default. With the layer off, `jmpxx::diagnostic::context` and `inspect` do not exist,
so code that reads a failure's context does not compile rather than compiling to nothing.
This module guards those reads and asks for the layer by name in its `SCsub`.

The engine normalizes a node name before anything else sees it, so a validation rule
naming a prefix the engine itself rejects can never fire. The rule here names one the
engine keeps.

## What it checks about the library

The module holds jmpxx to its documented layout in the engine's own build, with static
assertions on the size, alignment and trivial copyability of the transport and the minimal
error, and on the version the header carries. The layout gate in the library's own suite
measures one host cell; these assertions answer the same question in a consumer's build,
and a documented layout that does not hold here stops the engine from building.

## What it reports

```
jmpxx scene audit inside the engine, headless
  clean          ok=true fault=none nodes=5 where=- origin=false hops=-
  duplicate      ok=false fault=duplicate_name nodes=0 where=a origin=true hops=7
  reserved       ok=false fault=reserved_prefix nodes=0 where=tmp_scratch origin=true hops=5
  too deep       ok=false fault=too_deep nodes=0 where=d origin=true hops=4
  clean again    ok=true fault=none nodes=5 where=- origin=false hops=-
  no tree        ok=false fault=empty_tree nodes=0 where=- origin=false hops=-
```

`duplicate` is the case the propagation exists for: a fault six levels down, arriving with
the name that caused it and the seven propagation sites it passed through. `clean again`
is the property that matters for a long-lived object: the audit after a failed one behaves
as if the failure never happened.
