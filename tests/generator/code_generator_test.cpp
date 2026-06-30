#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
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
} // namespace

int main()
{
	GeneratesMinimalHogeFiles();
	GeneratedOutputDoesNotContainKetTokens();
	return 0;
}
