#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "model/GeneratedFile.h"
#include "model/ProjectModel.h"

namespace mockfakegen
{
	enum class OutputWriteStatus
	{
		Planned,
		Written,
		Unchanged,
		SkippedExisting,
		Failed,
	};

	struct OutputWriterOptions
	{
		std::filesystem::path output_dir;
		bool dry_run = false;
		bool overwrite = false;
	};

	struct OutputFileResult
	{
		std::filesystem::path path;
		GeneratedFileKind kind = GeneratedFileKind::MockHeader;
		OutputWriteStatus status = OutputWriteStatus::Failed;
	};

	struct OutputWriteDiagnostic
	{
		DiagnosticSeverity severity = DiagnosticSeverity::Error;
		std::string code;
		std::string kind;
		std::filesystem::path path;
		std::string message;
	};

	struct OutputWriteResult
	{
		std::vector<OutputFileResult> files;
		std::vector<OutputWriteDiagnostic> diagnostics;

		[[nodiscard]] bool ok() const noexcept
		{
			return diagnostics.empty();
		}
	};

	[[nodiscard]] OutputWriteResult WriteGeneratedFiles(const OutputWriterOptions& options,
														std::span<const GeneratedFile> files);
} // namespace mockfakegen
