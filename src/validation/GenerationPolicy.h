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
		WriteFailure,
		FormatFailure,
		KetContamination,
		ValidationFailure,
		FallbackIncompatibility,
		LinkReadinessFailure,
	};

	enum class GenerationFailureKind
	{
		ParseFailure,
		UnsupportedItem,
		WriteFailure,
		FormatFailure,
		KetContamination,
		CompileValidationFailure,
		LinkValidationFailure,
		FallbackIncompatibility,
	};

	struct GenerationPolicyDiagnostic
	{
		GenerationPolicyDiagnosticKind kind = GenerationPolicyDiagnosticKind::ParseFailure;
		std::string message;
		std::string command = {};
		std::string stderr_summary = {};
	};

	struct GenerationFailurePolicy
	{
		int exit_code = 0;
		bool publish_generated_files = true;
		bool emit_manifest = true;
		bool emit_report = true;
	};

	struct ClassLinkReadiness
	{
		std::string qualified_name;
		bool link_ready = true;
		std::vector<std::string> reasons;
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
		bool publish_generated_files = true;
		bool emit_manifest = true;
		bool emit_report = true;
		bool has_parse_failure = false;
		bool has_unsupported_items = false;
		bool has_validation_failure = false;
		bool has_policy_failure = false;
		std::vector<ClassLinkReadiness> class_link_readiness;
		std::vector<GenerationPolicyDiagnostic> diagnostics;
	};

	[[nodiscard]] GenerationFailurePolicy EvaluateFailurePolicy(const Config& config,
																GenerationFailureKind failure_kind);
	[[nodiscard]] ClassLinkReadiness EvaluateClassLinkReadiness(const Config& config,
																const ClassModel& class_model);
	[[nodiscard]] GenerationPolicyDecision EvaluateGenerationPolicy(const Config& config,
																	GenerationPolicyInput input);
} // namespace mockfakegen
