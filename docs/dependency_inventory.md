# Dependency Inventory

このファイルは、mockfakegen が直接取り込む外部依存の pin と使用範囲を記録します。

## ket

- Source: <https://github.com/horiyamayoh/ket.git>
- Import form: Git submodule at `third_party/ket`
- Branch tracking: `main`
- Initial pinned commit: `9a40b5034a58ef5f7f65681fb8897d7e6a3824ca`
- Initial commit date: `2026-06-23 09:53:13 +0900`
- Usage policy: repository is imported whole, but CMake selects only required modules.
- CMake boundary: do not call `add_subdirectory(third_party/ket)`; use `mockfakegen_ket`
  to expose selected include directories and compiled `.cpp` files.
- Source boundary: ket usage stays in CLI/I/O/support code. Do not add ket dependencies to
  `src/clang`, `src/model`, `src/generator`, or `src/runtime_template`.

Selected initial modules:

- `ascii`
- `cli`
- `concurrency`
- `contract`
- `file`
- `parse`
- `scope`
- `string`

Compiled ket sources:

- `third_party/ket/modules/ascii/ket_ascii.cpp`
- `third_party/ket/modules/file/ket_file.cpp`

Header-only modules are exposed through `mockfakegen_ket` include directories only.

## Update procedure

```sh
git submodule update --remote third_party/ket
git -C third_party/ket status
git submodule status third_party/ket
```

After reviewing the upstream change, update this file if the pinned commit, selected modules,
or license status changes.

## License status

The inspected ket snapshot does not contain a root `LICENSE`, `COPYING`, or `NOTICE` file.
Before public distribution of mockfakegen, confirm the upstream license and add the appropriate
root license or third-party notices.
