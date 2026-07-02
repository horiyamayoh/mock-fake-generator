# mockfakegen generation report

## Summary

| Classes | Link-ready classes | Not link-ready classes | Generated methods | Unsupported items | Diagnostics | Info | Warnings | Errors | Validation commands |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 1 | 2 | 3 | 3 | 0 | 3 | 0 | 0 |

Registry mode: `thread-local` - per-thread mock stack; use when product code calls fakes on the same thread as the scoped mock.

Fallback policy: `abort`.

## Link Replacement Notice

Do not link generated `FakeXXX.cpp` files together with the corresponding product `.cpp` files in the same test target. Link each generated fake source instead of the product implementation it replaces.

Compile validation checks generated headers and fake sources as separate translation units; it does not prove that your test target performs correct link substitution. Use `--validate link` to link a smoke executable with generated fakes and gMock where practical.

Usable fake sources for build-system integration:

- none; no class is link-ready.

## Generated Classes

| Class | Source header | Parse mode | Generation mode | Mock header | Fake source | Link ready | Link-readiness reason | Generated methods | Unsupported items |
|---|---|---|---|---|---|---|---|---:|---:|
| negative::BrokenInterface | Interface.h | synthetic-tu | interface-mock | MockBrokenInterface.h |  | no | unsupported items remain: interface_construct, non_public_method | 2 | 3 |

## Diagnostics

| Severity | Component | Code | Kind | Path | Class | Member | Message | Suggested action | Command | Stderr summary | Validation artifact |
|---|---|---|---|---|---|---|---|---|---|---|---|
| warning | clang | unsupported_interface_construct | interface_construct | Interface.h | negative::BrokenInterface | negative::BrokenInterface::BrokenInterface(int) | interface mock mode only supports public defaulted constructors | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_interface_construct | interface_construct | Interface.h | negative::BrokenInterface | negative::BrokenInterface::~BrokenInterface() | interface destructor must be virtual | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_non_public_method | non_public_method | Interface.h | negative::BrokenInterface | negative::BrokenInterface::ProtectedPure() | only public interface methods are generated | exclude this member or provide a hand-authored mock |  |  |  |

## Validation Commands

No validation commands recorded.

## Unsupported Items

| Header | Class | Member | Reason | Suggested action |
|---|---|---|---|---|
| Interface.h | negative::BrokenInterface | negative::BrokenInterface::BrokenInterface(int) | interface mock mode only supports public defaulted constructors | exclude this member or provide a hand-authored mock |
| Interface.h | negative::BrokenInterface | negative::BrokenInterface::ProtectedPure() | only public interface methods are generated | exclude this member or provide a hand-authored mock |
| Interface.h | negative::BrokenInterface | negative::BrokenInterface::~BrokenInterface() | interface destructor must be virtual | exclude this member or provide a hand-authored mock |
