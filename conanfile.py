from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout


class KryptosVXConan(ConanFile):
    name = "kryptos-vx"
    version = "1.0.0"
    license = "MIT"
    description = "Zero-Bloat C++20 SIMD System Intelligence, De-Bloater & AI Context Engine"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
