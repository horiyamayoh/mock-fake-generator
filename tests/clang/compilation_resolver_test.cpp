#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "CompilationResolver.h"

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

	class TempTree
	{
	  public:
		TempTree()
			: root_(std::filesystem::temp_directory_path() /
					("mockfakegen_compilation_resolver_test_" + std::to_string(UniqueSuffix())))
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
			Expect(stream.good(), "test file should be written");
		}

	  private:
		[[nodiscard]] static long long UniqueSuffix()
		{
			return std::chrono::steady_clock::now().time_since_epoch().count();
		}

		std::filesystem::path root_;
	};

	struct CompileEntry
	{
		std::filesystem::path directory = {};
		std::filesystem::path source;
		std::vector<std::string> args;
	};

	class ScopedCurrentPath
	{
	  public:
		explicit ScopedCurrentPath(const std::filesystem::path& path)
			: previous_(std::filesystem::current_path())
		{
			std::filesystem::current_path(path);
		}

		ScopedCurrentPath(const ScopedCurrentPath&) = delete;
		ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

		~ScopedCurrentPath()
		{
			std::filesystem::current_path(previous_);
		}

	  private:
		std::filesystem::path previous_;
	};

	[[nodiscard]] std::string JsonEscape(std::string text)
	{
		std::string escaped;
		for (const char character : text)
		{
			if (character == '\\' || character == '"')
			{
				escaped += '\\';
			}
			escaped += character;
		}
		return escaped;
	}

	void WriteCompileCommandsAt(const TempTree& tree,
								std::string_view build_relative_path,
								const std::vector<CompileEntry>& entries)
	{
		std::string json = "[\n";
		for (std::size_t entry_index = 0U; entry_index < entries.size(); ++entry_index)
		{
			const auto& entry = entries[entry_index];
			const auto directory = entry.directory.empty() ? tree.root() : entry.directory;
			json += "  {\n";
			json += "    \"directory\": \"" + JsonEscape(directory.generic_string()) + "\",\n";
			json += "    \"file\": \"" + JsonEscape(entry.source.generic_string()) + "\",\n";
			json += "    \"arguments\": [";
			for (std::size_t arg_index = 0U; arg_index < entry.args.size(); ++arg_index)
			{
				if (arg_index != 0U)
				{
					json += ", ";
				}
				json += "\"" + JsonEscape(entry.args[arg_index]) + "\"";
			}
			json += "]\n";
			json += "  }";
			if (entry_index + 1U != entries.size())
			{
				json += ',';
			}
			json += '\n';
		}
		json += "]\n";
		const auto compile_database_path =
			(std::filesystem::path(build_relative_path) / "compile_commands.json").generic_string();
		tree.Write(compile_database_path, json);
	}

	void WriteCompileCommands(const TempTree& tree, const std::vector<CompileEntry>& entries)
	{
		WriteCompileCommandsAt(tree, "build", entries);
	}

	[[nodiscard]] mockfakegen::HeaderModel Header(const TempTree& tree,
												  std::string_view relative_path)
	{
		return mockfakegen::HeaderModel{
			.absolute_path = std::filesystem::weakly_canonical(
				tree.root() / std::filesystem::path(relative_path)),
			.project_relative_path = std::filesystem::path(relative_path),
			.include_spelling = std::filesystem::path(relative_path).generic_string(),
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = false,
		};
	}

	[[nodiscard]] mockfakegen::HeaderModel HeaderAt(const std::filesystem::path& project_root,
													std::string_view relative_path)
	{
		return mockfakegen::HeaderModel{
			.absolute_path = std::filesystem::weakly_canonical(
				project_root / std::filesystem::path(relative_path)),
			.project_relative_path = std::filesystem::path(relative_path),
			.include_spelling = std::filesystem::path(relative_path).generic_string(),
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = false,
		};
	}

	[[nodiscard]] bool HasCompileArg(const std::vector<std::string>& args,
									 std::string_view expected)
	{
		for (const auto& arg : args)
		{
			if (arg == expected)
			{
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool HasAdjacentCompileArg(const std::vector<std::string>& args,
											 std::string_view option,
											 std::string_view expected_value)
	{
		for (std::size_t index = 0U; index + 1U < args.size(); ++index)
		{
			if (args[index] == option && args[index + 1U] == expected_value)
			{
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] std::size_t CountCompileArg(const std::vector<std::string>& args,
											  std::string_view expected)
	{
		return static_cast<std::size_t>(
			std::count(args.begin(), args.end(), std::string(expected)));
	}

	[[nodiscard]] bool
	HasDiagnostic(const std::vector<mockfakegen::CompilationResolverDiagnostic>& diagnostics,
				  mockfakegen::CompilationResolverDiagnosticCode code)
	{
		for (const auto& diagnostic : diagnostics)
		{
			if (diagnostic.code == code)
			{
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool HasUnsupportedKind(const mockfakegen::ClassModel& class_model,
										  std::string_view kind)
	{
		for (const auto& item : class_model.unsupported_items)
		{
			if (item.kind == kind)
			{
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] const mockfakegen::CompilationResolverDiagnostic*
	FindDiagnostic(const std::vector<mockfakegen::CompilationResolverDiagnostic>& diagnostics,
				   mockfakegen::CompilationResolverDiagnosticCode code)
	{
		for (const auto& diagnostic : diagnostics)
		{
			if (diagnostic.code == code)
			{
				return &diagnostic;
			}
		}
		return nullptr;
	}

	[[nodiscard]] bool DiagnosticMessageContains(
		const std::vector<mockfakegen::CompilationResolverDiagnostic>& diagnostics,
		mockfakegen::CompilationResolverDiagnosticCode code,
		std::string_view expected)
	{
		for (const auto& diagnostic : diagnostics)
		{
			if (diagnostic.code == code && diagnostic.message.find(expected) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}

	void ParsesHeaderThroughRealTu()
	{
		TempTree tree;
		tree.Write("include/RealService.h",
				   "#pragma once\n"
				   "#ifndef FROM_REAL_TU\n"
				   "#error expected real TU compile flag\n"
				   "#endif\n"
				   "class RealService { public: int Value(); };\n");
		tree.Write("src/real.cpp",
				   "#include \"RealService.h\"\n"
				   "int RealService::Value() { return 42; }\n");

		const auto source = tree.root() / "src/real.cpp";
		WriteCompileCommands(tree,
							 {{
								 .source = source,
								 .args =
									 {
										 "c++",
										 "--driver-mode=g++",
										 "-std=c++23",
										 "-I" + (tree.root() / "include").generic_string(),
										 "-DFROM_REAL_TU",
										 "-c",
										 source.generic_string(),
									 },
							 }});

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = tree.root(),
			.build_path = tree.root() / "build",
			.headers = {Header(tree, "include/RealService.h")},
		});

		Expect(result.ok(), "real TU resolver should not report errors");
		Expect(result.project.headers.size() == 1U, "one header should be reported");
		Expect(result.project.headers[0].parsed_by_real_tu, "header should be parsed by real TU");
		Expect(!result.project.headers[0].parsed_by_synthetic_tu,
			   "real TU header should not need synthetic fallback");
		Expect(result.project.classes.size() == 1U, "one class should be extracted");
		Expect(result.project.classes[0].qualified_name == "RealService",
			   "real TU class should be extracted");
		Expect(!HasUnsupportedKind(result.project.classes[0], "inline_body"),
			   "product source body should not be treated as header-included inline body");
		Expect(result.project.classes[0].source_header.parsed_by_real_tu,
			   "class source header should record real TU parse mode");
		Expect(result.parse_attempts.size() == 1U, "real TU parse attempt should be reportable");
		Expect(result.parse_attempts[0].mode == mockfakegen::HeaderParseMode::RealTu,
			   "parse attempt should record real TU mode");
		Expect(mockfakegen::ToString(result.parse_attempts[0].mode) == "real-tu",
			   "parse mode string should be reportable");
		Expect(HasCompileArg(result.parse_attempts[0].compile_args, "-DFROM_REAL_TU"),
			   "parse attempt should expose compile args");
		Expect(HasCompileArg(result.validation_args, "-DFROM_REAL_TU"),
			   "validation args should inherit real TU defines");
		Expect(!HasCompileArg(result.validation_args, "--driver-mode=g++"),
			   "validation args should omit clang driver mode");
		Expect(!result.parse_attempts[0].parse_command.empty(),
			   "parse attempt should expose parse command");
	}

	void NormalizesCompileDatabaseStandardToCxx23()
	{
		TempTree tree;
		tree.Write("include/FixedStandard.h",
				   "#pragma once\n"
				   "#if __cplusplus <= 202002L\n"
				   "#error expected C++23 mode\n"
				   "#endif\n"
				   "class FixedStandard { public: int Value(); };\n");
		tree.Write("src/fixed_standard.cpp", "#include \"FixedStandard.h\"\n");

		const auto source = tree.root() / "src/fixed_standard.cpp";
		WriteCompileCommands(tree,
							 {{
								 .source = source,
								 .args =
									 {
										 "c++",
										 "-std",
										 "c++20",
										 "-I" + (tree.root() / "include").generic_string(),
										 "-c",
										 source.generic_string(),
									 },
							 }});

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = tree.root(),
			.build_path = tree.root() / "build",
			.headers = {Header(tree, "include/FixedStandard.h")},
			.extra_args = {"--std=c++17"},
		});

		Expect(result.ok(), "C++23-fixed resolver should ignore compile DB std downgrade");
		Expect(result.project.classes.size() == 1U, "C++23-fixed fixture should extract class");
		Expect(result.parse_attempts.size() == 1U, "C++23-fixed parse should be attempted once");
		const auto& args = result.parse_attempts[0].compile_args;
		Expect(HasCompileArg(args, "-std=c++23"), "parse args should include fixed C++23");
		Expect(CountCompileArg(args, "-std=c++23") == 1U, "parse args should include C++23 once");
		Expect(!HasAdjacentCompileArg(args, "-std", "c++20"),
			   "parse args should drop separate compile DB C++20");
		Expect(!HasCompileArg(args, "--std=c++17"),
			   "parse args should drop CLI extra standard downgrade");
		Expect(HasCompileArg(result.validation_args, "-std=c++23"),
			   "validation args should include fixed C++23");
		Expect(!HasCompileArg(result.validation_args, "--std=c++17"),
			   "validation args should drop extra standard downgrade");
		Expect(result.parse_attempts[0].parse_command.find("c++20") == std::string::npos,
			   "parse command should not keep compile DB C++20");
		Expect(result.parse_attempts[0].parse_command.find("c++17") == std::string::npos,
			   "parse command should not keep extra standard downgrade");
	}

	void PreservesSeparatePairedValidationArgs()
	{
		TempTree tree;
		tree.Write("include/Feature.h",
				   "#pragma once\n"
				   "#include \"Dependency.h\"\n"
				   "class Feature { public: int Value(Dependency dependency); };\n");
		tree.Write("config/Dependency.h", "#pragma once\nstruct Dependency { int value; };\n");
		tree.Write("src/Feature.cpp",
				   "#include \"Feature.h\"\n"
				   "int Feature::Value(Dependency dependency) { return dependency.value; }\n");

		const auto include_dir = tree.root() / "include";
		const auto config_dir = tree.root() / "config";
		const auto source = tree.root() / "src/Feature.cpp";
		WriteCompileCommands(tree,
							 {{
								 .source = source,
								 .args =
									 {
										 "c++",
										 "-std=c++23",
										 "-I",
										 include_dir.generic_string(),
										 "-I",
										 config_dir.generic_string(),
										 "-c",
										 source.generic_string(),
									 },
							 }});

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = tree.root(),
			.build_path = tree.root() / "build",
			.headers = {Header(tree, "include/Feature.h")},
		});

		Expect(result.ok(), "paired option compile database should parse successfully");
		Expect(result.project.classes.size() == 1U, "paired option fixture should extract class");
		Expect(HasAdjacentCompileArg(result.validation_args, "-I", include_dir.generic_string()),
			   "validation args should preserve first separate -I pair");
		Expect(HasAdjacentCompileArg(result.validation_args, "-I", config_dir.generic_string()),
			   "validation args should preserve second separate -I pair");
		Expect(CountCompileArg(result.validation_args, "-I") == 2U,
			   "validation args should keep both separate -I option tokens");
	}

	void FallsBackToSyntheticTuWithoutCompileDatabase()
	{
		TempTree tree;
		tree.Write("include/SyntheticOnly.h",
				   "#pragma once\n"
				   "class SyntheticOnly { public: bool Ready(); };\n");
		std::filesystem::create_directories(tree.root() / "build");

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = tree.root(),
			.build_path = tree.root() / "build",
			.headers = {Header(tree, "include/SyntheticOnly.h")},
		});

		Expect(result.ok(), "missing compile database should still allow synthetic fallback");
		Expect(
			HasDiagnostic(result.diagnostics,
						  mockfakegen::CompilationResolverDiagnosticCode::CompileDatabaseNotFound),
			"missing compile database should be reported");
		Expect(result.project.headers[0].parsed_by_synthetic_tu,
			   "header should be parsed by synthetic TU");
		Expect(result.project.classes.size() == 1U, "synthetic fallback should extract class");
		Expect(result.project.classes[0].qualified_name == "SyntheticOnly",
			   "synthetic fallback class should be extracted");
		Expect(result.parse_attempts.size() == 1U, "synthetic parse attempt should be reportable");
		Expect(result.parse_attempts[0].mode == mockfakegen::HeaderParseMode::SyntheticTu,
			   "parse attempt should record synthetic mode");
		Expect(mockfakegen::ToString(result.parse_attempts[0].mode) == "synthetic-tu",
			   "synthetic parse mode string should be reportable");
	}

	void SyntheticFallbackUsesExtraCompilerArgs()
	{
		TempTree tree;
		tree.Write("sdk/Dependency.h", "#pragma once\nstruct Dependency { int value; };\n");
		tree.Write("include/NeedsRescue.h",
				   "#pragma once\n"
				   "#ifndef CLI_DEFINE_ENABLED\n"
				   "#error expected CLI define\n"
				   "#endif\n"
				   "#ifndef CLI_EXTRA_ARG_ENABLED\n"
				   "#error expected CLI extra arg\n"
				   "#endif\n"
				   "#include \"Dependency.h\"\n"
				   "class NeedsRescue { public: bool Run(Dependency dependency); };\n");
		std::filesystem::create_directories(tree.root() / "build");
		const auto sdk_dir = tree.root() / "sdk";

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = tree.root(),
			.build_path = tree.root() / "build",
			.headers = {Header(tree, "include/NeedsRescue.h")},
			.extra_include_dirs = {sdk_dir},
			.extra_args = {"-DCLI_DEFINE_ENABLED=1", "-DCLI_EXTRA_ARG_ENABLED=1"},
		});

		Expect(result.ok(), "CLI compiler args should rescue synthetic fallback");
		Expect(result.project.classes.size() == 1U, "rescued fallback should extract class");
		Expect(result.project.classes[0].qualified_name == "NeedsRescue",
			   "rescued fallback class should be extracted");
		Expect(HasAdjacentCompileArg(
				   result.parse_attempts[0].compile_args, "-I", sdk_dir.generic_string()),
			   "synthetic parse args should include CLI include dir");
		Expect(HasCompileArg(result.validation_args, "-DCLI_DEFINE_ENABLED=1"),
			   "validation args should include CLI define");
		Expect(HasCompileArg(result.validation_args, "-DCLI_EXTRA_ARG_ENABLED=1"),
			   "validation args should include CLI extra arg");
	}

	void FallsBackToSyntheticTuWhenRealTuDoesNotIncludeHeader()
	{
		TempTree tree;
		tree.Write("include/FallbackHeader.h",
				   "#pragma once\n"
				   "#ifndef FROM_NEAREST_COMMAND\n"
				   "#error expected nearest compile args\n"
				   "#endif\n"
				   "class FallbackHeader { public: int Value(); };\n");
		tree.Write("src/unrelated.cpp", "int unrelated() { return 0; }\n");

		const auto source = tree.root() / "src/unrelated.cpp";
		WriteCompileCommands(tree,
							 {{
								 .source = source,
								 .args =
									 {
										 "c++",
										 "-std=c++23",
										 "-I" + tree.root().generic_string(),
										 "-DFROM_NEAREST_COMMAND",
										 "-c",
										 source.generic_string(),
									 },
							 }});

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = tree.root(),
			.build_path = tree.root() / "build",
			.headers = {Header(tree, "include/FallbackHeader.h")},
		});

		Expect(result.ok(), "unmapped header should parse through synthetic fallback");
		Expect(result.project.headers[0].parsed_by_synthetic_tu,
			   "unmapped header should record synthetic fallback");
		Expect(!result.project.headers[0].parsed_by_real_tu,
			   "unmapped header should not be marked as real TU parsed");
		Expect(result.project.classes.size() == 1U, "fallback class should be extracted");
		Expect(result.project.classes[0].qualified_name == "FallbackHeader",
			   "fallback class should come from unmapped header");
		Expect(result.parse_attempts.size() == 1U,
			   "synthetic fallback parse attempt should be reportable");
		Expect(result.parse_attempts[0].mode == mockfakegen::HeaderParseMode::SyntheticTu,
			   "unmapped header parse attempt should be synthetic");
		Expect(HasCompileArg(result.parse_attempts[0].compile_args, "-DFROM_NEAREST_COMMAND"),
			   "synthetic fallback should reuse nearest compile args");
	}

	[[nodiscard]] mockfakegen::CompilationResolveResult
	ResolveOutOfTreeFixture(const TempTree& tree)
	{
		const auto project_root = tree.root() / "product";
		const auto build_dir = tree.root() / "out/build";
		return mockfakegen::ResolveCompilation({
			.project_root = project_root,
			.build_path = build_dir,
			.headers = {HeaderAt(project_root, "include/ContextService.h")},
		});
	}

	void ParsesOutOfTreeCompileDatabaseWithCommandDirectoryRelativePaths()
	{
		TempTree tree;
		const auto project_root = tree.root() / "product";
		const auto build_dir = tree.root() / "out/build";
		tree.Write("product/include/ContextService.h",
				   "#pragma once\n"
				   "#ifndef FORCED_FROM_BUILD_DIR\n"
				   "#error expected forced include\n"
				   "#endif\n"
				   "#ifndef FROM_DIRECTORY_RELATIVE_DB\n"
				   "#error expected compile database define\n"
				   "#endif\n"
				   "#include \"QuoteOnly.h\"\n"
				   "#include <SystemOnly.h>\n"
				   "class ContextService {\n"
				   "public:\n"
				   "  int Value(QuoteOnly quote, SystemOnly system);\n"
				   "};\n");
		tree.Write("product/src/context.cpp", "#include \"ContextService.h\"\n");
		tree.Write("out/build/config/ForcedConfig.h",
				   "#pragma once\n"
				   "#define FORCED_FROM_BUILD_DIR 1\n");
		tree.Write("out/build/generated/quote/QuoteOnly.h",
				   "#pragma once\n"
				   "struct QuoteOnly { int value; };\n");
		tree.Write("out/build/generated/system/SystemOnly.h",
				   "#pragma once\n"
				   "struct SystemOnly { int value; };\n");
		std::filesystem::create_directories(build_dir / "deps");
		std::filesystem::create_directories(build_dir / "obj");

		WriteCompileCommandsAt(tree,
							   "out/build",
							   {{
								   .directory = build_dir,
								   .source = "../../product/src/context.cpp",
								   .args =
									   {
										   "c++",
										   "-std=c++23",
										   "-I../../product/include",
										   "-iquote",
										   "generated/quote",
										   "-isystem",
										   "generated/system",
										   "-include",
										   "config/ForcedConfig.h",
										   "-DFROM_DIRECTORY_RELATIVE_DB",
										   "-MD",
										   "-MF",
										   "deps/context.d",
										   "-c",
										   "../../product/src/context.cpp",
										   "-o",
										   "obj/context.o",
									   },
							   }});

		mockfakegen::CompilationResolveResult from_project_root;
		{
			const ScopedCurrentPath scoped_path(tree.root());
			from_project_root = ResolveOutOfTreeFixture(tree);
		}

		mockfakegen::CompilationResolveResult from_command_directory;
		{
			const ScopedCurrentPath scoped_path(build_dir);
			from_command_directory = ResolveOutOfTreeFixture(tree);
		}

		const auto expected_include =
			std::filesystem::weakly_canonical(build_dir / "../../product/include");
		const auto expected_quote =
			std::filesystem::weakly_canonical(build_dir / "generated/quote");
		const auto expected_system =
			std::filesystem::weakly_canonical(build_dir / "generated/system");
		const auto expected_forced_include =
			std::filesystem::weakly_canonical(build_dir / "config/ForcedConfig.h");

		for (const auto& result : {from_project_root, from_command_directory})
		{
			Expect(result.ok(), "out-of-tree compile database should parse successfully");
			Expect(result.project.classes.size() == 1U,
				   "directory-relative fixture should extract one class");
			Expect(result.project.classes[0].qualified_name == "ContextService",
				   "directory-relative fixture should extract target class");
			Expect(result.project.headers[0].parsed_by_real_tu,
				   "directory-relative fixture should use real TU parsing");
			Expect(!result.project.headers[0].parsed_by_synthetic_tu,
				   "directory-relative fixture should not need synthetic fallback");
			Expect(result.parse_attempts.size() == 1U,
				   "directory-relative fixture should record one real attempt");
			Expect(result.parse_attempts[0].success,
				   "directory-relative real attempt should be successful");
			Expect(HasCompileArg(result.parse_attempts[0].compile_args,
								 "-I" + expected_include.generic_string()),
				   "joined -I path should be resolved against command directory");
			Expect(HasAdjacentCompileArg(result.parse_attempts[0].compile_args,
										 "-iquote",
										 expected_quote.generic_string()),
				   "separate -iquote path should be resolved against command directory");
			Expect(HasAdjacentCompileArg(result.parse_attempts[0].compile_args,
										 "-isystem",
										 expected_system.generic_string()),
				   "separate -isystem path should be resolved against command directory");
			Expect(HasAdjacentCompileArg(result.parse_attempts[0].compile_args,
										 "-include",
										 expected_forced_include.generic_string()),
				   "-include path should be resolved against command directory");
			Expect(!HasCompileArg(result.parse_attempts[0].compile_args, "-MF"),
				   "dependency output option should be stripped from parse args");
			Expect(!HasCompileArg(result.parse_attempts[0].compile_args, "deps/context.d"),
				   "dependency output path should be stripped from parse args");
			Expect(!HasCompileArg(result.parse_attempts[0].compile_args, "obj/context.o"),
				   "object output path should be stripped from parse args");
			Expect(!HasCompileArg(result.parse_attempts[0].compile_args,
								  "../../product/src/context.cpp"),
				   "source path should be stripped from reusable compile args");
			Expect(HasAdjacentCompileArg(result.validation_args,
										 "-include",
										 expected_forced_include.generic_string()),
				   "validation args should inherit resolved forced include path");
		}

		Expect(from_project_root.project.classes[0].qualified_name ==
				   from_command_directory.project.classes[0].qualified_name,
			   "fixture should parse identically from project root and command directory cwd");
		Expect(from_project_root.project.classes[0].mock_methods.size() ==
				   from_command_directory.project.classes[0].mock_methods.size(),
			   "method extraction should be identical across cwd changes");
	}

	void MapsContainerCompileCommandPaths()
	{
		TempTree tree;
		const auto project_root = tree.root() / "product";
		tree.Write("product/include/MappedService.h",
				   "#pragma once\n"
				   "#ifndef FROM_CONTAINER_DB\n"
				   "#error expected mapped compile database define\n"
				   "#endif\n"
				   "#include \"MappedDependency.h\"\n"
				   "class MappedService { public: bool Run(MappedDependency dependency); };\n");
		tree.Write("product/include/MappedDependency.h",
				   "#pragma once\nstruct MappedDependency { int value; };\n");
		tree.Write("product/src/mapped.cpp", "#include \"MappedService.h\"\n");
		WriteCompileCommandsAt(tree,
							   "build",
							   {{
								   .directory = "/workspace",
								   .source = "/workspace/src/mapped.cpp",
								   .args =
									   {
										   "c++",
										   "-std=c++23",
										   "-I/workspace/include",
										   "-DFROM_CONTAINER_DB",
										   "-c",
										   "/workspace/src/mapped.cpp",
									   },
							   }});

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = project_root,
			.build_path = tree.root() / "build",
			.headers = {HeaderAt(project_root, "include/MappedService.h")},
			.path_maps = {{.from = "/workspace", .to = project_root}},
		});

		Expect(result.ok(), "mapped container compile database should parse successfully");
		Expect(result.project.headers[0].parsed_by_real_tu,
			   "mapped container compile command should parse real TU");
		Expect(result.project.classes.size() == 1U,
			   "mapped container fixture should extract class");
		Expect(result.project.classes[0].qualified_name == "MappedService",
			   "mapped container fixture should extract target class");
		Expect(result.parse_attempts[0].translation_unit == project_root / "src/mapped.cpp",
			   "parse attempt should record host-mapped translation unit");
		Expect(HasCompileArg(result.validation_args,
							 "-I" + (project_root / "include").generic_string()),
			   "validation args should rewrite relative include path against mapped directory");
		Expect(!HasCompileArg(result.validation_args, "-I/workspace/include"),
			   "validation args should not retain container include path");
	}

	void MapsVmProducedCompileDatabaseIntoConsumerBuildTree()
	{
		TempTree tree;
		const auto project_root = tree.root() / "consumer/project";
		const auto build_dir = tree.root() / "consumer/build";
		tree.Write("consumer/project/include/ImportedService.h",
				   "#pragma once\n"
				   "#ifndef GENERATED_FROM_VM_BUILD\n"
				   "#error expected generated config from VM build path\n"
				   "#endif\n"
				   "class ImportedService { public: int Value(); };\n");
		tree.Write("consumer/project/src/imported.cpp", "#include \"ImportedService.h\"\n");
		tree.Write("consumer/build/generated/GeneratedConfig.h",
				   "#pragma once\n#define GENERATED_FROM_VM_BUILD 1\n");

		WriteCompileCommandsAt(tree,
							   "consumer/build",
							   {{
								   .directory = "/vm/work/build",
								   .source = "/vm/work/project/src/imported.cpp",
								   .args =
									   {
										   "c++",
										   "-std=c++23",
										   "-I/vm/work/project/include",
										   "-include",
										   "/vm/work/build/generated/GeneratedConfig.h",
										   "-c",
										   "/vm/work/project/src/imported.cpp",
									   },
							   }});

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = project_root,
			.build_path = build_dir,
			.headers = {HeaderAt(project_root, "include/ImportedService.h")},
			.path_maps =
				{
					{.from = "/vm/work/project", .to = project_root},
					{.from = "/vm/work/build", .to = build_dir},
				},
		});

		Expect(result.ok(), "VM-produced compile database should map into consumer tree");
		Expect(result.project.headers[0].parsed_by_real_tu,
			   "mapped VM compile command should parse through real TU");
		Expect(result.project.classes.size() == 1U, "mapped VM fixture should extract class");
		Expect(result.project.classes[0].qualified_name == "ImportedService",
			   "mapped VM fixture should extract expected class");
		Expect(HasAdjacentCompileArg(result.parse_attempts[0].compile_args,
									 "-include",
									 (build_dir / "generated/GeneratedConfig.h").generic_string()),
			   "forced include should be remapped from VM build root");
		Expect(HasDiagnostic(
				   result.diagnostics,
				   mockfakegen::CompilationResolverDiagnosticCode::CompileDatabasePathMapped),
			   "path mapping should be reported as a preflight diagnostic");
	}

	void ReportsMissingCrossEnvironmentCompileDatabaseInputs()
	{
		TempTree tree;
		tree.Write("include/PortableService.h",
				   "#pragma once\nclass PortableService { public: int Value(); };\n");
		tree.Write("src/portable.cpp", "#include \"PortableService.h\"\n");

		const auto source = tree.root() / "src/portable.cpp";
		WriteCompileCommands(tree,
							 {{
								 .source = source,
								 .args =
									 {
										 "/producer/toolchain/bin/clang++",
										 "-std=c++23",
										 "-I" + (tree.root() / "include").generic_string(),
										 "-target",
										 "arm-none-eabi",
										 "-isystem",
										 "/producer/sysroot/include",
										 "-resource-dir",
										 "/producer/clang-resource",
										 "--sysroot=/producer/sysroot",
										 "-include",
										 "/producer/build/generated/MissingConfig.h",
										 "-c",
										 source.generic_string(),
									 },
							 }});

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = tree.root(),
			.build_path = tree.root() / "build",
			.headers = {Header(tree, "include/PortableService.h")},
		});

		Expect(!result.ok(), "missing producer environment paths should fail preflight");
		Expect(HasDiagnostic(
				   result.diagnostics,
				   mockfakegen::CompilationResolverDiagnosticCode::CompileDatabaseCompilerMissing),
			   "missing producer compiler should be diagnosed");
		Expect(HasDiagnostic(result.diagnostics,
							 mockfakegen::CompilationResolverDiagnosticCode::
								 CompileDatabaseUnmappedAbsolutePath),
			   "unmapped producer absolute paths should be diagnosed");
		Expect(
			HasDiagnostic(
				result.diagnostics,
				mockfakegen::CompilationResolverDiagnosticCode::CompileDatabaseMappedPathMissing),
			"missing sysroot/resource-dir/generated include should be diagnosed");
		Expect(HasDiagnostic(
				   result.diagnostics,
				   mockfakegen::CompilationResolverDiagnosticCode::CompileDatabaseTargetMismatch),
			   "target triple mismatch should be diagnosed");
		Expect(DiagnosticMessageContains(
				   result.diagnostics,
				   mockfakegen::CompilationResolverDiagnosticCode::CompileDatabaseMappedPathMissing,
				   "/producer/build/generated/MissingConfig.h"),
			   "missing generated forced include should be named in diagnostics");
	}

	void ReportsCaseAndSymlinkCarryoverRisks()
	{
		TempTree tree;
		tree.Write("include/Foo.h", "#pragma once\nclass CaseService { public: int Value(); };\n");
		tree.Write("include/foo.h", "#pragma once\nstruct lower_case_collision {};\n");
		tree.Write("src/case.cpp", "#include \"Foo.h\"\n");
		std::error_code symlink_error;
		std::filesystem::create_directory_symlink(std::filesystem::temp_directory_path(),
												  tree.root() / "include/ExternalLink",
												  symlink_error);

		const auto source = tree.root() / "src/case.cpp";
		std::vector<std::string> args = {
			"c++",
			"-std=c++23",
			"-I" + (tree.root() / "Include").generic_string(),
		};
		if (!symlink_error)
		{
			args.push_back("-I");
			args.push_back((tree.root() / "include/ExternalLink").generic_string());
		}
		args.push_back("-c");
		args.push_back(source.generic_string());

		WriteCompileCommands(tree, {{.source = source, .args = args}});

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = tree.root(),
			.build_path = tree.root() / "build",
			.headers = {Header(tree, "include/Foo.h")},
			.path_maps = {{.from = "/producer/project", .to = tree.root()}},
		});

		Expect(HasDiagnostic(
				   result.diagnostics,
				   mockfakegen::CompilationResolverDiagnosticCode::CompileDatabasePathCaseMismatch),
			   "case-mismatched compile database path should be diagnosed");
		Expect(
			HasDiagnostic(
				result.diagnostics,
				mockfakegen::CompilationResolverDiagnosticCode::CompileDatabaseCaseFoldCollision),
			"case-fold collisions should be diagnosed");
		if (!symlink_error)
		{
			Expect(HasDiagnostic(
					   result.diagnostics,
					   mockfakegen::CompilationResolverDiagnosticCode::CompileDatabaseSymlinkRisk),
				   "symlink include path escaping project/build should be diagnosed");
		}
	}

	void ParsesCrlfCompileDatabaseAndHeader()
	{
		TempTree tree;
		tree.Write("include/CrlfService.h",
				   "#pragma once\r\nclass CrlfService { public: int Value(); };\r\n");
		tree.Write("src/crlf.cpp", "#include \"CrlfService.h\"\r\n");

		const auto source = tree.root() / "src/crlf.cpp";
		const auto include_dir = tree.root() / "include";
		const auto json = std::string("[\r\n"
									  "  {\r\n"
									  "    \"directory\": \"") +
			JsonEscape(tree.root().generic_string()) +
			"\",\r\n"
			"    \"file\": \"" +
			JsonEscape(source.generic_string()) +
			"\",\r\n"
			"    \"arguments\": [\"c++\", \"-std=c++23\", \"-I" +
			JsonEscape(include_dir.generic_string()) + "\", \"-c\", \"" +
			JsonEscape(source.generic_string()) +
			"\"]\r\n"
			"  }\r\n"
			"]\r\n";
		tree.Write("build/compile_commands.json", json);

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = tree.root(),
			.build_path = tree.root() / "build",
			.headers = {Header(tree, "include/CrlfService.h")},
		});

		Expect(result.ok(), "CRLF compile database and header should parse");
		Expect(result.project.classes.size() == 1U, "CRLF fixture should extract one class");
		Expect(result.project.classes[0].qualified_name == "CrlfService",
			   "CRLF fixture should extract expected class");
	}

	void RecordsRealTuParseFailuresAsAttempts()
	{
		TempTree tree;
		tree.Write("include/Broken.h",
				   "#pragma once\n"
				   "class Broken { public: int Value(\n");
		tree.Write("src/broken.cpp", "#include \"Broken.h\"\n");

		const auto source = tree.root() / "src/broken.cpp";
		WriteCompileCommands(tree,
							 {{
								 .source = source,
								 .args =
									 {
										 "c++",
										 "-std=c++23",
										 "-Iinclude",
										 "-DBROKEN_REAL_TU",
										 "-c",
										 "src/broken.cpp",
									 },
							 }});

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = tree.root(),
			.build_path = tree.root() / "build",
			.headers = {Header(tree, "include/Broken.h")},
		});

		Expect(!result.ok(), "broken real and synthetic parse should fail resolver status");
		const auto* real_diagnostic = FindDiagnostic(
			result.diagnostics, mockfakegen::CompilationResolverDiagnosticCode::RealTuParseFailure);
		Expect(real_diagnostic != nullptr, "real TU parse failure should be diagnosed");
		Expect(!real_diagnostic->command.empty(),
			   "real TU parse failure diagnostic should include command");
		Expect(!real_diagnostic->stderr_summary.empty(),
			   "real TU parse failure diagnostic should include clang diagnostics");
		Expect(result.parse_attempts.size() == 2U,
			   "real failure and synthetic fallback attempts should both be reportable");
		Expect(result.parse_attempts[0].mode == mockfakegen::HeaderParseMode::RealTu,
			   "first failed attempt should be real TU");
		Expect(!result.parse_attempts[0].success, "real TU failed attempt should record failure");
		Expect(!result.parse_attempts[0].diagnostics.empty(),
			   "real TU failed attempt should retain clang diagnostics");
		Expect(result.parse_attempts[0].translation_unit ==
				   std::filesystem::weakly_canonical(source),
			   "real TU failed attempt should keep source path");
		Expect(HasCompileArg(result.parse_attempts[0].compile_args, "-DBROKEN_REAL_TU"),
			   "real TU failed attempt should keep compile args");
		Expect(result.parse_attempts[1].mode == mockfakegen::HeaderParseMode::SyntheticTu,
			   "synthetic fallback attempt should still be recorded after real failure");
		Expect(!result.parse_attempts[1].success,
			   "synthetic fallback should also record failure for broken header");
	}

	void ReportsConflictingCompileConfigs()
	{
		TempTree tree;
		tree.Write("include/Configurable.h",
				   "#pragma once\n"
				   "class Configurable { public:\n"
				   "#ifdef ENABLE_EXTRA\n"
				   "  int Extra();\n"
				   "#else\n"
				   "  int Base();\n"
				   "#endif\n"
				   "};\n");
		tree.Write("src/first.cpp", "#include \"Configurable.h\"\n");
		tree.Write("src/second.cpp", "#include \"Configurable.h\"\n");

		const auto first = tree.root() / "src/first.cpp";
		const auto second = tree.root() / "src/second.cpp";
		const auto include_arg = "-I" + (tree.root() / "include").generic_string();
		WriteCompileCommands(tree,
							 {
								 CompileEntry{
									 .source = first,
									 .args =
										 {
											 "c++",
											 "-std=c++23",
											 include_arg,
											 "-DENABLE_EXTRA",
											 "-c",
											 first.generic_string(),
										 },
								 },
								 CompileEntry{
									 .source = second,
									 .args =
										 {
											 "c++",
											 "-std=c++23",
											 include_arg,
											 "-c",
											 second.generic_string(),
										 },
								 },
							 });

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = tree.root(),
			.build_path = tree.root() / "build",
			.headers = {Header(tree, "include/Configurable.h")},
		});

		Expect(!result.ok(), "conflicting compile configs should fail resolver status");
		Expect(HasDiagnostic(result.diagnostics,
							 mockfakegen::CompilationResolverDiagnosticCode::CompileConfigConflict),
			   "compile config conflict should be reported");
		Expect(result.project.diagnostics.size() == 1U,
			   "compile config conflict should be mirrored into project diagnostics");
		Expect(result.parse_attempts.size() == 2U,
			   "both real TU parse attempts should be reportable");
		Expect(result.project.headers[0].parsed_by_real_tu,
			   "conflicting header should still record real TU parsing");
	}

	void ReportsConflictingFakeSpecialMembers()
	{
		TempTree tree;
		tree.Write("include/Widget.h",
				   "#pragma once\n"
				   "class Widget { public:\n"
				   "#ifdef USE_INT_CTOR\n"
				   "  Widget(int value);\n"
				   "#else\n"
				   "  Widget(double value);\n"
				   "#endif\n"
				   "  ~Widget() noexcept;\n"
				   "  bool Run();\n"
				   "};\n");
		tree.Write("src/first.cpp", "#include \"Widget.h\"\n");
		tree.Write("src/second.cpp", "#include \"Widget.h\"\n");

		const auto first = tree.root() / "src/first.cpp";
		const auto second = tree.root() / "src/second.cpp";
		const auto include_arg = "-I" + (tree.root() / "include").generic_string();
		WriteCompileCommands(tree,
							 {
								 CompileEntry{
									 .source = first,
									 .args =
										 {
											 "c++",
											 "-std=c++23",
											 include_arg,
											 "-DUSE_INT_CTOR",
											 "-c",
											 first.generic_string(),
										 },
								 },
								 CompileEntry{
									 .source = second,
									 .args =
										 {
											 "c++",
											 "-std=c++23",
											 include_arg,
											 "-c",
											 second.generic_string(),
										 },
								 },
							 });

		const auto result = mockfakegen::ResolveCompilation({
			.project_root = tree.root(),
			.build_path = tree.root() / "build",
			.headers = {Header(tree, "include/Widget.h")},
			.fake_special_members = true,
		});

		Expect(!result.ok(), "conflicting fake special members should fail resolver status");
		Expect(HasDiagnostic(result.diagnostics,
							 mockfakegen::CompilationResolverDiagnosticCode::CompileConfigConflict),
			   "fake special member compile config conflict should be reported");
		Expect(result.project.diagnostics.size() == 1U,
			   "fake special member conflict should be mirrored into project diagnostics");
		Expect(result.parse_attempts.size() == 2U,
			   "both fake special member parse attempts should be reportable");
	}
} // namespace

int main()
{
	ParsesHeaderThroughRealTu();
	NormalizesCompileDatabaseStandardToCxx23();
	PreservesSeparatePairedValidationArgs();
	FallsBackToSyntheticTuWithoutCompileDatabase();
	SyntheticFallbackUsesExtraCompilerArgs();
	FallsBackToSyntheticTuWhenRealTuDoesNotIncludeHeader();
	ParsesOutOfTreeCompileDatabaseWithCommandDirectoryRelativePaths();
	MapsContainerCompileCommandPaths();
	MapsVmProducedCompileDatabaseIntoConsumerBuildTree();
	ReportsMissingCrossEnvironmentCompileDatabaseInputs();
	ReportsCaseAndSymlinkCarryoverRisks();
	ParsesCrlfCompileDatabaseAndHeader();
	RecordsRealTuParseFailuresAsAttempts();
	ReportsConflictingCompileConfigs();
	ReportsConflictingFakeSpecialMembers();
	return 0;
}
