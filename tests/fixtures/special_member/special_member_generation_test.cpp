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

	void GeneratesSpecialMemberDefinitions()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/special_member/product";
		const auto generated_dir = source_dir / "tests/fixtures/special_member/generated";
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = product_dir / "Special.h",
			.project_root = product_dir,
		});
		Expect(parse_result.success, "special member fixture should parse");

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
															.fake_special_members = true,
														});
		Expect(extraction.classes.size() == 2U, "base and special classes should be extracted");
		const auto& class_model =
			extraction.classes[0].name == "Special" ? extraction.classes[0] : extraction.classes[1];

		Expect(class_model.fake_constructors.size() == 1U, "safe constructor should be extracted");
		Expect(class_model.fake_constructors[0].member_initializers.size() == 2U,
			   "constructor should initialize fields without default member initializers");
		Expect(class_model.fake_constructors[0].member_initializers[0] == "value_{}",
			   "first field initializer should keep declaration order");
		Expect(class_model.fake_constructors[0].member_initializers[1] == "limit_{}",
			   "const field initializer should be synthesized");
		Expect(class_model.fake_destructors.size() == 1U, "safe destructor should be extracted");
		Expect(class_model.unsupported_items.empty(),
			   "safe special member fixture should not report unsupported items");

		const auto generated = mockfakegen::GenerateMinimalMockFake(class_model);
		for (const auto& file : generated)
		{
			Expect(!Contains(file.content, "ket::"), "generated file should not contain ket::");
			if (file.relative_path == "MockFakeRuntime.h")
			{
				continue;
			}

			const auto path = generated_dir / file.relative_path;
			const auto expected = ReadText(path);
			mockfakegen_fixture::ExpectGoldenTextEqual(file.content, expected, path);
		}
	}
} // namespace

int main()
{
	GeneratesSpecialMemberDefinitions();
	return 0;
}
