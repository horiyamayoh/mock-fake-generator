# mockfakegen

`mockfakegen` generates Google Mock test doubles from C++23 headers:

- `MockXXX.h` for gMock expectations.
- `FakeXXX.cpp` for link-time replacement of product implementations.
- `MockFakeRuntime.h`, optional `AllMocks.h`, `manifest.json`,
  `generation_report.md`, and `CMakeLists.fragment.cmake`.

The executable is implemented end to end for the supported surface: it scans `.h`
files, resolves `compile_commands.json`, parses with Clang LibTooling, generates and
formats output, runs generated-output validation, writes reports/manifests, and can be
installed with CMake. Unsupported input is diagnosed in stderr and in the report instead
of being silently skipped.

Current intentional limits are documented in
[`docs/user_guide.md`](docs/user_guide.md): C++23 only, `.h` headers only, public class
members only, deferred external config files, deferred extra CLI include/define args,
and unsupported C++ constructs reported with reasons.

The design source of truth remains
[`mock_fake_generator_design_ket_v1_1.md`](mock_fake_generator_design_ket_v1_1.md).

## Build

Install the main Linux/WSL dependencies, then initialize the ket submodule:

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  cmake ninja-build clang-18 llvm-18-dev libclang-18-dev python3

git submodule update --init --recursive
```

Configure, build, and test the development preset:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

If CMake cannot find LLVM/Clang package files, pass an explicit root containing
`lib/cmake/llvm/LLVMConfig.cmake` and `lib/cmake/clang/ClangConfig.cmake`:

```sh
cmake --preset dev -DMOCKFAKEGEN_LLVM_ROOT=/usr/lib/llvm-18
```

The sanitizer preset uses the same project configuration with sanitizers enabled:

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize --output-on-failure
```

Install the CLI from a configured build tree:

```sh
cmake --install build/dev --prefix /tmp/mockfakegen-install
/tmp/mockfakegen-install/bin/mockfakegen --help
```

## Basic Use

Run the tool against a project root with a build directory that contains
`compile_commands.json`:

```sh
build/dev/mockfakegen \
  --input-root /path/to/project/include \
  --output-dir /path/to/project/build/mockfakegen \
  --build-path /path/to/project/build \
  --project-root /path/to/project \
  --overwrite
```

Generated output depends only on Google Mock, the C++ standard library, product headers,
and generated `MockFakeRuntime.h`. It must not include ket.

For complete CLI option semantics, validation setup, generated file schemas, and a small
copyable integration example, see [`docs/user_guide.md`](docs/user_guide.md).

## Link Substitution

`FakeXXX.cpp` replaces the corresponding product `.cpp` at link time. Do not link both
the generated fake source and the product source that defines the same class members into
one test target. Doing so can produce duplicate symbols or can leave the product
implementation in use.

Generated `CMakeLists.fragment.cmake` exposes only link-ready fake sources:

- `MOCKFAKE_GENERATED_SOURCES`
- `MOCKFAKE_GENERATED_INCLUDE_DIR`

Use the fragment from tests while excluding replaced product `.cpp` files:

```cmake
include(path/to/generated/CMakeLists.fragment.cmake)

add_executable(MyTest
    MyTest.cpp
    ${MOCKFAKE_GENERATED_SOURCES}
)

target_include_directories(MyTest PRIVATE
    path/to/product/include
    ${MOCKFAKE_GENERATED_INCLUDE_DIR}
)

target_link_libraries(MyTest PRIVATE GTest::gmock_main)
```

## Quality Gates

The repository CI runs the same checks expected for local development:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
cmake --build --preset dev --target check-generated-output
cmake --build --preset dev --target check-format
git diff --check

cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize --output-on-failure
```

## ket Policy

ket is imported whole under `third_party/ket`, but this project does not build ket as a
subproject. The root CMake exposes only the ket modules currently used by tool-side
support code through `mockfakegen_ket`.

Selected modules:

- `cli`
- `parse`

Generated test-double files must not depend on ket. That rule applies to `MockXXX.h`,
`FakeXXX.cpp`, `MockFakeRuntime.h`, `AllMocks.h`, and generated CMake fragments.

Dependency details, pins, licenses, and update notes are tracked in
[`docs/dependency_inventory.md`](docs/dependency_inventory.md).
