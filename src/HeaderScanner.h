#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mockfakegen
{
	enum class HeaderScanDiagnosticCode
	{
		InputRootDoesNotExist,
		InputRootIsNotDirectory,
		FilesystemError,
	};

	struct HeaderScanDiagnostic
	{
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
	};

	struct HeaderScanResult
	{
		std::vector<HeaderCandidate> headers;
		std::vector<HeaderScanDiagnostic> diagnostics;

		[[nodiscard]] bool ok() const noexcept
		{
			return diagnostics.empty();
		}
	};

	[[nodiscard]] HeaderScanResult ScanHeaders(const HeaderScannerOptions& options);
} // namespace mockfakegen
