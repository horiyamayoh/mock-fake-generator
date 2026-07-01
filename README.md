# mockfakegen

`mockfakegen` は、C++ ヘッダから GoogleTest/gMock 用の `MockXXX.h` と、リンク差し替え用の
`FakeXXX.cpp` を生成する C++23 ツールとして設計されています。

現在のリポジトリは開発基盤の初期状態です。CLI、AST 解析、コード生成器、
`MockFakeRuntime.h` template はまだ実装していません。設計の正本は
[`mock_fake_generator_design_ket_v1_1.md`](mock_fake_generator_design_ket_v1_1.md) です。

## Development setup

ket は Git submodule として取り込みます。

```sh
git submodule update --init --recursive
```

configure / build / test:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The tool links against LLVM/Clang LibTooling through CMake package config files.
If CMake cannot find a complete LLVM/Clang installation, pass an explicit root:

```sh
cmake --preset dev -DMOCKFAKEGEN_LLVM_ROOT=/usr/lib/llvm-18
```

The root must contain `lib/cmake/llvm/LLVMConfig.cmake`,
`lib/cmake/clang/ClangConfig.cmake`, and Clang libraries such as
`clangTooling`, `clangAST`, `clangASTMatchers`, `clangFrontend`, `clangBasic`,
and `clangLex`.

sanitize preset:

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

format check:

```sh
cmake --build --preset dev --target check-format
```

## Link substitution and validation

`FakeXXX.cpp` is intended to replace the corresponding product implementation at link time.
Do not link a generated `FakeXXX.cpp` and the product `.cpp` that defines the same class
members into the same test target. That can produce duplicate symbols or accidentally keep the
product implementation in use.

Generated `CMakeLists.fragment.cmake` exposes:

- `MOCKFAKE_GENERATED_SOURCES`: only link-ready generated fake sources.
- `MOCKFAKE_GENERATED_INCLUDE_DIR`: the generated include directory.

Use those variables in a test target while excluding the replaced product `.cpp` files:

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

`--validate compile` is the default validation mode. It checks generated mock headers and fake
sources as separate translation units, but it does not prove that the final test target performs
correct link substitution. Use `--validate link` to additionally link a generated-fake smoke
executable with gMock where practical. The link mode can diagnose duplicate symbols when both a
product `.cpp` and generated `FakeXXX.cpp` are present in the validation link inputs.

When invoking the CLI outside this CMake test suite, link validation needs gMock include and link
artifacts to be discoverable. The environment variables accepted by the validator are `|`-separated:

```sh
MOCKFAKEGEN_GMOCK_INCLUDE_DIRS=/path/to/googlemock/include\|/path/to/googletest/include
MOCKFAKEGEN_GMOCK_LINK_FILES=/path/to/libgmock.a\|/path/to/libgtest.a
```

## Runtime registry modes

Generated fakes use `MockFakeRuntime.h` to find the active mock for each type.
Choose the registry mode with `--registry-mode`:

- `thread-local`: default. Each thread has its own mock stack. Use it when the product code
  calls generated fakes on the same thread that owns the scoped mock.
- `global-mutex`: one process-wide mock stack per mock type, protected by a mutex. Worker
  threads can see the scoped mock, but the returned pointer is not lifetime-protected after
  lookup; join worker threads before destroying `ScopedMock`, and avoid concurrent same-type
  scopes in the same process.
- `shared-owner`: one process-wide `std::shared_ptr` stack per mock type, protected by a mutex.
  Generated fakes keep a `shared_ptr` copy while calling the mock, so this is the safer mode for
  asynchronous or worker-thread tests.

## Repository layout

```txt
cmake/                  CMake helper functions
docs/                   dependency and development notes
src/clang/              future Clang LibTooling integration
src/generator/          future generated-code builders
src/model/              future IR model types
src/runtime_template/   future MockFakeRuntime.h template
src/support/            future ket-backed support layer
tests/foundation/       build skeleton smoke tests
tests/fixtures/         future test fixtures
third_party/ket/        ket Git submodule
```

## ket policy

ket is imported whole under `third_party/ket`, but this project does not build ket as a
subproject. The root CMake selects only the modules needed by `mockfakegen_ket`.

Initial selected modules:

- `ascii`
- `cli`
- `concurrency`
- `contract`
- `file`
- `parse`
- `scope`
- `string`

Generated test-double files must not depend on ket. That rule applies to `MockXXX.h`,
`FakeXXX.cpp`, `MockFakeRuntime.h`, `AllMocks.h`, and generated CMake fragments.

## Updating ket

Use Git to update the submodule, then review and pin the resulting commit.

```sh
git submodule update --remote third_party/ket
git -C third_party/ket status
git submodule status third_party/ket
```

When the pin changes, update [`docs/dependency_inventory.md`](docs/dependency_inventory.md)
with the reviewed commit and any module selection changes.
