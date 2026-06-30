#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../GoldenDiff.h"
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

	[[nodiscard]] mockfakegen::SimpleClassModel HogeModel()
	{
		return mockfakegen::SimpleClassModel{
			.name = "Hoge",
			.namespaces = {},
			.header_include = "Hoge.h",
			.mock_header_name = {},
			.fake_source_name = {},
			.methods =
				{
					mockfakegen::SimpleMethodModel{
						.return_type = "bool",
						.name = "Initialize",
						.parameters =
							{
								mockfakegen::SimpleParameterModel{.type = "int", .name = "argc"},
								mockfakegen::SimpleParameterModel{.type = "char**", .name = "argv"},
							},
					},
					mockfakegen::SimpleMethodModel{
						.return_type = "void",
						.name = "Finalize",
						.parameters = {},
					},
					mockfakegen::SimpleMethodModel{
						.return_type = "bool",
						.name = "DoSomething",
						.parameters = {},
					},
				},
		};
	}

	void GeneratedOutputMatchesFixture()
	{
		const auto generated = mockfakegen::GenerateMinimalMockFake(HogeModel());
		const auto fixture_dir =
			std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR) / "tests/fixtures/hoge/generated";

		Expect(generated.size() == 3U, "Hoge fixture should have three generated files");
		for (const auto& file : generated)
		{
			const auto path = fixture_dir / file.relative_path;
			const auto expected = ReadText(path);
			mockfakegen_fixture::ExpectGoldenTextEqual(file.content, expected, path);
		}
	}
} // namespace

int main()
{
	GeneratedOutputMatchesFixture();
	return 0;
}
