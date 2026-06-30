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

	void GeneratesStaticMethodForwarding()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/static_method/product";
		const auto generated_dir = source_dir / "tests/fixtures/static_method/generated";
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = product_dir / "Hoge.h",
			.project_root = product_dir,
		});
		Expect(parse_result.success, "static method fixture should parse");

		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		const auto extraction =
			mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
		Expect(extraction.classes.size() == 1U, "static method class should be extracted");

		const auto& class_model = extraction.classes[0];
		Expect(class_model.mock_methods.size() == 2U, "static fixture should generate two methods");
		Expect(class_model.mock_methods[0].name == "GetCount",
			   "static method should keep declaration order");
		Expect(class_model.mock_methods[0].is_static, "static method should be flagged");
		Expect(!class_model.mock_methods[1].is_static, "instance method should not be flagged");

		const auto generated = mockfakegen::GenerateMinimalMockFake(class_model);
		for (const auto& file : generated)
		{
			Expect(!Contains(file.content, "ket::"), "generated file should not contain ket::");
			const auto path = generated_dir / file.relative_path;
			const auto expected = ReadText(path);
			mockfakegen_fixture::ExpectGoldenTextEqual(file.content, expected, path);
		}
	}
} // namespace

int main()
{
	GeneratesStaticMethodForwarding();
	return 0;
}
