#pragma once

#include <span>
#include <string>
#include <vector>

#include "Config.h"
#include "model/ProjectModel.h"
#include "validation/GeneratedCompileValidator.h"

namespace mockfakegen
{
	enum class GenerationPolicyDiagnosticKind
	{
		ParseFailure,
		UnsupportedItem,
		ValidationFailure,
	};

	struct GenerationPolicyDiagnostic
	{
		GenerationPolicyDiagnosticKind kind = GenerationPolicyDiagnosticKind::ParseFailure;
		std::string message;
		std::string command;
		std::string stderr_summary;
	};

	struct GenerationPolicyInput
	{
		std::span<const ClassModel> classes;
		std::span<const Diagnostic> parse_diagnostics;
		std::span<const GeneratedCompileDiagnostic> validation_diagnostics;
	};

	struct GenerationPolicyDecision
	{
		int exit_code = 0;
		bool write_outputs = true;
		bool has_parse_failure = false;
		bool has_unsupported_items = false;
		bool has_validation_failure = false;
		std::vector<GenerationPolicyDiagnostic> diagnostics;
	};

	[[nodiscard]] GenerationPolicyDecision EvaluateGenerationPolicy(const Config& config,
																	GenerationPolicyInput input);
} // namespace mockfakegen
