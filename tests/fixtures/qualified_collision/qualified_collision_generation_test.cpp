#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
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

	[[nodiscard]] mockfakegen::ClassModel ExtractOneClass(const std::filesystem::path& product_dir,
														  std::string_view header_name)
	{
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = product_dir / std::filesystem::path(header_name),
			.project_root = product_dir,
		});
		Expect(parse_result.success, "qualified collision fixture should parse");

		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		const auto extraction =
			mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
		Expect(extraction.classes.size() == 1U,
			   "qualified collision header should provide one class");
		return extraction.classes[0];
	}

	[[nodiscard]] bool HasFile(const std::vector<mockfakegen::GeneratedFile>& files,
							   std::string_view relative_path)
	{
		for (const auto& file : files)
		{
			if (file.relative_path.generic_string() == relative_path)
			{
				return true;
			}
		}
		return false;
	}

	void GeneratesQualifiedFilenamesForCollidingClasses()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/qualified_collision/product";
		const auto generated_dir = source_dir / "tests/fixtures/qualified_collision/generated";
		std::vector<mockfakegen::ClassModel> classes;
		classes.push_back(ExtractOneClass(product_dir, "b/Hoge.h"));
		classes.push_back(ExtractOneClass(product_dir, "a/Hoge.h"));

		const auto generated = mockfakegen::GenerateMockFakeProject(classes);
		Expect(generated.size() == 9U, "qualified collision fixture should generate nine files");
		Expect(HasFile(generated, "Mock_a_Hoge.h"), "a::Hoge mock should use qualified filename");
		Expect(HasFile(generated, "Mock_b_Hoge.h"), "b::Hoge mock should use qualified filename");
		Expect(!HasFile(generated, "MockHoge.h"), "colliding short mock filename should be absent");
		Expect(!HasFile(generated, "FakeHoge.cpp"),
			   "colliding short fake filename should be absent");

		for (const auto& file : generated)
		{
			const auto path = generated_dir / file.relative_path;
			ExpectGolden(path, file.content);
		}
	}
} // namespace

int main()
{
	GeneratesQualifiedFilenamesForCollidingClasses();
	return 0;
}
