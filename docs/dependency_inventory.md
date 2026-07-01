# Dependency Inventory

このファイルは、mockfakegen が直接取り込む外部依存の pin、使用範囲、更新方針を記録します。

## Distribution scope

- Install support: `install(TARGETS mockfakegen RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})`.
- Install smoke: CTest runs `cmake --install` into a temporary prefix and executes the installed
  `mockfakegen --help`.
- Package metadata: CPack/export config packages are intentionally not emitted yet. The current
  distribution scope is a built CLI executable installed from this source tree. Revisit CPack and
  CMake package exports when the project needs binary archives, relocatable SDK metadata, or
  installed libraries.

## LLVM / Clang

- Source: distribution LLVM/Clang CMake packages.
- CMake lookup: `MOCKFAKEGEN_LLVM_ROOT` if provided, otherwise `/usr/lib/llvm-18`,
  `/usr/lib/llvm-19`, `/usr/lib/llvm-17`, `/usr/lib/llvm-20`, then default CMake package search.
- CI version: Ubuntu 24.04 packages `clang-18`, `llvm-18-dev`, and `libclang-18-dev`.
- Observed local package version: LLVM `18.1.3`.
- License: Apache-2.0 with LLVM exception.
- Usage: LibTooling/AST parsing, formatting via libFormat, and generated-output validation support.
- clang-format config: `.clang-format` uses `Standard: Latest` because clang-format 18.1.3 rejects
  the literal `c++23` style value while the project itself still builds as C++23.
- Reproducibility: CI installs distro packages with apt rather than pinning exact package
  revisions. For fully offline or bit-reproducible builds, provide a preinstalled LLVM/Clang tree
  and configure with `-DMOCKFAKEGEN_LLVM_ROOT=/path/to/llvm`.
- Update procedure: test the new LLVM package with both `dev` and `sanitize` presets, then update
  the CI apt package names and this inventory if the supported major version changes.

## ket

- Source: <https://github.com/horiyamayoh/ket.git>
- Import form: Git submodule at `third_party/ket`
- Branch tracking: `main`
- Initial pinned commit: `9a40b5034a58ef5f7f65681fb8897d7e6a3824ca`
- Initial commit date: `2026-06-23 09:53:13 +0900`
- Usage policy: repository is imported whole, but CMake selects only required modules.
- CMake boundary: do not call `add_subdirectory(third_party/ket)`; use `mockfakegen_ket`
  to expose selected include directories and compiled `.cpp` files when a selected module has one.
- Source boundary: ket usage stays in CLI/I/O/support code. Do not add ket dependencies to
  `src/clang`, `src/model`, `src/generator`, or `src/runtime_template`.

Selected modules:

- `cli`
- `parse`

Compiled ket sources: none. The selected `cli` and `parse` modules are header-only in the pinned
snapshot.

The older broad foundation smoke has been narrowed to the production/test modules actually exposed
by `mockfakegen_ket`.

## GoogleTest / Google Mock

- Source: CMake package `GTest` when available; otherwise CMake `FetchContent`.
- FetchContent pin: `https://github.com/google/googletest.git` tag `v1.14.0`.
- License: BSD-3-Clause.
- Usage: generated-output smoke tests, validator link smoke tests, and fixture tests that exercise
  generated gMock code. Generated output includes `<gmock/gmock.h>` but does not vendor gMock.
- Reproducibility: CI does not install system GTest, so configure downloads the pinned tag during
  CMake configure. Offline CI must either preinstall a compatible `GTest` package exposing
  `GTest::gmock_main` or pre-populate CMake FetchContent's download cache.
- Update procedure: change the FetchContent tag, run dev/sanitize presets, verify generated-output
  smoke/link validation tests, and update this inventory with the new tag and license status.

## ket update procedure

```sh
git submodule update --remote third_party/ket
git -C third_party/ket status
git submodule status third_party/ket
```

After reviewing the upstream change, update this file if the pinned commit, selected modules,
or license status changes.

## ket license status

The inspected ket snapshot does not contain a root `LICENSE`, `COPYING`, or `NOTICE` file.
Before public distribution of mockfakegen, confirm the upstream license and add the appropriate
root license or third-party notices.
