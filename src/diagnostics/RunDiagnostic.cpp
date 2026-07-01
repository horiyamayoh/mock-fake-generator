#include "diagnostics/RunDiagnostic.h"

#include <algorithm>
#include <map>
#include <tuple>
#include <utility>

#include "validation/GeneratedOutputCheck.h"

namespace mockfakegen
{
	namespace
	{
		[[nodiscard]] int SeverityRank(DiagnosticSeverity severity) noexcept
		{
			switch (severity)
			{
				case DiagnosticSeverity::Error:
					return 0;
				case DiagnosticSeverity::Warning:
					return 1;
				case DiagnosticSeverity::Info:
					return 2;
			}

			return 3;
		}

		[[nodiscard]] std::filesystem::path DiagnosticPath(const RunDiagnostic& diagnostic)
		{
			if (!diagnostic.path.empty())
			{
				return diagnostic.path;
			}
			return diagnostic.source_range.begin.file;
		}

		[[nodiscard]] std::string MemberName(const UnsupportedItem& unsupported)
		{
			if (!unsupported.member_signature.empty())
			{
				return unsupported.member_signature;
			}
			return unsupported.name;
		}

		[[nodiscard]] std::filesystem::path UnsupportedPath(const ClassModel& class_model,
															const UnsupportedItem& unsupported)
		{
			if (!class_model.source_header.project_relative_path.empty())
			{
				return class_model.source_header.project_relative_path;
			}
			if (!class_model.source_header.include_spelling.empty())
			{
				return class_model.source_header.include_spelling;
			}
			return unsupported.source_range.begin.file;
		}

		[[nodiscard]] std::string UnsupportedCode(const UnsupportedItem& unsupported)
		{
			if (unsupported.reason_code != UnsupportedReasonCode::Unknown)
			{
				return std::string("unsupported_") + std::string(ToString(unsupported.reason_code));
			}
			if (!unsupported.kind.empty())
			{
				return "unsupported_" + unsupported.kind;
			}
			return "unsupported_unknown";
		}

		[[nodiscard]] SourceRange NormalizedUnsupportedRange(const ClassModel& class_model,
															 const UnsupportedItem& unsupported)
		{
			auto range = unsupported.source_range;
			const auto path = UnsupportedPath(class_model, unsupported);
			if (!path.empty())
			{
				range.begin.file = path;
				range.end.file = path;
			}
			return range;
		}
	} // namespace

	std::string_view ToString(DiagnosticSeverity severity) noexcept
	{
		switch (severity)
		{
			case DiagnosticSeverity::Info:
				return "info";
			case DiagnosticSeverity::Warning:
				return "warning";
			case DiagnosticSeverity::Error:
				return "error";
		}

		return "unknown";
	}

	std::string_view ToString(UnsupportedReasonCode reason) noexcept
	{
		switch (reason)
		{
			case UnsupportedReasonCode::Unknown:
				return "unknown";
			case UnsupportedReasonCode::ClassTemplate:
				return "class_template";
			case UnsupportedReasonCode::ClassTemplateSpecialization:
				return "class_template_specialization";
			case UnsupportedReasonCode::FunctionTemplate:
				return "function_template";
			case UnsupportedReasonCode::Constructor:
				return "constructor";
			case UnsupportedReasonCode::Destructor:
				return "destructor";
			case UnsupportedReasonCode::ConversionOperator:
				return "conversion_operator";
			case UnsupportedReasonCode::OverloadedOperator:
				return "overloaded_operator";
			case UnsupportedReasonCode::PureVirtualMethod:
				return "pure_virtual_method";
			case UnsupportedReasonCode::NonPublicMethod:
				return "non_public_method";
			case UnsupportedReasonCode::DeletedMethod:
				return "deleted_method";
			case UnsupportedReasonCode::DefaultedMethod:
				return "defaulted_method";
			case UnsupportedReasonCode::InlineBody:
				return "inline_body";
			case UnsupportedReasonCode::ConstexprMethod:
				return "constexpr_method";
			case UnsupportedReasonCode::ConstevalMethod:
				return "consteval_method";
			case UnsupportedReasonCode::ConditionalNoexcept:
				return "conditional_noexcept";
			case UnsupportedReasonCode::VolatileMethod:
				return "volatile_method";
			case UnsupportedReasonCode::UnsupportedAttribute:
				return "attribute";
			case UnsupportedReasonCode::MacroOrigin:
				return "macro_origin";
			case UnsupportedReasonCode::StaticDataMember:
				return "static_data_member";
			case UnsupportedReasonCode::InterfaceConstruct:
				return "interface_construct";
		}

		return "unknown";
	}

	void SortRunDiagnostics(std::vector<RunDiagnostic>& diagnostics)
	{
		std::stable_sort(diagnostics.begin(),
						 diagnostics.end(),
						 [](const auto& lhs, const auto& rhs)
						 {
							 const auto lhs_path = DiagnosticPath(lhs).generic_string();
							 const auto rhs_path = DiagnosticPath(rhs).generic_string();
							 const auto lhs_key = std::tuple{SeverityRank(lhs.severity),
															 lhs.component,
															 lhs.code,
															 lhs.kind,
															 lhs_path,
															 lhs.class_name,
															 lhs.member,
															 lhs.message,
															 lhs.command};
							 const auto rhs_key = std::tuple{SeverityRank(rhs.severity),
															 rhs.component,
															 rhs.code,
															 rhs.kind,
															 rhs_path,
															 rhs.class_name,
															 rhs.member,
															 rhs.message,
															 rhs.command};
							 return lhs_key < rhs_key;
						 });
	}

	std::vector<RunDiagnostic> SortedRunDiagnostics(std::span<const RunDiagnostic> diagnostics)
	{
		std::vector<RunDiagnostic> sorted(diagnostics.begin(), diagnostics.end());
		SortRunDiagnostics(sorted);
		return sorted;
	}

	DiagnosticSummary SummarizeDiagnostics(std::span<const RunDiagnostic> diagnostics)
	{
		DiagnosticSummary summary;
		summary.total = diagnostics.size();
		for (const auto& diagnostic : diagnostics)
		{
			switch (diagnostic.severity)
			{
				case DiagnosticSeverity::Info:
					++summary.info;
					break;
				case DiagnosticSeverity::Warning:
					++summary.warnings;
					break;
				case DiagnosticSeverity::Error:
					++summary.errors;
					break;
			}
		}
		return summary;
	}

	std::vector<ComponentDiagnosticSummary>
	SummarizeDiagnosticsByComponent(std::span<const RunDiagnostic> diagnostics)
	{
		std::map<std::string, std::vector<RunDiagnostic>> grouped;
		for (const auto& diagnostic : diagnostics)
		{
			grouped[diagnostic.component].push_back(diagnostic);
		}

		std::vector<ComponentDiagnosticSummary> summaries;
		summaries.reserve(grouped.size());
		for (const auto& [component, component_diagnostics] : grouped)
		{
			summaries.push_back(ComponentDiagnosticSummary{
				.component = component,
				.summary = SummarizeDiagnostics(component_diagnostics),
			});
		}
		return summaries;
	}

	std::vector<RunDiagnostic>
	BuildUnsupportedItemDiagnostics(std::span<const ClassModel> class_models)
	{
		std::vector<RunDiagnostic> diagnostics;
		for (const auto& class_model : class_models)
		{
			for (const auto& unsupported : class_model.unsupported_items)
			{
				RunDiagnostic diagnostic;
				diagnostic.severity = DiagnosticSeverity::Warning;
				diagnostic.component = "clang";
				diagnostic.code = UnsupportedCode(unsupported);
				diagnostic.kind = unsupported.kind;
				diagnostic.path = UnsupportedPath(class_model, unsupported);
				diagnostic.source_range = NormalizedUnsupportedRange(class_model, unsupported);
				diagnostic.class_name = unsupported.class_name.empty() ? class_model.qualified_name
																	   : unsupported.class_name;
				diagnostic.member = MemberName(unsupported);
				diagnostic.message = unsupported.reason;
				diagnostic.suggested_action = unsupported.suggested_action;
				diagnostics.push_back(std::move(diagnostic));
			}
		}
		SortRunDiagnostics(diagnostics);
		return diagnostics;
	}

	std::vector<RunDiagnostic>
	BuildUnsupportedItemDiagnostics(std::span<const UnsupportedItem> unsupported_items)
	{
		std::vector<RunDiagnostic> diagnostics;
		diagnostics.reserve(unsupported_items.size());
		for (const auto& unsupported : unsupported_items)
		{
			RunDiagnostic diagnostic;
			diagnostic.severity = DiagnosticSeverity::Warning;
			diagnostic.component = "clang";
			diagnostic.code = UnsupportedCode(unsupported);
			diagnostic.kind = unsupported.kind;
			diagnostic.path = unsupported.source_range.begin.file;
			diagnostic.source_range = unsupported.source_range;
			diagnostic.class_name = unsupported.class_name;
			diagnostic.member = MemberName(unsupported);
			diagnostic.message = unsupported.reason;
			diagnostic.suggested_action = unsupported.suggested_action;
			diagnostics.push_back(std::move(diagnostic));
		}
		SortRunDiagnostics(diagnostics);
		return diagnostics;
	}

	std::vector<RunDiagnostic> BuildGeneratedOutputTokenDiagnostics(
		std::span<const GeneratedOutputTokenDiagnostic> token_diagnostics)
	{
		std::vector<RunDiagnostic> diagnostics;
		diagnostics.reserve(token_diagnostics.size());
		for (const auto& token_diagnostic : token_diagnostics)
		{
			RunDiagnostic diagnostic;
			diagnostic.severity = DiagnosticSeverity::Error;
			diagnostic.component = "ket-contamination";
			diagnostic.code = "generated_output_forbidden_token";
			diagnostic.kind =
				token_diagnostic.token.empty() ? "generated_output_check" : token_diagnostic.token;
			diagnostic.path = token_diagnostic.path;
			diagnostic.message = token_diagnostic.message;
			diagnostic.suggested_action = "remove tool-side ket references from generated output";
			diagnostics.push_back(std::move(diagnostic));
		}
		SortRunDiagnostics(diagnostics);
		return diagnostics;
	}
} // namespace mockfakegen
