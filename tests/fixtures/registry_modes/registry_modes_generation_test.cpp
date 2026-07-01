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

	[[nodiscard]] mockfakegen::HeaderModel HeaderNamed(std::string include_spelling)
	{
		mockfakegen::HeaderModel header;
		header.include_spelling = std::move(include_spelling);
		return header;
	}

	[[nodiscard]] mockfakegen::MethodModel RunMethod(std::string return_type)
	{
		mockfakegen::MethodModel method;
		method.name = "Run";
		method.return_type_spelling = std::move(return_type);
		method.gmock_return_type_spelling = method.return_type_spelling;
		method.signature_for_report = "Run()";
		return method;
	}

	[[nodiscard]] mockfakegen::ClassModel
	WorkerModel(std::string name, std::string header, std::string return_type)
	{
		auto method = RunMethod(std::move(return_type));
		method.qualified_owner_name = name;
		return mockfakegen::ClassModel{
			.name = name,
			.qualified_name = name,
			.namespaces = {},
			.mock_name = "Mock" + name,
			.mock_header_name = "Mock" + name + ".h",
			.fake_source_name = "Fake" + name + ".cpp",
			.source_header = HeaderNamed(std::move(header)),
			.mock_methods = {method},
			.fake_methods = {method},
			.unsupported_items = {},
		};
	}

	[[nodiscard]] const mockfakegen::GeneratedFile&
	FindFile(const std::vector<mockfakegen::GeneratedFile>& files, std::string_view path)
	{
		for (const auto& file : files)
		{
			if (file.relative_path.generic_string() == path)
			{
				return file;
			}
		}

		std::cerr << "missing generated file: " << path << '\n';
		std::exit(1);
	}

	void ExpectFileMatches(const mockfakegen::GeneratedFile& file,
						   const std::filesystem::path& expected_path)
	{
		const auto expected = ReadText(expected_path);
		mockfakegen_fixture::ExpectGoldenTextEqual(file.content, expected, expected_path);
	}

	void GeneratedGlobalMutexFilesMatchFixture()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto fixture_dir =
			source_dir / "tests/fixtures/registry_modes/global_mutex/generated";
		const std::vector classes = {
			WorkerModel("GlobalWorker", "GlobalWorker.h", "bool"),
		};
		const auto generated = mockfakegen::GenerateMockFakeProject(
			classes,
			mockfakegen::ProjectGenerationOptions{
				.registry_mode = mockfakegen::RegistryMode::GlobalMutex,
				.emit_all_mocks = false,
				.emit_cmake_fragment = false,
				.emit_manifest = false,
				.emit_report = false,
			});

		Expect(generated.size() == 3U, "global-mutex fixture should generate mock, fake, runtime");
		ExpectFileMatches(FindFile(generated, "MockGlobalWorker.h"),
						  fixture_dir / "MockGlobalWorker.h");
		ExpectFileMatches(FindFile(generated, "FakeGlobalWorker.cpp"),
						  fixture_dir / "FakeGlobalWorker.cpp");
		ExpectFileMatches(FindFile(generated, "MockFakeRuntime.h"),
						  source_dir /
							  "tests/fixtures/generated_runtime_global_mutex/MockFakeRuntime.h");
	}

	void GeneratedSharedOwnerFilesMatchFixture()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto fixture_dir =
			source_dir / "tests/fixtures/registry_modes/shared_owner/generated";
		const std::vector classes = {
			WorkerModel("SharedWorker", "SharedWorker.h", "int"),
		};
		const auto generated = mockfakegen::GenerateMockFakeProject(
			classes,
			mockfakegen::ProjectGenerationOptions{
				.registry_mode = mockfakegen::RegistryMode::SharedOwner,
				.emit_all_mocks = false,
				.emit_cmake_fragment = false,
				.emit_manifest = false,
				.emit_report = false,
			});

		Expect(generated.size() == 3U, "shared-owner fixture should generate mock, fake, runtime");
		ExpectFileMatches(FindFile(generated, "MockSharedWorker.h"),
						  fixture_dir / "MockSharedWorker.h");
		ExpectFileMatches(FindFile(generated, "FakeSharedWorker.cpp"),
						  fixture_dir / "FakeSharedWorker.cpp");
		ExpectFileMatches(FindFile(generated, "MockFakeRuntime.h"),
						  source_dir /
							  "tests/fixtures/generated_runtime_shared_owner/"
							  "MockFakeRuntime.h");
	}
} // namespace

int main()
{
	GeneratedGlobalMutexFilesMatchFixture();
	GeneratedSharedOwnerFilesMatchFixture();
	return 0;
}
