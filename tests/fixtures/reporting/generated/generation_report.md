# mockfakegen generation report

## Summary

| Classes | Link-ready classes | Not link-ready classes | Generated methods | Unsupported items | Warnings | Errors |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 1 | 1 | 2 | 2 | 0 |

## Link Replacement Notice

Do not link generated `FakeXXX.cpp` files together with the corresponding product `.cpp` files in the same test target. Link each generated fake source instead of the product implementation it replaces.

## Generated Classes

| Class | Source header | Mock header | Fake source | Link ready | Link-readiness reason | Generated methods | Unsupported items |
|---|---|---|---|---|---|---:|---:|
| sample::Service | Service.h | MockService.h | FakeService.cpp | no | unsupported items remain | 1 | 2 |

## Unsupported Items

| Header | Class | Member | Reason | Suggested action |
|---|---|---|---|---|
| Service.h | sample::Service | sample::Service::Convert | function template member is not supported | exclude this member or provide a hand-authored mock |
| Service.h | sample::Service | sample::Service::operator+= | overloaded operator is not supported | exclude this member or provide a hand-authored mock |
