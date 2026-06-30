#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "runtime_template/RuntimeTemplate.h"

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
		std::ifstream stream(path);
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		Expect(stream.good(), "fixture should be readable");
		return buffer.str();
	}

	[[nodiscard]] bool Contains(std::string_view text, std::string_view token)
	{
		return text.find(token) != std::string_view::npos;
	}

	void BuildsRuntimeGeneratedFile()
	{
		const auto file = mockfakegen::MakeThreadLocalRuntimeHeader();
		Expect(file.relative_path == "MockFakeRuntime.h", "runtime path should be deterministic");
		Expect(file.kind == mockfakegen::GeneratedFileKind::RuntimeHeader,
			   "runtime generated file should have runtime kind");
		Expect(!file.source_class.has_value(),
			   "runtime generated file should not have source class");
		Expect(Contains(file.content, "template <typename Mock>"),
			   "runtime should define templates");
		Expect(Contains(file.content, "thread_local std::vector<Mock*> stack;"),
			   "runtime should use thread-local stack");
		Expect(Contains(file.content, "ScopedMock destruction order mismatch"),
			   "runtime should detect destruction order mismatch");
		Expect(Contains(file.content, "MissingMockReturn"),
			   "runtime should define missing mock fallback");
		Expect(!Contains(file.content, "ket::"), "runtime should not contain ket namespace");
		Expect(!Contains(file.content, "#include \"ket_"),
			   "runtime should not include ket quoted headers");
		Expect(!Contains(file.content, "#include <ket_"),
			   "runtime should not include ket angle headers");
	}

	void MatchesGeneratedFixture()
	{
		const auto expected = ReadText(std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR) /
									   "tests/fixtures/generated_runtime/MockFakeRuntime.h");
		const auto file = mockfakegen::MakeThreadLocalRuntimeHeader();
		Expect(file.content == expected, "runtime template output should match generated fixture");
	}
} // namespace

int main()
{
	BuildsRuntimeGeneratedFile();
	MatchesGeneratedFixture();
	return 0;
}
