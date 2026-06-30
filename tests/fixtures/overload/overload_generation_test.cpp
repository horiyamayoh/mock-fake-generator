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

	void GeneratesOverloadedMockAndFake()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/overload/product";
		const auto generated_dir = source_dir / "tests/fixtures/overload/generated";
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = product_dir / "Hoge.h",
			.project_root = product_dir,
		});
		Expect(parse_result.success, "overload fixture should parse");

		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		const auto extraction =
			mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
		Expect(extraction.classes.size() == 1U, "overload class should be extracted");

		const auto& class_model = extraction.classes[0];
		Expect(class_model.mock_methods.size() == 2U,
			   "both overloads should be retained for mock generation");
		Expect(class_model.fake_methods.size() == 2U,
			   "both overloads should be retained for fake generation");
		Expect(class_model.mock_methods[0].signature_for_report == "Hoge::Get(int)",
			   "first overload signature should include the int parameter");
		Expect(class_model.mock_methods[1].signature_for_report == "Hoge::Get(const char*)",
			   "second overload signature should include the const char* parameter");
		Expect(HasUnsupportedKind(class_model, "overloaded_operator"),
			   "operator overload should remain a distinct unsupported diagnostic");

		const auto generated = mockfakegen::GenerateMinimalMockFake(class_model);
		for (const auto& file : generated)
		{
			if (file.source_class.has_value())
			{
				Expect(file.source_class->generated_method_count == 2U,
					   "generated source metadata should count both overloads");
			}

			const auto path = generated_dir / file.relative_path;
			const auto expected = ReadText(path);
			ExpectEqual(file.content, expected, path);
		}
	}
} // namespace

int main()
{
	GeneratesOverloadedMockAndFake();
	return 0;
}
