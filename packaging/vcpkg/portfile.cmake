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
    SHA512 5ad9b2a1996502712f31b34f6fd4866646fee641fd5fe36dba6c00b9b30e0db5b2735ea5719891fbf26f3a1358d33b7cf91df4fd80f2cbed317e71f39419dc45
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
