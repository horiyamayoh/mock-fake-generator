# C++ Header-to-Mock/Fake 自動生成ツール設計書（ket 適用版）

**文書バージョン:** 1.1  
**想定ツール名:** `mockfakegen`  
**対象 OS:** Linux  
**ツール実装言語:** C++23 固定  
**ket 適用:** ツール本体の周辺処理に ket を採用する。生成物には ket 依存を持ち込まない。  
**生成対象:** C++ ヘッダ `.h` から GoogleTest/gMock 用 `MockXXX.h` とリンク差し替え用 `FakeXXX.cpp` を生成  
**最重要方針:** 生成物がテストコードへ即投入でき、かつ誤った生成を黙って行わないこと

---

## 1. 背景と目的

製品コードが次のようなクラス定義を持つ場合、

```cpp
class Hoge
{
public:
    Hoge() = default;
    ~Hoge() = default;

    bool Initialize(int argc, char* argv[]);
    void Finalize();
    bool DoSomething();
};
```

テストビルドでは製品 `.cpp` の代わりに `FakeHoge.cpp` をリンクし、`FakeHoge.cpp` から gMock ベースの `MockHoge` へ委譲することで、既存の製品コードを大きく改造せずにテストダブルを差し込める。

本ツールはこの作業を自動化する。

主な目的は以下である。

1. 指定フォルダ配下を再帰的に探索し、`.h` ファイルを取得する。
2. Clang LibTooling により C++ 構文を AST として解析し、正規表現ベースより高い Fidelity でクラス・メンバ関数を抽出する。
3. 各クラスに対応する `MockXXX.h` と `FakeXXX.cpp` を単一の出力フォルダに生成する。
4. 生成物は GoogleTest/gMock と組み合わせて即座に使用できる品質とする。
5. 並列テスト、ODR、名前衝突、未対応構文、戻り値、noexcept、const、ref qualifier、namespace、overload などを設計段階から考慮する。
6. ツール本体の CLI、ファイル I/O、文字列補助、RAII cleanup、契約チェックなどには ket を適用し、Clang/LLVM 固有領域と生成物ランタイムからは明確に分離する。

---

## 2. 基本コンセプト

### 2.1 生成されるテストダブルの考え方

本ツールが標準で生成するテストダブルは、継承ベースの mock ではなく、**リンク差し替え型の fake + mock 委譲**である。

製品コードのクラス `Hoge` に対して、以下を生成する。

```text
MockHoge.h   : gMock の MOCK_METHOD を持つ MockHoge クラス
FakeHoge.cpp : Hoge::Initialize などの実体を定義し、登録済み MockHoge へ委譲する fake 実装
```

テストターゲットでは、製品実装 `Hoge.cpp` をリンクせず、代わりに `FakeHoge.cpp` をリンクする。

```text
Production build:
    Hoge.h + Hoge.cpp

Test build:
    Hoge.h + FakeHoge.cpp + MockHoge.h + test.cpp
```

この方式により、製品ヘッダの ABI と呼び出し側コードを維持したまま、メンバ関数の実体だけをテスト用に差し替える。

### 2.2 標準生成コードのイメージ

生成される `MockHoge.h` の標準イメージは以下である。

```cpp
#pragma once

#include <gmock/gmock.h>

#include "Hoge.h"
#include "MockFakeRuntime.h"

class MockHoge
{
public:
    MockHoge() = default;
    ~MockHoge() = default;

    MOCK_METHOD(bool, Initialize, (int, char**), ());
    MOCK_METHOD(void, Finalize, (), ());
    MOCK_METHOD(bool, DoSomething, (), ());
};

using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;
```

生成される `FakeHoge.cpp` の標準イメージは以下である。

```cpp
#include "Hoge.h"
#include "MockHoge.h"

bool Hoge::Initialize(int argc, char** argv)
{
    if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
    {
        return mock->Initialize(argc, argv);
    }

    return ::mockfake::MissingMockReturn<bool>("Hoge::Initialize(int, char**)");
}

void Hoge::Finalize()
{
    if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
    {
        mock->Finalize();
        return;
    }

    return ::mockfake::MissingMockReturn<void>("Hoge::Finalize()");
}

bool Hoge::DoSomething()
{
    if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
    {
        return mock->DoSomething();
    }

    return ::mockfake::MissingMockReturn<bool>("Hoge::DoSomething()");
}
```

提示スニペットの `g_pMockHoge` 方式はシンプルだが、並列テスト、ネストした mock scope、ODR、複数クラス対応の観点で弱い。標準設計では、クラスごとのグローバルポインタを生成せず、共通ランタイム `MockFakeRuntime.h` が型ごとの mock スタックを管理する。

---

## 3. 重要設計判断

### 3.1 正規表現ではなく Clang LibTooling を採用する

C++ の関数宣言は、戻り値、テンプレート引数、default argument、macro、namespace、attribute、trailing return type、`noexcept`、ref qualifier、`const`、overload などで簡単に複雑化する。

そのため本ツールでは、文字列ベースの簡易解析ではなく、Clang LibTooling による AST 解析を標準とする。

利用する主な Clang 機能は以下である。

| 用途 | 採用技術 |
|---|---|
| standalone tool 化 | `clang::tooling::ClangTool` |
| コンパイルオプション取得 | `clang::tooling::CompilationDatabase` / `compile_commands.json` |
| AST ノード抽出 | LibASTMatchers + 必要に応じて `RecursiveASTVisitor` |
| 元ソース表記の復元 | `SourceManager`, `Lexer`, `PrintingPolicy` |
| 生成物整形 | LibFormat / `clang-format` |
| 単体テスト | `runToolOnCode`, fixture project, golden file test |

### 3.2 「最高 Fidelity」と「確実な診断」の両立

Clang AST を使っても、生成物を 100% 自動で正しく作るのが難しい C++ 構文は存在する。

代表例は以下である。

- 関数テンプレート
- クラステンプレートの out-of-line fake
- inline メンバ関数
- `constexpr` / `consteval` 関数
- macro で生成された複雑な宣言
- conditional `noexcept(expr)`
- 参照戻り値の未登録 mock fallback
- 非 default constructible フィールドを持つ out-of-line constructor fake
- private type を引数や戻り値に使う public 関数
- platform/compiler 固有 attribute

本ツールは、対応困難な構文を無理に生成して壊れた fake/mock を出すのではなく、以下のポリシーを取る。

1. AST から意味情報を取得できる範囲は最大限対応する。
2. 生成に必要な情報が不足する場合は、該当関数・クラスを `unsupported` として明示的に診断する。
3. `--strict` では未対応が 1 件でもあれば非ゼロ終了にする。
4. `--best-effort` では対応可能なものだけ生成し、未対応一覧を `generation_report.md` と `manifest.json` に出力する。
5. 生成後に compile smoke test を走らせ、生成物が本当にコンパイル可能か検証する。

---


## 4. ket 適用方針

### 4.1 結論

本ツールは、Clang LibTooling を中核にした C++ 解析・生成ツールである。したがって、AST 解析、型表記、source location、diagnostic、formatting、compile validation には Clang/LLVM API を直接使用する。

一方で、ツール本体の周辺処理には ket を採用する。ket は「C++で毎回調べる・毎回迷う・毎回バグる・毎回儀式が長い小さな処理」を drop-in 可能な小部品として切り出す思想のライブラリであり、本ツールの CLI、ファイル I/O、文字列補助、RAII cleanup、契約チェックなどと相性が良い。

ただし、生成された `MockXXX.h`、`FakeXXX.cpp`、`MockFakeRuntime.h`、`AllMocks.h`、`CMakeLists.fragment.cmake` は ket に依存させない。生成物は、製品ヘッダ、Google Mock、標準ライブラリ、生成された共通ランタイムだけでテストコードへコピペ投入できることを優先する。

```text
ツール本体:
    Clang / LLVM + ket + 標準ライブラリ

生成物:
    製品ヘッダ + Google Mock + 標準ライブラリ + MockFakeRuntime.h
    ket 依存なし
```

### 4.2 ket を使う領域

| 領域 | 採用 ket module | 適用内容 | 備考 |
|---|---|---|---|
| CLI parse | `ket::cli`, `ket::parse` | `--input-root`、`--output-dir`、`--jobs` などの取得と数値変換 | `ket::cli` の簡易 option semantics をツール仕様として固定する |

現行の `mockfakegen_ket` は `cli` と `parse` の include directory だけを公開する。
`ket::file`、`ket::ascii`、`ket::string`、`ket::scope`、`ket::contract`、
`ket::concurrency`、`ket::testing` などを追加する場合は、実装での利用箇所を確認し、
`CMakeLists.txt` と `docs/dependency_inventory.md` の選択 module を同時に更新する。

`ket::format`、`ket::container` など未導入または未実装の module は、必須依存にしない。
利用する場合は、対象 ket module が実装済みであり、drop-in できることを確認してから採用する。

### 4.3 ket を使わない領域

以下は Fidelity と責務分離のため ket を使わない。

| 領域 | 理由 |
|---|---|
| AST traversal | `CXXRecordDecl`、`CXXMethodDecl`、`ASTMatchers` は Clang の意味情報を直接扱うため |
| 型表記復元 | `QualType`、`PrintingPolicy`、`Lexer` の情報を使う必要があるため |
| source range 復元 | macro expansion、spelling location、file location を Clang API で扱うため |
| gMock `MOCK_METHOD` の構文生成判断 | top-level comma、qualifier、overload など C++ 型情報に依存するため |
| `MockFakeRuntime.h` | 生成物に ket 依存を持ち込まないため |
| 生成された `MockXXX.h` / `FakeXXX.cpp` | コピペ即使用性と導入障壁の低さを優先するため |
| compile validation | 実際の compiler / Clang invocation の結果を信頼するため |

### 4.4 ket 導入形態

ket は Git submodule として `third_party/ket` に丸ごと取り込む。CMake では ket
自体を subproject として build せず、`mockfakegen_ket` target で実際に使う module
の include directory と `.cpp` だけを明示的に選ぶ。

```text
third_party/ket/              # full Git submodule
  modules/
    ascii/
    cli/
    concurrency/
    contract/
    file/
    parse/
    scope/
    string/
    ...
```

採用ルール:

1. `git submodule update --init --recursive` で `third_party/ket` 全体を取得する。
2. CMake では `add_subdirectory(third_party/ket)` を呼ばない。
3. `mockfakegen_ket` target が必要な ket module の include directory と `.cpp` だけを選択する。
4. ket module 自体は原則として改変しない。
5. 必要な workaround は `src/support/KetCompat.h` または `src/support/KetSupport.*` に閉じ込める。
6. ket pin、選択 module、license 状態を変更した場合は `docs/dependency_inventory.md` を更新する。
7. 生成物 compile fixture には ket include directory を渡さず、生成物が ket なしでコンパイルできることを検証する。

### 4.5 ket dependency boundary

ket 依存は `src/support` と CLI / I/O 周辺に閉じる。

```text
src/main.cpp
src/Config.*                 -> ket::cli, ket::parse を使用可
src/support/CliSupport.*     -> ket::cli, ket::parse を使用可
src/output/*                 -> 現行は標準 library に限定する
src/support/*                -> ket module 追加時は inventory 更新を必須にする

src/clang/*                  -> 原則 ket 非依存
src/model/*                  -> 原則 ket 非依存。純粋な IR 型に保つ
src/generator/*              -> 原則 ket 非依存。CodeBuilder は標準ライブラリ中心
src/runtime_template/*        -> ket 非依存
```

AST 意味解析・型表記・生成物 template には ket を混ぜない。追加 module を採用する場合でも、
Clang AST、IR、生成 template へ ket 依存が広がった場合は設計違反として review で戻す。

### 4.6 ket を使った Config 実装方針

`main()` は標準 C++ の `int argc, char** argv` を受け、ただちに `ket::cli::ArgvView` に変換する。

```cpp
int main(int argc, char** argv)
{
    const ket::cli::ArgvView args(argc, argv);
    auto config_result = mockfake::ParseConfig(args);
    if (!config_result.ok())
    {
        mockfake::PrintDiagnostic(config_result.error());
        return 2;
    }

    return mockfake::Run(config_result.value());
}
```

`--jobs` のような数値 option は `ket::parse::UInt<std::size_t>` を使って完全消費 parse する。parse 失敗は `ket::contract` ではなく、ユーザー向け diagnostic として返す。

### 4.7 ket を使った OutputWriter 方針

生成物の書き込みは `CodeGenerator` が直接 filesystem に触らず、`OutputWriter` が担当する。

```text
CodeGenerator
  -> GeneratedFile { path, content, kind, source_class }
  -> OutputWriter
       - output-dir 作成
       - overwrite / dry-run 判定
       - temporary file への書き込み
       - rename による best-effort atomic replace
       - generation_report.md / manifest.json 出力
```

`OutputWriter` は現行実装では標準 library の filesystem / stream API を使う。
将来 `ket::file` を採用する場合でも、atomic replace が必要な箇所は標準
`std::filesystem` と POSIX `rename` 相当を使い、非 atomic write 仕様に依存しすぎない。

### 4.8 ket 適用に関する受け入れ条件

- ツール本体は必要な ket module を `third_party/ket` から include してビルドできる。
- `mockfakegen --help`、必須 option 不足、`--jobs` 不正値などの CLI エラーが deterministic な diagnostic を返す。
- `OutputWriter` の unit test は、dry-run、overwrite 禁止、既存ファイル衝突、write 失敗を検証する。
- 生成物 compile fixture は ket include directory なしで成功する。
- CI で生成物ディレクトリに対して `#include "ket_`、`#include <ket_`、`ket::` が含まれないことを grep 検証する。
- ket module を追加した PR では、なぜ標準ライブラリ直書きではなく ket を使うのかを PR description に記載する。

---

## 5. 想定利用フロー

### 5.1 ビルド側の前提

製品プロジェクトは `compile_commands.json` を出力できることが望ましい。

CMake プロジェクトなら以下を指定する。

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

### 5.2 ツール実行例

```bash
mockfakegen \
  --input-root ./include \
  --output-dir ./generated/test_doubles \
  --build-path ./build \
  --project-root . \
  --std c++23 \
  --registry-mode thread-local \
  --fallback-policy abort \
  --format-style file \
  --validate compile \
  --strict
```

### 5.3 生成されるファイル構成

出力先は単一フォルダである。

```text
generated/test_doubles/
  MockFakeRuntime.h
  MockHoge.h
  FakeHoge.cpp
  MockFoo.h
  FakeFoo.cpp
  AllMocks.h
  CMakeLists.fragment.cmake
  manifest.json
  generation_report.md
```

`AllMocks.h` は全 `MockXXX.h` を include する利便ヘッダである。

```cpp
#pragma once

#include "MockHoge.h"
#include "MockFoo.h"
```

`CMakeLists.fragment.cmake` はテストターゲットから読み込める補助ファイルである。

```cmake
set(MOCKFAKE_GENERATED_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/FakeHoge.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/FakeFoo.cpp"
)

set(MOCKFAKE_GENERATED_INCLUDE_DIR
    "${CMAKE_CURRENT_LIST_DIR}"
)
```

---

## 6. CLI 設計


CLI の実装は `ket::cli` と `ket::parse` を使う。ここで定義する option semantics は、そのまま `ket::cli` の挙動と合わせる。

- `--key value` と `--key=value` を受け付ける。
- bare `--` 以降は option として扱わない。
- repeatable と明記された option 以外の重複 option は deterministic な `DuplicateOption` diagnostic にする。
- 数値 option は完全消費 parse に失敗したらユーザー診断として扱う。
- `ket::contract` は CLI 入力不正には使わない。contract は実装バグ検出用である。
- 未実装の deferred option は unknown として扱わず、deterministic な `DeferredOption` diagnostic にする。

### 6.1 option spelling と共通エラー規則

- presence flag は `--dry-run` のように値を取らない。`--dry-run=true` は invalid。
- bool value option は `--emit-manifest true` または `--emit-manifest=true` の形で `true` / `false` を必ず取る。
- value option は値がない場合 `MissingOptionValue` にする。次の token が `--` で始まる場合は値ではなく option とみなす。
- singleton option の 2 回目以降は `DuplicateOption` にし、後勝ち・先勝ちの暗黙解決をしない。
- `--strict` と `--best-effort` は mutually exclusive とし、同時指定は `ConflictingOption` にする。
- unknown option は `UnknownOption` にする。ただしこの表にある deferred option は unknown ではなく `DeferredOption` にする。
- path option は `std::filesystem::absolute(path).lexically_normal()` 相当で正規化する。存在チェックは scanner / resolver / writer 側の責務とし、Config では行わない。

### 6.2 path option の関係

| オプション | 必須 | 関係 | Config 時の診断 |
|---|---:|---|---|
| `--project-root <path>` | yes | 相対 include 表記、report、manifest の基準 | 空値は invalid |
| `--input-root <path>` | yes | `project-root` と同一または配下。`.h` 探索の起点 | `project-root` 外は invalid |
| `--output-dir <path>` | yes | 生成物の出力先。`input-root` 配下でもよいが scanner は生成物を再入力しない | 空値は invalid |
| `--build-path <path>` | yes | `compile_commands.json` 探索の起点。out-of-tree build を許可するため `project-root` 外でもよい | 空値は invalid |

### 6.3 complete Config option matrix

| オプション | 型 / spelling | default | valid values / status | owner | エラー規則 |
|---|---|---:|---|---|---|
| `--help` | presence flag | off | implemented | CLI | 値付きは invalid。指定時は必須 path を要求しない |
| `--input-root <path>` | path | required | implemented | HeaderScanner | 空値 invalid。`project-root` 外は invalid |
| `--output-dir <path>` | path | required | implemented | OutputWriter | 空値 invalid |
| `--build-path <path>` | path | required | implemented | CompilationResolver | 空値 invalid。`compile_commands.json` 不在は resolver diagnostic |
| `--project-root <path>` | path | required | implemented | shared path policy | 空値 invalid |
| `--std <value>` | value | `c++23` | `c++23` only | Clang parse / validation | 他値は invalid |
| `--config <path>` | path | none | deferred | future config loader | 使用時は `DeferredOption` |
| `--header-extension <ext>` | value | `.h` | `.h` only | HeaderScanner | 他値は invalid |
| `--header-filter <regex>` | value | none | deferred | HeaderScanner | 使用時は `DeferredOption` |
| `--exclude <glob>` | repeatable value | none | deferred | HeaderScanner | 使用時は `DeferredOption` |
| `--class-filter <regex>` | value | none | deferred | ClassExtractor | 使用時は `DeferredOption` |
| `--access <policy>` | value | `public` | `public` implemented, `protected` / `private` deferred | ClassExtractor / PolicyEngine | unknown value invalid。deferred value は `DeferredOption` |
| `--include-struct <bool>` | bool value | `false` | `false` implemented, `true` deferred | ClassExtractor | `true` は `DeferredOption` |
| `--registry-mode <mode>` | value | `thread-local` | `thread-local` / `global-mutex` / `shared-owner` implemented | runtime template | unknown value invalid |
| `--fallback-policy <policy>` | value | `abort` | `abort` / `default-return` / `throw` implemented | PolicyEngine / runtime template | unknown value invalid。`compile-error` は accepted option から除外 |
| `--mock-namespace-mode <mode>` | value | `same-as-product` | `same-as-product` only | CodeGenerator | 他値は invalid |
| `--collision-policy <policy>` | value | `qualified-filename` | `qualified-filename` only | CodeGenerator | 他値は invalid |
| `--fake-special-members <bool>` | bool value | `false` | implemented | CodeGenerator / Validator | safe な constructor/destructor fake のみ生成し、unsafe case は unsupported |
| `--fake-static-data <bool>` | bool value | `false` | implemented | CodeGenerator / Validator | safe な static data member definition のみ生成し、unsafe case は unsupported |
| `--interface-mock <bool>` | bool value | `false` | implemented | CodeGenerator | pure interface では継承型 Mock header のみ生成し、unsafe case は unsupported |
| `--include-dir <path>` | repeatable value | none | implemented | CompilationResolver / Validator | synthetic fallback parse と generated compile/link validation に追加 |
| `--define <macro>` | repeatable value | none | implemented | CompilationResolver / Validator | `-D...` として synthetic fallback parse と generated compile/link validation に追加 |
| `--extra-arg <arg>` | repeatable value | none | implemented | CompilationResolver / Validator | synthetic fallback parse と generated compile/link validation に追加。`--target=...` など option-looking value も separate form で受理 |
| `--dry-run` | presence flag | off | implemented | OutputWriter / Runner | 値付きは invalid |
| `--overwrite` | presence flag | off | implemented | OutputWriter | 値付きは invalid |
| `--strict` | presence flag | off | implemented | PolicyEngine / Runner | `--best-effort` との同時指定は conflict |
| `--best-effort` | presence flag | on | implemented | PolicyEngine / Runner | `--strict` との同時指定は conflict |
| `--emit-all-mocks <bool>` | bool value | `true` | implemented | CodeGenerator | `true` / `false` 以外 invalid |
| `--emit-manifest <bool>` | bool value | `true` | implemented | CodeGenerator / OutputWriter | `true` / `false` 以外 invalid |
| `--emit-cmake-fragment <bool>` | bool value | `true` | implemented | CodeGenerator | `true` / `false` 以外 invalid |
| `--format-style <style>` | value | `file` | `file`, `llvm`, `google`, `none` | Formatter | 他値は invalid |
| `--validate <mode>` | value | `compile` | `none`, `syntax`, `compile`, `link` | Validator / Runner | 他値は invalid |
| `--jobs <N>` | integer value | CPU 数 | positive integer | scheduler | 完全消費 parse 失敗、0、負数は invalid |

### 6.4 mode option interaction

- `--strict` は unsupported diagnostic、policy failure、compile validation failure を runner の非ゼロ終了条件に含める。
- `--best-effort` は生成可能な範囲を出力し、unsupported は report に残す。`--strict` と同時指定しない。
- `--validate none` は compile validation を実行しない。`--strict` と組み合わせても unsupported / config error は失敗条件のままである。
- `--format-style none` は formatter を実行しない。manifest / report / validation の有無とは独立である。
- `--emit-all-mocks false`、`--emit-manifest false`、`--emit-cmake-fragment false` は該当 artifact の生成だけを止める。生成しない artifact に対して validator が暗黙に依存してはいけない。

### 6.5 registry mode

| mode | 説明 | 推奨用途 |
|---|---|---|
| `thread-local` | 型ごとの `thread_local` stack。テスト同士の並列実行に強い | 標準。SUT が同一スレッドで mock を呼ぶ場合 |
| `global-mutex` | 型ごとの global stack を mutex で保護。ただし lookup 後の raw pointer lifetime は保護しない | SUT が worker thread から同じ mock を呼び、worker を scope 破棄前に join できる場合 |
| `shared-owner` | `std::shared_ptr<Mock>` を登録し、fake 呼び出し中の lifetime を保証 | 非同期・worker thread が多い高安全モード |

標準は `thread-local` とする。単純なグローバルポインタと違い、並列テストで別スレッドの mock が混線しない。また stack 方式により、同一テスト内で一時的に mock を差し替えるネスト利用も可能になる。

### 6.6 fallback policy

mock 未登録時の挙動を選べる。

| policy | 挙動 | 特徴 |
|---|---|---|
| `abort` | 診断を出して `std::abort()` | 標準。テストの登録漏れを即発見できる |
| `default-return` | `void` は何もしない、default constructible な戻り値は `{}` | 提示スニペット互換 |
| `throw` | `std::runtime_error` を投げる | `noexcept` 関数では使用不可。デバッグ向け |

品質優先の標準は `abort` である。提示スニペットと同じ `return {};` 挙動が必要な場合は `--fallback-policy default-return` を指定する。
`compile-error` は意図的に壊れた C++ を出す policy になるため、生成物品質の方針に合わせて accepted option から除外する。

---

## 7. 全体アーキテクチャ

```text
+-----------------------+
| CLI / Config Loader   |
+-----------+-----------+
            |
            v
+-----------------------+
| Header Scanner        |  input-root 配下の .h を再帰収集
+-----------+-----------+
            |
            v
+-----------------------+
| Compilation Resolver  |  compile_commands.json から parse context を決定
+-----------+-----------+
            |
            v
+-----------------------+
| Clang Parse Manager   |  ClangTool / FrontendAction 実行
+-----------+-----------+
            |
            v
+-----------------------+
| AST Extractor         |  CXXRecordDecl / CXXMethodDecl 抽出
+-----------+-----------+
            |
            v
+-----------------------+
| IR Normalizer         |  型表記、namespace、qualifier、引数名、衝突を正規化
+-----------+-----------+
            |
            v
+-----------------------+
| Policy Engine         |  対象/非対象/unsupported 判定
+-----------+-----------+
            |
            v
+-----------------------+
| Code Generator        |  Runtime / Mock / Fake / AllMocks / CMake fragment
+-----------+-----------+
            |
            v
+-----------------------+
| Formatter             |  clang-format / LibFormat
+-----------+-----------+
            |
            v
+-----------------------+
| Validator             |  compile smoke test / diagnostics
+-----------+-----------+
            |
            v
+-----------------------+
| Report Writer         |  manifest.json / generation_report.md
+-----------------------+
```


### 7.1 ket-backed Support Layer

実装上は、Clang/LLVM に直接関係しない横断的処理を `src/support` に集約する。

```text
+-----------------------------+
| ket-backed Support Layer    |
|-----------------------------|
| CliSupport      ket::cli    |
| ParseSupport    ket::parse  |
| FileSupport     std library |
| TextSupport     std library |
+-----------------------------+
```

この層は便利関数の集約であり、Clang AST model や生成物 template へ侵入させない。これにより、ket の追加・削除が parser / generator の本質部分へ波及しない。

---

## 8. ヘッダ探索設計

### 8.1 探索ルール

`--input-root` 配下を `std::filesystem::recursive_directory_iterator` で探索し、通常ファイルかつ拡張子 `.h` のものを対象候補とする。path traversal 自体は標準 `std::filesystem` を直接使い、拡張子・除外 pattern・診断用 path 表記も現行は標準 library に閉じる。

除外するものは以下である。

- `--exclude <glob>` に一致するファイル
- symlink loop を起こすパス
- 出力フォルダ配下
- `third_party`, `external`, `build` など、設定で除外されたパス
- generated marker が付いた過去の生成物

### 8.2 パス管理

内部では全パスを canonical absolute path で保持する。

生成物の include には、`--project-root` からの相対パスを優先する。

例:

```text
project-root: /repo
header:       /repo/include/foo/Hoge.h
include:      #include "include/foo/Hoge.h"
```

必要に応じて `--include-prefix-strip include` を指定し、

```cpp
#include "foo/Hoge.h"
```

のように出力できるようにする。

---

## 9. Compilation Context 解決

### 9.1 なぜ compile_commands.json が必要か

ヘッダ単体では、以下の情報が不足する。

- include directory
- macro 定義
- target architecture
- language standard
- warning/error 設定
- feature macro
- conditional compilation の有効分岐

最高 Fidelity を目指すため、`compile_commands.json` を第一級入力として扱う。

### 9.2 解析モード

本ツールは 2 段階の解析モードを持つ。

#### 9.2.1 TU scan mode

`compile_commands.json` に記録された実際の translation unit を解析し、その中で `--input-root` 配下ヘッダに由来するクラス宣言を収集する。

利点:

- 実ビルドと同じ macro 条件で解析できる。
- include 順、target 固有 define、ビルドフラグの Fidelity が高い。
- macro によって有効化/無効化される API を実態に近く取得できる。

欠点:

- どの translation unit からも include されないヘッダは拾えない。
- 大規模プロジェクトでは解析コストが高い。
- 同じヘッダが複数構成で include される場合、API が config ごとに異なる可能性がある。

#### 9.2.2 Synthetic header TU mode

未解析のヘッダについて、以下のような一時 `.cpp` を仮想生成し、Clang に渡す。

```cpp
#include "target/header/Hoge.h"
```

コンパイルオプションは、以下の優先順で決定する。

1. そのヘッダを include している translation unit の compile command
2. 同じディレクトリ階層に最も近い translation unit の compile command
3. compile command または fallback command に追加される `--extra-arg`, `--include-dir`, `--define`
4. 最小構成の `clang++ -std=c++23 -I<project-root>`

利点:

- 未使用ヘッダも対象にできる。
- 要件どおり、指定フォルダ配下 `.h` を網羅できる。

欠点:

- 実際の include 文脈と完全一致しない可能性がある。
- header が特定の include 順や macro 前提に依存していると parse error になる可能性がある。

### 9.3 標準フロー

標準は以下とする。

1. `.h` ファイルを再帰収集する。
2. `compile_commands.json` の TU を parse し、対象ヘッダ由来の宣言を収集する。
3. 収集できなかったヘッダを synthetic TU で parse する。
4. 同じクラスが複数 config で異なる宣言として観測された場合、衝突として report する。
5. 衝突がなければ 1 つのクラスモデルに統合する。

---

## 10. AST 抽出設計

### 10.1 抽出対象

主対象は `CXXRecordDecl` の class definition である。

抽出対象条件:

- `class` または設定で許可された `struct`
- definition を持つ
- source location が対象 `.h` に属する
- anonymous class ではない
- dependent template specialization ではない
- 外部ライブラリ・system header に由来しない

標準では `class` の public member function を対象とする。`struct` も対象にしたい場合は `--include-struct` を指定する。

### 10.2 メンバ関数の抽出

各 `CXXRecordDecl` の `decls()` を走査し、宣言順を維持して `CXXMethodDecl` を抽出する。

標準対象:

- public non-static member function
- public static member function
- overload された通常メンバ関数
- `const` member function
- `noexcept` member function
- lvalue/rvalue ref-qualified member function
- default argument 付き関数。ただし fake/mock 生成時は default argument を除去する

標準除外:

- constructor
- destructor
- conversion function
- overloaded operator
- function template
- deleted function
- defaulted function
- inline body を持つ関数
- `constexpr` / `consteval` 関数
- pure virtual function の fake 生成

ただし constructor/destructor はテストリンク時に必要になることがあるため、Phase 2 以降で `--fake-special-members` により no-op fake を生成可能にする。

### 10.3 mock 生成対象と fake 生成対象の分離

`MockXXX.h` と `FakeXXX.cpp` で対象関数を完全に同じにすると問題が出ることがある。

例:

- constructor/destructor は fake 定義が必要になる場合があるが、gMock method にはできない。
- static data member は fake 側で定義が必要になる場合があるが、Mock method ではない。
- pure virtual interface は継承 mock だけ生成すればよく、Fake.cpp は不要な場合がある。

したがって内部 IR では以下を分離する。

```text
ClassModel
  mock_methods       : MockXXX.h に MOCK_METHOD として出す関数
  fake_methods       : FakeXXX.cpp に委譲実装として出す関数
  fake_constructors  : FakeXXX.cpp に no-op/field-init 実装として出す constructor
  fake_destructors   : FakeXXX.cpp に no-op 実装として出す destructor
  static_data_defs   : FakeXXX.cpp に定義が必要な static data member
  unsupported_items  : 生成しない理由付き item
```

---

## 11. 中間表現 IR

### 11.1 ProjectModel

```cpp
struct ProjectModel
{
    std::vector<HeaderModel> headers;
    std::vector<ClassModel> classes;
    std::vector<Diagnostic> diagnostics;
};
```

### 11.2 HeaderModel

```cpp
struct HeaderModel
{
    std::filesystem::path absolute_path;
    std::filesystem::path project_relative_path;
    std::string include_spelling;
    bool parsed_by_real_tu;
    bool parsed_by_synthetic_tu;
};
```

### 11.3 ClassModel

```cpp
struct ClassModel
{
    std::string name;                    // Hoge
    std::string qualified_name;          // app::core::Hoge
    std::vector<std::string> namespaces; // app, core
    std::string mock_name;               // MockHoge
    std::string mock_header_name;        // MockHoge.h or Mock_app_core_Hoge.h
    std::string fake_source_name;        // FakeHoge.cpp or Fake_app_core_Hoge.cpp
    HeaderModel source_header;
    std::vector<MethodModel> mock_methods;
    std::vector<MethodModel> fake_methods;
    std::vector<SpecialMemberModel> fake_special_members;
    std::vector<UnsupportedItem> unsupported_items;
};
```

### 11.4 MethodModel

```cpp
struct MethodModel
{
    std::string name;
    std::string qualified_owner_name;
    std::string return_type_spelling;
    std::string gmock_return_type_spelling;
    std::vector<ParameterModel> parameters;
    std::string signature_for_report;

    bool is_static;
    bool is_const;
    bool is_volatile;
    bool is_noexcept;
    bool has_conditional_noexcept;
    bool is_lvalue_ref_qualified;
    bool is_rvalue_ref_qualified;
    bool is_virtual;
    bool is_pure_virtual;
    bool is_inline;
    bool is_deleted;
    bool is_defaulted;

    AccessKind access;
    SourceRange source_range;
};
```

### 11.5 ParameterModel

```cpp
struct ParameterModel
{
    std::string type_spelling;        // fake signature 用
    std::string gmock_type_spelling;  // MOCK_METHOD 用。必要なら括弧で wrap
    std::string original_name;        // 空の可能性あり
    std::string generated_name;       // arg0, arg1 など
    bool has_default_argument;
    bool is_rvalue_ref;
    bool is_nonconst_by_value;
};
```

---

## 12. 型表記と gMock 対応

### 12.1 基本方針

`MOCK_METHOD` の引数は、可読性と macro 安定性のため、標準では**型のみ**を出力する。

```cpp
MOCK_METHOD(bool, Initialize, (int, char**), ());
```

fake 側では呼び出しのために引数名を生成する。

```cpp
bool Hoge::Initialize(int argc, char** argv)
{
    return mock->Initialize(argc, argv);
}
```

元ヘッダに引数名がない場合は `arg0`, `arg1`, ... を生成する。

### 12.2 カンマを含む型

gMock の `MOCK_METHOD` では、戻り値型や引数型が top-level comma を含む場合、追加の括弧が必要になる。

例:

```cpp
std::pair<bool, int> GetPair();
void SetMap(std::map<int, double> value);
```

生成例:

```cpp
MOCK_METHOD((std::pair<bool, int>), GetPair, (), ());
MOCK_METHOD(void, SetMap, ((std::map<int, double>)), ());
```

括弧付け判定は文字列正規表現ではなく、Clang の type spelling と token depth を使い、`< >`, `( )`, `[ ]` の nest を考慮して top-level comma を判定する。

### 12.3 default argument

default argument は宣言側にのみ置くべきであり、out-of-line fake definition や gMock method には出力しない。

入力:

```cpp
bool Open(const std::string& path, int flags = 0);
```

生成:

```cpp
MOCK_METHOD(bool, Open, (const std::string&, int), ());

bool Hoge::Open(const std::string& path, int flags)
{
    ...
}
```

### 12.4 array parameter

C++ の関数引数における array parameter は pointer に調整される。

入力:

```cpp
bool Initialize(int argc, char* argv[]);
```

生成標準:

```cpp
MOCK_METHOD(bool, Initialize, (int, char**), ());

bool Hoge::Initialize(int argc, char** argv)
{
    ...
}
```

元表記を可能な限り保持したい場合は `--preserve-parameter-spelling` を指定し、`char* argv[]` のような表記を復元する。ただし gMock 側は pointer spelling に正規化する方が安定する。

---

## 13. qualifier 対応

### 13.1 const / volatile

入力:

```cpp
int GetValue() const;
```

生成:

```cpp
MOCK_METHOD(int, GetValue, (), (const));

int Hoge::GetValue() const
{
    if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
    {
        return mock->GetValue();
    }
    return ::mockfake::MissingMockReturn<int>("Hoge::GetValue() const");
}
```

`volatile` member function は gMock 側のサポートが限定的なため、標準では unsupported とする。必要になった時点で個別対応する。

### 13.2 noexcept

入力:

```cpp
bool Save() noexcept;
```

生成:

```cpp
MOCK_METHOD(bool, Save, (), (noexcept));

bool Hoge::Save() noexcept
{
    if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
    {
        return mock->Save();
    }
    return ::mockfake::MissingMockReturn<bool>("Hoge::Save() noexcept");
}
```

`noexcept(expr)` は、gMock macro 側で完全に同一表現を出せない可能性があるため、初期版では unsupported として report する。fake definition だけなら維持可能だが、Mock method との整合が崩れるため strict mode では失敗させる。

### 13.3 ref qualifier

入力:

```cpp
std::string Take() &&;
int Peek() const&;
```

生成:

```cpp
MOCK_METHOD(std::string, Take, (), (ref(&&)));
MOCK_METHOD(int, Peek, (), (const, ref(&)));
```

fake 側では呼び出し対象の value category を合わせる。

```cpp
std::string Hoge::Take() &&
{
    if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
    {
        return std::move(*mock).Take();
    }
    return ::mockfake::MissingMockReturn<std::string>("Hoge::Take() &&");
}
```

---

## 14. namespace と名前衝突

### 14.1 標準 namespace 配置

標準では、mock class は製品クラスと同じ namespace に出力する。

入力:

```cpp
namespace app::core
{
class Hoge
{
public:
    bool DoSomething();
};
}
```

生成:

```cpp
namespace app::core
{

class MockHoge
{
public:
    MOCK_METHOD(bool, DoSomething, (), ());
};

using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;

} // namespace app::core
```

この方針により、元ヘッダ内の unqualified type spelling を維持しやすくなる。

### 14.2 代替 namespace 配置

既存 namespace を汚したくない場合は、以下を指定できる。

```bash
--mock-namespace-mode mockfake-generated
```

この場合、mock は以下に生成する。

```cpp
namespace mockfake::generated::app::core
{
class MockHoge { ... };
}
```

このモードでは、引数型・戻り値型を可能な限り fully qualified spelling に正規化する。

### 14.3 ファイル名衝突

単一出力フォルダのため、以下は衝突しうる。

```cpp
namespace a { class Hoge; }
namespace b { class Hoge; }
```

標準の `--collision-policy qualified-filename` では、衝突時のみ qualified name を使う。

```text
MockHoge.h                  # 衝突がない場合
FakeHoge.cpp

Mock_a_Hoge.h               # 衝突がある場合
Fake_a_Hoge.cpp
Mock_b_Hoge.h
Fake_b_Hoge.cpp
```

衝突情報は `manifest.json` に記録する。

---

## 15. MockFakeRuntime.h 設計

### 15.1 目的

`MockFakeRuntime.h` は、すべての生成物が共有する軽量ランタイムである。

責務:

- mock 登録・解除
- RAII scope 管理
- ネストした mock scope の stack 管理
- 並列テスト対応
- mock 未登録時の fallback
- 診断メッセージ出力
- thread-local / global-mutex / shared-owner policy の切り替え

### 15.2 thread-local 標準実装イメージ

```cpp
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace mockfake
{

namespace detail
{
[[noreturn]] inline void AbortWithMessage(std::string_view message)
{
    std::fprintf(stderr, "%.*s\n", static_cast<int>(message.size()), message.data());
    std::abort();
}
} // namespace detail

template <typename Mock>
class MockRegistry
{
public:
    static void Push(Mock& mock)
    {
        Stack().push_back(std::addressof(mock));
    }

    static void Pop(Mock& mock)
    {
        auto& stack = Stack();
        if (stack.empty() || stack.back() != std::addressof(mock))
        {
            detail::AbortWithMessage("[mockfake] ScopedMock destruction order mismatch");
        }
        stack.pop_back();
    }

    static Mock* Current() noexcept
    {
        auto& stack = Stack();
        return stack.empty() ? nullptr : stack.back();
    }

private:
    static std::vector<Mock*>& Stack()
    {
        static thread_local std::vector<Mock*> stack;
        return stack;
    }
};

template <typename Mock>
Mock* CurrentMock() noexcept
{
    return MockRegistry<Mock>::Current();
}

template <typename Mock>
class ScopedMock
{
public:
    explicit ScopedMock(Mock& mock) noexcept
        : m_mock(std::addressof(mock))
    {
        MockRegistry<Mock>::Push(*m_mock);
    }

    explicit ScopedMock(Mock* mock)
        : m_mock(mock)
    {
        if (m_mock == nullptr)
        {
            detail::AbortWithMessage("[mockfake] ScopedMock received nullptr");
        }
        MockRegistry<Mock>::Push(*m_mock);
    }

    ScopedMock(const ScopedMock&) = delete;
    ScopedMock& operator=(const ScopedMock&) = delete;

    ScopedMock(ScopedMock&& other) noexcept
        : m_mock(std::exchange(other.m_mock, nullptr))
    {
    }

    ScopedMock& operator=(ScopedMock&&) = delete;

    ~ScopedMock()
    {
        if (m_mock != nullptr)
        {
            MockRegistry<Mock>::Pop(*m_mock);
        }
    }

private:
    Mock* m_mock{};
};

template <typename R>
R MissingMockReturn(std::string_view signature)
{
#if !defined(MOCKFAKE_MISSING_MOCK_DEFAULT_RETURN)
    detail::AbortWithMessage("[mockfake] no mock registered");
#else
    if constexpr (std::is_void_v<R>)
    {
        return;
    }
    else if constexpr (!std::is_reference_v<R> && std::is_default_constructible_v<R>)
    {
        return R{};
    }
    else
    {
        detail::AbortWithMessage("[mockfake] no mock registered and return type has no default fallback");
    }
#endif
}

} // namespace mockfake
```

実装時には、`signature` をメッセージへ含める。上記は設計説明用の簡略版である。

### 15.3 global-mutex mode

`thread-local` は並列テストの混線防止に強い一方、SUT が別 worker thread から fake を呼ぶ場合、その worker thread には mock 登録が見えない。

その場合は `global-mutex` を使う。

特徴:

- 型ごとの global stack を `std::mutex` で保護する。
- `CurrentMock()` は lock 中に raw pointer を取得して返すだけで、lookup 後の mock lifetime は保護しない。
- 複数テストが同一プロセス・同一型 mock を同時利用すると同じ process-wide stack を shadow しうるため、同一テストバイナリ内での完全並列には向かない。
- mock の lifetime はテスト側が保証する。worker thread は `ScopedMock` の破棄前に join する必要がある。
- この mode は unsafe concurrent same-type scope pattern を runtime で禁止しない。必要ならテスト runner 側で同一型・同一プロセスの並列実行を避ける。

### 15.4 shared-owner mode

非同期コードや worker thread を安全に扱うための高安全モードとして `shared-owner` を用意する。

特徴:

- `std::shared_ptr<Mock>` を登録する。
- fake 呼び出し時に `shared_ptr` を取得してから mock method を呼ぶため、呼び出し中の lifetime が保証される。
- stack mock より記述は少し重いが、非同期テストで安全性が高い。
- generated mock alias は `ScopedSharedMock<Mock>` を指し、generated fake は
  `CurrentMock<Mock>()` の `shared_ptr` copy を保持してから mock method を呼ぶ。
- LIFO mismatch は mock raw pointer ではなく、`ScopedSharedMock` ごとの内部 token で判定する。これにより同一 raw pointer を指す別 ownership でも destruction order mismatch を検出できる。

テスト例:

```cpp
auto mock = std::make_shared<MockHoge>();
::mockfake::ScopedSharedMock scoped(mock);

EXPECT_CALL(*mock, DoSomething()).WillOnce(::testing::Return(true));
```

---

## 16. 生成 Mock 設計

### 16.1 基本ルール

`MockXXX.h` は以下を含む。

1. `#pragma once`
2. `<gmock/gmock.h>`
3. 元ヘッダ
4. `MockFakeRuntime.h`
5. 元 namespace と同じ namespace ブロック
6. `class MockXXX`
7. `MOCK_METHOD` 群
8. `using ScopedMockXXX = ::mockfake::ScopedMock<MockXXX>;`

### 16.2 生成例

入力:

```cpp
namespace app
{
class Hoge
{
public:
    bool Initialize(int argc, char* argv[]);
    void Finalize();
    bool DoSomething() const noexcept;
};
}
```

生成:

```cpp
#pragma once

#include <gmock/gmock.h>

#include "app/Hoge.h"
#include "MockFakeRuntime.h"

namespace app
{

class MockHoge
{
public:
    MockHoge() = default;
    ~MockHoge() = default;

    MOCK_METHOD(bool, Initialize, (int, char**), ());
    MOCK_METHOD(void, Finalize, (), ());
    MOCK_METHOD(bool, DoSomething, (), (const, noexcept));
};

using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;

} // namespace app
```

### 16.3 StrictMock / NiceMock 利便 alias

必要に応じて以下の alias を生成する。

```cpp
using NiceMockHoge = ::testing::NiceMock<MockHoge>;
using StrictMockHoge = ::testing::StrictMock<MockHoge>;
```

標準では off とし、`--emit-gmock-aliases` で有効化する。

理由:

- `StrictMock` は未期待呼び出しを fail にできるため品質は高い。
- ただし全テストに `StrictMock` を強制すると既存テスト移行時の摩擦が大きい。

---

## 17. 生成 Fake 設計

### 17.1 基本ルール

`FakeXXX.cpp` は以下を含む。

1. 元ヘッダ
2. 対応 `MockXXX.h`
3. 必要に応じて `<utility>`
4. 元 namespace と同じ namespace ブロック
5. `Class::Method` の out-of-line definition
6. 登録済み mock があれば委譲
7. mock 未登録なら fallback policy に従う

### 17.2 by-value / rvalue reference 引数の forward

fake は受け取った引数を mock へ渡す。

- lvalue reference / pointer / const reference はそのまま渡す。
- rvalue reference は `std::move(arg)` で渡す。
- non-const by-value は `std::move(arg)` で渡す。これにより `std::unique_ptr<T>` など move-only 型にも対応しやすい。

例:

```cpp
void Hoge::SetName(std::string name)
{
    if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
    {
        mock->SetName(std::move(name));
        return;
    }

    return ::mockfake::MissingMockReturn<void>("Hoge::SetName(std::string)");
}
```

### 17.3 static member function

入力:

```cpp
class Hoge
{
public:
    static int GetCount();
};
```

生成 mock:

```cpp
MOCK_METHOD(int, GetCount, (), ());
```

生成 fake:

```cpp
int Hoge::GetCount()
{
    if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
    {
        return mock->GetCount();
    }

    return ::mockfake::MissingMockReturn<int>("Hoge::GetCount()");
}
```

`MOCK_METHOD` は static method を直接 mock するわけではない。fake の static member function が通常の mock object method へ委譲する、という設計にする。

### 17.4 void 戻り値

```cpp
void Hoge::Finalize()
{
    if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
    {
        mock->Finalize();
        return;
    }

    return ::mockfake::MissingMockReturn<void>("Hoge::Finalize()");
}
```

`void` 関数でも fallback を通すことで、mock 未登録時の診断ポリシーを一元化できる。

### 17.5 参照戻り値

参照戻り値は fallback が難しい。

```cpp
const Config& Hoge::GetConfig() const;
```

mock 登録済みなら問題ない。

```cpp
const Config& Hoge::GetConfig() const
{
    if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
    {
        return mock->GetConfig();
    }

    return ::mockfake::MissingMockReturn<const Config&>("Hoge::GetConfig() const");
}
```

`--fallback-policy abort` では mock 未登録時に `std::abort()` するため安全にコンパイルできる。

`--fallback-policy default-return` では参照戻り値に `{}` を返せないため、対象関数を unsupported とするか、該当関数だけ abort fallback に自動昇格する。

標準設計では、参照戻り値は `default-return` 非対応として report する。

---

## 18. constructor / destructor / static data member

### 18.1 なぜ重要か

テストターゲットで製品 `.cpp` を除外し `FakeXXX.cpp` をリンクする場合、通常メンバ関数だけでなく constructor/destructor/static data member の実体が必要になることがある。

例:

```cpp
class Hoge
{
public:
    explicit Hoge(int id);
    ~Hoge();
    bool DoSomething();
};
```

このとき `Hoge::Hoge(int)` と `Hoge::~Hoge()` が製品 `.cpp` にしかないなら、`FakeHoge.cpp` 側にも定義が必要になる。

### 18.2 標準方針

初期版では以下とする。

| 対象 | 標準動作 |
|---|---|
| defaulted constructor/destructor がヘッダ内にある | 何も生成しない |
| out-of-line constructor 宣言 | `unsupported` として report。ただし `--fake-special-members` で生成を試みる |
| out-of-line destructor 宣言 | `--fake-special-members` で no-op 定義を生成可能 |
| copy/move assignment 宣言 | `--fake-special-members` で `return *this;` 生成可能 |
| static data member | `--fake-static-data` で default definition 生成可能 |

### 18.3 constructor fake の難しさ

constructor fake は単に空 body を生成すればよいとは限らない。

```cpp
class Hoge
{
public:
    Hoge();
private:
    const int m_value;
    Dependency& m_dep;
};
```

この場合、fake constructor も `m_value` と `m_dep` を初期化しなければコンパイルできない。`Dependency&` のような reference member は安全な default 初期化ができない。

そのため `--fake-special-members` は以下の方針とする。

1. default member initializer がある field はそれを使う。
2. default constructible と判定できる field は `{}` で初期化を試みる。
3. reference member、non-default-constructible member、private inaccessible type がある場合は constructor fake を unsupported とする。
4. 生成後 compile validation で必ず検証する。

---

## 19. interface mock 生成モード

リンク差し替え型 fake とは別に、抽象 interface には継承型 mock の方が自然な場合がある。

入力:

```cpp
class IStorage
{
public:
    virtual ~IStorage() = default;
    virtual bool Save(const std::string& key, std::string value) = 0;
};
```

生成:

```cpp
class MockIStorage : public IStorage
{
public:
    MOCK_METHOD(bool, Save, (const std::string&, std::string), (override));
};
```

標準では、pure virtual のみからなる interface に対して `--interface-mock` を指定した場合に生成する。

| モード | 出力 |
|---|---|
| link-seam fake | `MockXXX.h` + `FakeXXX.cpp` |
| interface mock | `MockXXX.h` のみ。`FakeXXX.cpp` は生成しない |

既存コードが dependency injection を使っている場合は interface mock が望ましい。既存コードを改造せずリンク差し替えしたい場合は link-seam fake が望ましい。

---

## 20. 未対応構文と診断ポリシー

### 20.1 未対応一覧

初期版で unsupported とするもの:

| 構文 | 理由 |
|---|---|
| function template | gMock の `MOCK_METHOD` と fake forwarding の組み合わせが複雑 |
| class template | fake definition を `.cpp` に置けない場合が多い |
| inline body 付き member function | fake `.cpp` で置換できない |
| `constexpr` / `consteval` | runtime mock へ委譲できない |
| conversion operator | gMock method 名生成が特殊 |
| overloaded operator | 初期版では安全な method 名 mapping を持たない |
| conditional `noexcept(expr)` | gMock spec と fake signature の完全一致が難しい |
| volatile member function | 初期版では利用頻度が低くサポートコストが高い |
| macro expansion 由来で source range が不安定な宣言 | 型表記・署名復元の Fidelity が落ちる |

### 20.2 診断出力例

`generation_report.md`:

```markdown
### 20.1 Unsupported items

| Header | Class | Member | Reason | Suggested action |
|---|---|---|---|---|
| include/Hoge.h | Hoge | template <class T> T Get(); | function template is not supported | 手書き mock を追加するか、対象外フィルタに入れてください |
| include/Foo.h | Foo | int Bar() const noexcept(sizeof(int) == 4); | conditional noexcept is not supported | noexcept に単純化できるか検討してください |
```

`manifest.json`:

```json
{
  "classes": [
    {
      "qualified_name": "Hoge",
      "mock_header": "MockHoge.h",
      "fake_source": "FakeHoge.cpp",
      "source_header": "include/Hoge.h",
      "generated_methods": 3,
      "unsupported_methods": 0
    }
  ]
}
```

---

## 21. 出力品質保証

### 21.1 clang-format / LibFormat

生成直後に LibFormat または `clang-format` を適用する。

標準は `--format-style file` とし、プロジェクトの `.clang-format` を探索して使用する。

`.clang-format` がない場合は `fallback-style=LLVM` とする。

### 21.2 deterministic output

CI 差分を安定させるため、以下を守る。

- 生成順は header path / qualified class name で sort する。
- class 内の `mock_methods`、`fake_methods`、constructor、destructor、static data は header の宣言順を canonical order として保持し、signature/name では sort しない。
- 同一 class を複数 TU で観測した場合は、最初に採用した canonical declaration order と fingerprint を保持し、異なる宣言順または宣言内容は compile configuration conflict として診断する。
- unsupported item の report/manifest 表示は source location を優先して安定化し、source location がない場合だけ kind/name/reason で tie-break する。
- timestamp をデフォルトでは出力しない。
- 既存ファイルと内容が同一なら rewrite しない。
- 改行コードは LF 固定。
- 末尾改行を必ず入れる。
- include 順を安定化する。

### 21.3 atomic write

ファイル書き込みは以下の手順で行う。

1. temporary file に書く。
2. flush / close する。
3. format / validation 対象にする。
4. 成功後に rename で置換する。

これにより、途中失敗で壊れた生成ファイルが残ることを防ぐ。

### 21.4 compile validation

`--validate compile` では、生成物がコンパイル可能か検証する。

検証対象:

1. すべての `MockXXX.h` を include する `mock_headers_smoke.cpp`
2. 生成された各 `FakeXXX.cpp`
3. 必要に応じて `AllMocks.h`

検証コマンドは compilation database から include path / define を引き継ぎ、以下を追加する。

```text
-std=c++23
-I<output-dir>
-I<project-root or configured include roots>
```

gMock include path は設定または CMake integration で与える。

`--validate link` は上記 compile validation に加えて、生成 fake object と gMock を使った
smoke executable をリンクする。これは生成物側のリンク可能性を検証するためのものであり、
ユーザーの最終テストターゲットの source list を自動検査するものではない。製品 `.cpp` と
`FakeXXX.cpp` が同じ link input に含まれた場合は duplicate symbol を diagnostic として
報告する。

### 21.5 PolicyEngine failure / publication matrix

PolicyEngine は failure kind ごとに exit code、generated file publish、manifest/report
emission を同じ表で判断する。`publish generated files` は `MockXXX.h` / `FakeXXX.cpp`
/ `AllMocks.h` / CMake fragment を usable output として採用するかを表す。diagnostic
artifact としての manifest/report は別に判断する。

| failure kind | best-effort exit | strict exit | publish generated files | emit manifest | emit report |
|---|---:|---:|---|---|---|
| parse failure | 1 | 1 | no | yes | yes |
| unsupported item | 0 | 1 | yes | yes | yes |
| write failure | 1 | 1 | no | no | yes |
| format failure | 1 | 1 | no | yes | yes |
| ket contamination | 1 | 1 | no | yes | yes |
| compile validation failure | 1 | 1 | no | yes | yes |
| link validation failure | 1 | 1 | no | yes | yes |
| fallback incompatibility | 1 | 1 | no | yes | yes |

### 21.6 fallback compatibility matrix

Missing mock fallback は method signature ごとに compatibility を判定する。

| fallback policy | `void` | value return, default constructible | reference return | non-default-constructible value return | `noexcept` function |
|---|---|---|---|---|---|
| `abort` | compatible | compatible | compatible | compatible | compatible |
| `default-return` | compatible | compatible | incompatible diagnostic | incompatible diagnostic | compatible |
| `throw` | compatible | compatible | compatible | compatible | incompatible diagnostic |

`throw` fallback は `noexcept` function に使わない。`default-return` は reference return と
default construct できない value return には使わず、該当 method を fallback
incompatibility として report する。
`compile-error` は accepted policy ではないため、この compatibility matrix の対象外とする。

### 21.7 link-readiness and usable source list

class-level link readiness は、生成された fake source を test target に入れてよいかを表す。

| class state | link-ready | CMake fragment usable source list | manifest/report |
|---|---|---|---|
| generated methods only, unsupported なし | yes | `FakeXXX.cpp` を載せる | `link_ready: true` |
| unsupported item が残る | no | `FakeXXX.cpp` を載せない | `link_ready: false`, reason を出す |
| fallback incompatibility がある | no | `FakeXXX.cpp` を載せない | `link_ready: false`, reason を出す |
| parse / validation / ket contamination failure | no publish | usable source list を publish しない | diagnostic artifact に failure を出す |

not link-ready な fake は diagnostic と手動調査のために生成できても、CMake fragment の
`MOCKFAKE_GENERATED_SOURCES` には含めない。

---

## 22. テストコードでの利用例

生成後、テストコードでは以下のように使う。

```cpp
#include <gtest/gtest.h>

#include "Hoge.h"
#include "MockHoge.h"

TEST(HogeTest, InitializeReturnsTrue)
{
    MockHoge mock;
    ScopedMockHoge scoped(mock);

    EXPECT_CALL(mock, Initialize(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(true));

    Hoge hoge;
    EXPECT_TRUE(hoge.Initialize(0, nullptr));
}
```

`ScopedMockHoge` は RAII で mock 登録・解除を行う。

ネストも可能である。

```cpp
MockHoge outer;
ScopedMockHoge scoped_outer(outer);

{
    MockHoge inner;
    ScopedMockHoge scoped_inner(inner);
    // この scope では inner が優先される
}

// ここでは outer に戻る
```

---

## 23. テストターゲットへの組み込み

### 23.1 CMake 例

```cmake
add_executable(HogeTest
    HogeTest.cpp
    ${MOCKFAKE_GENERATED_SOURCES}
)

target_include_directories(HogeTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${MOCKFAKE_GENERATED_INCLUDE_DIR}
)

target_link_libraries(HogeTest PRIVATE
    GTest::gtest
    GTest::gmock
    GTest::gtest_main
)
```

重要: `Hoge.cpp` と `FakeHoge.cpp` を同時にリンクしない。

同じ `Hoge::Initialize` シンボルが両方に存在すると duplicate symbol になるか、意図しない実装がリンクされる。

### 23.2 静的ライブラリ利用時の注意

製品コードが `libproduct.a` にまとまっている場合、`FakeHoge.cpp` で差し替えたいクラスの実装が同じ object file に含まれていると、リンク単位の都合で差し替えが難しいことがある。

推奨:

- テストターゲットでは差し替え対象 `.cpp` を除外した source list を使う。
- 1 class / 1 source file に近い構成にしておく。
- 必要に応じて CMake の object library を分割する。

---

## 24. 実装モジュール詳細

### 24.1 `Config`

責務:

- `ket::cli::ArgvView` による CLI 引数の parse
- `ket::parse` による `--jobs` などの数値 option parse
- section 6 の complete Config option matrix に従う ownership / default / deferred 状態の固定
- deferred option を unknown / silent ignore せず `DeferredOption` diagnostic にする
- YAML/TOML config file support はこの段階では deferred として診断する
- default 値の解決
- path の canonical 化
- option conflict の検出
- CLI 入力不正を `ConfigError` としてユーザー向け diagnostic に変換する

注意:

- ユーザー入力の不足・不正は contract violation ではなく通常エラーとして返す。
- `ket::contract` を採用する場合でも、内部 invariant 用に限り、CLI 入力不正には使わない。

### 24.2 `HeaderScanner`

責務:

- `.h` の再帰探索
- exclude rule 適用
- symlink 対策
- project relative path 算出
- suffix/prefix 判定と診断用 path 表記補助

注意:

- filesystem traversal は `std::filesystem` を直接使う。
- ket を追加採用する場合でも、path 操作ライブラリとしてではなく、文字列補助としてのみ使う。

### 24.3 `CompilationResolver`

責務:

- `compile_commands.json` 読み込み
- 対象 header と translation unit の対応付け
- synthetic TU 用 compile command 生成
- 複数 config 差分の検出

### 24.4 `AstFrontendAction`

責務:

- Clang FrontendAction の実装
- SourceManager と ASTContext の保持
- MatchFinder callback 呼び出し
- parse diagnostics 収集

### 24.5 `ClassExtractor`

責務:

- `CXXRecordDecl` 抽出
- class/struct 判定
- source location filter
- method 宣言順維持
- unsupported 判定の一次情報取得

### 24.6 `TypeSpellingService`

責務:

- `QualType` から出力可能な型表記を生成
- 元ソース表記の復元
- fully qualified name 生成
- gMock 用括弧 wrap 判定
- parameter type の array-to-pointer 調整

### 24.7 `PolicyEngine`

責務:

- mock/fake 対象判定
- strict / best-effort 判定
- fallback policy 適用
- constructor/static data 生成可否判定
- interface mock mode 判定

### 24.8 `CodeGenerator`

責務:

- `MockFakeRuntime.h`
- `MockXXX.h`
- `FakeXXX.cpp`
- `AllMocks.h`
- `CMakeLists.fragment.cmake`
- `manifest.json`
- `generation_report.md`
- `GeneratedFile` の配列を生成し、実際の filesystem 書き込みは `OutputWriter` に委譲する

注意:

- 生成される C++ コードには ket include や `ket::` を出力しない。
- `CodeGenerator` 自体も原則として Clang/IR と標準ライブラリ中心に保ち、ket 依存は必要最小限にする。

### 24.9 `Formatter`

責務:

- LibFormat による C++ 整形
- `.clang-format` 探索
- fallback style 適用
- format 失敗時の診断

### 24.10 `Validator`

責務:

- smoke compile 用 temporary source 生成
- `clang++ -fsyntax-only` または object compile 実行
- gMock include path 確認
- エラー行と生成元 method の対応付け
- ket include path を意図的に外した生成物 compile fixture を実行する

### 24.11 `OutputWriter`

責務:

- 出力ディレクトリ作成
- `--dry-run` / `--overwrite` policy 適用
- 標準 library による text write
- atomic replace が必要な場合の temporary file + rename
- 生成済みファイル一覧と checksum の記録
- write failure を `generation_report.md` に反映する

### 24.12 `SupportServices`

責務:

- `CliSupport`: `ket::cli` / `ket::parse` を薄く包み、Config 以外から CLI details を隠す
- File / text support は現行では標準 library に閉じる
- ket module を追加する場合は SupportServices に閉じ、dependency inventory を更新する

この層は、ket の適用範囲を見える化するために置く。Clang AST、IR、生成 template へ ket 依存が広がった場合は設計違反として review で戻す。

---

## 25. CMake 設計: ツール本体

ツール本体は C++23 固定でビルドする。ket は full Git submodule として
`third_party/ket` に置く。CMake では ket 自体を subdirectory として build せず、
必要 module の include directory と `.cpp` だけを専用 adapter target
`mockfakegen_ket` に明示する。選択 module が header-only だけの場合は
`INTERFACE` target とする。

```cmake
cmake_minimum_required(VERSION 3.25)
project(mockfakegen LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

set(KET_MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ket/modules")

add_library(mockfakegen_ket INTERFACE)

target_include_directories(mockfakegen_ket SYSTEM INTERFACE
    "${KET_MODULE_DIR}/cli"
    "${KET_MODULE_DIR}/parse"
)

target_compile_features(mockfakegen_ket INTERFACE cxx_std_23)

add_executable(mockfakegen
    src/main.cpp
    src/Config.cpp
    src/HeaderScanner.cpp
    src/CompilationResolver.cpp
    src/AstFrontendAction.cpp
    src/ClassExtractor.cpp
    src/TypeSpellingService.cpp
    src/PolicyEngine.cpp
    src/CodeGenerator.cpp
    src/OutputWriter.cpp
    src/Formatter.cpp
    src/Validator.cpp
    src/support/CliSupport.cpp
)

target_include_directories(mockfakegen PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
    src
)

target_compile_definitions(mockfakegen PRIVATE
    ${LLVM_DEFINITIONS}
)

target_link_libraries(mockfakegen PRIVATE
    mockfakegen_ket
    clangTooling
    clangBasic
    clangAST
    clangASTMatchers
    clangFrontend
    clangLex
    clangFormat
)
```

LLVM/Clang のバージョン差により link target 名が異なる場合があるため、実装時は対象ディストリビューションのパッケージに合わせて調整する。

### 25.1 ket submodule の CMake ルール

- `third_party/ket` は full Git submodule として扱い、`add_subdirectory(third_party/ket)` は使わない。
- ket の include directory を `mockfakegen_ket` の `INTERFACE` / `PUBLIC` include directory に閉じる。
- `mockfakegen` 以外の生成物 compile fixture target には ket include directory を渡さない。
- ket module の `.cpp` がある場合だけ `mockfakegen_ket` の source に追加する。
- header-only module は include directory のみ追加する。
- ket pin、選択 module、license 状態を変更した場合は `docs/dependency_inventory.md` を更新する。

### 25.2 生成物 compile fixture の ket 非依存検証

CI では以下のような target を用意する。

```cmake
add_executable(generated_fixture
    fixtures/generated/FakeHoge.cpp
    fixtures/generated/GeneratedUsageTest.cpp
)

target_include_directories(generated_fixture PRIVATE
    fixtures/product_include
    fixtures/generated
    # third_party/ket は意図的に入れない
)

target_link_libraries(generated_fixture PRIVATE
    GTest::gtest
    GTest::gmock
    GTest::gtest_main
)
```

加えて、生成物に ket token が混入していないことを text check する。

```bash
! grep -R 'ket::\|#include [<"]ket_' generated/test_doubles
```

---

## 26. 開発フェーズ計画

### Phase 0: 技術検証

目的:

- LibTooling で `.h` 由来の `CXXRecordDecl` を抽出できることを確認する。
- `MOCK_METHOD` 生成と fake forwarding の最小実装を作る。

対応:

- global namespace の単純 class
- public normal member function
- `void` / `bool` / `int` return
- 引数なし、基本型引数
- `MockFakeRuntime.h` thread-local 版
- `third_party/ket` full submodule と `mockfakegen_ket` module selection
- `ket::cli` を使った `--help` / 必須 option parse
- 標準 library を使った単一ファイル出力
- 生成物が ket include path なしで compile できる検証

### Phase 1: 実用 MVP

対応:

- input-root 再帰探索
- output-dir 一括生成
- namespace 保存
- overload
- const
- noexcept
- ref qualifier
- static member function
- default argument 除去
- top-level comma 型の gMock wrap
- `AllMocks.h`
- `manifest.json`
- `generation_report.md`
- clang-format
- smoke compile validation
- `OutputWriter` の dry-run / overwrite / write failure test
- ket token 非混入 grep check

この段階で、一般的な業務コードの相当部分をカバーできる。

### Phase 2: 品質強化

対応:

- TU scan + synthetic TU fallback
- 複数 compile config 差分検出
- constructor/destructor fake 試行生成
- static data member definition
- `shared-owner` registry mode
- interface mock mode
- collision policy 強化
- YAML config
- golden test 大量追加

### Phase 3: 高度機能

対応候補:

- free function fake/mock
- operator function mapping
- class template の header-only fake 生成
- custom fallback hook
- per-class generation policy
- IDE integration
- Git pre-commit / CI 差分チェック
- HTML report

---

## 27. 受け入れ基準

### 27.1 機能基準

最低限、以下入力から期待される `MockHoge.h` / `FakeHoge.cpp` が生成されること。

```cpp
class Hoge
{
public:
    Hoge() = default;
    ~Hoge() = default;

    bool Initialize(int argc, char* argv[]);
    void Finalize();
    bool DoSomething();
};
```

生成物を用いたテストが以下のようにコンパイル・実行できること。

```cpp
TEST(HogeTest, Example)
{
    MockHoge mock;
    ScopedMockHoge scoped(mock);

    EXPECT_CALL(mock, Initialize(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(true));

    Hoge hoge;
    EXPECT_TRUE(hoge.Initialize(0, nullptr));
}
```

### 27.2 品質基準

- 生成物は `clang-format` 済みである。
- 生成物は C++23 と gMock で compile smoke test を通る。
- 未対応構文は理由付きで report される。
- `--strict` では未対応や compile validation failure により非ゼロ終了する。
- 同じ入力に対する生成結果は deterministic である。
- ファイル名衝突が正しく処理される。
- `thread-local` mode で並列テストの mock pointer 混線が起きない。
- ツール本体は `third_party/ket` submodule から選択した ket module を使用してビルドされる。
- 生成物は ket include path なしで compile validation を通る。
- 生成物に `ket::` や `#include "ket_..."` が出力されない。

---

## 28. テスト戦略

### 28.1 Unit test

対象:

- path filter
- include spelling generation
- top-level comma 判定
- parameter name synthesis
- fallback policy
- filename collision resolver
- report writer
- ket-backed `CliSupport`
- generated-file ket-token contamination check

### 28.2 AST extraction test

Clang の `runToolOnCode` を使い、短いコード片から IR を生成して検証する。

ケース:

- simple method
- const method
- noexcept method
- ref-qualified method
- overload
- static member function
- default argument
- array parameter
- template type with comma
- namespace
- nested namespace
- unsupported function template
- inline method skip

### 28.3 Golden file test

fixture header を入力し、生成された `MockXXX.h` / `FakeXXX.cpp` が期待 golden file と一致することを確認する。

### 28.4 Compile integration test

小さな CMake fixture project を作り、以下を CI で検証する。

1. `compile_commands.json` 生成
2. `mockfakegen` 実行
3. 生成物 compile validation
4. gTest 実行

### 28.5 Runtime behavior test

- mock 登録なしで abort/default-return が policy どおり動く
- `ScopedMock` が RAII で pop する
- nested scope で inner mock が優先される
- destruction order mismatch を検出する
- `thread-local` で別スレッドの mock が見えない
- `global-mutex` で別スレッドから mock が見える
- `shared-owner` で fake 呼び出し中の lifetime が保持される
- `global-mutex` / `shared-owner` の generated fake が worker thread から gMock expectation へ委譲する

---

## 29. リスクと対策

| リスク | 影響 | 対策 |
|---|---|---|
| ヘッダ単体 parse が失敗する | 対象クラスが生成できない | TU scan mode、synthetic fallback、include/define 追加 option |
| macro 由来宣言の表記復元が難しい | 壊れた gMock 生成 | AST type printer fallback、strict 診断、source range report |
| fake と製品 `.cpp` の二重リンク | duplicate symbol | report と CMake fragment に注意喚起、validation document 化 |
| constructor が fake 生成できない | テストリンク失敗 | defaulted/in-header constructor 推奨、`--fake-special-members`、compile validation |
| 参照戻り値の fallback 不可能 | compile/runtime failure | default `abort`、`default-return` では unsupported 診断 |
| 同名クラス衝突 | 出力ファイル上書き | qualified filename policy、manifest 記録 |
| 並列テストで mock 混線 | flaky test | default thread-local registry、global/shared mode の明示切替 |
| worker thread から mock が見えない | テスト失敗 | global-mutex/shared-owner mode を提供 |
| gMock macro の構文制約 | compile failure | top-level comma wrap、type-only args、smoke compile validation |
| ket 依存が生成物へ漏れる | コピペ即使用性が落ちる | 生成物 compile fixture から ket include path を外す、grep check を CI 化 |
| ket module の未実装・仕様差 | ツール本体の build failure | 必須利用は実装済み module に限定、追加時は dependency inventory と CI を更新 |
| ket を Clang 領域に使いすぎる | AST fidelity 低下 | `src/clang` と `src/model` は原則 ket 非依存にする |

---

## 30. 推奨デフォルト設定

品質優先の推奨デフォルトは以下である。

```yaml
std: c++23
registry_mode: thread-local
fallback_policy: abort
mock_namespace_mode: same-as-product
access: public
include_struct: false
format_style: file
validate: compile
strict: false
best_effort: true
emit_all_mocks: true
emit_manifest: true
emit_cmake_fragment: true
collision_policy: qualified-filename
ket_profile: tool-core
```

CI で使う場合は以下を推奨する。

```bash
mockfakegen ... --strict --validate compile
```

ローカル導入初期は以下を推奨する。

```bash
mockfakegen ... --best-effort --validate compile
```

---

## 31. 最初に実装すべき最小サンプル

入力:

```cpp
#pragma once

class Hoge
{
public:
    Hoge() = default;
    ~Hoge() = default;

    bool Initialize(int argc, char* argv[]);
    void Finalize();
    bool DoSomething();
};
```

生成 `MockHoge.h`:

```cpp
#pragma once

#include <gmock/gmock.h>

#include "Hoge.h"
#include "MockFakeRuntime.h"

class MockHoge
{
public:
    MockHoge() = default;
    ~MockHoge() = default;

    MOCK_METHOD(bool, Initialize, (int, char**), ());
    MOCK_METHOD(void, Finalize, (), ());
    MOCK_METHOD(bool, DoSomething, (), ());
};

using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;
```

生成 `FakeHoge.cpp`:

```cpp
#include "Hoge.h"
#include "MockHoge.h"

bool Hoge::Initialize(int argc, char** argv)
{
    if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
    {
        return mock->Initialize(argc, argv);
    }

    return ::mockfake::MissingMockReturn<bool>("Hoge::Initialize(int, char**)");
}

void Hoge::Finalize()
{
    if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
    {
        mock->Finalize();
        return;
    }

    return ::mockfake::MissingMockReturn<void>("Hoge::Finalize()");
}

bool Hoge::DoSomething()
{
    if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
    {
        return mock->DoSomething();
    }

    return ::mockfake::MissingMockReturn<bool>("Hoge::DoSomething()");
}
```

---


## 32. コーディングエージェント実装ガイド

この設計書は、コーディングエージェントと対話しながら実装を進める前提で使う。エージェントには一度に巨大な完成品を依頼せず、phase / component / acceptance criteria 単位で依頼する。

### 32.1 エージェントに渡す共通コンテキスト

実装依頼時は、毎回以下を明示する。

```txt
このプロジェクトは C++23 固定の mockfakegen です。
中核は Clang LibTooling で、正規表現パーサではありません。
ket はツール本体の周辺処理に使いますが、生成物には ket 依存を出してはいけません。
生成物は gMock + 標準ライブラリ + MockFakeRuntime.h だけでコンパイルできる必要があります。
未対応構文は壊れたコードを出さず、理由付き diagnostic にしてください。
```

### 32.2 実装依頼テンプレート

```txt
mock_fake_generator_design.md の「<section>」に従って、<component> を実装してください。
対象は <scope> に限定してください。
ket の適用範囲は設計書の「ket 適用方針」に従ってください。
生成物には ket include や ket:: を出力しないでください。
実装、単体テスト、必要な fixture、CMake 更新、検証コマンドの結果まで1セットで提示してください。
```

### 32.3 最初の実装依頼順序

推奨順序:

1. `third_party/ket` full submodule と `mockfakegen_ket` CMake skeleton
2. `Config` / `CliSupport`
3. `HeaderScanner`
4. `MockFakeRuntime.h` template
5. 最小 `CodeGenerator` と `OutputWriter`
6. Clang `ClassExtractor` の最小 AST 抽出
7. `TypeSpellingService` の最小型表記
8. end-to-end fixture: `Hoge.h` から `MockHoge.h` / `FakeHoge.cpp`
9. compile validation
10. report / manifest

この順序なら、早い段階で「ツールとして動く薄い縦切り」を作り、その後に Fidelity を高められる。

### 32.4 エージェント向け禁止事項

- 正規表現だけで C++ 宣言を parse しない。
- `MockXXX.h` / `FakeXXX.cpp` に ket を include しない。
- 未対応構文を silent skip しない。
- compile validation なしに「生成できた」とみなさない。
- timestamp など非 deterministic な生成物をデフォルトで出さない。
- `--strict` の失敗条件を曖昧にしない。
- ユーザー入力エラーを `ket::contract` で terminate しない。
- ket module を改変して問題を解決しない。必要なら `src/support/KetCompat.*` に隔離する。

### 32.5 エージェント向け Definition of Done

各実装タスクは最低限以下を満たす。

```txt
[ ] 設計書の該当 section と差分がない
[ ] CMake が通る
[ ] 単体テストがある
[ ] 生成物に ket 依存が出ていない
[ ] 失敗ケースが diagnostic または test で確認されている
[ ] clang-format 済み
[ ] 同じ入力で deterministic な出力になる
[ ] 変更した component の責務境界が崩れていない
```

### 32.6 レビューで重点的に見る点

- ket が便利だからという理由だけで Clang 領域へ入り込んでいないか。
- `src/support` 以外に ket include が増えている場合、その理由が明確か。
- 生成物 template に `ket::`、`ket_`、ket 前提の helper が混ざっていないか。
- `QualType` の文字列化に source spelling と canonical type のどちらを使ったかが明示されているか。
- `MOCK_METHOD` の戻り値型・引数型に top-level comma wrap が必要な場合を扱えているか。
- fallback policy が `noexcept`、参照戻り値、非 default constructible 戻り値と矛盾していないか。

---

## 33. 将来拡張案

### 33.1 free function 対応

namespace free function も fake/mock 化できると、C API やユーティリティ関数の差し替えに使える。

```cpp
bool LoadConfig(const char* path);
```

生成:

```cpp
class MockGlobalFunctions
{
public:
    MOCK_METHOD(bool, LoadConfig, (const char*), ());
};

bool LoadConfig(const char* path)
{
    if (auto* mock = ::mockfake::CurrentMock<MockGlobalFunctions>())
    {
        return mock->LoadConfig(path);
    }
    return ::mockfake::MissingMockReturn<bool>("LoadConfig(const char*)");
}
```

### 33.2 per-class policy file

クラスごとに fallback や registry mode を切り替えられるようにする。

```yaml
classes:
  app::Hoge:
    fallback_policy: abort
    registry_mode: thread-local
  app::AsyncClient:
    registry_mode: shared-owner
```

### 33.3 custom fallback hook

mock 未登録時に独自処理を差し込む。

```cpp
namespace mockfake
{
void OnMissingMock(std::string_view signature);
}
```

### 33.4 生成コードへの provenance comment

標準では timestamp を出さないが、必要に応じて入力ヘッダと tool version をコメント出力する。

```cpp
// Generated by mockfakegen 1.1
// Source: include/Hoge.h
// Do not edit manually.
```

CI 差分の安定性を優先する場合、timestamp は出さない。

---

## 34. 参考資料

- Clang LibTooling: https://clang.llvm.org/docs/LibTooling.html
- Clang LibASTMatchers: https://clang.llvm.org/docs/LibASTMatchers.html
- Clang AST Matcher Reference: https://clang.llvm.org/docs/LibASTMatchersReference.html
- Clang JSON Compilation Database Format: https://clang.llvm.org/docs/JSONCompilationDatabase.html
- ClangFormat / LibFormat: https://clang.llvm.org/docs/ClangFormat.html
- Clang-Format Style Options: https://clang.llvm.org/docs/ClangFormatStyleOptions.html
- GoogleTest Mocking Reference: https://google.github.io/googletest/reference/mocking.html
- GoogleTest gMock Cookbook: https://google.github.io/googletest/gmock_cook_book.html
- CMake `CXX_STANDARD`: https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD.html
- CMake `CXX_STANDARD_REQUIRED`: https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD_REQUIRED.html
- ket Coding Agent Brief: https://github.com/horiyamayoh/ket/blob/main/ket_coding_agent_brief.md
- ket module/API catalog: https://github.com/horiyamayoh/ket/blob/main/docs/module_api_catalog.md

---

## 35. 結論

本ツールは、単純な文字列置換ツールではなく、Clang LibTooling を中核にした C++ テストダブル生成基盤として設計する。

最初の MVP では、通常の public member function から `MockXXX.h` と `FakeXXX.cpp` を安定生成する。そのうえで、namespace、overload、const、noexcept、ref qualifier、static member function、default argument、gMock macro の制約、並列テスト対応、compile validation までを初期実用ラインに含める。

特に重要なのは以下である。

1. **生成しない勇気を持つこと。** C++ の難しい構文を無理に生成せず、理由付きで report する。
2. **生成後に必ず検証すること。** AST から正しく見えても、gMock macro や include context で壊れる可能性があるため compile validation を標準化する。
3. **ランタイムを共通化すること。** クラスごとの global pointer ではなく、型ごとの registry と RAII scope により、並列テスト・ネスト・ODR の問題を抑える。
4. **リンク差し替えの制約を明示すること。** `FakeXXX.cpp` と製品 `.cpp` を同時リンクしない設計を CMake fragment と report で支援する。
5. **ket は生成ツール本体の補助として使い、生成物へ漏らさないこと。** ket の drop-in utility としての価値は活用しつつ、テストコード利用者に ket 導入を強制しない。

この方針なら、提示されたスニペットのシンプルさを保ちながら、実プロジェクトに導入できる品質の自動生成ツールに発展させられる。
