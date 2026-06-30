#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "ValidationMode.h"
#include "model/GeneratedFile.h"

namespace mockfakegen
{
	struct GeneratedCompileValidationOptions
	{
		ValidationMode mode = ValidationMode::Compile;
		std::filesystem::path compiler = "c++";
		std::vector<std::filesystem::path> include_dirs;
		std::vector<std::string> extra_args;
	};

	struct GeneratedCompileCommandResult
	{
		std::filesystem::path source_path;
		std::string command;
		int exit_code = 0;
	};

	struct GeneratedCompileDiagnostic
	{
		std::filesystem::path source_path;
		std::string message;
		std::string command;
		std::string stderr_summary;
	};

	struct GeneratedCompileValidationResult
	{
		std::vector<GeneratedCompileCommandResult> commands;
		std::vector<GeneratedCompileDiagnostic> diagnostics;
		bool skipped = false;

		[[nodiscard]] bool ok() const noexcept
		{
			return diagnostics.empty();
		}
	};

	[[nodiscard]] GeneratedCompileValidationResult
	ValidateGeneratedOutputCompile(const GeneratedCompileValidationOptions& options,
								   std::span<const GeneratedFile> files);
} // namespace mockfakegen
