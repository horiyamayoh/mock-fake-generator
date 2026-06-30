#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <clang/Frontend/ASTUnit.h>

namespace mockfakegen
{
	enum class ClangDiagnosticSeverity
	{
		Note,
		Warning,
		Error,
	};

	struct ClangParseDiagnostic
	{
		ClangDiagnosticSeverity severity = ClangDiagnosticSeverity::Error;
		std::string message;
	};

	struct HeaderParseRecord
	{
		std::filesystem::path header_path;
		std::string include_spelling;
		bool parsed_by_synthetic_tu = false;
	};

	struct SyntheticTuParseOptions
	{
		std::filesystem::path header_path;
		std::filesystem::path project_root;
		std::vector<std::string> compile_args = {};
	};

	struct SyntheticTuParseResult
	{
		bool success = false;
		HeaderParseRecord header;
		std::vector<std::string> compile_args;
		std::string synthetic_code;
		std::vector<ClangParseDiagnostic> diagnostics;
		std::unique_ptr<clang::ASTUnit> ast;
	};

	[[nodiscard]] std::string BuildSyntheticTuCode(std::string include_spelling);
	[[nodiscard]] std::vector<std::string>
	BuildSyntheticTuFallbackArgs(const std::filesystem::path& project_root);
	[[nodiscard]] SyntheticTuParseResult
	ParseHeaderWithSyntheticTu(const SyntheticTuParseOptions& options);
} // namespace mockfakegen
