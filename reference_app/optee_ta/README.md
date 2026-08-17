<!-- SPDX-License-Identifier: MIT -->
# Manifest verifier for OP-TEE

A boot-manifest verifier that runs in the ARM secure world as an OP-TEE trusted
application, with a normal-world client that drives it. It is the field-use exercise for
the experimental unwind arm inside a real secure-firmware stack: Trusted Firmware-A brings
the machine up, OP-TEE OS hosts the trusted application, and the verification the client
asks for is performed by jmpxx running on the secure side.

The verifier walks a manifest describing a chain of firmware images, hashing each image
with the TEE's own cryptographic operations and following an entry's reference to the entry
it depends on. Verification is recursive descent over a buffer the normal world supplied,
so it is untrusted input parsed at depth, which is the shape a secure boot stage has.

## Why the arm is here

A fault found several levels down escapes to one landing in `verify_manifest`, and each
level releases the hash context and the visit marker it owns as the unwinder passes
through. The levels carry no propagation construct: each parses its entry and descends.

The trusted application instance is kept alive between invocations, so what a failed
verification leaves behind is what the next verification starts from. A leaked hash context
would exhaust a long-lived secure-world service, and a visit path left populated would make
the next request report a cycle that is not there. The client checks this directly by
verifying a bad manifest and then a good one through the same session.

## Layout

| Path | What it is |
| --- | --- |
| `ta/manifest_ta.cpp` | the verifier, secure side |
| `ta/include/manifest_ta.h` | the interface the two sides share |
| `ta/compat/features.h` | a shim the trusted-application build needs, described at its head |
| `ta/unwinder_hooks.c` | the platform hooks the unwinder asks a hosted system for |
| `host/verify_client.c` | the normal-world client |

## Building

The trusted application needs an OP-TEE development kit and jmpxx installed to a prefix.

```sh
cmake -S . -B build/install -DJMPXX_BUILD_TESTS=OFF -DJMPXX_BUILD_VERIFY=OFF
cmake --install build/install --prefix "$PREFIX"

export TA_DEV_KIT_DIR=<optee_os>/out/arm/export-ta_arm64
# The workspace's own toolchain, not a distribution cross-compiler. A distribution
# aarch64 toolchain links its libstdc++ copy of std::random_device into the trusted
# application and then fails on getentropy, open, read and ioctl, which a trusted
# application's C library does not provide.
export CROSS_COMPILE=<workspace>/toolchains/aarch64/bin/aarch64-none-linux-gnu-
export JMPXX_INCLUDE_DIR="$PREFIX/include"
# The kit builds with no default include search, so the toolchain's C++ headers are named.
export JMPXX_TA_CXX_INCLUDE=<toolchain>/aarch64-none-linux-gnu/include/c++/<version>
export JMPXX_TA_TRIPLE=aarch64-none-linux-gnu
make -C reference_app/optee_ta/ta O=<build dir>
```

The client is an ordinary aarch64 program against `libteec`:

```sh
aarch64-linux-gnu-gcc -O2 -I<optee_client>/libteec/include \
  -I reference_app/optee_ta/ta/include \
  reference_app/optee_ta/host/verify_client.c -lteec -o verify_client
```

Install the signed `.ta` under `/lib/optee_armtz` and the client anywhere on the target's
path, then run `verify_client` with `tee-supplicant` running.

## What it reports

Driven on the full stack, Trusted Firmware-A through BL1, BL2 and BL31, OP-TEE OS as BL32,
U-Boot and Linux, the client reports:

```
  valid        verified, 3 entries
  valid again  verified, 3 entries
  dangling     fault 7 at depth 0 in 'absent': a referenced entry is absent
  cycle        fault 8 at depth 2 in 'one': the references form a cycle
  bad header   fault 2 at depth 0 in 'root': the header is not MFST
  truncated    fault 1 at depth 0 in 'boot': the manifest is truncated
```

`cycle` is the escape the arm exists for: a fault found two levels down, unwinding through
two frames that each hold a hash context and a visit marker, arriving with its code, its
depth and the offending name. `valid again` is the property a bare `longjmp` would break,
a good manifest verified after a failed one on the same secure-world code.

## What the environment requires

C++ in a trusted application needs `CFG_TA_LIBGCC=y`, which is what links the unwinder the
arm drives, and the development kit refuses a `.cpp` source without it. The arm's
precondition and that setting are the same requirement: without the unwinder and its
exception tables the escape would skip destructors.

Five constraints come from the environment rather than from jmpxx, and each is documented
where it is met.

Include order matters, because the kit's headers define short macros that collide with
libstdc++ member names. The kit's headers and `setjmp.h` carry no `extern "C"` guard. The
workspace toolchain's unwinder was built against a hosted C library, so its threading and
loader hooks need answering; `ta/unwinder_hooks.c` explains each one and what a bare-metal
toolchain would do instead. Linking libstdc++ and that unwinder marks the binary
`ELFOSABI_GNU`, which the loader refuses before reading a program header, so
`ta/normalize_osabi.py` sets the identifier back and the `osabi` make target signs the
result again.

The fifth is a limit rather than a step. The verifier keeps its landing state in
thread-local storage, and this OP-TEE build does not establish a trusted application's
thread-local block for every TEE thread that enters it, so an invocation that lands on such
a thread reads a landing pointer that is not one. Opening a session per invocation, which
the client does, keeps calls on threads whose block is initialized. Any trusted application
using the arm needs the same care until the environment establishes the block for every
thread.
