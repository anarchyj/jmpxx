# SPDX-License-Identifier: MIT
# Conan 2.x recipe for jmpxx. A consumer uses it without waiting for ConanCenter:
# `conan create .` from the repo root, then depend on `jmpxx/0.1.5` and link the
# `jmpxx::jmpxx` CMake target.
#
# jmpxx is header-only, so the package copies the headers and the license, declares no
# binary or library directories, and clears its package id so one package serves every
# configuration.
import os

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.files import copy


class JmpxxConan(ConanFile):
    name = "jmpxx"
    version = "0.1.5"
    license = "MIT"
    homepage = "https://github.com/anarchyj/jmpxx"
    description = "Non-local, RAII-correct, exception-free failure propagation for C++20."
    topics = ("error-handling", "result", "no-exceptions", "header-only", "embedded")
    package_type = "header-library"
    settings = "os", "arch", "compiler", "build_type"
    no_copy_source = True
    exports_sources = "include/*", "LICENSE"

    def validate(self):
        # jmpxx is C++20. CMakeDeps does not carry a compile-feature requirement to the
        # consumer, so the consumer sets the standard; this rejects a lower one with a
        # clear message rather than a wall of failed requires-clauses.
        check_min_cppstd(self, 20)

    def package(self):
        copy(self, "*.hpp", os.path.join(self.source_folder, "include"),
             os.path.join(self.package_folder, "include"))
        copy(self, "LICENSE", self.source_folder,
             os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.set_property("cmake_file_name", "jmpxx")
        self.cpp_info.set_property("cmake_target_name", "jmpxx::jmpxx")

    def package_id(self):
        self.info.clear()
