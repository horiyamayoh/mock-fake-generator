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

	[[nodiscard]] bool HasFile(const std::vector<mockfakegen::GeneratedFile>& files,
							   std::string_view path)
	{
		for (const auto& file : files)
		{
			if (file.relative_path.generic_string() == path)
			{
				return true;
			}
		}
		return false;
	}

	void GeneratesInterfaceMockOnly()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/interface_mock/product";
		const auto generated_dir = source_dir / "tests/fixtures/interface_mock/generated";
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = product_dir / "IStorage.h",
			.project_root = product_dir,
		});
		Expect(parse_result.success, "interface fixture should parse");

		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		const auto extraction =
			mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast,
														header,
														mockfakegen::ClassExtractionOptions{
															.interface_mock = true,
														});
		Expect(extraction.classes.size() == 1U, "interface class should be extracted");
		Expect(extraction.classes[0].mock_methods.size() == 2U,
			   "interface pure virtual methods should be extracted");
		Expect(extraction.classes[0].fake_methods.empty(),
			   "interface mode should not extract fake methods");
		Expect(extraction.classes[0].unsupported_items.empty(),
			   "pure interface should not report unsupported items");

		const auto generated =
			mockfakegen::GenerateMockFakeProject(extraction.classes,
												 mockfakegen::ProjectGenerationOptions{
													 .emit_all_mocks = false,
													 .emit_cmake_fragment = false,
													 .emit_manifest = false,
													 .emit_report = false,
													 .interface_mock = true,
												 });
		Expect(generated.size() == 1U, "interface fixture should generate only one mock header");
		Expect(HasFile(generated, "MockIStorage.h"),
			   "interface fixture should generate mock header");
		Expect(!HasFile(generated, "FakeIStorage.cpp"),
			   "interface fixture should not generate fake source");
		Expect(!HasFile(generated, "MockFakeRuntime.h"),
			   "interface fixture should not generate runtime");

		for (const auto& file : generated)
		{
			Expect(!Contains(file.content, "ket::"), "generated file should not contain ket::");
			Expect(!Contains(file.content, "MockFakeRuntime.h"),
				   "interface generated header should not include runtime");
			const auto path = generated_dir / file.relative_path;
			const auto expected = ReadText(path);
			mockfakegen_fixture::ExpectGoldenTextEqual(file.content, expected, path);
		}
	}
} // namespace

int main()
{
	GeneratesInterfaceMockOnly();
	return 0;
}
