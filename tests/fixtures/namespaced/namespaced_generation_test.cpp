#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

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

	void ExpectEqual(std::string_view actual,
					 std::string_view expected,
					 const std::filesystem::path& path)
	{
		if (actual == expected)
		{
			return;
		}

		std::cerr << "generated content mismatch: " << path.generic_string() << '\n';
		std::cerr << "expected:\n" << expected << "\nactual:\n" << actual << '\n';
		std::exit(1);
	}

	[[nodiscard]] std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		Expect(stream.good(), "fixture file should be readable");
		return buffer.str();
	}

	void GeneratesNamespacedMockAndFake()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/namespaced/product";
		const auto generated_dir = source_dir / "tests/fixtures/namespaced/generated";
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = product_dir / "Hoge.h",
			.project_root = product_dir,
		});
		Expect(parse_result.success, "namespaced fixture should parse");

		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		const auto extraction =
			mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
		Expect(extraction.classes.size() == 1U, "namespaced class should be extracted");
		Expect(extraction.classes[0].qualified_name == "app::core::Hoge",
			   "qualified name should include nested namespace");
		Expect(extraction.classes[0].namespaces.size() == 2U, "namespace parts should be stored");

		const auto generated = mockfakegen::GenerateMinimalMockFake(extraction.classes[0]);
		for (const auto& file : generated)
		{
			const auto path = generated_dir / file.relative_path;
			const auto expected = ReadText(path);
			ExpectEqual(file.content, expected, path);
		}
	}
} // namespace

int main()
{
	GeneratesNamespacedMockAndFake();
	return 0;
}
