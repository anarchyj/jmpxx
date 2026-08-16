#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Set a trusted application's ELF OS/ABI identifier back to the System V default.

OP-TEE's loader accepts a trusted application only when its ELF header declares
`ELFOSABI_NONE`; `ldelf/ta_elf.c` rejects anything else outright with "Cannot parse ELF",
before it looks at a single program header. Linking a C++ trusted application against the
GNU toolchain's libstdc++ and libgcc unwinder marks the output `ELFOSABI_GNU`, because
those archives use GNU extensions, and the link propagates the identifier. The result is a
trusted application that builds, signs, installs, and is then refused at load time with a
message that says nothing about the cause.

Nothing else about the binary needs to change. The identifier describes which extensions
the object may use, and the loader's requirement is that it claim none; the code the
trusted application runs is unaffected, which is why a single byte is the whole fix. Run
this on the stripped ELF before signing, and sign the result with the development kit's own
signer.

A bare-metal aarch64-none-elf toolchain avoids the situation, because its libraries carry
no GNU marking. Where only a Linux-targeted toolchain is available, as in this workspace,
this is the smaller of the two costs.

Usage: normalize_osabi.py ELF_FILE
"""

import sys

EI_OSABI = 7
ELFOSABI_NONE = 0

NAMES = {0: "System V", 3: "GNU", 9: "FreeBSD", 12: "OpenBSD"}


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    path = sys.argv[1]
    with open(path, "r+b") as f:
        head = f.read(16)
        if head[:4] != b"\x7fELF":
            print(f"{path} is not an ELF file", file=sys.stderr)
            return 1
        current = head[EI_OSABI]
        if current == ELFOSABI_NONE:
            print(f"{path}: OS/ABI is already System V")
            return 0
        f.seek(EI_OSABI)
        f.write(bytes([ELFOSABI_NONE]))
    print(f"{path}: OS/ABI {NAMES.get(current, current)} -> System V, "
          f"which is what the loader accepts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
