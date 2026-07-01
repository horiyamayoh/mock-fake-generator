#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "model/ProjectModel.h"

namespace mockfakegen
{
	struct GeneratedOutputTokenDiagnostic;

	struct RunDiagnostic
	{
		DiagnosticSeverity severity = DiagnosticSeverity::Error;
		std::string component;
		std::string code;
		std::string kind;
		std::filesystem::path path;
		SourceRange source_range;
		std::string class_name;
		std::string member;
		std::string message;
		std::string suggested_action;
		std::string command;
		std::string stderr_summary;
		std::filesystem::path validation_artifact_path;
	};

	struct RunCommand
	{
		std::filesystem::path source_path;
		std::string command;
		int exit_code = 0;
	};

	struct DiagnosticSummary
	{
		std::size_t total = 0U;
		std::size_t info = 0U;
		std::size_t warnings = 0U;
		std::size_t errors = 0U;
	};

	struct ComponentDiagnosticSummary
	{
		std::string component;
		DiagnosticSummary summary;
	};

	struct GenerationReportMetadata
	{
		std::vector<RunDiagnostic> diagnostics;
		std::vector<RunCommand> validation_commands;
	};

	[[nodiscard]] std::string_view ToString(DiagnosticSeverity severity) noexcept;
	[[nodiscard]] std::string_view ToString(UnsupportedReasonCode reason) noexcept;

	void SortRunDiagnostics(std::vector<RunDiagnostic>& diagnostics);
	[[nodiscard]] std::vector<RunDiagnostic>
	SortedRunDiagnostics(std::span<const RunDiagnostic> diagnostics);
	[[nodiscard]] DiagnosticSummary
	SummarizeDiagnostics(std::span<const RunDiagnostic> diagnostics);
	[[nodiscard]] std::vector<ComponentDiagnosticSummary>
	SummarizeDiagnosticsByComponent(std::span<const RunDiagnostic> diagnostics);
	[[nodiscard]] std::vector<RunDiagnostic>
	BuildUnsupportedItemDiagnostics(std::span<const ClassModel> class_models);
	[[nodiscard]] std::vector<RunDiagnostic>
	BuildUnsupportedItemDiagnostics(std::span<const UnsupportedItem> unsupported_items);
	[[nodiscard]] std::vector<RunDiagnostic> BuildGeneratedOutputTokenDiagnostics(
		std::span<const GeneratedOutputTokenDiagnostic> diagnostics);
} // namespace mockfakegen
