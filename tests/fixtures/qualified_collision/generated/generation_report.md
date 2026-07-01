# mockfakegen generation report

## Summary

| Classes | Link-ready classes | Not link-ready classes | Generated methods | Unsupported items | Diagnostics | Info | Warnings | Errors | Validation commands |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 2 | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 |

## Link Replacement Notice

Do not link generated `FakeXXX.cpp` files together with the corresponding product `.cpp` files in the same test target. Link each generated fake source instead of the product implementation it replaces.

## Generated Classes

| Class | Source header | Mock header | Fake source | Link ready | Link-readiness reason | Generated methods | Unsupported items |
|---|---|---|---|---|---|---:|---:|
| a::Hoge | a/Hoge.h | Mock_a_Hoge.h | Fake_a_Hoge.cpp | yes |  | 1 | 0 |
| b::Hoge | b/Hoge.h | Mock_b_Hoge.h | Fake_b_Hoge.cpp | yes |  | 1 | 0 |

## Diagnostics

No diagnostics.

## Validation Commands

No validation commands recorded.

## Unsupported Items

No unsupported items.
