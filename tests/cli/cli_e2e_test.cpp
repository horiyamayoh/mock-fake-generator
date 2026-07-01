#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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

	[[nodiscard]] std::size_t CountOccurrences(std::string_view text, std::string_view token)
	{
		std::size_t count = 0U;
		std::size_t offset = 0U;
		while (!token.empty() && offset < text.size())
		{
			const auto found = text.find(token, offset);
			if (found == std::string_view::npos)
			{
				break;
			}
			++count;
			offset = found + token.size();
		}
		return count;
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

	[[nodiscard]] std::vector<std::string> SplitPipeList(std::string_view text)
	{
		std::vector<std::string> values;
		std::size_t offset = 0U;
		while (offset <= text.size())
		{
			const auto separator = text.find('|', offset);
			const auto end = separator == std::string_view::npos ? text.size() : separator;
			const auto value = text.substr(offset, end - offset);
			if (!value.empty())
			{
				values.emplace_back(value);
			}
			if (separator == std::string_view::npos)
			{
				break;
			}
			offset = separator + 1U;
		}
		return values;
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

	[[nodiscard]] CommandResult RunShellCommand(const std::filesystem::path& temp_root,
												std::string command,
												std::string_view label)
	{
		const auto stdout_path = temp_root / (std::string(label) + ".stdout.txt");
		const auto stderr_path = temp_root / (std::string(label) + ".stderr.txt");
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

	void DumpCommandFailure(std::string_view label, const CommandResult& result)
	{
		if (result.exit_code == 0)
		{
			return;
		}

		std::cerr << label << " failed with exit code " << result.exit_code << '\n';
		std::cerr << "--- stdout ---\n" << ReadText(result.stdout_path);
		std::cerr << "--- stderr ---\n" << ReadText(result.stderr_path);
	}

	void CompileLinkAndRunGeneratedSmoke(const std::filesystem::path& temp_root,
										 const std::filesystem::path& product_dir,
										 const std::filesystem::path& output_dir)
	{
		const auto smoke_dir = temp_root / "generated-smoke";
		const auto smoke_source = smoke_dir / "generated_smoke.cpp";
		const auto smoke_executable = smoke_dir / "generated_smoke";
		WriteText(smoke_source,
				  "#include <gmock/gmock.h>\n"
				  "#include <gtest/gtest.h>\n"
				  "\n"
				  "#include \"Hoge.h\"\n"
				  "#include \"MockHoge.h\"\n"
				  "\n"
				  "namespace\n"
				  "{\n"
				  "    using ::testing::Return;\n"
				  "\n"
				  "    TEST(CliGeneratedSmoke, ForwardsCallsThroughScopedMock)\n"
				  "    {\n"
				  "        MockHoge mock;\n"
				  "        ScopedMockHoge scoped_mock(mock);\n"
				  "        Hoge hoge;\n"
				  "\n"
				  "        char arg0[] = \"mockfakegen\";\n"
				  "        char* argv[] = {arg0, nullptr};\n"
				  "\n"
				  "        EXPECT_CALL(mock, Initialize(1, argv)).WillOnce(Return(true));\n"
				  "        EXPECT_CALL(mock, DoSomething()).WillOnce(Return(true));\n"
				  "        EXPECT_CALL(mock, Finalize());\n"
				  "\n"
				  "        EXPECT_TRUE(hoge.Initialize(1, argv));\n"
				  "        EXPECT_TRUE(hoge.DoSomething());\n"
				  "        hoge.Finalize();\n"
				  "    }\n"
				  "} // namespace\n");

		std::string compile_command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23";
		compile_command += " -I ";
		compile_command += ShellQuote(product_dir.string());
		compile_command += " -I ";
		compile_command += ShellQuote(output_dir.string());
		for (const auto& include_dir : SplitPipeList(MOCKFAKEGEN_GMOCK_INCLUDE_DIRS))
		{
			compile_command += " -I ";
			compile_command += ShellQuote(include_dir);
		}
		compile_command += ' ';
		compile_command += ShellQuote(smoke_source.string());
		compile_command += ' ';
		compile_command += ShellQuote((output_dir / "FakeHoge.cpp").string());
		for (const auto& link_file : SplitPipeList(MOCKFAKEGEN_GMOCK_LINK_FILES))
		{
			compile_command += ' ';
			compile_command += ShellQuote(link_file);
		}
#if defined(__unix__)
		compile_command += " -pthread";
#endif
		compile_command += " -o ";
		compile_command += ShellQuote(smoke_executable.string());

		Expect(!Contains(compile_command, "third_party/ket"),
			   "generated-output smoke compile should not use ket include directories");
		Expect(!Contains(compile_command, "mockfakegen_ket"),
			   "generated-output smoke compile should not link mockfakegen_ket");
		Expect(!Contains(compile_command, (product_dir / "Hoge.cpp").string()),
			   "generated-output smoke compile should not link the product implementation");

		const auto compile_result =
			RunShellCommand(temp_root, std::move(compile_command), "generated_smoke_compile");
		DumpCommandFailure("generated smoke compile", compile_result);
		Expect(compile_result.exit_code == 0, "generated smoke should compile and link");

		const auto run_result = RunShellCommand(
			temp_root, ShellQuote(smoke_executable.string()), "generated_smoke_run");
		DumpCommandFailure("generated smoke run", run_result);
		Expect(run_result.exit_code == 0, "generated smoke executable should pass");
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
		CompileLinkAndRunGeneratedSmoke(temp_root, product_dir, output_dir);
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
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 -I " +
			ShellQuote(include_dir.string()) + " -I " + ShellQuote(config_dir.string()) +
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

	void CompileValidationAcceptsDeclaratorAwareTypes(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "complex-types-product";
		const auto include_dir = product_root / "include";
		const auto source_dir = product_root / "src";
		const auto build_dir = temp_root / "complex-types-build";
		const auto output_dir = temp_root / "complex-types-generated";
		WriteText(include_dir / "ComplexTypes.h",
				  "#pragma once\n"
				  "class ComplexTypes {\n"
				  "public:\n"
				  "  struct Token {};\n"
				  "  bool Use(Token token, void (*callback)(int, int), int values[]);\n"
				  "};\n");
		const auto source = source_dir / "ComplexTypes.cpp";
		WriteText(source, "#include \"ComplexTypes.h\"\n");
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 -I " +
			ShellQuote(include_dir.string()) + " -c " + ShellQuote(source.string()) +
			" -o complex.o";
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

		const auto result = RunMockfakegen(temp_root, args, "complex_type_validation");
		const auto stdout_text = ReadText(result.stdout_path);
		const auto stderr_text = ReadText(result.stderr_path);
		Expect(result.exit_code == 0, "declarator-aware complex type generation should compile");
		Expect(Contains(stdout_text, "mockfakegen: validation commands 2"),
			   "complex type run should execute compile validation");
		Expect(!Contains(stderr_text, "error [validation]"),
			   "complex type validation should not produce validation errors");
		Expect(std::filesystem::exists(output_dir / "MockComplexTypes.h"),
			   "complex type mock should be generated");
		const auto fake_source = ReadText(output_dir / "FakeComplexTypes.cpp");
		Expect(Contains(fake_source, "void (*callback)(int, int)"),
			   "fake source should contain name-aware function pointer declarator");
		Expect(Contains(fake_source, "ComplexTypes::Token token"),
			   "fake source should qualify public nested type");
	}

	void FinalInterfaceIsUnsupportedBeforeCompileValidation(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "final-interface-product";
		const auto include_dir = product_root / "include";
		const auto source_dir = product_root / "src";
		const auto build_dir = temp_root / "final-interface-build";
		const auto output_dir = temp_root / "final-interface-generated";
		WriteText(include_dir / "IFace.h",
				  "#pragma once\n"
				  "class IFace final {\n"
				  "public:\n"
				  "  virtual ~IFace() = default;\n"
				  "  virtual void Run() = 0;\n"
				  "};\n");
		const auto source = source_dir / "IFace.cpp";
		WriteText(source, "#include \"IFace.h\"\n");
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 -I " +
			ShellQuote(include_dir.string()) + " -c " + ShellQuote(source.string()) + " -o iface.o";
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
				   "--interface-mock",
				   "true",
				   "--validate",
				   "compile",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "final_interface");

		Expect(result.exit_code == 0, "best-effort final interface unsupported should succeed");
		Expect(std::filesystem::exists(output_dir / "MockIFace.h"),
			   "final interface diagnostic mock should be written");
		Expect(std::filesystem::exists(output_dir / "MockFakeRuntime.h"),
			   "diagnostic non-interface mock should include generated runtime");
		Expect(!std::filesystem::exists(output_dir / "FakeIFace.cpp"),
			   "final interface should not publish a fake source");
		const auto mock_header = ReadText(output_dir / "MockIFace.h");
		Expect(!Contains(mock_header, ": public IFace"),
			   "final interface mock should not derive from final product class");
		const auto stderr_text = ReadText(result.stderr_path);
		Expect(!Contains(stderr_text, "cannot derive from"),
			   "final interface should not fail during compile validation");
		Expect(Contains(stderr_text, "final interface class cannot be mocked"),
			   "final interface should emit unsupported diagnostic");
		const auto manifest = ReadText(output_dir / "manifest.json");
		Expect(Contains(manifest, "\"link_ready\": false"),
			   "manifest should mark final interface not link-ready");
		Expect(Contains(manifest, "\"code\": \"unsupported_interface_construct\""),
			   "manifest should include interface construct diagnostic");
		Expect(Contains(manifest, "final interface class cannot be mocked"),
			   "manifest should explain final interface unsupported reason");
	}

	void GeneratedNamesAvoidProductScopeCollisions(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "generated-name-collision-product";
		const auto include_dir = product_root / "include";
		const auto source_dir = product_root / "src";
		const auto build_dir = temp_root / "generated-name-collision-build";
		const auto output_dir = temp_root / "generated-name-collision-generated";
		WriteText(include_dir / "Service.h",
				  "#pragma once\n"
				  "class Service {\n"
				  "public:\n"
				  "  void Run();\n"
				  "};\n"
				  "class MockService {};\n"
				  "using ScopedMockService = int;\n");
		const auto source = source_dir / "Service.cpp";
		WriteText(source,
				  "#include \"Service.h\"\n"
				  "void Service::Run() {}\n");
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 -I " +
			ShellQuote(include_dir.string()) + " -c " + ShellQuote(source.string()) +
			" -o service.o";
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

		const auto result = RunMockfakegen(temp_root, args, "generated_name_collision");

		Expect(result.exit_code == 0, "generated name collision run should compile validate");
		const auto stderr_text = ReadText(result.stderr_path);
		Expect(!Contains(stderr_text, "redefinition"),
			   "generated safe names should avoid class redefinition");
		Expect(!Contains(stderr_text, "conflicting declaration"),
			   "generated safe names should avoid alias conflict");
		const auto mock_header = ReadText(output_dir / "MockService.h");
		const auto fake_source = ReadText(output_dir / "FakeService.cpp");
		Expect(Contains(mock_header, "class MockFakeService"),
			   "mock header should use safe mock class name");
		Expect(!Contains(mock_header, "class MockService\n"),
			   "mock header should not redeclare product mock name");
		Expect(Contains(mock_header,
						"using ScopedMockFakeService = ::mockfake::ScopedMock<MockFakeService>;"),
			   "mock header should use safe scoped mock alias");
		Expect(!Contains(mock_header, "using ScopedMockService = ::mockfake"),
			   "mock header should not redeclare product scoped alias");
		Expect(Contains(fake_source, "MockFakeService"),
			   "fake source should use safe mock class name");
	}

	void NestedGeneratedOutputIsNotReingested(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "scanner-generated-product";
		const auto build_dir = temp_root / "scanner-generated-build";
		const auto output_dir = product_root / "generated";
		WriteText(product_root / "Product.h",
				  "#pragma once\n"
				  "class Product {\n"
				  "public:\n"
				  "  bool Run();\n"
				  "};\n");
		WriteText(product_root / "Product.cpp", "#include \"Product.h\"\n");
		WriteText(output_dir / "MockFakeRuntime.h", "#pragma once\n");
		WriteText(output_dir / "AllMocks.h", "#pragma once\n");
		WriteText(output_dir / "MockProduct.h",
				  "#pragma once\n"
				  "#include <gmock/gmock.h>\n"
				  "#include \"MockFakeRuntime.h\"\n"
				  "class MockProduct {};\n");
		const auto source = product_root / "Product.cpp";
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 -I " +
			ShellQuote(product_root.string()) + " -c " + ShellQuote(source.string()) +
			" -o product.o";
		WriteSingleCompileCommand(build_dir, product_root, source, command);

		auto args = BaseArgs(product_root, build_dir, output_dir);
		Append(args,
			   {
				   "--overwrite",
				   "--validate",
				   "none",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "scanner_generated_output");
		const auto stdout_text = ReadText(result.stdout_path);
		const auto stderr_text = ReadText(result.stderr_path);
		Expect(result.exit_code == 0, "nested generated output run should succeed");
		Expect(Contains(stdout_text, "mockfakegen: scanned 1 header(s)"),
			   "nested generated output should not be scanned as product input");
		Expect(Contains(stderr_text, "scanner_skipped_generated_output") ||
				   Contains(stderr_text, "skipped configured output directory"),
			   "nested generated output skip should be diagnosed");
		Expect(std::filesystem::exists(output_dir / "MockProduct.h"),
			   "product mock should be generated");
		Expect(!std::filesystem::exists(output_dir / "MockMockProduct.h"),
			   "generated MockProduct.h should not be reingested");
		const auto manifest = ReadText(output_dir / "manifest.json");
		Expect(Contains(manifest, "\"code\": \"scanner_skipped_generated_output\""),
			   "manifest should include generated output skip diagnostic");
	}

	void QualifiedFilenameCollisionsAppearInCliArtifacts(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "qualified-collision-product";
		const auto include_dir = product_root / "include";
		const auto source_dir = product_root / "src";
		const auto build_dir = temp_root / "qualified-collision-build";
		const auto output_dir = temp_root / "qualified-collision-generated";
		WriteText(include_dir / "a/Hoge.h",
				  "#pragma once\n"
				  "namespace a {\n"
				  "class Hoge { public: bool RunA(); };\n"
				  "} // namespace a\n");
		WriteText(include_dir / "b/Hoge.h",
				  "#pragma once\n"
				  "namespace b {\n"
				  "class Hoge { public: bool RunB(); };\n"
				  "} // namespace b\n");
		const auto source = source_dir / "Hoge.cpp";
		WriteText(source,
				  "#include \"a/Hoge.h\"\n"
				  "#include \"b/Hoge.h\"\n"
				  "bool a::Hoge::RunA() { return true; }\n"
				  "bool b::Hoge::RunB() { return true; }\n");
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 -I " +
			ShellQuote(include_dir.string()) + " -c " + ShellQuote(source.string()) + " -o hoge.o";
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
				   "none",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "qualified_collision_cli");

		Expect(result.exit_code == 0, "qualified filename collision run should succeed");
		Expect(std::filesystem::exists(output_dir / "Mock_a_Hoge.h"),
			   "resolved a::Hoge mock should be written");
		Expect(std::filesystem::exists(output_dir / "Mock_b_Hoge.h"),
			   "resolved b::Hoge mock should be written");
		Expect(std::filesystem::exists(output_dir / "Fake_a_Hoge.cpp"),
			   "resolved a::Hoge fake should be written");
		Expect(std::filesystem::exists(output_dir / "Fake_b_Hoge.cpp"),
			   "resolved b::Hoge fake should be written");
		Expect(!std::filesystem::exists(output_dir / "MockHoge.h"),
			   "pre-collision mock filename should not be written");
		Expect(!std::filesystem::exists(output_dir / "FakeHoge.cpp"),
			   "pre-collision fake filename should not be written");

		const auto manifest = ReadText(output_dir / "manifest.json");
		const auto report = ReadText(output_dir / "generation_report.md");
		Expect(Contains(manifest, "\"mock_header\": \"Mock_a_Hoge.h\""),
			   "manifest should use resolved a::Hoge mock filename");
		Expect(Contains(manifest, "\"mock_header\": \"Mock_b_Hoge.h\""),
			   "manifest should use resolved b::Hoge mock filename");
		Expect(Contains(manifest, "\"fake_source\": \"Fake_a_Hoge.cpp\""),
			   "manifest should use resolved a::Hoge fake filename");
		Expect(Contains(manifest, "\"fake_source\": \"Fake_b_Hoge.cpp\""),
			   "manifest should use resolved b::Hoge fake filename");
		Expect(Contains(manifest, "\"filename_collision\""),
			   "manifest should include filename collision metadata");
		Expect(Contains(manifest, "\"resolved_mock_header\": \"Mock_a_Hoge.h\""),
			   "manifest should record resolved mock filename");
		Expect(Contains(report, "| a::Hoge |"), "report should include a::Hoge class row");
		Expect(Contains(report, "Mock_a_Hoge.h"),
			   "report should use resolved a::Hoge mock filename");
		Expect(Contains(report, "Fake_b_Hoge.cpp"),
			   "report should use resolved b::Hoge fake filename");
		Expect(!Contains(report, "| MockHoge.h | FakeHoge.cpp |"),
			   "report should not use pre-collision filenames in class rows");
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
				   "--best-effort",
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

	void StrictModeFailsUnsupportedInput(const std::filesystem::path& temp_root)
	{
		const auto product_dir = temp_root / "strict-product";
		const auto build_dir = temp_root / "strict-build";
		const auto output_dir = temp_root / "strict";
		WriteText(product_dir / "Hard.h",
				  "#pragma once\n"
				  "class Hard {\n"
				  "public:\n"
				  "  bool Inline() { return true; }\n"
				  "  bool Supported();\n"
				  "};\n");
		WriteText(product_dir / "Hard.cpp",
				  "#include \"Hard.h\"\n"
				  "bool Hard::Supported() { return true; }\n");
		const auto source = product_dir / "Hard.cpp";
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 -I " +
			ShellQuote(product_dir.string()) + " -c " + ShellQuote(source.string()) + " -o hard.o";
		WriteSingleCompileCommand(build_dir, product_dir, source, command);

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
		Expect(result.exit_code == 1, "strict mode should fail unsupported inline methods");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "strict mode should still emit report when policy allows it");
		Expect(Contains(ReadText(output_dir / "generation_report.md"),
						"inline method body is not supported"),
			   "strict report should include unsupported reason");
	}

	void TopLevelUnsupportedAppearsInManifest(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "top-level-unsupported-product";
		const auto include_dir = product_root / "include";
		const auto source_dir = product_root / "src";
		const auto build_dir = temp_root / "top-level-unsupported-build";
		const auto output_dir = temp_root / "top-level-unsupported-generated";
		WriteText(include_dir / "Box.h",
				  "#pragma once\n"
				  "#define MOCKFAKEGEN_DECLARE_METHOD(name) bool name();\n"
				  "template <class T> class Box { public: T value; };\n"
				  "class Hard {\n"
				  "public:\n"
				  "  virtual int Abstract() = 0;\n"
				  "  void HeaderBody();\n"
				  "  MOCKFAKEGEN_DECLARE_METHOD(FromMacro)\n"
				  "  [[clang::annotate(\"mockfakegen\")]] int Marked();\n"
				  "  consteval int Immediate() const { return 1; }\n"
				  "  bool Supported();\n"
				  "};\n"
				  "inline void Hard::HeaderBody() {}\n"
				  "class Worker { public: bool Run(); };\n");
		const auto source = source_dir / "Worker.cpp";
		WriteText(source,
				  "#include \"Box.h\"\n"
				  "bool Worker::Run() { return true; }\n");
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 -I " +
			ShellQuote(include_dir.string()) + " -c " + ShellQuote(source.string()) +
			" -o worker.o";
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
				   "none",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "top_level_unsupported");

		Expect(result.exit_code == 0, "best-effort top-level unsupported should succeed");
		Expect(std::filesystem::exists(output_dir / "manifest.json"),
			   "top-level unsupported run should emit manifest");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "top-level unsupported run should emit report");
		const auto manifest = ReadText(output_dir / "manifest.json");
		const auto report = ReadText(output_dir / "generation_report.md");
		Expect(Contains(manifest, "\"unsupported_items\": 6"),
			   "manifest summary should count top-level unsupported items");
		Expect(Contains(manifest, "\"code\": \"unsupported_class_template\""),
			   "manifest should include top-level unsupported diagnostic code");
		Expect(Contains(manifest, "class template is not supported"),
			   "manifest should include top-level unsupported reason");
		Expect(Contains(report, "unsupported_class_template"),
			   "report diagnostics should include top-level unsupported diagnostic code");
		Expect(Contains(report, "class template is not supported"),
			   "report should include top-level unsupported reason");
		Expect(Contains(report, "| Box | Box | class template is not supported"),
			   "report unsupported table should include top-level unsupported item");
		Expect(Contains(manifest, "\"code\": \"unsupported_pure_virtual_method\""),
			   "manifest should include pure virtual unsupported diagnostic code");
		Expect(Contains(manifest, "\"code\": \"unsupported_inline_body\""),
			   "manifest should include inline-body unsupported diagnostic code");
		Expect(Contains(manifest, "\"code\": \"unsupported_macro_origin\""),
			   "manifest should include macro-origin unsupported diagnostic code");
		Expect(Contains(manifest, "\"code\": \"unsupported_attribute\""),
			   "manifest should include attribute unsupported diagnostic code");
		Expect(Contains(manifest, "\"code\": \"unsupported_consteval_method\""),
			   "manifest should include consteval unsupported diagnostic code");
	}

	void PrivateTypeUsesAppearUnsupportedInManifest(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "private-type-product";
		const auto include_dir = product_root / "include";
		const auto source_dir = product_root / "src";
		const auto build_dir = temp_root / "private-type-build";
		const auto output_dir = temp_root / "private-type-generated";
		WriteText(include_dir / "PrivateTypeUses.h",
				  "#pragma once\n"
				  "#include <vector>\n"
				  "class AliasPrivate {\n"
				  "private:\n"
				  "  using Hidden = int;\n"
				  "public:\n"
				  "  Hidden Get();\n"
				  "  void Put(Hidden value);\n"
				  "  bool Supported();\n"
				  "};\n"
				  "class UsesPrivateTemplateArg {\n"
				  "private:\n"
				  "  enum class Hidden {};\n"
				  "public:\n"
				  "  std::vector<Hidden> Items();\n"
				  "  void Put(std::vector<Hidden> values);\n"
				  "  int Supported();\n"
				  "};\n");
		const auto source = source_dir / "PrivateTypeUses.cpp";
		WriteText(source,
				  "#include \"PrivateTypeUses.h\"\n"
				  "bool AliasPrivate::Supported() { return true; }\n"
				  "int UsesPrivateTemplateArg::Supported() { return 0; }\n");
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 -I " +
			ShellQuote(include_dir.string()) + " -c " + ShellQuote(source.string()) +
			" -o private_types.o";
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
				   "none",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "private_type_unsupported");

		Expect(result.exit_code == 0, "best-effort private type unsupported should succeed");
		Expect(std::filesystem::exists(output_dir / "manifest.json"),
			   "private type unsupported run should emit manifest");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "private type unsupported run should emit report");
		const auto manifest = ReadText(output_dir / "manifest.json");
		const auto report = ReadText(output_dir / "generation_report.md");
		Expect(Contains(manifest, "\"unsupported_items\": 4"),
			   "manifest summary should count private type unsupported items");
		Expect(Contains(manifest, "\"not_link_ready_classes\": 2"),
			   "manifest summary should mark both classes not link-ready");
		Expect(Contains(manifest, "\"link_ready\": false"),
			   "manifest class entries should not be link-ready");
		Expect(Contains(manifest, "\"code\": \"unsupported_private_nested_type\""),
			   "manifest should include private nested type diagnostic code");
		Expect(Contains(manifest, "AliasPrivate::Hidden"), "manifest should name private alias");
		Expect(Contains(manifest, "UsesPrivateTemplateArg::Hidden"),
			   "manifest should name private template argument");
		Expect(Contains(report, "| AliasPrivate |"),
			   "report should include alias private class entry");
		Expect(Contains(report, "| UsesPrivateTemplateArg |"),
			   "report should include template argument class entry");
		Expect(Contains(report, "| no |"), "report should mark affected classes not link-ready");
	}

	void RealTuFailureSurvivesSyntheticFallbackInManifest(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "real-tu-failure-product";
		const auto include_dir = product_root / "include";
		const auto source_dir = product_root / "src";
		const auto build_dir = temp_root / "real-tu-failure-build";
		const auto output_dir = temp_root / "real-tu-failure-generated";
		WriteText(include_dir / "Stable.h",
				  "#pragma once\n"
				  "#ifndef FROM_REAL_TU_FAILURE_DB\n"
				  "#error expected compile database args for synthetic fallback\n"
				  "#endif\n"
				  "class Stable { public: bool Run(); };\n");
		const auto source = source_dir / "Broken.cpp";
		WriteText(source,
				  "#include \"MissingFromRealTu.h\"\n"
				  "int broken_real_tu() { return 0; }\n");
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 -I " +
			ShellQuote(include_dir.string()) + " -DFROM_REAL_TU_FAILURE_DB -c " +
			ShellQuote(source.string()) + " -o broken.o";
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
				   "none",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "real_tu_failure_fallback");
		Expect(result.exit_code == 0,
			   "best-effort generation should succeed through synthetic fallback");
		Expect(std::filesystem::exists(output_dir / "MockStable.h"),
			   "synthetic fallback should publish generated mock");
		Expect(std::filesystem::exists(output_dir / "manifest.json"),
			   "fallback run should emit manifest");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "fallback run should emit report");
		const auto manifest = ReadText(output_dir / "manifest.json");
		const auto report = ReadText(output_dir / "generation_report.md");
		Expect(Contains(manifest, "\"code\": \"real_tu_parse_failure\""),
			   "manifest should retain real TU failure diagnostic");
		Expect(Contains(manifest, "\"parse_mode\": \"synthetic-tu\""),
			   "manifest should record that synthetic fallback produced the class");
		Expect(Contains(manifest, "\"command\": \"cd "),
			   "manifest should include failed real TU command");
		Expect(Contains(manifest, "MissingFromRealTu.h"),
			   "manifest should include clang diagnostic summary");
		Expect(Contains(report, "real_tu_parse_failure"),
			   "report should retain real TU failure diagnostic");
		Expect(Contains(report, "synthetic-tu"),
			   "report should record synthetic fallback parse mode");
		Expect(Contains(report, "MissingFromRealTu.h"),
			   "report should include clang diagnostic summary");
	}

	void MissingCompileDatabaseDiagnosticIsPrintedOnce(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "missing-compile-db-product";
		const auto build_dir = temp_root / "missing-compile-db-build";
		const auto output_dir = temp_root / "missing-compile-db-generated";
		std::filesystem::create_directories(build_dir);
		WriteText(product_root / "Service.h",
				  "#pragma once\n"
				  "class Service {\n"
				  "public:\n"
				  "  bool Run();\n"
				  "};\n");

		auto args = BaseArgs(product_root, build_dir, output_dir);
		Append(args,
			   {
				   "--validate",
				   "none",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "missing_compile_db");

		Expect(result.exit_code == 0, "missing compile database fallback should succeed");
		Expect(std::filesystem::exists(output_dir / "MockService.h"),
			   "synthetic fallback should publish generated mock");
		constexpr std::string_view warning =
			"compile_commands.json was not found; using synthetic TU fallback.";
		const auto stderr_text = ReadText(result.stderr_path);
		Expect(CountOccurrences(stderr_text, warning) == 1U,
			   "missing compile database warning should be printed once");
		const auto manifest = ReadText(output_dir / "manifest.json");
		Expect(Contains(manifest, "\"code\": \"compile_database_not_found\""),
			   "manifest should retain missing compile database diagnostic");
		Expect(Contains(manifest, "synthetic-tu"),
			   "manifest should record synthetic fallback parse mode");
	}

	void CliCompilerArgsRescueMissingCompileDatabase(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "cli-compiler-args-product";
		const auto include_dir = product_root / "include";
		const auto sdk_dir = product_root / "sdk";
		const auto build_dir = temp_root / "cli-compiler-args-build";
		const auto output_dir = temp_root / "cli-compiler-args-generated";
		std::filesystem::create_directories(build_dir);
		WriteText(sdk_dir / "Dependency.h", "#pragma once\nstruct Dependency { int value; };\n");
		WriteText(include_dir / "CliArgs.h",
				  "#pragma once\n"
				  "#ifndef CLI_DEFINE_ENABLED\n"
				  "#error expected CLI define\n"
				  "#endif\n"
				  "#ifndef CLI_EXTRA_ARG_ENABLED\n"
				  "#error expected CLI extra arg\n"
				  "#endif\n"
				  "#include \"Dependency.h\"\n"
				  "class CliArgs {\n"
				  "public:\n"
				  "  bool Run(Dependency dependency);\n"
				  "};\n");

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
				   "--include-dir",
				   sdk_dir.string(),
				   "--define",
				   "CLI_DEFINE_ENABLED=1",
				   "--extra-arg",
				   "-DCLI_EXTRA_ARG_ENABLED=1",
				   "--validate",
				   "compile",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "cli_compiler_args");

		Expect(result.exit_code == 0, "CLI compiler args should rescue missing compile database");
		const auto stdout_text = ReadText(result.stdout_path);
		const auto stderr_text = ReadText(result.stderr_path);
		Expect(Contains(stdout_text, "mockfakegen: validation commands 2"),
			   "rescued CLI compiler args should reach compile validation");
		Expect(!Contains(stderr_text, "synthetic TU parse failed"),
			   "CLI compiler args should prevent synthetic parse failure");
		Expect(!Contains(stderr_text, "error [validation]"),
			   "CLI compiler args should prevent validation failure");
		Expect(std::filesystem::exists(output_dir / "MockCliArgs.h"),
			   "rescued CLI compiler args should publish mock header");
		Expect(std::filesystem::exists(output_dir / "FakeCliArgs.cpp"),
			   "rescued CLI compiler args should publish fake source");
		const auto manifest = ReadText(output_dir / "manifest.json");
		Expect(Contains(manifest, "\"code\": \"compile_database_not_found\""),
			   "manifest should retain missing compile database warning");
		Expect(Contains(manifest, "\"parse_mode\": \"synthetic-tu\""),
			   "manifest should record synthetic fallback parse mode");
	}

	void PathMapRescuesContainerCompileDatabase(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "path-map-product";
		const auto include_dir = product_root / "include";
		const auto source_dir = product_root / "src";
		const auto build_dir = temp_root / "path-map-build";
		const auto output_dir = temp_root / "path-map-generated";
		WriteText(include_dir / "PathMapped.h",
				  "#pragma once\n"
				  "#ifndef FROM_CONTAINER_DB\n"
				  "#error expected mapped compile database define\n"
				  "#endif\n"
				  "#include \"PathMappedDependency.h\"\n"
				  "class PathMapped {\n"
				  "public:\n"
				  "  bool Run(PathMappedDependency dependency);\n"
				  "};\n");
		WriteText(include_dir / "PathMappedDependency.h",
				  "#pragma once\nstruct PathMappedDependency { int value; };\n");
		const auto source = source_dir / "PathMapped.cpp";
		WriteText(source, "#include \"PathMapped.h\"\n");
		const auto container_source = std::filesystem::path("/workspace/src/PathMapped.cpp");
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) +
			" -std=c++23 -I/workspace/include -DFROM_CONTAINER_DB -c " +
			ShellQuote(container_source.string()) + " -o mapped.o";
		WriteSingleCompileCommand(build_dir, "/workspace", container_source, command);

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
				   "--path-map",
				   "/workspace=" + product_root.string(),
				   "--validate",
				   "compile",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "path_map_container_db");

		Expect(result.exit_code == 0, "path map should rescue container compile database");
		const auto stdout_text = ReadText(result.stdout_path);
		const auto stderr_text = ReadText(result.stderr_path);
		Expect(Contains(stdout_text, "mockfakegen: validation commands 2"),
			   "path-mapped compile database should reach compile validation");
		Expect(!Contains(stderr_text, "translation unit could not be read"),
			   "path map should prevent container source read failure");
		Expect(!Contains(stderr_text, "error [validation]"),
			   "path-mapped validation should succeed");
		Expect(std::filesystem::exists(output_dir / "MockPathMapped.h"),
			   "path map should publish mock header");
		const auto manifest = ReadText(output_dir / "manifest.json");
		Expect(Contains(manifest, "\"parse_mode\": \"real-tu\""),
			   "manifest should record real TU parsing through path map");
		Expect(Contains(manifest, (product_root / "include").generic_string()),
			   "manifest validation commands should use host-mapped include path");
		Expect(!Contains(manifest, "/workspace/"),
			   "manifest validation commands should not retain container paths");
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

	void FallbackIncompatibleOutputIsNotReportedUsable(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "fallback-incompatible-product";
		const auto include_dir = product_root / "include";
		const auto source_dir = product_root / "src";
		const auto build_dir = temp_root / "fallback-incompatible-build";
		const auto output_dir = temp_root / "fallback-incompatible-generated";
		WriteText(include_dir / "FallbackNoexcept.h",
				  "#pragma once\n"
				  "class FallbackNoexcept {\n"
				  "public:\n"
				  "  bool Save() noexcept;\n"
				  "};\n");
		const auto source = source_dir / "FallbackNoexcept.cpp";
		WriteText(source,
				  "#include \"FallbackNoexcept.h\"\n"
				  "bool FallbackNoexcept::Save() noexcept { return true; }\n");
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 -I " +
			ShellQuote(include_dir.string()) + " -c " + ShellQuote(source.string()) +
			" -o fallback_noexcept.o";
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
				   "--fallback-policy",
				   "throw",
				   "--validate",
				   "none",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "fallback_incompatible");

		Expect(result.exit_code == 1, "fallback-incompatible output should fail policy");
		Expect(!std::filesystem::exists(output_dir / "MockFallbackNoexcept.h"),
			   "fallback-incompatible mock should not be published");
		Expect(!std::filesystem::exists(output_dir / "FakeFallbackNoexcept.cpp"),
			   "fallback-incompatible fake should not be published");
		Expect(std::filesystem::exists(output_dir / "manifest.json"),
			   "fallback-incompatible run should emit manifest");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "fallback-incompatible run should emit report");
		const auto manifest = ReadText(output_dir / "manifest.json");
		const auto report = ReadText(output_dir / "generation_report.md");
		Expect(Contains(manifest, "\"link_ready_classes\": 0"),
			   "manifest should not count fallback-incompatible class as link-ready");
		Expect(Contains(manifest, "\"not_link_ready_classes\": 1"),
			   "manifest should count fallback-incompatible class as not link-ready");
		Expect(Contains(manifest, "\"usable_fake_sources\": []"),
			   "manifest should list no usable fake sources");
		Expect(Contains(manifest, "\"link_ready\": false"),
			   "manifest class entry should be not link-ready");
		Expect(Contains(manifest, "throw fallback cannot be used for noexcept functions"),
			   "manifest should include fallback incompatibility reason");
		Expect(Contains(report, "- none; no class is link-ready."),
			   "report should not advertise usable fake sources");
		Expect(!Contains(report, "- `FakeFallbackNoexcept.cpp`"),
			   "report usable source list should omit fallback-incompatible fake");
	}

	void LinkValidationFailureAppearsAsLinkDiagnostic(const std::filesystem::path& temp_root,
													  const std::filesystem::path& product_dir,
													  const std::filesystem::path& build_dir)
	{
#if defined(__unix__)
		auto link_files = std::string(MOCKFAKEGEN_GMOCK_LINK_FILES);
		if (!link_files.empty())
		{
			link_files += '|';
		}
		link_files += (product_dir / "Hoge.cpp").string();
		setenv("MOCKFAKEGEN_GMOCK_LINK_FILES", link_files.c_str(), 1);
#endif
		const auto output_dir = temp_root / "link-validation-failure";
		auto args = BaseArgs(product_dir, build_dir, output_dir);
		Append(args,
			   {
				   "--validate",
				   "link",
				   "--format-style",
				   "none",
				   "--fake-special-members",
				   "true",
			   });

		const auto result = RunMockfakegen(temp_root, args, "link_validation_failure");
#if defined(__unix__)
		setenv("MOCKFAKEGEN_GMOCK_LINK_FILES", MOCKFAKEGEN_GMOCK_LINK_FILES, 1);
#endif

		Expect(result.exit_code == 1, "link validation failure should return non-zero");
		Expect(std::filesystem::exists(output_dir / "manifest.json"),
			   "link validation failure should emit manifest");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "link validation failure should emit report");
		Expect(!std::filesystem::exists(output_dir / "MockHoge.h"),
			   "link validation failure should not publish generated mock header");
		const auto stderr_text = ReadText(result.stderr_path);
		Expect(Contains(stderr_text, "generated output link validation failed"),
			   "stderr should identify link validation failure");
		const auto manifest = ReadText(output_dir / "manifest.json");
		Expect(Contains(manifest, "\"code\": \"link_validation_failure\""),
			   "manifest should use link validation code");
		Expect(Contains(manifest, "\"kind\": \"link\""),
			   "manifest should use link validation kind");
		Expect(!Contains(manifest, "\"code\": \"compile_validation_failure\""),
			   "manifest should not label link failure as compile validation");
		Expect(Contains(manifest, "do not link product .cpp files"),
			   "manifest should include link-substitution guidance");
		const auto report = ReadText(output_dir / "generation_report.md");
		Expect(Contains(report, "link_validation_failure"),
			   "report should use link validation code");
		Expect(Contains(report, "do not link product .cpp files"),
			   "report should include link-substitution guidance");
	}

	void KetContaminatedGeneratedOutputFailsPolicy(const std::filesystem::path& temp_root)
	{
		const auto product_root = temp_root / "ket-contaminated-product";
		const auto include_dir = product_root / "include";
		const auto source_dir = product_root / "src";
		const auto build_dir = temp_root / "ket-contaminated-build";
		const auto output_dir = temp_root / "ket-contaminated-generated";
		WriteText(include_dir / "Service.h",
				  "#pragma once\n"
				  "namespace ket {\n"
				  "class Service {\n"
				  "public:\n"
				  "  bool Run();\n"
				  "};\n"
				  "} // namespace ket\n");
		const auto source = source_dir / "Service.cpp";
		WriteText(source,
				  "#include \"Service.h\"\n"
				  "bool ket::Service::Run() { return true; }\n");
		const auto command = std::string(MOCKFAKEGEN_CXX_COMPILER) + " -std=c++23 -I " +
			ShellQuote(include_dir.string()) + " -c " + ShellQuote(source.string()) +
			" -o service.o";
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
				   "none",
				   "--format-style",
				   "none",
			   });

		const auto result = RunMockfakegen(temp_root, args, "ket_contaminated_output");

		Expect(result.exit_code == 1, "ket-contaminated generated output should fail policy");
		Expect(std::filesystem::exists(output_dir / "manifest.json"),
			   "ket contamination should emit manifest");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "ket contamination should emit report");
		Expect(!std::filesystem::exists(output_dir / "MockService.h"),
			   "ket-contaminated mock header should not be published");
		Expect(!std::filesystem::exists(output_dir / "FakeService.cpp"),
			   "ket-contaminated fake source should not be published");
		const auto manifest = ReadText(output_dir / "manifest.json");
		Expect(Contains(manifest, "\"component\": \"ket-contamination\""),
			   "manifest should include generated-output token diagnostic");
		Expect(Contains(manifest, "\"code\": \"generated_output_forbidden_token\""),
			   "manifest should include forbidden-token diagnostic code");
		Expect(Contains(manifest, "\"kind\": \"ket::\""),
			   "manifest should include forbidden ket token kind");
		Expect(Contains(manifest, "\"kind\": \"ket_contamination\""),
			   "manifest should include policy ket-contamination diagnostic");
		const auto report = ReadText(output_dir / "generation_report.md");
		Expect(Contains(report, "ket-contamination"),
			   "report should include generated-output token diagnostic");
		Expect(Contains(report, "ket_contamination"),
			   "report should include policy ket-contamination diagnostic");
	}

	void InvalidValidationArtifactDirAppearsInManifest(const std::filesystem::path& temp_root,
													   const std::filesystem::path& product_dir,
													   const std::filesystem::path& build_dir)
	{
		const auto output_dir = temp_root / "invalid-validation-artifact-dir";
		const auto artifact_file = temp_root / "validation-artifact-file";
		WriteText(artifact_file, "x");
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
				   artifact_file.string(),
			   });

		const auto result = RunMockfakegen(temp_root, args, "invalid_validation_artifact_dir");

		Expect(result.exit_code == 1, "invalid validation artifact dir should return non-zero");
		Expect(std::filesystem::exists(output_dir / "manifest.json"),
			   "invalid validation artifact dir should emit manifest");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "invalid validation artifact dir should emit report");
		Expect(!std::filesystem::exists(output_dir / "MockHoge.h"),
			   "invalid validation artifact dir should not publish generated mock header");
		const auto manifest = ReadText(output_dir / "manifest.json");
		Expect(Contains(manifest, "\"component\": \"validation\""),
			   "invalid artifact diagnostic should appear in manifest");
		Expect(Contains(manifest, "\"validation_commands\": 0"),
			   "invalid artifact diagnostic should not run compiler commands");
		Expect(Contains(manifest, "invalid validation artifact directory"),
			   "invalid artifact diagnostic message should appear in manifest");
		Expect(Contains(manifest, "validation-artifact-file"),
			   "invalid artifact diagnostic should name the bad path");
	}
} // namespace

int main()
{
	Expect(std::string_view(MOCKFAKEGEN_GMOCK_INCLUDE_DIRS).size() != 0U,
		   "test should receive gMock include dirs");
	Expect(std::string_view(MOCKFAKEGEN_GMOCK_LINK_FILES).size() != 0U,
		   "test should receive gMock link files");
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
	CompileValidationAcceptsDeclaratorAwareTypes(temp_root);
	FinalInterfaceIsUnsupportedBeforeCompileValidation(temp_root);
	GeneratedNamesAvoidProductScopeCollisions(temp_root);
	NestedGeneratedOutputIsNotReingested(temp_root);
	QualifiedFilenameCollisionsAppearInCliArtifacts(temp_root);
	DryRunDoesNotPublishFiles(temp_root, product_dir, build_dir);
	OverwriteControlsExistingFiles(temp_root, product_dir, build_dir);
	EmitOptionsControlOptionalArtifacts(temp_root, product_dir, build_dir);
	StrictModeFailsUnsupportedInput(temp_root);
	TopLevelUnsupportedAppearsInManifest(temp_root);
	PrivateTypeUsesAppearUnsupportedInManifest(temp_root);
	RealTuFailureSurvivesSyntheticFallbackInManifest(temp_root);
	MissingCompileDatabaseDiagnosticIsPrintedOnce(temp_root);
	CliCompilerArgsRescueMissingCompileDatabase(temp_root);
	PathMapRescuesContainerCompileDatabase(temp_root);
	RegistryModeAffectsRuntime(temp_root, product_dir, build_dir);
	ScannerFailureAppearsInManifest(temp_root, build_dir);
	ValidationFailureAppearsInManifest(temp_root, product_dir, build_dir);
	FallbackIncompatibleOutputIsNotReportedUsable(temp_root);
	LinkValidationFailureAppearsAsLinkDiagnostic(temp_root, product_dir, build_dir);
	KetContaminatedGeneratedOutputFailsPolicy(temp_root);
	InvalidValidationArtifactDirAppearsInManifest(temp_root, product_dir, build_dir);

	std::error_code remove_error;
	std::filesystem::remove_all(temp_root, remove_error);
	return 0;
}
