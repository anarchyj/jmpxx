# SPDX-License-Identifier: MIT
# vcpkg overlay port for jmpxx. A consumer installs it without waiting for the central
# registry: vcpkg install jmpxx --overlay-ports=<path-to>/packaging/vcpkg
#
# jmpxx is header-only, so the port installs the headers and the CMake package config
# and removes the empty debug and library trees. SHA512 is the hash of the GitHub
# release tarball for the tag named by VERSION; recompute it whenever VERSION changes
# in vcpkg.json. vcpkg also prints the expected value on a mismatched install attempt.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO anarchyj/jmpxx
    REF "v${VERSION}"
    SHA512 65ffbcaf46bce1450444aa2fa60a0a3c233a3d6263b59b870a8f3929affbbf8f56ead3c55f0625f442658ef0204e664e5250d6d3fd1db30eea3bab8647fb825a
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DJMPXX_BUILD_VERIFY=OFF
        -DJMPXX_BUILD_TESTS=OFF
        -DJMPXX_BUILD_BENCHMARKS=OFF
        -DJMPXX_BUILD_LINT=OFF
        -DJMPXX_INSTALL=ON
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME jmpxx CONFIG_PATH lib/cmake/jmpxx)

# Header-only: no debug tree and no compiled libraries to keep.
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug" "${CURRENT_PACKAGES_DIR}/lib")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
