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

	void GeneratesWithoutDefaultArguments()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/default_argument/product";
		const auto generated_dir = source_dir / "tests/fixtures/default_argument/generated";
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = product_dir / "Hoge.h",
			.project_root = product_dir,
		});
		Expect(parse_result.success, "default argument fixture should parse");

		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		const auto extraction =
			mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
		Expect(extraction.classes.size() == 1U, "default argument class should be extracted");

		const auto& class_model = extraction.classes[0];
		Expect(class_model.mock_methods.size() == 2U,
			   "methods with default arguments should remain supported");
		Expect(!class_model.mock_methods[0].parameters[0].has_default_argument,
			   "non-defaulted parameter should be marked without default argument");
		Expect(class_model.mock_methods[0].parameters[1].has_default_argument,
			   "Open flags parameter should preserve default argument presence");
		Expect(class_model.mock_methods[1].parameters[0].has_default_argument,
			   "Retry count parameter should preserve default argument presence");
		Expect(class_model.mock_methods[1].parameters[1].has_default_argument,
			   "Retry label parameter should preserve default argument presence");

		const auto generated = mockfakegen::GenerateMinimalMockFake(class_model);
		for (const auto& file : generated)
		{
			Expect(!Contains(file.content, "= 0"),
				   "generated output should not contain numeric default expressions");
			Expect(!Contains(file.content, "= 3"),
				   "generated output should not contain retry count default expression");
			Expect(!Contains(file.content, "retry-default"),
				   "generated output should not contain string default expression");

			const auto path = generated_dir / file.relative_path;
			const auto expected = ReadText(path);
			mockfakegen_fixture::ExpectGoldenTextEqual(file.content, expected, path);
		}
	}
} // namespace

int main()
{
	GeneratesWithoutDefaultArguments();
	return 0;
}
