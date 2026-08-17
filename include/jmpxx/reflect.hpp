// SPDX-License-Identifier: MIT
// The optional reflection forward layer.
//
// Where C++26 static reflection (P2996) is available, this layer derives error
// metadata that a program would otherwise hand-maintain: an enumerator's text from
// its value and back, an error_domain whose per-value message is the enumerator name
// rather than a hand-written switch, and the static set of failures an error enum
// declares. Where reflection is absent, every one of those is delivered by a
// hand-written C++20 equivalent that parses the compiler's function signature, so the
// surface and the results are identical and a program written against this layer
// builds and behaves the same on a C++20 toolchain and a reflection-capable one.
//
// Nothing in the core requires this layer. It is reached only by including this header,
// depends only on the error representations it derives metadata for, and leaves the
// full core buildable on a C++20 toolchain with this layer absent.
// The reflection path is selected by JMPXX_REFLECTION, which defaults to the standard
// __cpp_lib_reflection feature test and can be forced on for a pre-standardization
// toolchain that implements P2996 without yet advertising the macro.
//
// The two paths are guaranteed to agree on the metadata they derive, which the
// behavioral tier checks by running both and comparing. Where they differ is the
// range of enumerator values the fallback can see, which docs/reference/reflect.md
// states along with the trait that widens it.
#ifndef JMPXX_REFLECT_HPP
#define JMPXX_REFLECT_HPP

#include "jmpxx/core/config.hpp"
#include "jmpxx/erased.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

// JMPXX_REFLECTION is 1 when C++26 static reflection is available. The default is the
// standard library feature-test macro; a toolchain that implements P2996 before
// defining it (a pre-standardization fork) is opted in with -DJMPXX_REFLECTION=1.
#if !defined(JMPXX_REFLECTION)
#if defined(__cpp_lib_reflection) && __cpp_lib_reflection >= 202506L
#define JMPXX_REFLECTION 1
#else
#define JMPXX_REFLECTION 0
#endif
#endif

#if JMPXX_REFLECTION
#include <meta>
#endif

namespace jmpxx {
namespace reflect {

// One failure an error enum declares: its underlying value and its enumerator name.
// The name aliases static storage that outlives the program, so a failure_info is
// freely copied and stored.
struct failure_info {
  int value;
  std::string_view name;
};

// The value range the C++20 fallback scans for enumerators. Specialize for an enum
// whose enumerators fall outside the default window. Unused on the reflection path,
// which enumerates directly.
template <class E>
struct enum_range {
  static constexpr int min = -128;
  static constexpr int max = 127;
};

namespace detail {

#if !JMPXX_REFLECTION

// Recover an enumerator's spelling by instantiating this template on the enum
// constant and slicing the compiler's pretty signature. The markers bracket the
// value so that GCC appending the return-type alias, with its own '=', does not fool
// the slice: GCC and Clang read after "V = " and stop at the first ']' or ';'; MSVC
// reads between the function-name '<' and ">(". The MSVC vs GCC/Clang spelling choice
// asks the platform unit through JMPXX_COMPILER_MSVC rather than reading a raw
// compiler macro, so the platform fence stays clean. __PRETTY_FUNCTION__ and
// __FUNCSIG__ themselves are compiler-portability builtins, not platform constructs.
template <auto V>
constexpr std::string_view enumerator_spelling() noexcept {
#if JMPXX_COMPILER_MSVC
  std::string_view p = __FUNCSIG__;
  const auto end = p.rfind(">(");
  const auto lt = p.rfind('<', end);
  p = p.substr(lt + 1, end - lt - 1);
  if (p.rfind("enum ", 0) == 0) p.remove_prefix(5);
#else
  std::string_view p = __PRETTY_FUNCTION__;
  const auto m = p.find("V = ");
  p.remove_prefix(m + 4);
  p = p.substr(0, p.find_first_of(";]"));
#endif
  const auto scope = p.rfind("::");
  if (scope != std::string_view::npos) p.remove_prefix(scope + 2);
  return p;
}

// True when V names an enumerator. A value that is not an enumerator prints as a cast,
// "(app::status)99", "status(99)", or a bare number; after scope stripping that leaves
// a fragment like "status)99" or a leading digit, none of which is a bare identifier.
// Requiring every character of the stripped spelling to be an identifier character,
// and the first not to be a digit, rejects all those forms and accepts only a real
// enumerator name.
template <auto V>
constexpr bool is_enumerator() noexcept {
  const std::string_view n = enumerator_spelling<V>();
  if (n.empty()) return false;
  const char f = n.front();
  if (!(f == '_' || (f >= 'a' && f <= 'z') || (f >= 'A' && f <= 'Z'))) return false;
  for (char c : n)
    if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9')))
      return false;
  return true;
}

// The unqualified spelling of a type, sliced the same way, so the fallback's type name
// matches the reflection path's identifier_of, which is also unqualified.
template <class T>
constexpr std::string_view type_spelling() noexcept {
#if JMPXX_COMPILER_MSVC
  std::string_view p = __FUNCSIG__;
  const auto end = p.rfind(">(");
  const auto lt = p.rfind('<', end);
  p = p.substr(lt + 1, end - lt - 1);
  if (p.rfind("enum ", 0) == 0) p.remove_prefix(5);
#else
  std::string_view p = __PRETTY_FUNCTION__;
  const auto m = p.find("T = ");
  p.remove_prefix(m + 4);
  p = p.substr(0, p.find_first_of(";]"));
#endif
  const auto scope = p.rfind("::");
  if (scope != std::string_view::npos) p.remove_prefix(scope + 2);
  return p;
}

template <class E>
constexpr int range_lo() noexcept {
  return enum_range<E>::min;
}
template <class E>
constexpr int range_span() noexcept {
  return enum_range<E>::max - enum_range<E>::min + 1;
}

template <class E>
constexpr std::size_t count_enumerators() noexcept {
  return [&]<int... I>(std::integer_sequence<int, I...>) {
    return (std::size_t{0} + ... +
            (is_enumerator<static_cast<E>(range_lo<E>() + I)>() ? 1u : 0u));
  }(std::make_integer_sequence<int, range_span<E>()>());
}

template <class E>
constexpr std::string_view scan_name(E v) noexcept {
  return [&]<int... I>(std::integer_sequence<int, I...>) {
    std::string_view out;
    (void)(((static_cast<int>(v) == range_lo<E>() + I &&
             is_enumerator<static_cast<E>(range_lo<E>() + I)>())
                ? (out = enumerator_spelling<static_cast<E>(range_lo<E>() + I)>(),
                   true)
                : false) ||
           ...);
    return out;
  }(std::make_integer_sequence<int, range_span<E>()>());
}

template <class E>
constexpr std::optional<E> scan_value(std::string_view name) noexcept {
  return [&]<int... I>(std::integer_sequence<int, I...>) {
    std::optional<E> out;
    (void)(((is_enumerator<static_cast<E>(range_lo<E>() + I)>() &&
             enumerator_spelling<static_cast<E>(range_lo<E>() + I)>() == name)
                ? (out = static_cast<E>(range_lo<E>() + I), true)
                : false) ||
           ...);
    return out;
  }(std::make_integer_sequence<int, range_span<E>()>());
}

// The static failure set, value-ordered. The scan visits values in ascending order,
// so collecting in scan order is already value-ordered, which matches the order the
// reflection path sorts into, so failures<E>() is identical on both paths.
template <class E>
constexpr auto build_failures() noexcept {
  std::array<failure_info, count_enumerators<E>()> out{};
  std::size_t k = 0;
  [&]<int... I>(std::integer_sequence<int, I...>) {
    (void)((is_enumerator<static_cast<E>(range_lo<E>() + I)>()
                ? (out[k++] = failure_info{range_lo<E>() + I,
                                           enumerator_spelling<static_cast<E>(
                                               range_lo<E>() + I)>()},
                   void())
                : void()),
           ...);
  }(std::make_integer_sequence<int, range_span<E>()>());
  return out;
}

// A null-terminated static copy of one enumerator's name, for the domain message
// surface, which is a C string. The view from the signature is not terminated at the
// enumerator boundary, so it is copied once into static storage here.
template <auto V>
struct enumerator_cstr {
  static constexpr auto storage = []() noexcept {
    constexpr std::string_view sv = enumerator_spelling<V>();
    std::array<char, sv.size() + 1> a{};
    for (std::size_t i = 0; i < sv.size(); ++i) a[i] = sv[i];
    a[sv.size()] = '\0';
    return a;
  }();
};
template <auto V>
constexpr const char* enumerator_c_str() noexcept {
  // if constexpr keeps the null-terminated copy from being instantiated for the
  // unnamed values the scan also visits, so only real enumerators cost storage.
  if constexpr (is_enumerator<V>())
    return enumerator_cstr<V>::storage.data();
  else
    return nullptr;
}

template <class T>
struct type_cstr {
  static constexpr auto storage = []() noexcept {
    constexpr std::string_view sv = type_spelling<T>();
    std::array<char, sv.size() + 1> a{};
    for (std::size_t i = 0; i < sv.size(); ++i) a[i] = sv[i];
    a[sv.size()] = '\0';
    return a;
  }();
};

// A value-indexed table of null-terminated enumerator names for the domain's runtime
// message lookup: slot v - min is the name when v is an enumerator and null otherwise.
template <class E>
inline constexpr auto name_table = []() noexcept {
  std::array<const char*, range_span<E>()> t{};
  [&]<int... I>(std::integer_sequence<int, I...>) {
    ((t[I] = enumerator_c_str<static_cast<E>(range_lo<E>() + I)>()), ...);
  }(std::make_integer_sequence<int, range_span<E>()>());
  return t;
}();

#endif  // !JMPXX_REFLECTION

}  // namespace detail

#if JMPXX_REFLECTION

// The enumerator name for a value, or an empty view when the value names no
// enumerator. Constant-evaluable and usable at run time; the reflection work is
// confined to the compile-time loop and the returned view aliases static metadata.
template <class E>
  requires std::is_enum_v<E>
constexpr std::string_view enum_name(E value) noexcept {
  template for (constexpr auto e :
                std::define_static_array(std::meta::enumerators_of(^^E)))
    if (value == [:e:]) return std::meta::identifier_of(e);
  return {};
}

// The enumerator a name denotes, or no value when the name matches none.
template <class E>
  requires std::is_enum_v<E>
constexpr std::optional<E> enum_cast(std::string_view name) noexcept {
  template for (constexpr auto e :
                std::define_static_array(std::meta::enumerators_of(^^E)))
    if (name == std::meta::identifier_of(e)) return [:e:];
  return std::nullopt;
}

// The unqualified name of the enum type.
template <class E>
  requires std::is_enum_v<E>
constexpr std::string_view type_name() noexcept {
  return std::meta::identifier_of(^^E);
}

// The number of enumerators the type declares.
template <class E>
  requires std::is_enum_v<E>
constexpr std::size_t enum_count() noexcept {
  return std::meta::enumerators_of(^^E).size();
}

namespace detail {
// The static, value-ordered failure set. Each name is promoted to static storage with
// define_static_string so the {value, name} pairs survive to run time. The result is a
// plain std::array, not a define_static_array, because failure_info holds a
// string_view, which is not a structural type a reflection can promote element by
// element. Insertion keeps it value-ordered, matching the fallback's scan order.
template <class E>
constexpr auto build_failures() {
  std::array<failure_info, reflect::enum_count<E>()> out{};
  std::size_t k = 0;
  template for (constexpr auto e :
                std::define_static_array(std::meta::enumerators_of(^^E))) {
    failure_info fi{static_cast<int>([:e:]),
                    std::string_view(
                        std::define_static_string(std::meta::identifier_of(e)))};
    std::size_t j = k;
    while (j > 0 && out[j - 1].value > fi.value) {
      out[j] = out[j - 1];
      --j;
    }
    out[j] = fi;
    ++k;
  }
  return out;
}
template <class E>
inline constexpr auto failures_storage = build_failures<E>();
}  // namespace detail

#else  // C++20 hand-written fallback

template <class E>
  requires std::is_enum_v<E>
constexpr std::string_view enum_name(E value) noexcept {
  return detail::scan_name<E>(value);
}

template <class E>
  requires std::is_enum_v<E>
constexpr std::optional<E> enum_cast(std::string_view name) noexcept {
  return detail::scan_value<E>(name);
}

template <class E>
  requires std::is_enum_v<E>
constexpr std::string_view type_name() noexcept {
  return detail::type_spelling<E>();
}

template <class E>
  requires std::is_enum_v<E>
constexpr std::size_t enum_count() noexcept {
  return detail::count_enumerators<E>();
}

namespace detail {
template <class E>
inline constexpr auto failures_storage = build_failures<E>();
}  // namespace detail

#endif  // JMPXX_REFLECTION

// The static set of failures the enum declares, value-ordered, as {value, name}
// pairs. For an error enum this is the statically derivable specification of what a
// unit reporting that enum can fail with. Identical on both paths within the
// fallback's scanned range.
template <class E>
  requires std::is_enum_v<E>
constexpr std::span<const failure_info> failures() noexcept {
  return {detail::failures_storage<E>.data(), detail::failures_storage<E>.size()};
}

namespace detail {

// An error_domain whose identity is an enum type and whose per-value message is the
// derived enumerator name, so a component reports type-erased errors keyed by its enum
// with no hand-written name table. One instance exists per enum type.
template <class E>
class reflected_domain final : public error_domain {
 public:
  [[nodiscard]] const char* name() const noexcept override {
#if JMPXX_REFLECTION
    return std::define_static_string(reflect::type_name<E>());
#else
    return type_cstr<E>::storage.data();
#endif
  }
  [[nodiscard]] const char* message(int value) const noexcept override {
#if JMPXX_REFLECTION
    const char* out = "unknown error";
    template for (constexpr auto e :
                  std::define_static_array(std::meta::enumerators_of(^^E)))
      if (value == static_cast<int>([:e:]))
        out = std::define_static_string(std::meta::identifier_of(e));
    return out;
#else
    const int lo = range_lo<E>();
    if (value >= lo && value < lo + range_span<E>()) {
      const char* s = name_table<E>[value - lo];
      if (s) return s;
    }
    return "unknown error";
#endif
  }
};

template <class E>
inline const reflected_domain<E> reflected_domain_instance{};

}  // namespace detail

// The error_domain derived from an enum type. Its name is the enum's type name and its
// message for a value is that value's enumerator name, both derived rather than
// hand-written. A component selects it once and reports erased errors keyed by its own
// enum without writing a name switch.
template <class E>
  requires std::is_enum_v<E>
[[nodiscard]] const error_domain& enum_domain() noexcept {
  return detail::reflected_domain_instance<E>;
}

// Build a type-erased error from an enumerator, tagged with the enum's derived domain.
// The boundary that reads it gets the enumerator name as the message with no table the
// component had to maintain.
template <class E>
  requires std::is_enum_v<E>
[[nodiscard]] erased_error as_erased(E value) noexcept {
  return erased_error(static_cast<int>(value), enum_domain<E>());
}

}  // namespace reflect
}  // namespace jmpxx

#endif  // JMPXX_REFLECT_HPP
