#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "clang/ClassExtractor.h"
#include "clang/SyntheticTuParser.h"
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

	[[nodiscard]] std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		Expect(stream.good(), "fixture file should be readable");
		return buffer.str();
	}

	[[nodiscard]] bool Contains(std::string_view text, std::string_view token)
	{
		return text.find(token) != std::string_view::npos;
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

	void GeneratesHogeFromAst()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/hoge/product";
		const auto generated_dir = source_dir / "tests/fixtures/hoge/generated";
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = product_dir / "Hoge.h",
			.project_root = product_dir,
		});

		Expect(parse_result.success, "Hoge.h should parse through synthetic TU");
		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};

		const auto extraction =
			mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
		Expect(extraction.classes.size() == 1U, "Hoge class should be extracted");
		const auto& class_model = extraction.classes[0];
		Expect(class_model.qualified_name == "Hoge", "Hoge qualified name should be extracted");
		Expect(class_model.mock_methods.size() == 3U, "three public methods should be extracted");
		Expect(class_model.mock_methods[0].name == "Initialize", "Initialize should keep order");
		Expect(class_model.mock_methods[0].parameters.size() == 2U,
			   "Initialize parameters should be extracted");
		Expect(class_model.mock_methods[0].parameters[1].type_spelling == "char**",
			   "array parameter should be generated as char**");
		Expect(class_model.mock_methods[1].name == "Finalize", "Finalize should keep order");
		Expect(class_model.mock_methods[2].name == "DoSomething", "DoSomething should keep order");

		const auto generated = mockfakegen::GenerateMinimalMockFake(class_model);
		Expect(generated.size() == 3U, "AST-driven generation should produce three files");

		for (const auto& file : generated)
		{
			const auto expected = ReadText(generated_dir / file.relative_path);
			Expect(file.content == expected,
				   "AST-driven generated output should match golden file");
			Expect(!Contains(file.content, "ket::"),
				   "generated output should not contain ket namespace");
			Expect(!Contains(file.content, "#include \"ket_"),
				   "generated output should not include quoted ket headers");
			Expect(!Contains(file.content, "#include <ket_"),
				   "generated output should not include angle ket headers");
		}

		const auto& mock_header = FindFile(generated, "MockHoge.h");
		Expect(Contains(mock_header.content, "MOCK_METHOD(bool, Initialize, (int, char**), ());"),
			   "AST-driven mock should include Initialize");
		const auto& fake_source = FindFile(generated, "FakeHoge.cpp");
		Expect(Contains(fake_source.content, "bool Hoge::Initialize(int argc, char** argv)"),
			   "AST-driven fake should include Initialize definition");
	}
} // namespace

int main()
{
	GeneratesHogeFromAst();
	return 0;
}
