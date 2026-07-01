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
		std::filesystem::path source;
		std::vector<std::string> args;
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

	void WriteCompileCommands(const TempTree& tree, const std::vector<CompileEntry>& entries)
	{
		std::string json = "[\n";
		for (std::size_t entry_index = 0U; entry_index < entries.size(); ++entry_index)
		{
			const auto& entry = entries[entry_index];
			json += "  {\n";
			json += "    \"directory\": \"" + JsonEscape(tree.root().generic_string()) + "\",\n";
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
		tree.Write("build/compile_commands.json", json);
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

	void ParsesHeaderThroughRealTu()
	{
		TempTree tree;
		tree.Write("include/RealService.h",
				   "#pragma once\n"
				   "#ifndef FROM_REAL_TU\n"
				   "#error expected real TU compile flag\n"
				   "#endif\n"
				   "class RealService { public: int Value(); };\n");
		tree.Write("src/real.cpp", "#include \"RealService.h\"\n");

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
} // namespace

int main()
{
	ParsesHeaderThroughRealTu();
	FallsBackToSyntheticTuWithoutCompileDatabase();
	FallsBackToSyntheticTuWhenRealTuDoesNotIncludeHeader();
	ReportsConflictingCompileConfigs();
	return 0;
}
