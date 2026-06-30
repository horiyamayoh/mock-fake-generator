# AGENTS.md

このリポジトリで作業するコーディングエージェント向けの指示です。

## Source of truth

- 設計の正本は `mock_fake_generator_design_ket_v1_1.md` です。
- 今回のプロジェクトは C++23 固定の `mockfakegen` です。
- 中核は Clang LibTooling です。C++ 宣言を正規表現だけで parse しないでください。
- ket はツール本体の周辺処理に使いますが、生成物には ket 依存を出してはいけません。

## Project direction

- `mockfakegen` は C++ ヘッダから gMock 用 `MockXXX.h` とリンク差し替え用
  `FakeXXX.cpp` を生成するツールです。
- 生成物は Google Mock、標準ライブラリ、生成された `MockFakeRuntime.h` だけで
  compile できる必要があります。
- 未対応構文は silent skip せず、理由付き diagnostic と report に残してください。
- 生成できたことと使えることは別です。生成後の compile validation を前提にしてください。
- 実装は phase / component / acceptance criteria 単位で進めてください。

## Dependency rules

- ket は `third_party/ket` の Git submodule として丸ごと取り込みます。
- CMake では ket 自体を `add_subdirectory` しません。
- `mockfakegen_ket` target で、実際に使う ket module の include directory と `.cpp` だけを
  明示的に選んでください。
- ket module 自体は原則として改変しないでください。workaround が必要な場合は
  `src/support` に隔離してください。
- `src/clang`, `src/model`, `src/generator`, `src/runtime_template` へ ket 依存を広げないでください。

## Generated output rules

- `MockXXX.h`, `FakeXXX.cpp`, `MockFakeRuntime.h`, `AllMocks.h`,
  `CMakeLists.fragment.cmake` には `ket::` や `ket_` include を出力しないでください。
- timestamp など非 deterministic な内容をデフォルトで出力しないでください。
- 壊れた C++ を出すより、unsupported として診断してください。
- `FakeXXX.cpp` と製品 `.cpp` を同時リンクしない前提を docs と report で明示してください。

## Build and check

主環境は Linux/WSL です。

```sh
git submodule update --init --recursive
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --build --preset dev --target check-format
git diff --check
```

sanitize preset:

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

## Keep changes scoped

- ユーザーが求めていない component や生成機能をついでに追加しないでください。
- 既存の未コミット変更を勝手に戻さないでください。
- 実装ファイルを追加する場合は、設計書の該当 section と責務境界を確認してください。
- 初期実装や MVP という言葉を品質低下の理由にしないでください。
