#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "model/GeneratedFile.h"

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
