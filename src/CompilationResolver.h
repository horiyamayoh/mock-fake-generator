#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "clang/SyntheticTuParser.h"
#include "model/GeneratedFile.h"
#include "model/ProjectModel.h"

namespace mockfakegen
{
	enum class HeaderParseMode
	{
		RealTu,
		SyntheticTu,
	};

	enum class CompilationResolverDiagnosticCode
	{
		CompileDatabaseNotFound,
		CompileDatabaseLoadFailure,
		CompileDatabasePathMapped,
		CompileDatabaseUnmappedAbsolutePath,
		CompileDatabaseMappedPathMissing,
		CompileDatabaseCompilerMissing,
		CompileDatabaseCompilerWrapper,
		CompileDatabaseTargetMismatch,
		CompileDatabaseSystemContextAssumption,
		CompileDatabasePathCaseMismatch,
		CompileDatabaseCaseFoldCollision,
		CompileDatabaseSymlinkRisk,
		TranslationUnitReadFailure,
		RealTuParseFailure,
		SyntheticTuParseFailure,
		CompileConfigConflict,
	};

	struct HeaderParseAttempt
	{
		HeaderModel header;
		HeaderParseMode mode = HeaderParseMode::SyntheticTu;
		std::filesystem::path translation_unit;
		std::filesystem::path compiler;
		std::vector<std::string> compile_args;
		std::string parse_command;
		bool success = false;
		std::vector<ClangParseDiagnostic> diagnostics;
	};

	struct CompilationResolverDiagnostic
	{
		DiagnosticSeverity severity = DiagnosticSeverity::Error;
		CompilationResolverDiagnosticCode code =
			CompilationResolverDiagnosticCode::CompileDatabaseLoadFailure;
		std::filesystem::path header_path;
		std::filesystem::path translation_unit;
		std::string message;
		std::string command;
		std::string stderr_summary;
	};

	struct CompilationResolverOptions
	{
		std::filesystem::path project_root;
		std::filesystem::path build_path;
		std::vector<HeaderModel> headers;
		bool fake_special_members = false;
		bool fake_static_data = false;
		bool interface_mock = false;
		std::vector<std::filesystem::path> extra_include_dirs = {};
		std::vector<std::string> extra_args = {};
		std::vector<PathMapEntry> path_maps = {};
	};

	struct CompilationResolveResult
	{
		ProjectModel project;
		std::vector<HeaderParseAttempt> parse_attempts;
		std::vector<std::string> validation_args;
		std::vector<GeneratedSourceCompileArgs> validation_arg_sets;
		std::vector<CompilationResolverDiagnostic> diagnostics;

		[[nodiscard]] bool ok() const noexcept;
	};

	[[nodiscard]] std::string ToString(HeaderParseMode mode);
	[[nodiscard]] CompilationResolveResult
	ResolveCompilation(const CompilationResolverOptions& options);
} // namespace mockfakegen
