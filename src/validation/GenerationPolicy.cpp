#include "validation/GenerationPolicy.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace mockfakegen
{
	namespace
	{
		[[nodiscard]] bool IsParseFailure(const Diagnostic& diagnostic) noexcept
		{
			return diagnostic.code == DiagnosticCode::ParseError ||
				diagnostic.severity == DiagnosticSeverity::Error;
		}

		[[nodiscard]] std::size_t UnsupportedItemCount(std::span<const ClassModel> classes)
		{
			std::size_t count = 0U;
			for (const auto& class_model : classes)
			{
				count += class_model.unsupported_items.size();
			}
			return count;
		}

		void AddParseDiagnostics(GenerationPolicyDecision& decision,
								 std::span<const Diagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				if (!IsParseFailure(diagnostic))
				{
					continue;
				}
				GenerationPolicyDiagnostic policy_diagnostic;
				policy_diagnostic.kind = GenerationPolicyDiagnosticKind::ParseFailure;
				policy_diagnostic.message = diagnostic.message;
				decision.diagnostics.push_back(std::move(policy_diagnostic));
			}
		}

		void AddUnsupportedDiagnostics(GenerationPolicyDecision& decision,
									   std::span<const ClassModel> classes)
		{
			for (const auto& class_model : classes)
			{
				for (const auto& unsupported : class_model.unsupported_items)
				{
					GenerationPolicyDiagnostic diagnostic;
					diagnostic.kind = GenerationPolicyDiagnosticKind::UnsupportedItem;
					diagnostic.message = unsupported.member_signature + ": " + unsupported.reason;
					decision.diagnostics.push_back(std::move(diagnostic));
				}
			}
		}

		void AddValidationDiagnostics(GenerationPolicyDecision& decision,
									  std::span<const GeneratedCompileDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				decision.diagnostics.push_back(GenerationPolicyDiagnostic{
					.kind = GenerationPolicyDiagnosticKind::ValidationFailure,
					.message = diagnostic.message,
					.command = diagnostic.command,
					.stderr_summary = diagnostic.stderr_summary,
				});
			}
		}
	} // namespace

	GenerationPolicyDecision EvaluateGenerationPolicy(const Config& config,
													  GenerationPolicyInput input)
	{
		GenerationPolicyDecision decision;
		decision.has_parse_failure = std::any_of(
			input.parse_diagnostics.begin(), input.parse_diagnostics.end(), IsParseFailure);
		decision.has_unsupported_items = UnsupportedItemCount(input.classes) != 0U;
		decision.has_validation_failure = !input.validation_diagnostics.empty();

		AddParseDiagnostics(decision, input.parse_diagnostics);
		AddUnsupportedDiagnostics(decision, input.classes);
		AddValidationDiagnostics(decision, input.validation_diagnostics);

		decision.write_outputs = !decision.has_parse_failure;

		if (decision.has_parse_failure || decision.has_validation_failure ||
			(config.strict && decision.has_unsupported_items))
		{
			decision.exit_code = 1;
		}

		return decision;
	}
} // namespace mockfakegen
