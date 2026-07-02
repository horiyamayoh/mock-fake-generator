#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "generator/CodeGenerator.h"
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

	[[nodiscard]] std::string ShellQuote(std::string_view value)
	{
		std::string quoted;
		quoted.reserve(value.size() + 2U);
		quoted += '\'';
		for (const auto character : value)
		{
			if (character == '\'')
			{
				quoted += "'\\''";
			}
			else
			{
				quoted += character;
			}
		}
		quoted += '\'';
		return quoted;
	}

	class TempTree
	{
	  public:
		TempTree()
			: root_(std::filesystem::temp_directory_path() /
					("mockfakegen_compile_validator_test_" + std::to_string(UniqueSuffix())))
		{
			std::filesystem::remove_all(root_);
			std::filesystem::create_directories(root_);
		}

		TempTree(const TempTree&) = delete;
		TempTree& operator=(const TempTree&) = delete;

		~TempTree()
		{
			std::error_code error;
			std::filesystem::remove_all(root_, error);
		}

		[[nodiscard]] const std::filesystem::path& root() const noexcept
		{
			return root_;
		}

		void Write(std::string_view relative_path, std::string_view content) const
		{
			const auto path = root_ / std::filesystem::path(relative_path);
			std::filesystem::create_directories(path.parent_path());
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			stream << content;
			Expect(stream.good(), "fixture file should be written");
		}

	  private:
		[[nodiscard]] static long long UniqueSuffix()
		{
			return std::chrono::steady_clock::now().time_since_epoch().count();
		}

		std::filesystem::path root_;
	};

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

	[[nodiscard]] std::vector<std::filesystem::path> GMockLinkFiles()
	{
		return SplitPaths(MOCKFAKEGEN_GMOCK_LINK_FILES);
	}

	[[nodiscard]] std::filesystem::path HogeProductSource()
	{
		return std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR) /
			"tests/fixtures/hoge/product/Hoge.cpp";
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

	[[nodiscard]] mockfakegen::ParameterModel Parameter(std::string type, std::string name)
	{
		mockfakegen::ParameterModel parameter;
		parameter.type_spelling = std::move(type);
		parameter.gmock_type_spelling = parameter.type_spelling;
		parameter.generated_name = std::move(name);
		return parameter;
	}

	[[nodiscard]] mockfakegen::MethodModel
	Method(std::string return_type,
		   std::string name,
		   std::vector<mockfakegen::ParameterModel> parameters = {})
	{
		mockfakegen::MethodModel method;
		method.return_type_spelling = std::move(return_type);
		method.gmock_return_type_spelling = method.return_type_spelling;
		method.name = std::move(name);
		method.parameters = std::move(parameters);
		method.access = mockfakegen::AccessKind::Public;
		return method;
	}

	[[nodiscard]] mockfakegen::ClassModel HogeClassModel()
	{
		mockfakegen::HeaderModel header;
		header.include_spelling = "Hoge.h";

		mockfakegen::ClassModel class_model;
		class_model.name = "Hoge";
		class_model.qualified_name = "Hoge";
		class_model.mock_name = "MockHoge";
		class_model.source_header = header;
		class_model.mock_methods = {
			Method("bool", "Initialize", {Parameter("int", "argc"), Parameter("char**", "argv")}),
			Method("void", "Finalize"),
			Method("bool", "DoSomething"),
		};
		class_model.fake_methods = class_model.mock_methods;
		return class_model;
	}

	[[nodiscard]] std::vector<mockfakegen::GeneratedFile> SharedOwnerGeneratedFiles()
	{
		const std::vector classes = {HogeClassModel()};
		return mockfakegen::GenerateMockFakeProject(
			classes,
			mockfakegen::ProjectGenerationOptions{
				.registry_mode = mockfakegen::RegistryMode::SharedOwner,
			});
	}

	[[nodiscard]] std::vector<mockfakegen::GeneratedFile> SameStemFakeFiles()
	{
		return {
			mockfakegen::MakeGeneratedFile("a/FakeService.cpp",
										   "int mockfakegen_fake_a()\n"
										   "{\n"
										   "\treturn 1;\n"
										   "}\n",
										   mockfakegen::GeneratedFileKind::FakeSource),
			mockfakegen::MakeGeneratedFile("b/FakeService.cpp",
										   "int mockfakegen_fake_b()\n"
										   "{\n"
										   "\treturn 2;\n"
										   "}\n",
										   mockfakegen::GeneratedFileKind::FakeSource),
		};
	}

	[[nodiscard]] std::vector<mockfakegen::GeneratedFile> CollidingObjectPathFakeFiles()
	{
		return {
			mockfakegen::MakeGeneratedFile("a/FakeService.cpp",
										   "int mockfakegen_fake_cpp()\n"
										   "{\n"
										   "\treturn 1;\n"
										   "}\n",
										   mockfakegen::GeneratedFileKind::FakeSource),
			mockfakegen::MakeGeneratedFile("a/FakeService.cc",
										   "int mockfakegen_fake_cc()\n"
										   "{\n"
										   "\treturn 2;\n"
										   "}\n",
										   mockfakegen::GeneratedFileKind::FakeSource),
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
			.link_files = GMockLinkFiles(),
			.extra_args = {},
			.source_args = {},
			.command_timeout = std::chrono::seconds(30),
			.keep_failed_artifacts = false,
			.artifact_dir = {},
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

	void CompileValidationNormalizesStandardArgs()
	{
		auto options = CompileOptions();
		options.extra_args = {"-std", "c++20", "--std=c++17"};

		const auto result =
			mockfakegen::ValidateGeneratedOutputCompile(options, HogeGeneratedFiles());

		Expect(result.ok(), "compile validation should ignore standard downgrades");
		Expect(!result.commands.empty(), "compile validation should record commands");
		for (const auto& command : result.commands)
		{
			Expect(Contains(command.command, "-std=c++23"),
				   "validation command should include fixed C++23");
			Expect(!Contains(command.command, "c++20"),
				   "validation command should drop separate C++20 downgrade");
			Expect(!Contains(command.command, "c++17"),
				   "validation command should drop joined C++17 downgrade");
		}
	}

	void CompileValidationSucceedsForSharedOwnerGeneratedOutput()
	{
		const auto result = mockfakegen::ValidateGeneratedOutputCompile(
			CompileOptions(), SharedOwnerGeneratedFiles());

		Expect(result.ok(), "generated shared-owner output should compile");
		Expect(!result.skipped, "shared-owner compile validation should not be skipped");
		Expect(result.commands.size() == 2U, "mock header smoke and fake source should compile");
	}

	void LinkValidationBuildsSmokeExecutable()
	{
		auto options = CompileOptions();
		options.mode = mockfakegen::ValidationMode::Link;

		const auto result =
			mockfakegen::ValidateGeneratedOutputCompile(options, HogeGeneratedFiles());

		Expect(result.ok(), "link validation should link generated fakes with gMock");
		Expect(!result.skipped, "link validation should not be skipped");
		Expect(result.commands.size() == 3U,
			   "link validation should compile smoke, compile fake, and link executable");
		Expect(Contains(result.commands.back().command, "generated_link_smoke"),
			   "link validation command should produce a smoke executable");
		Expect(!Contains(result.commands.back().command, "third_party/ket"),
			   "link validation should not pass ket include paths");
		Expect(!Contains(result.commands.back().command, "mockfakegen_ket"),
			   "link validation should not link mockfakegen_ket");
	}

	void LinkValidationReportsDuplicateProductImplementation()
	{
		auto options = CompileOptions();
		options.mode = mockfakegen::ValidationMode::Link;
		options.link_files.push_back(HogeProductSource());

		const auto result =
			mockfakegen::ValidateGeneratedOutputCompile(options, HogeGeneratedFiles());

		Expect(!result.ok(), "linking product implementation with generated fake should fail");
		Expect(result.commands.size() == 3U, "duplicate-symbol case should reach the link command");
		Expect(!result.diagnostics.empty(), "duplicate-symbol link failure should diagnose");
		Expect(result.diagnostics[0].stage == mockfakegen::GeneratedCompileValidationStage::Link,
			   "duplicate-symbol diagnostic should record link stage");
		Expect(Contains(result.diagnostics[0].message, "do not link product .cpp files"),
			   "duplicate-symbol diagnostic should explain link substitution boundary");
		Expect(Contains(result.diagnostics[0].command, HogeProductSource().string()),
			   "duplicate-symbol diagnostic should keep the product source in the command");
	}

	void LinkValidationUsesUniqueObjectPathsForSameStemFakes()
	{
		auto options = CompileOptions();
		options.mode = mockfakegen::ValidationMode::Link;
		options.link_files.clear();

		const auto result =
			mockfakegen::ValidateGeneratedOutputCompile(options, SameStemFakeFiles());

		Expect(result.ok(), "same-stem fake sources in different directories should link");
		Expect(result.commands.size() == 4U,
			   "same-stem case should compile smoke, compile both fakes, and link");
		const auto& link_command = result.commands.back().command;
		Expect(Contains(link_command, "objects/a/FakeService.o"),
			   "link command should include object path preserving directory a");
		Expect(Contains(link_command, "objects/b/FakeService.o"),
			   "link command should include object path preserving directory b");
	}

	void LinkValidationReportsObjectPathCollision()
	{
		auto options = CompileOptions();
		options.mode = mockfakegen::ValidationMode::Link;
		options.link_files.clear();

		const auto result =
			mockfakegen::ValidateGeneratedOutputCompile(options, CollidingObjectPathFakeFiles());

		Expect(!result.ok(), "object path collision should fail validation");
		Expect(result.commands.empty(), "object path collision should fail before compiler runs");
		Expect(result.diagnostics.size() == 1U,
			   "object path collision should produce one diagnostic");
		Expect(result.diagnostics[0].stage == mockfakegen::GeneratedCompileValidationStage::Compile,
			   "object path collision should record compile stage");
		Expect(Contains(result.diagnostics[0].message, "validation object path collision"),
			   "object path collision diagnostic should be explicit");
		Expect(Contains(result.diagnostics[0].message, "a/FakeService.cpp"),
			   "object path collision diagnostic should name first source");
		Expect(Contains(result.diagnostics[0].message, "a/FakeService.cc"),
			   "object path collision diagnostic should name second source");
	}

	void SyntaxValidationUsesSyntaxOnly()
	{
		auto options = CompileOptions();
		options.mode = mockfakegen::ValidationMode::Syntax;

		const auto result =
			mockfakegen::ValidateGeneratedOutputCompile(options, HogeGeneratedFiles());

		Expect(result.ok(), "syntax validation should succeed for generated fixture");
		Expect(result.commands.size() == 2U,
			   "syntax validation should check smoke and fake source");
		for (const auto& command : result.commands)
		{
			Expect(Contains(command.command, "-fsyntax-only"),
				   "syntax validation command should use -fsyntax-only");
		}
	}

	void SyntaxValidationReportsSyntaxStage()
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
		auto options = CompileOptions();
		options.mode = mockfakegen::ValidationMode::Syntax;

		const auto result = mockfakegen::ValidateGeneratedOutputCompile(options, files);

		Expect(!result.ok(), "invalid generated C++ should fail syntax validation");
		Expect(result.diagnostics.size() == 1U, "syntax failure should produce one diagnostic");
		Expect(result.diagnostics[0].stage == mockfakegen::GeneratedCompileValidationStage::Syntax,
			   "syntax validation failure should record syntax stage");
		Expect(result.diagnostics[0].message == "generated output syntax validation failed.",
			   "syntax validation failure should use syntax message");
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
		Expect(result.diagnostics[0].stage == mockfakegen::GeneratedCompileValidationStage::Compile,
			   "compile validation failure should record compile stage");
		Expect(!result.diagnostics[0].command.empty(), "failure diagnostic should keep command");
		Expect(!result.diagnostics[0].stderr_summary.empty(),
			   "failure diagnostic should keep stderr summary");
	}

	void AllMocksDoesNotHideBrokenMockHeader()
	{
		const std::vector files = {
			mockfakegen::MakeGeneratedFile(
				"AllMocks.h", "#pragma once\n", mockfakegen::GeneratedFileKind::AllMocksHeader),
			mockfakegen::MakeGeneratedFile("MockBroken.h",
										   "#pragma once\n"
										   "struct MockBroken\n"
										   "{\n"
										   "\tvoid Broken(;\n"
										   "};\n",
										   mockfakegen::GeneratedFileKind::MockHeader),
		};

		const auto result = mockfakegen::ValidateGeneratedOutputCompile(CompileOptions(), files);

		Expect(!result.ok(), "AllMocks.h should not hide a broken mock header");
		Expect(result.commands.size() == 1U, "broken mock header should fail smoke command");
		Expect(result.diagnostics.size() == 1U,
			   "broken mock header with AllMocks should produce one diagnostic");
		Expect(Contains(result.diagnostics[0].stderr_summary, "MockBroken.h"),
			   "diagnostic stderr should mention the directly included broken mock header");
	}

	void CompileValidationTimesOut()
	{
		TempTree tree;
		const auto leaked_marker = tree.root() / "leaked-grandchild.txt";
		tree.Write("slow-compiler.sh",
				   "#!/bin/sh\n"
				   "(\n"
				   "  sleep 1\n"
				   "  echo leaked > " +
					   ShellQuote(leaked_marker.string()) +
					   "\n"
					   ") &\n"
					   "wait\n");
		std::filesystem::permissions(tree.root() / "slow-compiler.sh",
									 std::filesystem::perms::owner_exec,
									 std::filesystem::perm_options::add);
		auto options = CompileOptions();
		options.compiler = tree.root() / "slow-compiler.sh";
		options.command_timeout = std::chrono::milliseconds(100);

		const auto result =
			mockfakegen::ValidateGeneratedOutputCompile(options, HogeGeneratedFiles());

		Expect(!result.ok(), "slow compiler should time out");
		Expect(!result.diagnostics.empty(), "timeout should produce diagnostics");
		Expect(result.commands[0].exit_code == 124, "timeout exit should be recorded");
		Expect(Contains(result.diagnostics[0].message, "timed out"),
			   "timeout diagnostic should be explicit");
		std::this_thread::sleep_for(std::chrono::milliseconds(1200));
		Expect(!std::filesystem::exists(leaked_marker),
			   "timeout should kill wrapper compiler descendants");
	}

	void KeepsFailedArtifactsWhenRequested()
	{
		TempTree tree;
		const std::vector files = {
			mockfakegen::MakeGeneratedFile("MockBroken.h",
										   "#pragma once\n"
										   "struct MockBroken\n"
										   "{\n"
										   "\tvoid Broken(;\n"
										   "};\n",
										   mockfakegen::GeneratedFileKind::MockHeader),
		};
		auto options = CompileOptions();
		options.keep_failed_artifacts = true;
		options.artifact_dir = tree.root() / "artifacts";

		const auto result = mockfakegen::ValidateGeneratedOutputCompile(options, files);

		Expect(!result.ok(), "invalid generated C++ should fail validation");
		Expect(!result.diagnostics[0].validation_artifact_path.empty(),
			   "kept artifact path should be recorded");
		Expect(std::filesystem::exists(result.diagnostics[0].validation_artifact_path),
			   "kept artifact path should exist");
		Expect(std::filesystem::exists(result.diagnostics[0].validation_artifact_path /
									   "generated/MockBroken.h"),
			   "generated failing file should be retained");
	}

	void InvalidArtifactDirectoryReportsDiagnostic()
	{
		TempTree tree;
		tree.Write("artifact-file", "x");
		auto options = CompileOptions();
		options.keep_failed_artifacts = true;
		options.artifact_dir = tree.root() / "artifact-file";

		const auto result =
			mockfakegen::ValidateGeneratedOutputCompile(options, HogeGeneratedFiles());

		Expect(!result.ok(), "invalid artifact directory should fail validation");
		Expect(result.commands.empty(), "invalid artifact directory should not run compiler");
		Expect(result.diagnostics.size() == 1U,
			   "invalid artifact directory should produce one diagnostic");
		Expect(Contains(result.diagnostics[0].message, "invalid validation artifact directory"),
			   "invalid artifact directory diagnostic should be explicit");
		Expect(Contains(result.diagnostics[0].message, "artifact-file"),
			   "invalid artifact directory diagnostic should name the bad path");
	}

	void UnsafeGeneratedPathsAreRejectedBeforeStaging()
	{
		TempTree tree;
		const std::vector files = {
			mockfakegen::MakeGeneratedFile("Safe.cpp",
										   "int mockfakegen_safe()\n"
										   "{\n"
										   "\treturn 1;\n"
										   "}\n",
										   mockfakegen::GeneratedFileKind::FakeSource),
			mockfakegen::MakeGeneratedFile("z/../../Escape.cpp",
										   "int mockfakegen_escape()\n"
										   "{\n"
										   "\treturn 2;\n"
										   "}\n",
										   mockfakegen::GeneratedFileKind::FakeSource),
		};
		auto options = CompileOptions();
		options.keep_failed_artifacts = true;
		options.artifact_dir = tree.root() / "artifacts";

		const auto result = mockfakegen::ValidateGeneratedOutputCompile(options, files);

		Expect(!result.ok(), "unsafe generated path should fail validation");
		Expect(result.commands.empty(), "unsafe generated path should not run compiler");
		Expect(result.diagnostics.size() == 1U,
			   "unsafe generated path should produce one diagnostic");
		Expect(Contains(result.diagnostics[0].message, "invalid generated validation path"),
			   "unsafe generated path diagnostic should identify validation path");
		Expect(Contains(result.diagnostics[0].message, "must not contain '..' traversal"),
			   "unsafe generated path diagnostic should explain traversal");
		Expect(!result.diagnostics[0].validation_artifact_path.empty(),
			   "unsafe path failure should keep artifact root when requested");
		Expect(std::filesystem::exists(result.diagnostics[0].validation_artifact_path),
			   "unsafe path artifact root should exist");
		Expect(!std::filesystem::exists(result.diagnostics[0].validation_artifact_path /
										"generated/Safe.cpp"),
			   "safe file should not be staged before path preflight completes");
		Expect(
			!std::filesystem::exists(result.diagnostics[0].validation_artifact_path / "Escape.cpp"),
			"traversal path should not write outside generated root");
	}

	void ValidationCompilerRunsWithStableCLocale()
	{
		TempTree tree;
		tree.Write("compiler.sh",
				   "#!/bin/sh\n"
				   "printf 'LC_ALL=%s\\n' \"$LC_ALL\" >&2\n"
				   "exit 1\n");
		std::filesystem::permissions(tree.root() / "compiler.sh",
									 std::filesystem::perms::owner_exec,
									 std::filesystem::perm_options::add);
		auto options = CompileOptions();
		options.compiler = tree.root() / "compiler.sh";

		const auto result =
			mockfakegen::ValidateGeneratedOutputCompile(options, HogeGeneratedFiles());

		Expect(!result.ok(), "wrapper compiler failure should fail validation");
		Expect(!result.diagnostics.empty(), "wrapper compiler failure should produce diagnostic");
		Expect(Contains(result.diagnostics[0].stderr_summary, "LC_ALL=C"),
			   "validation compiler should run with stable C locale");
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

	void MentioningGMockHeaderDoesNotImplyMissingIncludePath()
	{
		TempTree tree;
		tree.Write("compiler.sh",
				   "#!/bin/sh\n"
				   "printf '%s\\n' 'In file included from /usr/include/gmock/gmock.h:10:' >&2\n"
				   "printf '%s\\n' 'generated/MockBroken.h:4:1: error: invalid mock method' >&2\n"
				   "exit 1\n");
		std::filesystem::permissions(tree.root() / "compiler.sh",
									 std::filesystem::perms::owner_exec,
									 std::filesystem::perm_options::add);
		auto options = CompileOptions();
		options.compiler = tree.root() / "compiler.sh";

		const auto result =
			mockfakegen::ValidateGeneratedOutputCompile(options, HogeGeneratedFiles());

		Expect(!result.ok(), "wrapper compiler failure should fail validation");
		Expect(!result.diagnostics.empty(), "wrapper compiler failure should produce diagnostic");
		Expect(result.diagnostics[0].message == "generated output compile validation failed.",
			   "gMock include mention without include-not-found should stay generic");
		Expect(Contains(result.diagnostics[0].stderr_summary, "gmock/gmock.h"),
			   "diagnostic should keep the original stderr mention");
	}
} // namespace

int main()
{
	CompileValidationSucceedsForGeneratedFixture();
	CompileValidationNormalizesStandardArgs();
	CompileValidationSucceedsForSharedOwnerGeneratedOutput();
	LinkValidationBuildsSmokeExecutable();
	LinkValidationReportsDuplicateProductImplementation();
	LinkValidationUsesUniqueObjectPathsForSameStemFakes();
	LinkValidationReportsObjectPathCollision();
	SyntaxValidationUsesSyntaxOnly();
	SyntaxValidationReportsSyntaxStage();
	NoneValidationSkipsCompiler();
	CompileValidationReportsCxxFailure();
	AllMocksDoesNotHideBrokenMockHeader();
	CompileValidationTimesOut();
	KeepsFailedArtifactsWhenRequested();
	InvalidArtifactDirectoryReportsDiagnostic();
	UnsafeGeneratedPathsAreRejectedBeforeStaging();
	ValidationCompilerRunsWithStableCLocale();
	MissingGMockIncludePathIsClear();
	MentioningGMockHeaderDoesNotImplyMissingIncludePath();
	return 0;
}
