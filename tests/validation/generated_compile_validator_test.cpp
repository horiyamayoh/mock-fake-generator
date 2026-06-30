#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "validation/GeneratedCompileValidator.h"

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

	[[nodiscard]] bool Contains(std::string_view text, std::string_view token)
	{
		return text.find(token) != std::string_view::npos;
	}

	[[nodiscard]] std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		Expect(stream.good(), "fixture file should be readable");
		return buffer.str();
	}

	[[nodiscard]] std::vector<std::filesystem::path> SplitPaths(std::string_view text)
	{
		std::vector<std::filesystem::path> paths;
		std::size_t begin = 0U;
		while (begin <= text.size())
		{
			const auto end = text.find('|', begin);
			const auto part = text.substr(begin, end == std::string_view::npos ? end : end - begin);
			if (!part.empty())
			{
				paths.emplace_back(part);
			}
			if (end == std::string_view::npos)
			{
				break;
			}
			begin = end + 1U;
		}
		return paths;
	}

	[[nodiscard]] std::vector<std::filesystem::path> GMockIncludeDirs()
	{
		return SplitPaths(MOCKFAKEGEN_GMOCK_INCLUDE_DIRS);
	}

	[[nodiscard]] mockfakegen::GeneratedFile ReadGeneratedFile(const std::filesystem::path& dir,
															   std::string_view relative_path,
															   mockfakegen::GeneratedFileKind kind)
	{
		return mockfakegen::MakeGeneratedFile(
			std::filesystem::path(relative_path), ReadText(dir / relative_path), kind);
	}

	[[nodiscard]] std::vector<mockfakegen::GeneratedFile> HogeGeneratedFiles()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto generated_dir = source_dir / "tests/fixtures/hoge/generated";
		return {
			ReadGeneratedFile(
				generated_dir, "FakeHoge.cpp", mockfakegen::GeneratedFileKind::FakeSource),
			ReadGeneratedFile(
				generated_dir, "MockFakeRuntime.h", mockfakegen::GeneratedFileKind::RuntimeHeader),
			ReadGeneratedFile(
				generated_dir, "MockHoge.h", mockfakegen::GeneratedFileKind::MockHeader),
		};
	}

	[[nodiscard]] mockfakegen::GeneratedCompileValidationOptions CompileOptions()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		auto include_dirs = GMockIncludeDirs();
		Expect(!include_dirs.empty(), "test should receive gMock include directories");
		include_dirs.push_back(source_dir / "tests/fixtures/hoge/product");

		return mockfakegen::GeneratedCompileValidationOptions{
			.mode = mockfakegen::ValidationMode::Compile,
			.compiler = MOCKFAKEGEN_CXX_COMPILER,
			.include_dirs = include_dirs,
			.extra_args = {},
		};
	}

	void CompileValidationSucceedsForGeneratedFixture()
	{
		const auto result =
			mockfakegen::ValidateGeneratedOutputCompile(CompileOptions(), HogeGeneratedFiles());

		Expect(result.ok(), "generated Hoge fixture should compile");
		Expect(!result.skipped, "compile validation should not be skipped");
		Expect(result.commands.size() == 2U, "mock header smoke and fake source should compile");
		for (const auto& command : result.commands)
		{
			Expect(!Contains(command.command, "third_party/ket"),
				   "compile validation should not pass ket include paths");
			Expect(!Contains(command.command, "ket/modules"),
				   "compile validation should not pass ket module include paths");
		}
	}

	void NoneValidationSkipsCompiler()
	{
		auto options = CompileOptions();
		options.mode = mockfakegen::ValidationMode::None;
		options.compiler = "compiler-that-should-not-run";

		const auto result =
			mockfakegen::ValidateGeneratedOutputCompile(options, HogeGeneratedFiles());

		Expect(result.ok(), "none validation should succeed");
		Expect(result.skipped, "none validation should be marked skipped");
		Expect(result.commands.empty(), "none validation should not run compile commands");
	}

	void CompileValidationReportsCxxFailure()
	{
		const std::vector files = {
			mockfakegen::MakeGeneratedFile("MockBroken.h",
										   "#pragma once\n"
										   "struct MockBroken\n"
										   "{\n"
										   "\tvoid Broken(;\n"
										   "};\n",
										   mockfakegen::GeneratedFileKind::MockHeader),
		};

		const auto result = mockfakegen::ValidateGeneratedOutputCompile(CompileOptions(), files);

		Expect(!result.ok(), "invalid generated C++ should fail validation");
		Expect(result.commands.size() == 1U, "invalid mock header should run smoke command");
		Expect(result.diagnostics.size() == 1U, "invalid C++ should produce one diagnostic");
		Expect(!result.diagnostics[0].command.empty(), "failure diagnostic should keep command");
		Expect(!result.diagnostics[0].stderr_summary.empty(),
			   "failure diagnostic should keep stderr summary");
	}

	void MissingGMockIncludePathIsClear()
	{
		auto options = CompileOptions();
		options.include_dirs = {
			std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR) / "tests/fixtures/hoge/product",
		};

		const auto result =
			mockfakegen::ValidateGeneratedOutputCompile(options, HogeGeneratedFiles());

		Expect(!result.ok(), "missing gMock include path should fail validation");
		Expect(!result.diagnostics.empty(), "missing gMock should produce diagnostic");
		Expect(Contains(result.diagnostics[0].message, "gMock include path is missing"),
			   "missing gMock diagnostic should be explicit");
		Expect(!result.diagnostics[0].command.empty(),
			   "missing gMock diagnostic should keep command");
		Expect(Contains(result.diagnostics[0].stderr_summary, "gmock/gmock.h"),
			   "missing gMock diagnostic should keep compiler stderr");
	}
} // namespace

int main()
{
	CompileValidationSucceedsForGeneratedFixture();
	NoneValidationSkipsCompiler();
	CompileValidationReportsCxxFailure();
	MissingGMockIncludePathIsClear();
	return 0;
}
