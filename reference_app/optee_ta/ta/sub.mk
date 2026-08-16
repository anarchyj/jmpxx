# SPDX-License-Identifier: MIT
# include/ holds only the interface the normal-world client shares with this trusted
# application, so the client can add it without picking up anything meant for the secure
# side. compat/ holds the shims the trusted-application build needs and nothing else does.
global-incdirs-y += include
global-incdirs-y += compat
# JMPXX_INCLUDE_DIR points at an installed jmpxx include directory, so the trusted
# application consumes the library the way a consumer does rather than by a relative path.
# The _ext form takes the path as given; the plain form would prefix it with this subdir.
global-incdirs_ext-y += $(JMPXX_INCLUDE_DIR)

# A trusted application builds with no default include search, so the freestanding C++
# headers the core uses, <type_traits> and <cstddef> among them, have to be named. They are
# header-only and pull in no hosted runtime, which is the property that lets the core
# compile here at all. JMPXX_TA_CXX_INCLUDE is the toolchain's C++ include directory.
global-incdirs_ext-y += $(JMPXX_TA_CXX_INCLUDE)
global-incdirs_ext-y += $(JMPXX_TA_CXX_INCLUDE)/$(JMPXX_TA_TRIPLE)

srcs-y += manifest_ta.cpp
srcs-y += unwinder_hooks.c
# The library requires C++20 and the development kit does not select a standard.
cxxflags-manifest_ta.cpp-y += -std=c++20
