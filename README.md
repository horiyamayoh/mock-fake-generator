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
