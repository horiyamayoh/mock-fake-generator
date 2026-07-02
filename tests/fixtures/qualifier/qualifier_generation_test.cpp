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

	[[nodiscard]] bool HasUnsupportedKind(const mockfakegen::ClassModel& class_model,
										  std::string_view kind)
	{
		for (const auto& item : class_model.unsupported_items)
		{
			if (item.kind == kind)
			{
				return true;
			}
		}
		return false;
	}

	void GeneratesQualifiedMockAndFake()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/qualifier/product";
		const auto generated_dir = source_dir / "tests/fixtures/qualifier/generated";
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = product_dir / "Hoge.h",
			.project_root = product_dir,
		});
		Expect(parse_result.success, "qualifier fixture should parse");

		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		const auto extraction =
			mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
		Expect(extraction.classes.size() == 1U, "qualifier class should be extracted");

		const auto& class_model = extraction.classes[0];
		Expect(class_model.mock_methods.size() == 4U,
			   "four supported qualified methods should be generated");
		Expect(class_model.mock_methods[0].is_const, "const method should be flagged");
		Expect(class_model.mock_methods[1].is_noexcept, "noexcept method should be flagged");
		Expect(class_model.mock_methods[2].ref_qualifier == mockfakegen::RefQualifierKind::RValue,
			   "rvalue ref-qualified method should be flagged");
		Expect(class_model.mock_methods[3].is_const, "const ref-qualified method should be const");
		Expect(class_model.mock_methods[3].ref_qualifier == mockfakegen::RefQualifierKind::LValue,
			   "lvalue ref-qualified method should be flagged");
		Expect(HasUnsupportedKind(class_model, "conditional_noexcept"),
			   "conditional noexcept expression should be unsupported");
		Expect(HasUnsupportedKind(class_model, "volatile_method"),
			   "volatile method should remain unsupported");

		const auto generated = mockfakegen::GenerateMinimalMockFake(class_model);
		for (const auto& file : generated)
		{
			const auto path = generated_dir / file.relative_path;
			const auto expected = ReadText(path);
			mockfakegen_fixture::ExpectGoldenTextEqual(file.content, expected, path);
		}
	}
} // namespace

int main()
{
	GeneratesQualifiedMockAndFake();
	return 0;
}
