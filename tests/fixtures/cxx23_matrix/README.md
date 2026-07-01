# C++23 Coverage Matrix

This fixture records whether modern or complex declarations are generated or deliberately
unsupported. Update this table with every support change.

| Construct | Status | Evidence |
|---|---|---|
| Nested namespace `namespace app::v1` | generated | `cxx23_matrix_generation_test`, `cxx23_matrix_generated_smoke` |
| Using alias return / enum class parameter | generated | `Supported::CountItems` golden and smoke |
| Typedef return | generated | `Supported::Scale` golden and smoke |
| Static member function returning enum class | generated | `Supported::DefaultMode` golden and smoke |
| Friend declaration | ignored, no diagnostic | `Supported` fixture asserts no unsupported items |
| Anonymous namespace class | unsupported | `anonymous_namespace` assertion in `cxx23_matrix_generation_test` |
| Attributes and platform attributes | unsupported | `unsupported_diagnostics` report/manifest golden |
| Macro-origin declaration | unsupported | `unsupported_diagnostics` report/manifest golden |
| Trailing return type | unsupported | `unsupported_diagnostics` report/manifest golden |
| Plain `auto` return | unsupported | `auto_return` in `unsupported_diagnostics` report/manifest golden |
| `decltype(auto)` return | unsupported | `unsupported_diagnostics` report/manifest golden |
| Concepts / requires / constrained member template | unsupported as function template | `Constrained` in `unsupported_diagnostics` report/manifest golden |
| `consteval` member | unsupported | `unsupported_diagnostics` report/manifest golden |
| `constinit` static data | unsupported | `UnsafeStaticData::boot` in `unsupported_diagnostics` report/manifest golden |
| Explicit object parameter | compiler-dependent parse; unsupported when accepted | probe in `cxx23_matrix_generation_test` |
| Defaulted comparison | unsupported | `operator<=>` in `unsupported_diagnostics` report/manifest golden |
