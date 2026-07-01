# mockfakegen User Guide

This guide describes the current production-use surface of `mockfakegen`.

`mockfakegen` is a C++23 Clang LibTooling executable. It reads product `.h` files,
extracts supported classes and methods, generates gMock mock headers and link-replacement
fake sources, validates the generated C++, and writes diagnostic artifacts.

## Current Status

Implemented:

- CLI parsing and end-to-end runner.
- Header scanning with built-in exclusion of generated output, build directories,
  `third_party`, `external`, symlinks, and configured excludes.
- `compile_commands.json` based Clang parsing with synthetic translation-unit fallback.
- Code generation for supported public class methods, optional special members, optional
  static data definitions, and interface-mock mode.
- `MockFakeRuntime.h` generation for `thread-local`, `global-mutex`, and `shared-owner`
  registries.
- Generated C++ formatting using libFormat.
- Generated-output validation modes: `none`, `syntax`, `compile`, and `link`.
- `MockXXX.h`, `FakeXXX.cpp`, `AllMocks.h`, `CMakeLists.fragment.cmake`,
  `manifest.json`, and `generation_report.md` output.
- CMake install support and install smoke coverage.

Intentional limits:

- C++23 only.
- `.h` headers only.
- Public class members only.
- Class declarations in anonymous namespaces, nested class definitions, templates, and
  several complex method forms are reported as unsupported.
- External config files, class filters, CLI-provided include dirs, CLI-provided defines,
  and CLI-provided extra compiler args are recognized but deferred.
- Generated output never depends on ket.

## Requirements

Typical Linux/WSL packages:

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  cmake ninja-build clang-18 llvm-18-dev libclang-18-dev python3
```

Initialize the ket submodule before configuring:

```sh
git submodule update --init --recursive
```

The `dev` preset uses Ninja, enables `compile_commands.json`, builds tests, and turns
warnings into errors:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

If LLVM/Clang is installed outside the default search paths, point CMake at the LLVM root:

```sh
cmake --preset dev -DMOCKFAKEGEN_LLVM_ROOT=/usr/lib/llvm-18
```

The root must contain both LLVM and Clang CMake packages. See
[`docs/dependency_inventory.md`](dependency_inventory.md) for dependency versions,
licenses, and reproducibility notes.

## Running The CLI

The minimal command needs four paths:

```sh
mockfakegen \
  --input-root <project-or-include-subdir> \
  --output-dir <generated-output-dir> \
  --build-path <build-dir-with-compile_commands.json> \
  --project-root <project-root>
```

`--input-root` must be the same as or under `--project-root`. Header include spelling is
project-relative. `--build-path` should normally be the product project's CMake build
directory, not the `mockfakegen` build directory, unless you are generating for this
repository's fixtures.

The CLI accepts both `--option value` and `--option=value` for value options.

## CLI Options

| Option | Status | Default | Meaning |
|---|---|---|---|
| `--help` | supported | off | Print usage and exit. |
| `--input-root <path>` | required | none | Directory scanned recursively for `.h` files. |
| `--output-dir <path>` | required | none | Directory where generated files, report, and manifest are written. |
| `--build-path <path>` | required | none | Directory containing `compile_commands.json`. |
| `--project-root <path>` | required | none | Base directory for project-relative paths and include spelling. |
| `--std <value>` | supported | `c++23` | Must be `c++23`. |
| `--config <path>` | deferred | none | Recognized, but external config files are not implemented yet. |
| `--header-extension <ext>` | supported | `.h` | Must be `.h`. |
| `--header-filter <regex>` | supported | none | Include only project-relative header paths matching the regex. |
| `--exclude <glob>` | supported, repeatable | none | Exclude project-relative paths. `*`, `?`, and `**` are supported. |
| `--class-filter <regex>` | deferred | none | Recognized, but class filtering is not implemented yet. |
| `--access <policy>` | public only | `public` | `public` is supported. `protected` and `private` are deferred. |
| `--include-struct <bool>` | false only | `false` | `true` is recognized but deferred. |
| `--registry-mode <mode>` | supported | `thread-local` | Runtime mock registry: `thread-local`, `global-mutex`, or `shared-owner`. |
| `--fallback-policy <policy>` | supported | `abort` | Missing-mock behavior: `abort`, `default-return`, or `throw`. |
| `--mock-namespace-mode <mode>` | supported | `same-as-product` | Mock classes are generated in the same namespace as the product class. |
| `--collision-policy <policy>` | supported | `qualified-filename` | Filename collisions are resolved with namespace-qualified filenames. |
| `--fake-special-members <bool>` | supported | `false` | Generate safe constructor/destructor fakes when supported. |
| `--fake-static-data <bool>` | supported | `false` | Generate safe static data member definitions when supported. |
| `--interface-mock <bool>` | supported | `false` | Generate inheritance-based interface mocks instead of link-replacement fakes. |
| `--include-dir <path>` | deferred, repeatable | none | Recognized, but extra CLI include dirs are not implemented yet. |
| `--define <macro>` | deferred, repeatable | none | Recognized, but extra CLI defines are not implemented yet. |
| `--extra-arg <arg>` | deferred, repeatable | none | Recognized, but extra CLI compiler args are not implemented yet. |
| `--dry-run` | supported | off | Resolve and plan writes without changing files. |
| `--overwrite` | supported | off | Allow replacing existing generated files. |
| `--strict` | supported | off | Return failure for unsupported items and link-readiness warnings. |
| `--best-effort` | supported | on | Generate supported output while recording unsupported items. |
| `--emit-all-mocks <bool>` | supported | `true` | Generate `AllMocks.h`. |
| `--emit-manifest <bool>` | supported | `true` | Generate `manifest.json` when policy allows it. |
| `--emit-cmake-fragment <bool>` | supported | `true` | Generate `CMakeLists.fragment.cmake`. |
| `--format-style <style>` | supported | `file` | `file`, `llvm`, `google`, or `none`. |
| `--validate <mode>` | supported | `compile` | `none`, `syntax`, `compile`, or `link`. |
| `--validation-timeout-ms <N>` | supported | `30000` | Positive timeout for each validation command. |
| `--validation-keep-artifacts` | supported | off | Keep failed validation artifacts for reproduction. |
| `--validation-artifact-dir <path>` | supported | generated temp path | Directory for kept validation artifacts. |
| `--jobs <N>` | parsed, reserved | host concurrency | Positive worker count. Current runner work is effectively sequential. |

Boolean values must be `true` or `false`. `--strict` and `--best-effort` are mutually
exclusive. Deferred options fail with a `deferred_option` diagnostic instead of being
silently ignored.

## Generated Files

The generated output directory can contain:

- `MockFakeRuntime.h`: shared runtime used by generated mocks/fakes.
- `MockXXX.h`: gMock mock class and `ScopedMockXXX` alias.
- `FakeXXX.cpp`: link-replacement implementation for the product class.
- `AllMocks.h`: optional aggregate include.
- `CMakeLists.fragment.cmake`: optional build-system fragment for link-ready fake sources.
- `manifest.json`: machine-readable generation summary and diagnostics.
- `generation_report.md`: human-readable summary, link-readiness table, diagnostics,
  validation commands, and unsupported item table.

Generated C++ includes product headers, `<gmock/gmock.h>`, standard headers, and
`MockFakeRuntime.h`. It must compile without ket include directories.

## Link Substitution

`FakeXXX.cpp` is intended to replace the corresponding product implementation at link
time. Do not link both the product `.cpp` and generated `FakeXXX.cpp` that define the
same class members into one test executable.

When `CMakeLists.fragment.cmake` is emitted, it defines:

- `MOCKFAKE_GENERATED_SOURCES`: only link-ready generated fake sources.
- `MOCKFAKE_GENERATED_INCLUDE_DIR`: the generated include directory.

Example CMake usage:

```cmake
include(${CMAKE_BINARY_DIR}/mockfakegen/CMakeLists.fragment.cmake)

add_executable(hoge_test
    tests/HogeTest.cpp
    ${MOCKFAKE_GENERATED_SOURCES}
)

target_include_directories(hoge_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${MOCKFAKE_GENERATED_INCLUDE_DIR}
)

target_link_libraries(hoge_test PRIVATE GTest::gmock_main)
```

The test target intentionally omits the replaced product `.cpp` files.

## Validation

Validation modes:

- `none`: skip generated-output compile validation.
- `syntax`: run syntax-only validation where applicable.
- `compile`: compile generated mock headers and fake sources as separate translation units.
- `link`: do compile validation, then link a generated-fake smoke executable with gMock link
  artifacts.

`compile` is the default and catches invalid generated C++ early. It does not prove that
your final test target performs link substitution correctly. Use `link` when gMock link
artifacts are available and you want duplicate-symbol diagnostics for accidental product
`.cpp` linkage.

The validator uses these optional environment variables:

```sh
MOCKFAKEGEN_CXX_COMPILER=/path/to/c++
MOCKFAKEGEN_GMOCK_INCLUDE_DIRS=/path/to/googlemock/include\|/path/to/googletest/include
MOCKFAKEGEN_GMOCK_LINK_FILES=/path/to/libgmock.a\|/path/to/libgtest.a
```

`MOCKFAKEGEN_GMOCK_INCLUDE_DIRS` and `MOCKFAKEGEN_GMOCK_LINK_FILES` accept separated path
lists. `|` is recommended in shell examples because it avoids ambiguity with Unix path
names containing `:`.

## Registry Modes

Generated fakes use `MockFakeRuntime.h` to find the active mock for each type.

- `thread-local`: default. Each thread has its own mock stack. Use it when product code
  calls generated fakes on the same thread that owns the scoped mock.
- `global-mutex`: one process-wide mock stack per mock type, protected by a mutex. Worker
  threads can see the scoped mock, but tests must join workers before scope destruction and
  avoid concurrent same-type scopes.
- `shared-owner`: one process-wide `std::shared_ptr` stack per mock type, protected by a
  mutex. Generated fakes keep a `shared_ptr` copy during mock calls.

## Fallback Policies

Fallback policy controls what a generated fake does when no scoped mock is active:

- `abort`: call the runtime missing-mock handler and terminate.
- `default-return`: return a default value for default-constructible non-reference return
  types and return normally for `void`. Reference returns and non-default-constructible
  returns are policy failures.
- `throw`: throw a runtime missing-mock exception. `noexcept` methods are policy failures.

Policy failures are recorded in the report and manifest. They prevent publishing generated
files because the generated behavior would not be usable for the selected policy.

## Strict And Best Effort

`--best-effort` is the default. It writes supported generated files and records unsupported
items and link-readiness warnings in `generation_report.md` and `manifest.json`.

`--strict` turns unsupported items and link-readiness warnings into a nonzero exit code.

The following failures return nonzero regardless of strictness:

- CLI config errors.
- Header scan errors.
- Clang parse errors.
- Format failures.
- Generated-output ket contamination.
- Compile or link validation failures.
- Write failures.
- Fallback policy incompatibilities.

## Unsupported Construct Policy

The tool does not silently skip unsupported C++ constructs. Each unsupported item is
reported with a reason and suggested action.

Common unsupported categories include:

- Class templates and class template specializations.
- Anonymous-namespace classes.
- Nested class definitions.
- Function template members.
- Conversion operators and overloaded operators.
- Pure virtual methods in link-replacement mode.
- Non-public methods.
- Deleted or defaulted methods where generation is not safe.
- `constexpr`, `consteval`, volatile methods, conditional `noexcept`, explicit object
  parameters, and unsupported attributes.
- Inline method bodies when link replacement would duplicate behavior.
- Deduced `auto`, `decltype(auto)`, trailing return types, function pointer/reference
  returns, private nested types, and attributed types in method signatures.
- Special members and static data definitions unless enabled and considered safe.

Use the report's `Unsupported Items` table and the manifest's `diagnostics` array as the
source of truth for a specific run.

## Report And Manifest Schema

`generation_report.md` is intended for humans. It includes:

- Summary counts.
- Registry mode and fallback policy.
- Link replacement notice.
- Usable fake sources.
- Generated class table.
- Diagnostics table.
- Validation commands.
- Unsupported items table.

`manifest.json` has `schema_version: 1` and contains:

- `summary`: counts, registry mode, fallback policy, diagnostic summary, validation command
  count, and `usable_fake_sources`.
- `diagnostics`: severity, component, code, kind, path, source range, class, member,
  message, suggested action, command, stderr summary, and validation artifact path.
- `validation_commands`: source path, command, and exit code for generated-output validation.
- `classes`: qualified name, generated file names, source header, parse mode, generation
  mode, generated method counts, unsupported item counts, link readiness, and link-readiness
  reasons.

## Example

Input header:

```cpp
// include/Hoge.h
#pragma once

class Hoge
{
  public:
    bool Initialize(int argc, char* argv[]);
    void Finalize();
    bool DoSomething();
};
```

Run generation after configuring the product project with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`:

```sh
mockfakegen \
  --input-root "$PWD/include" \
  --output-dir "$PWD/build/mockfakegen" \
  --build-path "$PWD/build" \
  --project-root "$PWD" \
  --overwrite \
  --validate compile
```

Expected generated layout:

```text
build/mockfakegen/
  AllMocks.h
  CMakeLists.fragment.cmake
  FakeHoge.cpp
  MockFakeRuntime.h
  MockHoge.h
  generation_report.md
  manifest.json
```

Example test:

```cpp
#include <gmock/gmock.h>

#include "Hoge.h"
#include "MockHoge.h"

TEST(HogeTest, ForwardsToScopedMock)
{
    MockHoge mock;
    ScopedMockHoge scoped(mock);

    EXPECT_CALL(mock, DoSomething()).WillOnce(testing::Return(true));

    Hoge hoge;
    EXPECT_TRUE(hoge.DoSomething());
}
```

Example test target:

```cmake
include(${CMAKE_BINARY_DIR}/mockfakegen/CMakeLists.fragment.cmake)

add_executable(hoge_test
    tests/HogeTest.cpp
    ${MOCKFAKE_GENERATED_SOURCES}
)

target_include_directories(hoge_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${MOCKFAKE_GENERATED_INCLUDE_DIR}
)

target_link_libraries(hoge_test PRIVATE GTest::gmock_main)
```

Do not add the product `src/Hoge.cpp` to `hoge_test` when `FakeHoge.cpp` defines the same
members.

## Common Diagnostics

- `missing_required_option`, `missing_option_value`, `unknown_option`,
  `conflicting_option`, `duplicate_option`: fix the CLI invocation.
- `deferred_option`: the option is known but not implemented yet. Remove it or use the
  corresponding build-system configuration.
- `scanner_input_root_missing`, `scanner_input_root_not_directory`,
  `scanner_filesystem_error`: check `--input-root`, `--project-root`, permissions, and
  generated-output placement.
- `compile_database_not_found`, `compile_database_load_failure`: configure the product
  project with `CMAKE_EXPORT_COMPILE_COMMANDS=ON` and pass the build directory through
  `--build-path`.
- `real_tu_parse_failure`, `synthetic_tu_parse_failure`: inspect the recorded command and
  stderr summary in the report. Missing include paths usually need to be fixed in the
  product build configuration.
- `unsupported_*`: the construct was intentionally not generated. Check the unsupported
  item reason and suggested action.
- `format_failure`: generated C++ spelling could not be formatted. Reproduce with the
  reported path and selected `--format-style`.
- `compile_validation_failure`: rerun the recorded compiler command. Use
  `--validation-keep-artifacts` and `--validation-artifact-dir` to keep failed smoke files.
- Duplicate-symbol wording during `--validate link`: remove the product `.cpp` from the
  validation link inputs or final test target when the generated `FakeXXX.cpp` replaces it.
- Writer `skipped-existing`: rerun with `--overwrite` or choose a clean output directory.
