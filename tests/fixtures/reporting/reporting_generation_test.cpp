#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

	void WriteText(const std::filesystem::path& path, std::string_view content)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream << content;
		stream.close();
		Expect(stream.good(), "fixture file should be writable");
	}

	[[nodiscard]] bool UpdateGoldens()
	{
		const auto* const value = std::getenv("MOCKFAKEGEN_UPDATE_GOLDENS");
		return value != nullptr && std::string_view(value) == "1";
	}

	void ExpectGolden(const std::filesystem::path& path, std::string_view actual)
	{
		if (UpdateGoldens())
		{
			WriteText(path, actual);
			return;
		}

		const auto expected = ReadText(path);
		mockfakegen_fixture::ExpectGoldenTextEqual(actual, expected, path);
	}

	[[nodiscard]] std::vector<mockfakegen::ClassModel>
	ExtractClasses(const std::filesystem::path& product_dir)
	{
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = product_dir / "Service.h",
			.project_root = product_dir,
		});
		Expect(parse_result.success, "reporting fixture should parse");

		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		auto extraction = mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
		Expect(extraction.classes.size() == 1U, "reporting fixture should provide one class");
		Expect(extraction.classes[0].unsupported_items.size() == 2U,
			   "reporting fixture should record unsupported members");
		Expect(extraction.diagnostics.size() == 2U,
			   "unsupported members should produce diagnostics");
		return std::move(extraction.classes);
	}

	void GeneratesReportingFiles()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/reporting/product";
		const auto generated_dir = source_dir / "tests/fixtures/reporting/generated";
		const auto classes = ExtractClasses(product_dir);

		const auto generated = mockfakegen::GenerateMockFakeProject(classes);
		Expect(generated.size() == 7U, "reporting fixture should generate seven files");
		for (const auto& file : generated)
		{
			const auto path = generated_dir / file.relative_path;
			ExpectGolden(path, file.content);
		}
	}
} // namespace

int main()
{
	GeneratesReportingFiles();
	return 0;
}
