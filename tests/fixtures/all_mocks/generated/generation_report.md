# mockfakegen generation report

## Summary

| Classes | Link-ready classes | Not link-ready classes | Generated methods | Unsupported items | Diagnostics | Info | Warnings | Errors | Validation commands |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 2 | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 |

Registry mode: `thread-local` - per-thread mock stack; use when product code calls fakes on the same thread as the scoped mock.

Fallback policy: `abort`.

## Link Replacement Notice

Do not link generated `FakeXXX.cpp` files together with the corresponding product `.cpp` files in the same test target. Link each generated fake source instead of the product implementation it replaces.

Compile validation checks generated headers and fake sources as separate translation units; it does not prove that your test target performs correct link substitution. Use `--validate link` to link a smoke executable with generated fakes and gMock where practical.

Usable fake sources for build-system integration:

- `FakeAlpha.cpp`
- `FakeBeta.cpp`

## Generated Classes

| Class | Source header | Parse mode | Generation mode | Mock header | Fake source | Link ready | Link-readiness reason | Generated methods | Unsupported items |
|---|---|---|---|---|---|---|---|---:|---:|
| Alpha | Alpha.h | synthetic-tu | link-replacement | MockAlpha.h | FakeAlpha.cpp | yes |  | 1 | 0 |
| Beta | Beta.h | synthetic-tu | link-replacement | MockBeta.h | FakeBeta.cpp | yes |  | 1 | 0 |

## Diagnostics

No diagnostics.

## Validation Commands

No validation commands recorded.

## Unsupported Items

No unsupported items.
