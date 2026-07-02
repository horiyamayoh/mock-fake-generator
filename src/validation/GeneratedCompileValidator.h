#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ValidationMode.h"
#include "model/GeneratedFile.h"

namespace mockfakegen
{
	enum class GeneratedCompileValidationStage
	{
		Syntax,
		Compile,
		Link,
	};

	enum class GeneratedCompileValidationLinkStrategy
	{
		NotApplicable,
		GMockLinkInputs,
		SyntheticMainSmoke,
	};

	struct GeneratedCompileValidationOptions
	{
		ValidationMode mode = ValidationMode::Compile;
		std::filesystem::path compiler = "c++";
		std::vector<std::filesystem::path> include_dirs;
		std::vector<std::filesystem::path> link_files;
		std::vector<std::string> extra_args;
		std::vector<GeneratedSourceCompileArgs> source_args;
		std::chrono::milliseconds command_timeout = std::chrono::seconds(30);
		bool keep_failed_artifacts = false;
		std::filesystem::path artifact_dir;
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
		std::filesystem::path validation_artifact_path;
		std::string message;
		std::string command;
		std::string stderr_summary;
		GeneratedCompileValidationStage stage = GeneratedCompileValidationStage::Compile;
	};

	struct GeneratedCompileValidationResult
	{
		std::vector<GeneratedCompileCommandResult> commands;
		std::vector<GeneratedCompileDiagnostic> diagnostics;
		std::filesystem::path artifact_root;
		ValidationMode mode = ValidationMode::None;
		GeneratedCompileValidationLinkStrategy link_strategy =
			GeneratedCompileValidationLinkStrategy::NotApplicable;
		std::size_t link_input_count = 0U;
		bool skipped = false;

		[[nodiscard]] bool ok() const noexcept
		{
			return diagnostics.empty();
		}
	};

	[[nodiscard]] GeneratedCompileValidationResult
	ValidateGeneratedOutputCompile(const GeneratedCompileValidationOptions& options,
								   std::span<const GeneratedFile> files);

	[[nodiscard]] std::string_view ToString(GeneratedCompileValidationStage stage) noexcept;
	[[nodiscard]] std::string_view
	ToString(GeneratedCompileValidationLinkStrategy strategy) noexcept;
} // namespace mockfakegen
