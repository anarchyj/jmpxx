<!-- SPDX-License-Identifier: MIT -->
# Reflection forward layer

The reflection layer derives error metadata from an enum so a program does not
hand-maintain it. It is an optional, hosted extension reached by including
`jmpxx/reflect.hpp`, it depends only on the error representations it derives metadata
for, and nothing in the core requires it. Where C++26 static reflection is available it
uses reflection. Where it is not, a hand-written C++20 path parses the compiler's
function signature, and the two produce identical results. The surface is the same on
both, so a program written against it builds and behaves the same on a C++20 toolchain
and a reflection-capable one.

## Availability

`JMPXX_REFLECTION` is `1` when C++26 reflection is in use and `0` on the fallback. It
defaults to the standard feature test, `__cpp_lib_reflection >= 202506L`, so a
conforming C++26 toolchain such as GCC 16 selects the reflection path automatically.
A toolchain that implements the feature before defining the macro is opted in with
`-DJMPXX_REFLECTION=1`. The core builds with the layer absent on a C++20 cell, and a
program may branch on `JMPXX_REFLECTION` though it rarely needs to, because the surface
is identical.

## Surface

All functions are `constexpr` and usable at run time and in constant evaluation, and
require an enumeration type.

`jmpxx::reflect::enum_name(E value)` returns the enumerator's name as a
`std::string_view`, or an empty view when the value names no enumerator. The view
aliases static storage and outlives the program. **It is not null-terminated**: on the
C++20 path it points into the middle of a longer compiler-generated literal, so passing
its `data()` to a C-style string API hands over the rest of that literal. Carry the size
with the pointer, or copy into a terminated buffer, whenever the name crosses into a C
interface.

`jmpxx::reflect::enum_cast<E>(std::string_view name)` returns the enumerator a name
denotes as a `std::optional<E>`, or no value when the name matches none.

`jmpxx::reflect::type_name<E>()` returns the enum type's unqualified name.

`jmpxx::reflect::enum_count<E>()` returns the number of enumerators the type declares.

`jmpxx::reflect::failures<E>()` returns a `std::span<const jmpxx::reflect::failure_info>`
of every enumerator as a `{int value; std::string_view name;}` pair, value-ordered. For
an error enum this is the statically derivable set of failures a unit reporting that
enum can produce, the library-level analogue of a static failure specification.

`jmpxx::reflect::enum_domain<E>()` returns a `const jmpxx::error_domain&` derived from
the enum: its `name()` is the enum's type name and its `message(value)` is that value's
enumerator name, with no hand-written table. One instance exists per enum type.

`jmpxx::reflect::as_erased(E value)` builds a `jmpxx::erased_error` tagged with the
enum's derived domain, so a boundary reads the enumerator name as the message with
nothing the component had to maintain. See [policies.md](policies.md) for the
type-erased policy this feeds.

## What it derives, and the trade

The derived domain's name is the enum's type name and its messages are the enumerator
names. That removes the hand-written name table a component would otherwise maintain,
at the cost of the wording: `out_of_range` rather than a prose sentence, and a domain
named for the enum's identifier. Name the enum for what the boundary should read. A
component that wants prose, a specific domain name, or localized text writes its own
`jmpxx::error_domain` instead. The reflection layer is the zero-maintenance path, not a
replacement for a hand-authored one.

## The fallback's range

The C++20 fallback recovers an enumerator's spelling by instantiating a template on the
value and slicing the compiler's pretty signature, the established technique for
compile-time enum names without reflection. It sees only enumerators whose value lies
in a scanned range, `jmpxx::reflect::enum_range<E>`, default -128 to 127. An enum with
enumerators outside that window is covered by specializing the trait:

```cpp
template <>
struct jmpxx::reflect::enum_range<my_wide_enum> {
  static constexpr int min = 0;
  static constexpr int max = 4096;
};
```

The cost of the scan is proportional to the range, so the default is kept tight, and the
small error enums this layer is built for fall inside it. The reflection path has no
such bound and ignores the trait. This range limit is the one behavioral difference
between the two paths, and it does not arise for the enums the layer targets.

## Verification

The behavioral tier builds and runs the same fixture on a C++20 toolchain and a
reflection-capable one and compares the derived metadata, so the two paths are held to
identical results. `jmpxx-verify reflect` reports the derived metadata through the
verification surface and checks it against a known enum, on whichever path computed it.
