# mockfakegen generation report

## Summary

| Classes | Link-ready classes | Not link-ready classes | Generated methods | Unsupported items | Diagnostics | Info | Warnings | Errors | Validation commands |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 3 | 0 | 3 | 6 | 28 | 33 | 0 | 33 | 0 | 0 |

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
| negative::UnsafeSpecial | Unsupported.h | synthetic-tu | link-replacement | MockUnsafeSpecial.h | FakeUnsafeSpecial.cpp | no | unsupported items remain: constructor, destructor | 1 | 2 |
| negative::UnsafeStaticData | Unsupported.h | synthetic-tu | link-replacement | MockUnsafeStaticData.h | FakeUnsafeStaticData.cpp | no | unsupported items remain: static_data_member | 1 | 6 |
| negative::UnsupportedSurface | Unsupported.h | synthetic-tu | link-replacement | MockUnsupportedSurface.h | FakeUnsupportedSurface.cpp | no | unsupported items remain: function_template, inline_body, trailing_return_type, auto_return, decltype_auto_return, constexpr_method, consteval_method, attributed_type, conversion_operator, defaulted_method, overloaded_operator, pure_virtual_method, volatile_method, macro_origin, private_nested_type, non_public_method | 4 | 20 |

## Diagnostics

| Severity | Component | Code | Kind | Path | Class | Member | Message | Suggested action | Command | Stderr summary | Validation artifact |
|---|---|---|---|---|---|---|---|---|---|---|---|
| warning | clang | unsupported_class_template | class_template | Unsupported.h | negative::Box | negative::Box | class template is not supported by link replacement fake generation | exclude it or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_class_template | class_template | Unsupported.h | negative::PartialBox | negative::PartialBox | class template is not supported by link replacement fake generation | exclude it or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_class_template_specialization | class_template_partial_specialization | Unsupported.h | negative::PartialBox | negative::PartialBox | partial class template specialization is not supported | exclude it or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_class_template_specialization | class_template_specialization | Unsupported.h | negative::Box | negative::Box | explicit class template specialization is not supported | exclude it or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_consteval_method | consteval_method | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::ConstevalValue | consteval method is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_constexpr_method | constexpr_method | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::ConstexprValue | constexpr method is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_constructor | constructor | Unsupported.h | negative::UnsafeSpecial | negative::UnsafeSpecial::UnsafeSpecial | reference member 'ref_' cannot be safely default-initialized | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_conversion_operator | conversion_operator | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::operator bool | conversion operator is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_defaulted_method | defaulted_method | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::operator<=> | defaulted method is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_destructor | destructor | Unsupported.h | negative::UnsafeSpecial | negative::UnsafeSpecial::~UnsafeSpecial | destructor exception specification is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_function_template | function_template | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::Constrained | function template member is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_function_template | function_template | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::Convert | function template member is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_inline_body | inline_body | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::InlineBody | inline method body is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_inline_body | inline_body | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::OutOfClassInline | inline method body is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_macro_origin | macro_origin | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::FromMacro | macro-origin method declaration is not supported because source spelling may be unstable | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_nested_class | nested_class | Unsupported.h | negative::UnsupportedSurface::PublicNested | negative::UnsupportedSurface::PublicNested | nested class definitions are not generated as independent targets | generate the enclosing class or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_non_public_method | non_public_method | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::PrivateMethod | only public methods are generated | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_non_public_method | non_public_method | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::ProtectedMethod | only public methods are generated | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_overloaded_operator | overloaded_operator | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::operator+= | overloaded operator is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_private_nested_type | private_nested_type | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::HiddenParam | private nested type 'negative::UnsupportedSurface::PrivateToken' is not accessible from generated code | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_private_nested_type | private_nested_type | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::HiddenReturn | private nested type 'negative::UnsupportedSurface::PrivateToken' is not accessible from generated code | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_pure_virtual_method | pure_virtual_method | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::Abstract | pure virtual method requires interface mock mode and is not faked in normal link replacement mode | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_static_data_member | static_data_member | Unsupported.h | negative::UnsafeStaticData | negative::UnsafeStaticData::boot | constinit static data member requires an explicit initializer policy | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_static_data_member | static_data_member | Unsupported.h | negative::UnsafeStaticData | negative::UnsafeStaticData::initialized | static data member with in-class initializer is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_static_data_member | static_data_member | Unsupported.h | negative::UnsafeStaticData | negative::UnsafeStaticData::private_token | static data member 'private_token' uses a nested type that cannot be safely spelled outside the class | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_static_data_member | static_data_member | Unsupported.h | negative::UnsafeStaticData | negative::UnsafeStaticData::ref_value | reference static data member 'ref_value' cannot be safely default-initialized | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_static_data_member | static_data_member | Unsupported.h | negative::UnsafeStaticData | negative::UnsafeStaticData::tls_value | thread-local static data member is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_static_data_member | static_data_member | Unsupported.h | negative::UnsafeStaticData | negative::UnsafeStaticData::values | array static data member 'values' requires array declarator synthesis | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_type_spelling | attributed_type | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::NonNull | attributed type in method signature is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_type_spelling | auto_return | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::AutoValue | deduced auto return type is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_type_spelling | decltype_auto_return | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::Deduced | decltype(auto) return type is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_type_spelling | trailing_return_type | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::Trailing | trailing return type spelling is not supported | exclude this member or provide a hand-authored mock |  |  |  |
| warning | clang | unsupported_volatile_method | volatile_method | Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::Volatile | volatile method is not supported | exclude this member or provide a hand-authored mock |  |  |  |

## Validation Commands

No validation commands recorded.

## Unsupported Items

| Header | Class | Member | Reason | Suggested action |
|---|---|---|---|---|
| Unsupported.h | negative::UnsafeSpecial | negative::UnsafeSpecial::UnsafeSpecial | reference member 'ref_' cannot be safely default-initialized | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsafeSpecial | negative::UnsafeSpecial::~UnsafeSpecial | destructor exception specification is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsafeStaticData | negative::UnsafeStaticData::boot | constinit static data member requires an explicit initializer policy | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsafeStaticData | negative::UnsafeStaticData::initialized | static data member with in-class initializer is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsafeStaticData | negative::UnsafeStaticData::private_token | static data member 'private_token' uses a nested type that cannot be safely spelled outside the class | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsafeStaticData | negative::UnsafeStaticData::ref_value | reference static data member 'ref_value' cannot be safely default-initialized | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsafeStaticData | negative::UnsafeStaticData::tls_value | thread-local static data member is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsafeStaticData | negative::UnsafeStaticData::values | array static data member 'values' requires array declarator synthesis | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::Abstract | pure virtual method requires interface mock mode and is not faked in normal link replacement mode | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::AutoValue | deduced auto return type is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::ConstevalValue | consteval method is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::ConstexprValue | constexpr method is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::Constrained | function template member is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::Convert | function template member is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::Deduced | decltype(auto) return type is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::FromMacro | macro-origin method declaration is not supported because source spelling may be unstable | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::HiddenParam | private nested type 'negative::UnsupportedSurface::PrivateToken' is not accessible from generated code | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::HiddenReturn | private nested type 'negative::UnsupportedSurface::PrivateToken' is not accessible from generated code | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::InlineBody | inline method body is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::NonNull | attributed type in method signature is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::OutOfClassInline | inline method body is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::PrivateMethod | only public methods are generated | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::ProtectedMethod | only public methods are generated | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::Trailing | trailing return type spelling is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::Volatile | volatile method is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::operator bool | conversion operator is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::operator+= | overloaded operator is not supported | exclude this member or provide a hand-authored mock |
| Unsupported.h | negative::UnsupportedSurface | negative::UnsupportedSurface::operator<=> | defaulted method is not supported | exclude this member or provide a hand-authored mock |
