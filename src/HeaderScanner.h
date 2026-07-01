#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mockfakegen
{
	enum class HeaderScanDiagnosticSeverity
	{
		Info,
		Warning,
		Error,
	};

	enum class HeaderScanDiagnosticCode
	{
		InputRootDoesNotExist,
		InputRootIsNotDirectory,
		FilesystemError,
		InvalidHeaderFilter,
		SkippedGeneratedOutput,
		SkippedExcludedPath,
		SkippedSymlinkPath,
	};

	struct HeaderScanDiagnostic
	{
		HeaderScanDiagnosticSeverity severity = HeaderScanDiagnosticSeverity::Error;
		HeaderScanDiagnosticCode code;
		std::filesystem::path path;
		std::string message;
	};

	struct HeaderCandidate
	{
		std::filesystem::path absolute_path;
		std::filesystem::path project_relative_path;
		std::string include_spelling;
	};

	struct HeaderScannerOptions
	{
		std::filesystem::path input_root;
		std::filesystem::path project_root;
		std::filesystem::path output_dir;
		std::optional<std::string> header_filter = std::nullopt;
		std::vector<std::string> exclude_globs = {};
	};

	struct HeaderScanResult
	{
		std::vector<HeaderCandidate> headers;
		std::vector<HeaderScanDiagnostic> diagnostics;

		[[nodiscard]] bool ok() const noexcept
		{
			for (const auto& diagnostic : diagnostics)
			{
				if (diagnostic.severity == HeaderScanDiagnosticSeverity::Error)
				{
					return false;
				}
			}
			return true;
		}
	};

	[[nodiscard]] HeaderScanResult ScanHeaders(const HeaderScannerOptions& options);
} // namespace mockfakegen
