#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "../GoldenDiff.h"
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

	void GeneratesGMockWrappedCommaTypes()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/comma_type/product";
		const auto generated_dir = source_dir / "tests/fixtures/comma_type/generated";
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = product_dir / "Hoge.h",
			.project_root = product_dir,
		});
		Expect(parse_result.success, "comma type fixture should parse");

		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		const auto extraction =
			mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
		Expect(extraction.classes.size() == 1U, "comma type class should be extracted");

		const auto& class_model = extraction.classes[0];
		Expect(class_model.mock_methods[0].gmock_return_type_spelling == "(std::pair<bool, int>)",
			   "pair return type should be wrapped for gMock");
		Expect(class_model.mock_methods[1].parameters[0].gmock_type_spelling ==
				   "(std::map<int, double>)",
			   "map parameter type should be wrapped for gMock");
		Expect(class_model.mock_methods[2].gmock_return_type_spelling ==
				   "(std::pair<int, std::pair<int, int>>)",
			   "nested comma return type should be wrapped for gMock");

		const auto generated = mockfakegen::GenerateMinimalMockFake(class_model);
		for (const auto& file : generated)
		{
			if (file.relative_path == "FakeHoge.cpp")
			{
				Expect(Contains(file.content, "std::pair<bool, int> Hoge::GetPair()"),
					   "fake signature should keep unwrapped return spelling");
				Expect(Contains(file.content, "void Hoge::SetMap(std::map<int, double> value)"),
					   "fake signature should keep unwrapped parameter spelling");
				Expect(!Contains(file.content, "(std::pair<bool, int>) Hoge::GetPair"),
					   "fake signature should not use gMock return wrapping");
				Expect(!Contains(file.content, "((std::map<int, double>))"),
					   "fake signature should not use gMock parameter wrapping");
			}

			const auto path = generated_dir / file.relative_path;
			const auto expected = ReadText(path);
			mockfakegen_fixture::ExpectGoldenTextEqual(file.content, expected, path);
		}
	}
} // namespace

int main()
{
	GeneratesGMockWrappedCommaTypes();
	return 0;
}
