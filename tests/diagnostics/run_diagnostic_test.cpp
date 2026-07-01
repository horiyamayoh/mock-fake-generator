#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "diagnostics/RunDiagnostic.h"
#include "validation/GeneratedOutputCheck.h"

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "EXPECTATION FAILED: " << message << '\n';
			std::exit(1);
		}
	}

	[[nodiscard]] mockfakegen::RunDiagnostic Diagnostic(mockfakegen::DiagnosticSeverity severity,
														std::string_view component,
														std::string_view code,
														std::string_view path)
	{
		mockfakegen::RunDiagnostic diagnostic;
		diagnostic.severity = severity;
		diagnostic.component = component;
		diagnostic.code = code;
		diagnostic.path = std::string(path);
		diagnostic.message = std::string(code);
		return diagnostic;
	}

	[[nodiscard]] mockfakegen::UnsupportedItem UnsupportedFunctionTemplate()
	{
		mockfakegen::UnsupportedItem unsupported;
		unsupported.reason_code = mockfakegen::UnsupportedReasonCode::FunctionTemplate;
		unsupported.kind = "function_template";
		unsupported.class_name = "sample::Service";
		unsupported.name = "Convert";
		unsupported.member_signature = "sample::Service::Convert";
		unsupported.reason = "function template member is not supported";
		unsupported.suggested_action = "exclude this member or provide a hand-authored mock";
		unsupported.source_range.begin.line = 7U;
		unsupported.source_range.begin.column = 3U;
		return unsupported;
	}

	[[nodiscard]] mockfakegen::ClassModel ServiceClass()
	{
		mockfakegen::HeaderModel header;
		header.project_relative_path = "include/Service.h";
		header.include_spelling = "Service.h";

		mockfakegen::ClassModel class_model;
		class_model.name = "Service";
		class_model.qualified_name = "sample::Service";
		class_model.source_header = header;
		class_model.unsupported_items = {UnsupportedFunctionTemplate()};
		return class_model;
	}

	void SummariesAndOrderingAreDeterministic()
	{
		const std::vector diagnostics = {
			Diagnostic(mockfakegen::DiagnosticSeverity::Info, "scanner", "scan_info", "b.h"),
			Diagnostic(mockfakegen::DiagnosticSeverity::Error, "validation", "compile", "z.cpp"),
			Diagnostic(mockfakegen::DiagnosticSeverity::Warning, "clang", "unsupported", "a.h"),
		};

		const auto summary = mockfakegen::SummarizeDiagnostics(diagnostics);
		Expect(summary.total == 3U, "summary should count all diagnostics");
		Expect(summary.info == 1U, "summary should count info diagnostics");
		Expect(summary.warnings == 1U, "summary should count warnings");
		Expect(summary.errors == 1U, "summary should count errors");

		const auto by_component = mockfakegen::SummarizeDiagnosticsByComponent(diagnostics);
		Expect(by_component.size() == 3U, "component summary should include each component");
		Expect(by_component[0].component == "clang",
			   "component summary should be sorted by component");
		Expect(by_component[1].component == "scanner",
			   "component summary should keep deterministic order");
		Expect(by_component[2].component == "validation",
			   "component summary should keep deterministic order");

		const auto sorted = mockfakegen::SortedRunDiagnostics(diagnostics);
		Expect(sorted[0].severity == mockfakegen::DiagnosticSeverity::Error,
			   "errors should sort before warnings");
		Expect(sorted[1].severity == mockfakegen::DiagnosticSeverity::Warning,
			   "warnings should sort before info");
		Expect(sorted[2].severity == mockfakegen::DiagnosticSeverity::Info,
			   "info should sort last");
	}

	void UnsupportedItemsBecomeRunDiagnostics()
	{
		const std::vector classes = {ServiceClass()};
		const auto diagnostics = mockfakegen::BuildUnsupportedItemDiagnostics(classes);

		Expect(diagnostics.size() == 1U, "one unsupported item should produce one diagnostic");
		Expect(diagnostics[0].severity == mockfakegen::DiagnosticSeverity::Warning,
			   "unsupported items should be warnings");
		Expect(diagnostics[0].component == "clang", "unsupported component should be clang");
		Expect(diagnostics[0].code == "unsupported_function_template",
			   "unsupported reason should map to stable code");
		Expect(diagnostics[0].path == "include/Service.h",
			   "unsupported path should be project-relative");
		Expect(diagnostics[0].source_range.begin.file == "include/Service.h",
			   "source range file should be normalized");
		Expect(diagnostics[0].member == "sample::Service::Convert",
			   "unsupported diagnostic should include member");
		Expect(!diagnostics[0].suggested_action.empty(),
			   "unsupported diagnostic should include suggested action");
	}

	void KetTokenDiagnosticsBecomeRunDiagnostics()
	{
		const std::vector token_diagnostics = {
			mockfakegen::GeneratedOutputTokenDiagnostic{
				.path = "generated/MockHoge.h",
				.token = "ket::",
				.message = "generated output contains forbidden token: ket::",
			},
		};

		const auto diagnostics =
			mockfakegen::BuildGeneratedOutputTokenDiagnostics(token_diagnostics);

		Expect(diagnostics.size() == 1U, "token diagnostic should convert");
		Expect(diagnostics[0].severity == mockfakegen::DiagnosticSeverity::Error,
			   "token diagnostic should be an error");
		Expect(diagnostics[0].component == "ket-contamination",
			   "token diagnostic should identify component");
		Expect(diagnostics[0].code == "generated_output_forbidden_token",
			   "token diagnostic should have stable code");
		Expect(diagnostics[0].path == "generated/MockHoge.h", "token diagnostic should keep path");
		Expect(diagnostics[0].kind == "ket::", "token diagnostic should keep token kind");
	}
} // namespace

int main()
{
	SummariesAndOrderingAreDeterministic();
	UnsupportedItemsBecomeRunDiagnostics();
	KetTokenDiagnosticsBecomeRunDiagnostics();
	return 0;
}
