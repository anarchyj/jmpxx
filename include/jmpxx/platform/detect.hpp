// SPDX-License-Identifier: MIT
// Platform, architecture, and compiler detection: the only place the library reads
// compiler predefined macros. Every other header asks for a target fact
// through the JMPXX_COMPILER_*, JMPXX_OS_*, and JMPXX_ARCH_* tokens defined here,
// so a raw `_MSC_VER`, `__x86_64__`, or `_WIN32` appearing anywhere outside this
// platform unit is a fence violation the platform-fence scan rejects. Centralizing
// detection makes that fence enforceable and gives the experimental unwind arm one
// stable place to ask which ABI it is on.
//
// It uses only the preprocessor, so it costs a consumer nothing to include and is
// safe anywhere, including on the minimal core's path.
#ifndef JMPXX_PLATFORM_DETECT_HPP
#define JMPXX_PLATFORM_DETECT_HPP

// Compiler identity. Exactly one of these is 1. Clang is checked before GCC
// because Clang defines __GNUC__ for compatibility, and the MSVC test excludes
// Clang because clang-cl defines _MSC_VER while behaving like Clang for intrinsics
// and inline assembly, so clang-cl is reported as Clang, which matches the builtins
// it accepts.
#if defined(__clang__)
#define JMPXX_COMPILER_CLANG 1
#else
#define JMPXX_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define JMPXX_COMPILER_GCC 1
#else
#define JMPXX_COMPILER_GCC 0
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define JMPXX_COMPILER_MSVC 1
#else
#define JMPXX_COMPILER_MSVC 0
#endif

// The active C++ standard version. MSVC reports __cplusplus as 199711L unless
// /Zc:__cplusplus is passed, so the real version is read from _MSVC_LANG there.
#if defined(_MSVC_LANG)
#define JMPXX_CPLUSPLUS _MSVC_LANG
#else
#define JMPXX_CPLUSPLUS __cplusplus
#endif

// Whether C++ exceptions are enabled in this translation unit. GCC and Clang
// define __cpp_exceptions (and the historical __EXCEPTIONS); MSVC defines
// _CPPUNWIND under /EH. The exception interop bridge asks for this through the one
// token so the bridge header carries no raw compiler macro.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define JMPXX_HAS_EXCEPTIONS 1
#else
#define JMPXX_HAS_EXCEPTIONS 0
#endif

// Whether RTTI is enabled in this translation unit. Type-erased dispatch must not
// rely on it, but the configuration matrix records the dimension explicitly.
#if defined(__GXX_RTTI) || defined(__cpp_rtti) || defined(_CPPRTTI)
#define JMPXX_HAS_RTTI 1
#else
#define JMPXX_HAS_RTTI 0
#endif

// Operating system and environment. At most one hosted OS is 1. A freestanding or
// bare-metal target matches none of them and is reported by JMPXX_OS_NONE, and
// JMPXX_OS_HOSTED is the union so a header can ask "is there an OS here" without
// listing every one.
#if defined(_WIN32)
#define JMPXX_OS_WINDOWS 1
#else
#define JMPXX_OS_WINDOWS 0
#endif

// __APPLE__ with __MACH__ is Darwin, the kernel under macOS and the other Apple
// systems; jmpxx treats them as one OS cell.
#if defined(__APPLE__) && defined(__MACH__)
#define JMPXX_OS_MACOS 1
#else
#define JMPXX_OS_MACOS 0
#endif

#if defined(__linux__)
#define JMPXX_OS_LINUX 1
#else
#define JMPXX_OS_LINUX 0
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
#define JMPXX_OS_BSD 1
#else
#define JMPXX_OS_BSD 0
#endif

#if JMPXX_OS_WINDOWS || JMPXX_OS_MACOS || JMPXX_OS_LINUX || JMPXX_OS_BSD
#define JMPXX_OS_HOSTED 1
#define JMPXX_OS_NONE 0
#else
#define JMPXX_OS_HOSTED 0
#define JMPXX_OS_NONE 1
#endif

// Architecture. At most one is 1. The MSVC spellings (_M_X64, _M_ARM64, _M_ARM) sit
// beside the GCC/Clang ones so the detection holds on every compiler in the matrix.
#if defined(__x86_64__) || defined(_M_X64)
#define JMPXX_ARCH_X86_64 1
#else
#define JMPXX_ARCH_X86_64 0
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define JMPXX_ARCH_ARM64 1
#else
#define JMPXX_ARCH_ARM64 0
#endif

// 32-bit ARM. _M_ARM is the MSVC spelling; __arm__ covers GCC/Clang. AArch64
// needs no exclusion here because it defines neither, and it is matched above.
#if defined(__arm__) || defined(_M_ARM)
#define JMPXX_ARCH_ARM32 1
#else
#define JMPXX_ARCH_ARM32 0
#endif

#if defined(__riscv)
#define JMPXX_ARCH_RISCV 1
#else
#define JMPXX_ARCH_RISCV 0
#endif

#if defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)
#define JMPXX_ARCH_WASM 1
#else
#define JMPXX_ARCH_WASM 0
#endif

#endif  // JMPXX_PLATFORM_DETECT_HPP
