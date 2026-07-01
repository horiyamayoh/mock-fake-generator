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

		[[nodiscard]] std::size_t
		UnsupportedItemCount(std::span<const ClassModel> classes,
							 std::span<const UnsupportedItem> unsupported_items)
		{
			return UnsupportedItemCount(classes) + unsupported_items.size();
		}

		[[nodiscard]] std::string QualifiedClassName(const ClassModel& class_model)
		{
			if (!class_model.qualified_name.empty())
			{
				return class_model.qualified_name;
			}
			return BuildQualifiedName(class_model.namespaces, class_model.name);
		}

		[[nodiscard]] bool ReturnsVoid(const MethodModel& method) noexcept
		{
			return method.return_type_is_void || method.return_type_spelling == "void";
		}

		[[nodiscard]] std::vector<std::string>
		FallbackIncompatibilityReasons(const Config& config, const MethodModel& method)
		{
			std::vector<std::string> reasons;
			switch (config.fallback_policy)
			{
				case FallbackPolicy::Abort:
					break;
				case FallbackPolicy::DefaultReturn:
					if (method.return_type_is_reference)
					{
						reasons.push_back("default-return fallback cannot return a reference");
					}
					else if (!ReturnsVoid(method) && !method.return_type_is_default_constructible)
					{
						reasons.push_back(
							"default-return fallback requires a default-constructible return type");
					}
					break;
				case FallbackPolicy::Throw:
					if (method.is_noexcept)
					{
						reasons.push_back("throw fallback cannot be used for noexcept functions");
					}
					break;
			}
			return reasons;
		}

		[[nodiscard]] std::string
		UnsupportedItemsLinkReadinessReason(std::span<const UnsupportedItem> unsupported_items)
		{
			std::string reason = "unsupported items remain";
			std::vector<std::string> kinds;
			for (const auto& item : unsupported_items)
			{
				if (std::find(kinds.begin(), kinds.end(), item.kind) == kinds.end())
				{
					kinds.push_back(item.kind);
				}
			}
			if (!kinds.empty())
			{
				reason += ": ";
				for (std::size_t index = 0U; index < kinds.size(); ++index)
				{
					if (index != 0U)
					{
						reason += ", ";
					}
					reason += kinds[index];
				}
			}
			return reason;
		}

		void AppendClassLinkReadinessReasons(const Config& config,
											 const ClassModel& class_model,
											 std::vector<std::string>& reasons)
		{
			for (const auto& reason : class_model.link_readiness_reasons)
			{
				reasons.push_back(reason);
			}
			if (!class_model.unsupported_items.empty())
			{
				reasons.push_back(
					UnsupportedItemsLinkReadinessReason(class_model.unsupported_items));
			}
			for (const auto& method : class_model.fake_methods)
			{
				for (auto reason : FallbackIncompatibilityReasons(config, method))
				{
					reasons.push_back(method.signature_for_report + ": " + std::move(reason));
				}
			}
			if (!class_model.link_ready && reasons.empty())
			{
				reasons.push_back("class marked not link-ready");
			}
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

		void AddUnsupportedDiagnostics(GenerationPolicyDecision& decision,
									   std::span<const UnsupportedItem> unsupported_items)
		{
			for (const auto& unsupported : unsupported_items)
			{
				GenerationPolicyDiagnostic diagnostic;
				diagnostic.kind = GenerationPolicyDiagnosticKind::UnsupportedItem;
				diagnostic.message = unsupported.member_signature + ": " + unsupported.reason;
				decision.diagnostics.push_back(std::move(diagnostic));
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

		void AddFallbackDiagnostics(GenerationPolicyDecision& decision,
									const Config& config,
									std::span<const ClassModel> classes)
		{
			for (const auto& class_model : classes)
			{
				for (const auto& method : class_model.fake_methods)
				{
					for (const auto& reason : FallbackIncompatibilityReasons(config, method))
					{
						decision.diagnostics.push_back(GenerationPolicyDiagnostic{
							.kind = GenerationPolicyDiagnosticKind::FallbackIncompatibility,
							.message = method.signature_for_report + ": " + reason,
						});
					}
				}
			}
		}

		void ApplyFailurePolicy(GenerationPolicyDecision& decision,
								const GenerationFailurePolicy& policy)
		{
			decision.exit_code = std::max(decision.exit_code, policy.exit_code);
			decision.write_outputs = decision.write_outputs && policy.write_outputs;
			decision.publish_generated_files =
				decision.publish_generated_files && policy.publish_generated_files;
			decision.emit_manifest = decision.emit_manifest && policy.emit_manifest;
			decision.emit_report = decision.emit_report && policy.emit_report;
		}
	} // namespace

	GenerationFailurePolicy EvaluateFailurePolicy(const Config& config,
												  GenerationFailureKind failure_kind)
	{
		switch (failure_kind)
		{
			case GenerationFailureKind::ParseFailure:
				return {
					.exit_code = 1,
					.write_outputs = false,
					.publish_generated_files = false,
					.emit_manifest = true,
					.emit_report = true,
				};
			case GenerationFailureKind::UnsupportedItem:
				return {
					.exit_code = config.strict ? 1 : 0,
					.write_outputs = true,
					.publish_generated_files = true,
					.emit_manifest = true,
					.emit_report = true,
				};
			case GenerationFailureKind::WriteFailure:
				return {
					.exit_code = 1,
					.write_outputs = false,
					.publish_generated_files = false,
					.emit_manifest = false,
					.emit_report = true,
				};
			case GenerationFailureKind::FormatFailure:
			case GenerationFailureKind::KetContamination:
			case GenerationFailureKind::CompileValidationFailure:
			case GenerationFailureKind::LinkValidationFailure:
			case GenerationFailureKind::FallbackIncompatibility:
				return {
					.exit_code = 1,
					.write_outputs = false,
					.publish_generated_files = false,
					.emit_manifest = true,
					.emit_report = true,
				};
			case GenerationFailureKind::LinkReadinessFailure:
				return {
					.exit_code = config.strict ? 1 : 0,
					.write_outputs = true,
					.publish_generated_files = true,
					.emit_manifest = true,
					.emit_report = true,
				};
		}

		return {};
	}

	ClassLinkReadiness EvaluateClassLinkReadiness(const Config& config,
												  const ClassModel& class_model)
	{
		ClassLinkReadiness readiness;
		readiness.qualified_name = QualifiedClassName(class_model);
		AppendClassLinkReadinessReasons(config, class_model, readiness.reasons);
		readiness.link_ready = class_model.link_ready && readiness.reasons.empty();
		return readiness;
	}

	GenerationPolicyDecision EvaluateGenerationPolicy(const Config& config,
													  GenerationPolicyInput input)
	{
		GenerationPolicyDecision decision;
		decision.emit_manifest = config.emit_manifest;
		decision.has_parse_failure = std::any_of(
			input.parse_diagnostics.begin(), input.parse_diagnostics.end(), IsParseFailure);
		decision.has_unsupported_items =
			UnsupportedItemCount(input.classes, input.unsupported_items) != 0U;
		decision.has_validation_failure = !input.validation_diagnostics.empty();

		AddParseDiagnostics(decision, input.parse_diagnostics);
		AddUnsupportedDiagnostics(decision, input.classes);
		AddUnsupportedDiagnostics(decision, input.unsupported_items);
		AddValidationDiagnostics(decision, input.validation_diagnostics);
		AddFallbackDiagnostics(decision, config, input.classes);

		for (const auto& class_model : input.classes)
		{
			auto readiness = EvaluateClassLinkReadiness(config, class_model);
			if (!readiness.link_ready)
			{
				decision.has_link_readiness_failure = true;
				decision.diagnostics.push_back(GenerationPolicyDiagnostic{
					.kind = GenerationPolicyDiagnosticKind::LinkReadinessFailure,
					.message = readiness.qualified_name + " is not link-ready",
				});
			}
			decision.class_link_readiness.push_back(std::move(readiness));
		}
		const auto has_fallback_incompatibility = std::any_of(
			decision.diagnostics.begin(),
			decision.diagnostics.end(),
			[](const auto& diagnostic)
			{
				return diagnostic.kind == GenerationPolicyDiagnosticKind::FallbackIncompatibility;
			});
		decision.has_policy_failure =
			has_fallback_incompatibility || decision.has_link_readiness_failure;

		if (decision.has_parse_failure)
		{
			ApplyFailurePolicy(decision,
							   EvaluateFailurePolicy(config, GenerationFailureKind::ParseFailure));
		}
		if (decision.has_unsupported_items)
		{
			ApplyFailurePolicy(
				decision, EvaluateFailurePolicy(config, GenerationFailureKind::UnsupportedItem));
		}
		if (decision.has_validation_failure)
		{
			ApplyFailurePolicy(
				decision,
				EvaluateFailurePolicy(config, GenerationFailureKind::CompileValidationFailure));
		}
		if (has_fallback_incompatibility)
		{
			ApplyFailurePolicy(
				decision,
				EvaluateFailurePolicy(config, GenerationFailureKind::FallbackIncompatibility));
		}
		if (decision.has_link_readiness_failure)
		{
			ApplyFailurePolicy(
				decision,
				EvaluateFailurePolicy(config, GenerationFailureKind::LinkReadinessFailure));
		}

		return decision;
	}
} // namespace mockfakegen
