#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "generator/CodeGenerator.h"

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "EXPECTATION FAILED: " << message << '\n';
			std::exit(1);
		}
	}

	void ExpectEqual(std::string_view actual, std::string_view expected, const char* message)
	{
		if (actual == expected)
		{
			return;
		}

		std::cerr << "EXPECTATION FAILED: " << message << '\n';
		std::cerr << "expected:\n" << expected << "\nactual:\n" << actual << '\n';
		std::exit(1);
	}

	[[nodiscard]] bool Contains(std::string_view text, std::string_view token)
	{
		return text.find(token) != std::string_view::npos;
	}

	[[nodiscard]] mockfakegen::SimpleClassModel HogeModel()
	{
		return mockfakegen::SimpleClassModel{
			.name = "Hoge",
			.namespaces = {},
			.header_include = "Hoge.h",
			.mock_header_name = {},
			.fake_source_name = {},
			.methods =
				{
					mockfakegen::SimpleMethodModel{
						.return_type = "bool",
						.name = "Initialize",
						.parameters =
							{
								mockfakegen::SimpleParameterModel{.type = "int", .name = "argc"},
								mockfakegen::SimpleParameterModel{.type = "char**", .name = "argv"},
							},
					},
					mockfakegen::SimpleMethodModel{
						.return_type = "void",
						.name = "Finalize",
						.parameters = {},
					},
					mockfakegen::SimpleMethodModel{
						.return_type = "bool",
						.name = "DoSomething",
						.parameters = {},
					},
				},
		};
	}

	[[nodiscard]] mockfakegen::HeaderModel HeaderNamed(std::string include_spelling)
	{
		mockfakegen::HeaderModel header;
		header.include_spelling = std::move(include_spelling);
		return header;
	}

	[[nodiscard]] mockfakegen::MethodModel MethodNamed(std::string name)
	{
		mockfakegen::MethodModel method;
		method.name = std::move(name);
		method.return_type_spelling = "void";
		method.gmock_return_type_spelling = "void";
		method.signature_for_report = method.name + "()";
		return method;
	}

	[[nodiscard]] mockfakegen::ClassModel NamespacedHogeModel(std::string namespace_name,
															  std::string include_spelling)
	{
		const auto qualified_name = namespace_name + "::Hoge";
		return mockfakegen::ClassModel{
			.name = "Hoge",
			.qualified_name = qualified_name,
			.namespaces = {std::move(namespace_name)},
			.mock_name = "MockHoge",
			.mock_header_name = "MockHoge.h",
			.fake_source_name = "FakeHoge.cpp",
			.source_header = HeaderNamed(std::move(include_spelling)),
			.mock_methods =
				{
					MethodNamed("Run"),
				},
			.fake_methods =
				{
					MethodNamed("Run"),
				},
			.unsupported_items = {},
		};
	}

	[[nodiscard]] mockfakegen::UnsupportedItem UnsupportedFunctionTemplate()
	{
		mockfakegen::UnsupportedItem unsupported;
		unsupported.kind = "function_template";
		unsupported.class_name = "alpha::Alpha";
		unsupported.name = "Convert";
		unsupported.member_signature = "alpha::Alpha::Convert";
		unsupported.reason = "function template member is not supported";
		unsupported.suggested_action = "exclude this member or provide a hand-authored mock";
		return unsupported;
	}

	[[nodiscard]] mockfakegen::ClassModel ReportAlphaModel()
	{
		return mockfakegen::ClassModel{
			.name = "Alpha",
			.qualified_name = "alpha::Alpha",
			.namespaces = {"alpha"},
			.mock_name = "MockAlpha",
			.mock_header_name = "MockAlpha.h",
			.fake_source_name = "FakeAlpha.cpp",
			.source_header = HeaderNamed("include/Alpha.h"),
			.mock_methods =
				{
					MethodNamed("Get"),
					MethodNamed("Set"),
				},
			.fake_methods = {},
			.unsupported_items =
				{
					UnsupportedFunctionTemplate(),
				},
		};
	}

	[[nodiscard]] mockfakegen::ClassModel ReportBetaModel()
	{
		return mockfakegen::ClassModel{
			.name = "Beta",
			.qualified_name = "zeta::Beta",
			.namespaces = {"zeta"},
			.mock_name = "MockBeta",
			.mock_header_name = "MockBeta.h",
			.fake_source_name = "FakeBeta.cpp",
			.source_header = HeaderNamed("include/Beta.h"),
			.mock_methods =
				{
					MethodNamed("Run"),
				},
			.fake_methods = {},
			.unsupported_items = {},
		};
	}

	[[nodiscard]] mockfakegen::ClassModel SpecialMemberModel()
	{
		mockfakegen::ParameterModel value;
		value.type_spelling = "int";
		value.gmock_type_spelling = "int";
		value.generated_name = "value";

		return mockfakegen::ClassModel{
			.name = "Special",
			.qualified_name = "sample::Special",
			.namespaces = {"sample"},
			.mock_name = "MockSpecial",
			.mock_header_name = "MockSpecial.h",
			.fake_source_name = "FakeSpecial.cpp",
			.source_header = HeaderNamed("Special.h"),
			.mock_methods =
				{
					MethodNamed("Touch"),
				},
			.fake_methods =
				{
					MethodNamed("Touch"),
				},
			.fake_constructors =
				{
					mockfakegen::ConstructorModel{
						.parameters = {value},
						.member_initializers = {"value_{}"},
						.signature_for_report = "sample::Special::Special(int)",
					},
				},
			.fake_destructors =
				{
					mockfakegen::DestructorModel{
						.signature_for_report = "sample::Special::~Special()",
					},
				},
			.unsupported_items = {},
		};
	}

	[[nodiscard]] mockfakegen::ClassModel EscapingReportModel()
	{
		auto unsupported = UnsupportedFunctionTemplate();
		unsupported.class_name = "alpha::Escaping";
		unsupported.member_signature = "alpha::Escaping::Convert";
		unsupported.reason = "reason with | pipe\nand newline";
		unsupported.suggested_action = "use | manual mock";

		return mockfakegen::ClassModel{
			.name = "Escaping",
			.qualified_name = "alpha::Escaping",
			.namespaces = {"alpha"},
			.mock_name = "MockEscaping",
			.mock_header_name = "MockEscaping.h",
			.fake_source_name = "FakeEscaping.cpp",
			.source_header = HeaderNamed("include/Quote\"Back\\Slash.h"),
			.mock_methods = {},
			.fake_methods = {},
			.unsupported_items = {unsupported},
		};
	}

	[[nodiscard]] const mockfakegen::GeneratedFile&
	FindFile(const std::vector<mockfakegen::GeneratedFile>& files, std::string_view path)
	{
		for (const auto& file : files)
		{
			if (file.relative_path.generic_string() == path)
			{
				return file;
			}
		}

		std::cerr << "missing generated file: " << path << '\n';
		std::exit(1);
	}

	void GeneratesMinimalHogeFiles()
	{
		const auto files = mockfakegen::GenerateMinimalMockFake(HogeModel());

		Expect(files.size() == 3U, "minimal generator should produce three files");
		Expect(files[0].relative_path == "FakeHoge.cpp",
			   "files should be deterministically sorted");
		Expect(files[1].relative_path == "MockFakeRuntime.h", "runtime should be sorted by path");
		Expect(files[2].relative_path == "MockHoge.h", "mock should be sorted by path");

		const auto& mock = FindFile(files, "MockHoge.h");
		Expect(mock.kind == mockfakegen::GeneratedFileKind::MockHeader, "mock kind should be set");
		Expect(mock.source_class->qualified_name == "Hoge", "mock source class should be set");
		Expect(mock.source_class->generated_method_count == 3U,
			   "mock source class should record generated method count");
		Expect(Contains(mock.content, "#include <gmock/gmock.h>"), "mock should include gMock");
		Expect(Contains(mock.content, "#include \"Hoge.h\""), "mock should include source header");
		Expect(Contains(mock.content, "#include \"MockFakeRuntime.h\""),
			   "mock should include runtime header");
		Expect(Contains(mock.content, "class MockHoge"), "mock class should be generated");
		Expect(Contains(mock.content, "MOCK_METHOD(bool, Initialize, (int, char**), ());"),
			   "Initialize mock method should be generated");
		Expect(Contains(mock.content, "MOCK_METHOD(void, Finalize, (), ());"),
			   "Finalize mock method should be generated");
		Expect(Contains(mock.content, "using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;"),
			   "scoped mock alias should be generated");

		const auto& fake = FindFile(files, "FakeHoge.cpp");
		Expect(fake.kind == mockfakegen::GeneratedFileKind::FakeSource, "fake kind should be set");
		Expect(fake.source_class->generated_method_count == 3U,
			   "fake source class should record generated method count");
		Expect(Contains(fake.content, "#include \"Hoge.h\""), "fake should include source header");
		Expect(Contains(fake.content, "#include \"MockHoge.h\""),
			   "fake should include mock header");
		Expect(Contains(fake.content, "bool Hoge::Initialize(int argc, char** argv)"),
			   "Initialize fake signature should be generated");
		Expect(Contains(fake.content, "return mock->Initialize(argc, argv);"),
			   "Initialize fake should forward arguments");
		Expect(
			Contains(
				fake.content,
				"return ::mockfake::MissingMockReturn<bool>(\"Hoge::Initialize(int, char**)\");"),
			"Initialize fake should call missing mock fallback");
		Expect(Contains(fake.content, "void Hoge::Finalize()"),
			   "Finalize fake should be generated");
		Expect(Contains(fake.content, "mock->Finalize();"), "Finalize fake should forward");

		const auto& runtime = FindFile(files, "MockFakeRuntime.h");
		Expect(runtime.kind == mockfakegen::GeneratedFileKind::RuntimeHeader,
			   "runtime kind should be set");
		Expect(!runtime.source_class.has_value(), "runtime should not have source class metadata");
	}

	void GeneratedOutputDoesNotContainKetTokens()
	{
		const auto files = mockfakegen::GenerateMinimalMockFake(HogeModel());
		for (const auto& file : files)
		{
			Expect(!Contains(file.content, "ket::"), "generated file should not contain ket::");
			Expect(!Contains(file.content, "#include \"ket_"),
				   "generated file should not include quoted ket headers");
			Expect(!Contains(file.content, "#include <ket_"),
				   "generated file should not include angle ket headers");
		}
	}

	void GeneratesManifestJson()
	{
		const std::vector classes = {ReportBetaModel(), ReportAlphaModel()};

		const auto manifest = mockfakegen::GenerateManifestJson(classes);

		Expect(manifest.relative_path == "manifest.json", "manifest path should be stable");
		Expect(manifest.kind == mockfakegen::GeneratedFileKind::Manifest,
			   "manifest kind should be set");
		Expect(!manifest.source_class.has_value(), "manifest should not be tied to one class");
		ExpectEqual(manifest.content,
					"{\n"
					"  \"summary\": {\n"
					"    \"classes\": 2,\n"
					"    \"link_ready_classes\": 1,\n"
					"    \"not_link_ready_classes\": 1,\n"
					"    \"generated_methods\": 3,\n"
					"    \"unsupported_items\": 1,\n"
					"    \"diagnostic_summary\": {\n"
					"      \"warnings\": 1,\n"
					"      \"errors\": 0\n"
					"    }\n"
					"  },\n"
					"  \"classes\": [\n"
					"    {\n"
					"      \"qualified_name\": \"alpha::Alpha\",\n"
					"      \"mock_header\": \"MockAlpha.h\",\n"
					"      \"fake_source\": \"FakeAlpha.cpp\",\n"
					"      \"source_header\": \"include/Alpha.h\",\n"
					"      \"generated_methods\": 2,\n"
					"      \"unsupported_methods\": 1,\n"
					"      \"unsupported_items\": 1,\n"
					"      \"link_ready\": false,\n"
					"      \"link_readiness_reasons\": [\n"
					"        \"unsupported items remain\"\n"
					"      ],\n"
					"      \"diagnostic_summary\": {\n"
					"        \"warnings\": 1,\n"
					"        \"errors\": 0\n"
					"      }\n"
					"    },\n"
					"    {\n"
					"      \"qualified_name\": \"zeta::Beta\",\n"
					"      \"mock_header\": \"MockBeta.h\",\n"
					"      \"fake_source\": \"FakeBeta.cpp\",\n"
					"      \"source_header\": \"include/Beta.h\",\n"
					"      \"generated_methods\": 1,\n"
					"      \"unsupported_methods\": 0,\n"
					"      \"unsupported_items\": 0,\n"
					"      \"link_ready\": true,\n"
					"      \"link_readiness_reasons\": [],\n"
					"      \"diagnostic_summary\": {\n"
					"        \"warnings\": 0,\n"
					"        \"errors\": 0\n"
					"      }\n"
					"    }\n"
					"  ]\n"
					"}\n",
					"manifest content should be deterministic");
	}

	void ResolvesQualifiedFilenameCollisions()
	{
		const std::vector classes = {
			NamespacedHogeModel("b", "include/b/Hoge.h"),
			NamespacedHogeModel("a", "include/a/Hoge.h"),
		};

		const auto files = mockfakegen::GenerateMockFakeProject(classes);

		const auto& a_mock = FindFile(files, "Mock_a_Hoge.h");
		const auto& b_mock = FindFile(files, "Mock_b_Hoge.h");
		const auto& a_fake = FindFile(files, "Fake_a_Hoge.cpp");
		const auto& b_fake = FindFile(files, "Fake_b_Hoge.cpp");
		Expect(a_mock.kind == mockfakegen::GeneratedFileKind::MockHeader,
			   "a::Hoge mock should use qualified filename");
		Expect(b_mock.kind == mockfakegen::GeneratedFileKind::MockHeader,
			   "b::Hoge mock should use qualified filename");
		Expect(a_fake.kind == mockfakegen::GeneratedFileKind::FakeSource,
			   "a::Hoge fake should use qualified filename");
		Expect(b_fake.kind == mockfakegen::GeneratedFileKind::FakeSource,
			   "b::Hoge fake should use qualified filename");
		Expect(Contains(a_fake.content, "#include \"Mock_a_Hoge.h\""),
			   "a::Hoge fake should include resolved mock header");
		Expect(Contains(b_fake.content, "#include \"Mock_b_Hoge.h\""),
			   "b::Hoge fake should include resolved mock header");

		const auto& all_mocks = FindFile(files, "AllMocks.h");
		Expect(Contains(all_mocks.content, "#include \"Mock_a_Hoge.h\""),
			   "AllMocks should include a::Hoge resolved header");
		Expect(Contains(all_mocks.content, "#include \"Mock_b_Hoge.h\""),
			   "AllMocks should include b::Hoge resolved header");

		const auto& manifest = FindFile(files, "manifest.json");
		Expect(Contains(manifest.content, "\"filename_collision\""),
			   "manifest should record collision entries");
		Expect(Contains(manifest.content, "\"policy\": \"qualified-filename\""),
			   "manifest should record collision policy");
		Expect(Contains(manifest.content, "\"default_mock_header\": \"MockHoge.h\""),
			   "manifest should record default mock header");
		Expect(Contains(manifest.content, "\"resolved_mock_header\": \"Mock_a_Hoge.h\""),
			   "manifest should record resolved a::Hoge mock header");
		Expect(Contains(manifest.content, "\"resolved_fake_source\": \"Fake_b_Hoge.cpp\""),
			   "manifest should record resolved b::Hoge fake source");
	}

	void KeepsShortFilenamesWithoutCollision()
	{
		const std::vector classes = {
			NamespacedHogeModel("app", "include/app/Hoge.h"),
		};

		const auto files = mockfakegen::GenerateMockFakeProject(classes);

		const auto& mock = FindFile(files, "MockHoge.h");
		const auto& fake = FindFile(files, "FakeHoge.cpp");
		Expect(mock.kind == mockfakegen::GeneratedFileKind::MockHeader,
			   "non-colliding mock should keep short filename");
		Expect(fake.kind == mockfakegen::GeneratedFileKind::FakeSource,
			   "non-colliding fake should keep short filename");
		const auto& manifest = FindFile(files, "manifest.json");
		Expect(!Contains(manifest.content, "\"filename_collision\""),
			   "manifest should omit collision section when filenames are unique");
	}

	void ProjectOptionsSelectGlobalMutexRuntime()
	{
		const std::vector classes = {ReportBetaModel()};

		const auto files = mockfakegen::GenerateMockFakeProject(
			classes,
			mockfakegen::ProjectGenerationOptions{
				.registry_mode = mockfakegen::RegistryMode::GlobalMutex,
			});

		const auto& runtime = FindFile(files, "MockFakeRuntime.h");
		Expect(Contains(runtime.content, "#include <mutex>"),
			   "global-mutex project should generate mutex runtime");
		Expect(!Contains(runtime.content, "thread_local"),
			   "global-mutex project should not generate thread-local runtime");
	}

	void ProjectOptionsSelectSharedOwnerRuntimeAndApi()
	{
		const std::vector classes = {ReportBetaModel()};

		const auto files = mockfakegen::GenerateMockFakeProject(
			classes,
			mockfakegen::ProjectGenerationOptions{
				.registry_mode = mockfakegen::RegistryMode::SharedOwner,
			});

		const auto& runtime = FindFile(files, "MockFakeRuntime.h");
		Expect(Contains(runtime.content, "#include <memory>"),
			   "shared-owner project should generate shared runtime");
		Expect(Contains(runtime.content, "ScopedSharedMock"),
			   "shared-owner runtime should expose shared scope API");

		const auto& mock = FindFile(files, "MockBeta.h");
		Expect(Contains(mock.content,
						"using ScopedMockBeta = ::mockfake::ScopedSharedMock<MockBeta>;"),
			   "shared-owner mock alias should use shared scope API");

		const auto& fake = FindFile(files, "FakeBeta.cpp");
		Expect(Contains(fake.content, "if (auto mock = ::mockfake::CurrentMock<MockBeta>())"),
			   "shared-owner fake should retain a shared_ptr copy while forwarding");
		Expect(!Contains(fake.content, "if (auto* mock = ::mockfake::CurrentMock<MockBeta>())"),
			   "shared-owner fake should not expect a raw pointer registry");
	}

	void GeneratesSpecialMemberFakes()
	{
		const std::vector classes = {SpecialMemberModel()};

		const auto files = mockfakegen::GenerateMockFakeProject(classes);

		const auto& fake = FindFile(files, "FakeSpecial.cpp");
		Expect(Contains(fake.content, "Special::Special(int value)\n\t\t: value_{}"),
			   "constructor fake should initialize safe members");
		Expect(Contains(fake.content, "(void)value;"),
			   "constructor fake should mark ignored parameters");
		Expect(Contains(fake.content, "Special::~Special()"),
			   "destructor fake should be generated");
		Expect(Contains(fake.content, "void Special::Touch()"),
			   "normal fake methods should still be generated");
	}

	void CMakeFragmentUsesOnlyLinkReadyFakeSources()
	{
		const auto files = mockfakegen::GenerateMockFakeProject(
			std::vector<mockfakegen::ClassModel>{ReportBetaModel(), ReportAlphaModel()});

		const auto& fragment = FindFile(files, "CMakeLists.fragment.cmake");
		Expect(Contains(fragment.content, "FakeBeta.cpp"),
			   "link-ready fake should appear in usable source list");
		Expect(!Contains(fragment.content, "FakeAlpha.cpp"),
			   "not link-ready fake should be omitted from usable source list");
	}

	void GeneratesGenerationReport()
	{
		const std::vector classes = {ReportBetaModel(), ReportAlphaModel()};

		const auto report = mockfakegen::GenerateGenerationReport(classes);

		Expect(report.relative_path == "generation_report.md", "report path should be stable");
		Expect(report.kind == mockfakegen::GeneratedFileKind::Report, "report kind should be set");
		Expect(Contains(report.content, "| 2 | 1 | 1 | 3 | 1 | 1 | 0 |"),
			   "report should include diagnostic summary");
		Expect(Contains(report.content, "`FakeXXX.cpp`"), "report should include link warning");
		Expect(Contains(report.content,
						"| alpha::Alpha | include/Alpha.h | MockAlpha.h | FakeAlpha.cpp | no | "
						"unsupported items remain | 2 | 1 |"),
			   "report should include generated class row");
		Expect(Contains(report.content,
						"| include/Alpha.h | alpha::Alpha | alpha::Alpha::Convert | function "
						"template member is not supported | exclude this member or provide a "
						"hand-authored mock |"),
			   "report should include unsupported item suggested action");
	}

	void EscapesReportWriterText()
	{
		const std::vector classes = {EscapingReportModel()};

		const auto manifest = mockfakegen::GenerateManifestJson(classes);
		const auto report = mockfakegen::GenerateGenerationReport(classes);

		Expect(
			Contains(manifest.content, "\"source_header\": \"include/Quote\\\"Back\\\\Slash.h\""),
			"manifest should JSON-escape quote and backslash characters");
		Expect(Contains(report.content,
						"| include/Quote\"Back\\Slash.h | alpha::Escaping | "
						"alpha::Escaping::Convert | reason with \\| pipe and newline | use "
						"\\| manual mock |"),
			   "report should escape markdown table separators and flatten newlines");
	}
} // namespace

int main()
{
	GeneratesMinimalHogeFiles();
	GeneratedOutputDoesNotContainKetTokens();
	GeneratesManifestJson();
	ResolvesQualifiedFilenameCollisions();
	KeepsShortFilenamesWithoutCollision();
	ProjectOptionsSelectGlobalMutexRuntime();
	ProjectOptionsSelectSharedOwnerRuntimeAndApi();
	GeneratesSpecialMemberFakes();
	CMakeFragmentUsesOnlyLinkReadyFakeSources();
	GeneratesGenerationReport();
	EscapesReportWriterText();
	return 0;
}
