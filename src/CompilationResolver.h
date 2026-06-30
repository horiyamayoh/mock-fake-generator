#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "clang/SyntheticTuParser.h"
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
	};

	struct CompilationResolverOptions
	{
		std::filesystem::path project_root;
		std::filesystem::path build_path;
		std::vector<HeaderModel> headers;
		bool fake_special_members = false;
	};

	struct CompilationResolveResult
	{
		ProjectModel project;
		std::vector<HeaderParseAttempt> parse_attempts;
		std::vector<CompilationResolverDiagnostic> diagnostics;

		[[nodiscard]] bool ok() const noexcept;
	};

	[[nodiscard]] std::string ToString(HeaderParseMode mode);
	[[nodiscard]] CompilationResolveResult
	ResolveCompilation(const CompilationResolverOptions& options);
} // namespace mockfakegen
