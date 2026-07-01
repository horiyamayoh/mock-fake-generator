# mockfakegen generation report

## Summary

| Classes | Link-ready classes | Not link-ready classes | Generated methods | Unsupported items | Diagnostics | Info | Warnings | Errors | Validation commands |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 1 | 1 | 2 | 2 | 0 | 2 | 0 | 0 |

Fallback policy: `abort`.

## Link Replacement Notice

Do not link generated `FakeXXX.cpp` files together with the corresponding product `.cpp` files in the same test target. Link each generated fake source instead of the product implementation it replaces.

## Generated Classes

| Class | Source header | Parse mode | Generation mode | Mock header | Fake source | Link ready | Link-readiness reason | Generated methods | Unsupported items |
|---|---|---|---|---|---|---|---|---:|---:|
| sample::Service | Service.h | synthetic-tu | link-replacement | MockService.h | FakeService.cpp | no | unsupported items remain: function_template, overloaded_operator | 1 | 2 |

## Diagnostics

| Severity | Component | Code | Kind | Path | Class | Member | Message | Suggested action | Command | Stderr summary | Validation artifact |
|---|---|---|---|---|---|---|---|---|---|---|---|
| warning | clang | unsupported_function_template | function_template | Service.h | sample::Service | sample::Service::Convert | function template member is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_overloaded_operator | overloaded_operator | Service.h | sample::Service | sample::Service::operator+= | overloaded operator is not supported | exclude this member or provide a hand-authored mock |  |  |  |

## Validation Commands

No validation commands recorded.

## Unsupported Items

| Header | Class | Member | Reason | Suggested action |
|---|---|---|---|---|
| Service.h | sample::Service | sample::Service::Convert | function template member is not supported | exclude this member or provide a hand-authored mock |
| Service.h | sample::Service | sample::Service::operator+= | overloaded operator is not supported | exclude this member or provide a hand-authored mock |
