#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(__unix__)
#include <sys/wait.h>
#endif

namespace
{
	struct CommandResult
	{
		int exit_code = 1;
		std::filesystem::path stdout_path;
		std::filesystem::path stderr_path;
	};

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
		Expect(stream.good(), "file should be readable");
		return buffer.str();
	}

	void WriteText(const std::filesystem::path& path, std::string_view content)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream << content;
		stream.close();
		Expect(stream.good(), "file should be writable");
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

	[[nodiscard]] int DecodeSystemStatus(int status) noexcept
	{
#if defined(__unix__)
		if (WIFEXITED(status))
		{
			return WEXITSTATUS(status);
		}
		if (WIFSIGNALED(status))
		{
			return 128 + WTERMSIG(status);
		}
#endif
		return status == 0 ? 0 : 1;
	}

	[[nodiscard]] std::string JsonString(std::string_view text)
	{
		std::string escaped = "\"";
		for (const auto character : text)
		{
			if (character == '"' || character == '\\')
			{
				escaped.push_back('\\');
			}
			escaped.push_back(character);
		}
		escaped.push_back('"');
		return escaped;
	}

	[[nodiscard]] std::filesystem::path TempRoot()
	{
		const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
		auto root = std::filesystem::temp_directory_path() /
			("mockfakegen_cli_e2e_" + std::to_string(suffix));
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root);
		return root;
	}

	void WriteCompileCommands(const std::filesystem::path& build_dir,
							  const std::filesystem::path& product_dir)
	{
		const auto source = product_dir / "Hoge.cpp";
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 -I " +
			ShellQuote(product_dir.string()) + " -c " + ShellQuote(source.string()) + " -o hoge.o";
		const auto json = std::string("[\n") + "  {\n" +
			"    \"directory\": " + JsonString(product_dir.string()) + ",\n" +
			"    \"command\": " + JsonString(command) + ",\n" +
			"    \"file\": " + JsonString(source.string()) + "\n" + "  }\n" + "]\n";
		WriteText(build_dir / "compile_commands.json", json);
	}

	void WriteSingleCompileCommand(const std::filesystem::path& build_dir,
								   const std::filesystem::path& directory,
								   const std::filesystem::path& source,
								   std::string_view command)
	{
		const auto json = std::string("[\n") + "  {\n" +
			"    \"directory\": " + JsonString(directory.string()) + ",\n" +
			"    \"command\": " + JsonString(command) + ",\n" +
			"    \"file\": " + JsonString(source.string()) + "\n" + "  }\n" + "]\n";
		WriteText(build_dir / "compile_commands.json", json);
	}

	[[nodiscard]] CommandResult RunMockfakegen(const std::filesystem::path& temp_root,
											   std::vector<std::string> args,
											   std::string_view label)
	{
		const auto stdout_path = temp_root / (std::string(label) + ".stdout.txt");
		const auto stderr_path = temp_root / (std::string(label) + ".stderr.txt");
		std::string command = ShellQuote(MOCKFAKEGEN_EXECUTABLE);
		for (const auto& arg : args)
		{
			command += ' ';
			command += ShellQuote(arg);
		}
		command += " > ";
		command += ShellQuote(stdout_path.string());
		command += " 2> ";
		command += ShellQuote(stderr_path.string());

		const auto status = std::system(command.c_str());
		const auto exit_code = status == -1 ? 1 : DecodeSystemStatus(status);
		return CommandResult{
			.exit_code = exit_code,
			.stdout_path = stdout_path,
			.stderr_path = stderr_path,
		};
	}

	[[nodiscard]] std::vector<std::string> BaseArgs(const std::filesystem::path& product_dir,
													const std::filesystem::path& build_dir,
													const std::filesystem::path& output_dir)
	{
		return {
			"--input-root",
			product_dir.string(),
			"--output-dir",
			output_dir.string(),
			"--build-path",
			build_dir.string(),
			"--project-root",
			product_dir.string(),
		};
	}

	void Append(std::vector<std::string>& args, std::vector<std::string> extra)
	{
		for (auto& arg : extra)
		{
			args.push_back(std::move(arg));
		}
	}

	void ExpectGeneratedCoreFiles(const std::filesystem::path& output_dir)
	{
		for (const auto& name : {
				 "MockHoge.h",
				 "FakeHoge.cpp",
				 "MockFakeRuntime.h",
				 "AllMocks.h",
				 "CMakeLists.fragment.cmake",
				 "manifest.json",
				 "generation_report.md",
			 })
		{
			Expect(std::filesystem::exists(output_dir / name), "generated file should exist");
		}

		const auto mock_header = ReadText(output_dir / "MockHoge.h");
		Expect(Contains(mock_header, "MOCK_METHOD(bool, Initialize"),
			   "mock header should contain Initialize mock");
		Expect(!Contains(mock_header, "ket::"), "mock header should not contain ket namespace");

		const auto fake_source = ReadText(output_dir / "FakeHoge.cpp");
		Expect(Contains(fake_source, "Hoge::Initialize"),
			   "fake source should contain Initialize definition");
		Expect(!Contains(fake_source, "ket::"), "fake source should not contain ket namespace");

		const auto report = ReadText(output_dir / "generation_report.md");
		Expect(Contains(report, "Link Replacement Notice"),
			   "report should document link replacement");
	}

	void GeneratesAndValidatesFromRealCli(const std::filesystem::path& temp_root,
										  const std::filesystem::path& product_dir,
										  const std::filesystem::path& build_dir)
	{
		const auto output_dir = temp_root / "generated";
		auto args = BaseArgs(product_dir, build_dir, output_dir);
		Append(args,
			   {
				   "--validate",
				   "compile",
				   "--format-style",
				   "none",
				   "--fake-special-members",
				   "true",
			   });

		const auto result = RunMockfakegen(temp_root, args, "generate");
		const auto stdout_text = ReadText(result.stdout_path);
		const auto stderr_text = ReadText(result.stderr_path);
		Expect(result.exit_code == 0, "CLI generation should succeed");
		Expect(Contains(stdout_text, "mockfakegen: scanned 1 header(s)"),
			   "CLI should report scanned headers");
		Expect(Contains(stdout_text, "mockfakegen: validation commands 2"),
			   "CLI should run compile validation");
		Expect(!Contains(stderr_text, "error [validation]"),
			   "compile validation should not produce errors");
		ExpectGeneratedCoreFiles(output_dir);
	}

	void SyntaxValidationRunsFromRealCli(const std::filesystem::path& temp_root,
										 const std::filesystem::path& product_dir,
										 const std::filesystem::path& build_dir)
	{
		const auto output_dir = temp_root / "syntax-generated";
		auto args = BaseArgs(product_dir, build_dir, output_dir);
		Append(args,
			   {
				   "--validate",
				   "syntax",
				   "--format-style",
				   "none",
				   "--fake-special-members",
				   "true",
			   });

		const auto result = RunMockfakegen(temp_root, args, "syntax");
		const auto stdout_text = ReadText(result.stdout_path);
		const auto stderr_text = ReadText(result.stderr_path);
		Expect(result.exit_code == 0, "syntax validation CLI generation should succeed");
		Expect(Contains(stdout_text, "mockfakegen: validation commands 2"),
			   "syntax validation should run validation commands");
		Expect(!Contains(stderr_text, "error [validation]"),
			   "syntax validation should not produce validation errors");
		ExpectGeneratedCoreFiles(output_dir);
	}

	void CompileValidationInheritsCompileDatabaseArgs(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "compile-db-product";
		const auto include_dir = product_root / "include";
		const auto config_dir = product_root / "config";
		const auto source_dir = product_root / "src";
		const auto build_dir = temp_root / "compile-db-build";
		const auto output_dir = temp_root / "compile-db-generated";
		WriteText(config_dir / "Dependency.h",
				  "#pragma once\n"
				  "struct Dependency { int value; };\n");
		WriteText(include_dir / "Feature.h",
				  "#pragma once\n"
				  "#ifndef MOCKFAKEGEN_FROM_COMPILE_DB\n"
				  "#error expected compile database define\n"
				  "#endif\n"
				  "#include \"Dependency.h\"\n"
				  "class Feature { public: bool Run(Dependency dependency); };\n");
		const auto source = source_dir / "Feature.cpp";
		WriteText(source,
				  "#include \"Feature.h\"\n"
				  "bool Feature::Run(Dependency dependency) { return dependency.value != 0; }\n");
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 " +
			ShellQuote("-I" + include_dir.string()) + " " + ShellQuote("-I" + config_dir.string()) +
			" -DMOCKFAKEGEN_FROM_COMPILE_DB -c " + ShellQuote(source.string()) + " -o feature.o";
		WriteSingleCompileCommand(build_dir, product_root, source, command);

		std::vector<std::string> args = {
			"--input-root",
			include_dir.string(),
			"--output-dir",
			output_dir.string(),
			"--build-path",
			build_dir.string(),
			"--project-root",
			product_root.string(),
		};
		Append(args,
			   {
				   "--validate",
				   "compile",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "compile_db_validation");
		const auto stdout_text = ReadText(result.stdout_path);
		const auto stderr_text = ReadText(result.stderr_path);
		Expect(result.exit_code == 0, "compile DB validation inheritance should succeed");
		Expect(Contains(stdout_text, "mockfakegen: validation commands 2"),
			   "compile DB validation should run compile commands");
		Expect(!Contains(stderr_text, "error [validation]"),
			   "compile DB validation should not produce validation errors");
		Expect(std::filesystem::exists(output_dir / "MockFeature.h"),
			   "compile DB product mock should be generated");
	}

	void DryRunDoesNotPublishFiles(const std::filesystem::path& temp_root,
								   const std::filesystem::path& product_dir,
								   const std::filesystem::path& build_dir)
	{
		const auto output_dir = temp_root / "dry-run";
		auto args = BaseArgs(product_dir, build_dir, output_dir);
		Append(args,
			   {
				   "--dry-run",
				   "--validate",
				   "none",
				   "--format-style",
				   "none",
				   "--fake-special-members",
				   "true",
			   });

		const auto result = RunMockfakegen(temp_root, args, "dry_run");
		const auto stdout_text = ReadText(result.stdout_path);
		Expect(result.exit_code == 0, "dry-run CLI should succeed");
		Expect(Contains(stdout_text, "mockfakegen: planned"),
			   "dry-run should report planned outputs");
		Expect(!std::filesystem::exists(output_dir / "MockHoge.h"),
			   "dry-run should not write generated files");
	}

	void OverwriteControlsExistingFiles(const std::filesystem::path& temp_root,
										const std::filesystem::path& product_dir,
										const std::filesystem::path& build_dir)
	{
		const auto output_dir = temp_root / "overwrite-generated";
		WriteText(output_dir / "MockHoge.h", "// user edit\n");

		auto blocked_args = BaseArgs(product_dir, build_dir, output_dir);
		Append(blocked_args,
			   {
				   "--validate",
				   "none",
				   "--format-style",
				   "none",
				   "--fake-special-members",
				   "true",
			   });
		const auto blocked = RunMockfakegen(temp_root, blocked_args, "overwrite_blocked");
		Expect(blocked.exit_code == 1, "existing changed file should fail without overwrite");
		Expect(ReadText(output_dir / "MockHoge.h") == "// user edit\n",
			   "blocked publish should preserve existing file");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "write failure should emit diagnostic report");
		Expect(!std::filesystem::exists(output_dir / "manifest.json"),
			   "write failure should not emit manifest");
		const auto blocked_report = ReadText(output_dir / "generation_report.md");
		Expect(Contains(blocked_report, "writer"), "writer diagnostic should appear in report");
		Expect(Contains(blocked_report, "output_conflict"),
			   "writer diagnostic code should appear in report");
		Expect(Contains(blocked_report, "MockHoge.h"),
			   "writer diagnostic path should appear in report");

		auto overwrite_args = blocked_args;
		overwrite_args.push_back("--overwrite");
		const auto overwritten = RunMockfakegen(temp_root, overwrite_args, "overwrite");
		Expect(overwritten.exit_code == 0, "overwrite should replace existing generated files");
		Expect(Contains(ReadText(output_dir / "MockHoge.h"), "MOCK_METHOD(bool, Initialize"),
			   "overwrite should publish generated mock header");
	}

	void EmitOptionsControlOptionalArtifacts(const std::filesystem::path& temp_root,
											 const std::filesystem::path& product_dir,
											 const std::filesystem::path& build_dir)
	{
		const auto output_dir = temp_root / "emit-options";
		auto args = BaseArgs(product_dir, build_dir, output_dir);
		Append(args,
			   {
				   "--validate",
				   "none",
				   "--format-style",
				   "none",
				   "--fake-special-members",
				   "true",
				   "--emit-all-mocks",
				   "false",
				   "--emit-manifest",
				   "false",
				   "--emit-cmake-fragment",
				   "false",
			   });

		const auto result = RunMockfakegen(temp_root, args, "emit_options");
		Expect(result.exit_code == 0, "emit option run should succeed");
		Expect(std::filesystem::exists(output_dir / "MockHoge.h"),
			   "mock header should still be generated");
		Expect(std::filesystem::exists(output_dir / "FakeHoge.cpp"),
			   "fake source should still be generated");
		Expect(std::filesystem::exists(output_dir / "MockFakeRuntime.h"),
			   "runtime should still be generated");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "report should still be generated");
		Expect(!std::filesystem::exists(output_dir / "AllMocks.h"),
			   "AllMocks.h should honor emit-all-mocks false");
		Expect(!std::filesystem::exists(output_dir / "manifest.json"),
			   "manifest should honor emit-manifest false");
		Expect(!std::filesystem::exists(output_dir / "CMakeLists.fragment.cmake"),
			   "CMake fragment should honor emit-cmake-fragment false");
	}

	void StrictModeFailsUnsupportedInput(const std::filesystem::path& temp_root,
										 const std::filesystem::path& product_dir,
										 const std::filesystem::path& build_dir)
	{
		const auto output_dir = temp_root / "strict";
		auto args = BaseArgs(product_dir, build_dir, output_dir);
		Append(args,
			   {
				   "--strict",
				   "--validate",
				   "none",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "strict");
		Expect(result.exit_code == 1, "strict mode should fail unsupported constructors");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "strict mode should still emit report when policy allows it");
		Expect(Contains(ReadText(output_dir / "generation_report.md"),
						"constructor fake generation is not supported"),
			   "strict report should include unsupported reason");
	}

	void RegistryModeAffectsRuntime(const std::filesystem::path& temp_root,
									const std::filesystem::path& product_dir,
									const std::filesystem::path& build_dir)
	{
		const auto output_dir = temp_root / "global-mutex";
		auto args = BaseArgs(product_dir, build_dir, output_dir);
		Append(args,
			   {
				   "--registry-mode",
				   "global-mutex",
				   "--validate",
				   "none",
				   "--format-style",
				   "none",
				   "--fake-special-members",
				   "true",
			   });

		const auto result = RunMockfakegen(temp_root, args, "global_mutex");
		Expect(result.exit_code == 0, "global-mutex generation should succeed");
		const auto runtime = ReadText(output_dir / "MockFakeRuntime.h");
		Expect(Contains(runtime, "#include <mutex>"),
			   "global-mutex runtime should include mutex support");
		Expect(Contains(runtime, "std::lock_guard<std::mutex>"),
			   "global-mutex runtime should use mutex locking");
	}

	void ScannerFailureAppearsInManifest(const std::filesystem::path& temp_root,
										 const std::filesystem::path& build_dir)
	{
		const auto output_dir = temp_root / "scanner-failure";
		const auto missing_input = temp_root / "does-not-exist";
		auto args = BaseArgs(missing_input, build_dir, output_dir);
		Append(args,
			   {
				   "--validate",
				   "none",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "scanner_failure");
		Expect(result.exit_code == 1, "scanner failure should return non-zero");
		Expect(std::filesystem::exists(output_dir / "manifest.json"),
			   "scanner failure should emit manifest");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "scanner failure should emit report");
		const auto manifest = ReadText(output_dir / "manifest.json");
		Expect(Contains(manifest, "\"component\": \"scanner\""),
			   "scanner diagnostic should appear in manifest");
		Expect(Contains(manifest, "input root does not exist"),
			   "scanner diagnostic message should appear in manifest");
	}

	void ValidationFailureAppearsInManifest(const std::filesystem::path& temp_root,
											const std::filesystem::path& product_dir,
											const std::filesystem::path& build_dir)
	{
#if defined(__unix__)
		unsetenv("MOCKFAKEGEN_GMOCK_INCLUDE_DIRS");
#endif
		const auto output_dir = temp_root / "validation-failure";
		const auto artifact_dir = temp_root / "validation-artifacts";
		auto args = BaseArgs(product_dir, build_dir, output_dir);
		Append(args,
			   {
				   "--validate",
				   "compile",
				   "--format-style",
				   "none",
				   "--fake-special-members",
				   "true",
				   "--validation-keep-artifacts",
				   "--validation-artifact-dir",
				   artifact_dir.string(),
			   });

		const auto result = RunMockfakegen(temp_root, args, "validation_failure");
#if defined(__unix__)
		setenv("MOCKFAKEGEN_GMOCK_INCLUDE_DIRS", MOCKFAKEGEN_GMOCK_INCLUDE_DIRS, 1);
#endif
		Expect(result.exit_code == 1, "validation failure should return non-zero");
		Expect(std::filesystem::exists(output_dir / "manifest.json"),
			   "validation failure should emit manifest");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "validation failure should emit report");
		Expect(!std::filesystem::exists(output_dir / "MockHoge.h"),
			   "validation failure should not publish generated mock header");
		const auto manifest = ReadText(output_dir / "manifest.json");
		Expect(Contains(manifest, "\"component\": \"validation\""),
			   "validation diagnostic should appear in manifest");
		Expect(Contains(manifest, "\"validation_commands\": 2"),
			   "manifest should include validation command count");
		Expect(Contains(manifest, "gMock include path is missing"),
			   "validation diagnostic message should appear in manifest");
		Expect(Contains(manifest, artifact_dir.generic_string()),
			   "manifest should include kept validation artifact path");
		Expect(std::filesystem::exists(artifact_dir),
			   "validation artifact directory should be retained");
	}
} // namespace

int main()
{
	Expect(std::string_view(MOCKFAKEGEN_GMOCK_INCLUDE_DIRS).size() != 0U,
		   "test should receive gMock include dirs");
#if defined(__unix__)
	setenv("MOCKFAKEGEN_GMOCK_INCLUDE_DIRS", MOCKFAKEGEN_GMOCK_INCLUDE_DIRS, 1);
	setenv("MOCKFAKEGEN_CXX_COMPILER", MOCKFAKEGEN_CXX_COMPILER, 1);
#endif

	const auto temp_root = TempRoot();
	const auto product_dir =
		std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR) / "tests/fixtures/hoge/product";
	const auto build_dir = temp_root / "build";
	WriteCompileCommands(build_dir, product_dir);

	GeneratesAndValidatesFromRealCli(temp_root, product_dir, build_dir);
	SyntaxValidationRunsFromRealCli(temp_root, product_dir, build_dir);
	CompileValidationInheritsCompileDatabaseArgs(temp_root);
	DryRunDoesNotPublishFiles(temp_root, product_dir, build_dir);
	OverwriteControlsExistingFiles(temp_root, product_dir, build_dir);
	EmitOptionsControlOptionalArtifacts(temp_root, product_dir, build_dir);
	StrictModeFailsUnsupportedInput(temp_root, product_dir, build_dir);
	RegistryModeAffectsRuntime(temp_root, product_dir, build_dir);
	ScannerFailureAppearsInManifest(temp_root, build_dir);
	ValidationFailureAppearsInManifest(temp_root, product_dir, build_dir);

	std::error_code remove_error;
	std::filesystem::remove_all(temp_root, remove_error);
	return 0;
}
