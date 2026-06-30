#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Config.h"
#include "generator/CodeGenerator.h"
#include "output/OutputWriter.h"
#include "validation/GenerationPolicy.h"

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

	[[nodiscard]] bool Contains(std::string_view text, std::string_view token)
	{
		return text.find(token) != std::string_view::npos;
	}

	class TempTree
	{
	  public:
		TempTree()
			: root_(std::filesystem::temp_directory_path() /
					("mockfakegen_generation_policy_test_" + std::to_string(UniqueSuffix())))
		{
			std::filesystem::remove_all(root_);
			std::filesystem::create_directories(root_);
		}

		TempTree(const TempTree&) = delete;
		TempTree& operator=(const TempTree&) = delete;

		~TempTree()
		{
			std::error_code error;
			std::filesystem::remove_all(root_, error);
		}

		[[nodiscard]] const std::filesystem::path& root() const noexcept
		{
			return root_;
		}

	  private:
		[[nodiscard]] static long long UniqueSuffix()
		{
			return std::chrono::steady_clock::now().time_since_epoch().count();
		}

		std::filesystem::path root_;
	};

	[[nodiscard]] std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		Expect(stream.good(), "output file should be readable");
		return buffer.str();
	}

	[[nodiscard]] mockfakegen::Config BestEffortConfig()
	{
		mockfakegen::Config config;
		config.strict = false;
		config.best_effort = true;
		return config;
	}

	[[nodiscard]] mockfakegen::Config StrictConfig()
	{
		mockfakegen::Config config;
		config.strict = true;
		config.best_effort = false;
		return config;
	}

	[[nodiscard]] mockfakegen::Config DefaultReturnConfig()
	{
		auto config = BestEffortConfig();
		config.fallback_policy = mockfakegen::FallbackPolicy::DefaultReturn;
		return config;
	}

	[[nodiscard]] mockfakegen::Config ThrowFallbackConfig()
	{
		auto config = BestEffortConfig();
		config.fallback_policy = mockfakegen::FallbackPolicy::Throw;
		return config;
	}

	[[nodiscard]] mockfakegen::MethodModel RunMethod()
	{
		mockfakegen::ParameterModel parameter;
		parameter.type_spelling = "int";
		parameter.gmock_type_spelling = "int";
		parameter.generated_name = "value";

		mockfakegen::MethodModel method;
		method.name = "Run";
		method.qualified_owner_name = "sample::Service";
		method.return_type_spelling = "bool";
		method.gmock_return_type_spelling = "bool";
		method.parameters = {parameter};
		method.signature_for_report = "sample::Service::Run(int)";
		method.return_type_is_default_constructible = true;
		method.access = mockfakegen::AccessKind::Public;
		return method;
	}

	[[nodiscard]] mockfakegen::MethodModel ReferenceReturnMethod()
	{
		auto method = RunMethod();
		method.name = "GetConfig";
		method.return_type_spelling = "const Config&";
		method.gmock_return_type_spelling = "const Config&";
		method.signature_for_report = "sample::Service::GetConfig()";
		method.parameters = {};
		method.return_type_is_reference = true;
		method.return_type_is_default_constructible = false;
		return method;
	}

	[[nodiscard]] mockfakegen::MethodModel NonDefaultConstructibleReturnMethod()
	{
		auto method = RunMethod();
		method.name = "MakeToken";
		method.return_type_spelling = "Token";
		method.gmock_return_type_spelling = "Token";
		method.signature_for_report = "sample::Service::MakeToken()";
		method.parameters = {};
		method.return_type_is_default_constructible = false;
		return method;
	}

	[[nodiscard]] mockfakegen::MethodModel NoexceptReturnMethod()
	{
		auto method = RunMethod();
		method.name = "NoexceptRun";
		method.signature_for_report = "sample::Service::NoexceptRun()";
		method.parameters = {};
		method.is_noexcept = true;
		return method;
	}

	[[nodiscard]] mockfakegen::UnsupportedItem UnsupportedTemplate()
	{
		mockfakegen::UnsupportedItem item;
		item.kind = "function_template";
		item.class_name = "sample::Service";
		item.name = "Convert";
		item.member_signature = "sample::Service::Convert";
		item.reason = "function template member is not supported";
		item.suggested_action = "exclude this member or provide a hand-authored mock";
		return item;
	}

	[[nodiscard]] mockfakegen::ClassModel ServiceClass(bool include_unsupported)
	{
		auto method = RunMethod();
		mockfakegen::HeaderModel header;
		header.include_spelling = "Service.h";

		mockfakegen::ClassModel class_model;
		class_model.name = "Service";
		class_model.qualified_name = "sample::Service";
		class_model.namespaces = {"sample"};
		class_model.mock_name = "MockService";
		class_model.mock_header_name = "MockService.h";
		class_model.fake_source_name = "FakeService.cpp";
		class_model.source_header = header;
		class_model.mock_methods = {method};
		class_model.fake_methods = {method};
		if (include_unsupported)
		{
			class_model.unsupported_items = {UnsupportedTemplate()};
		}
		return class_model;
	}

	[[nodiscard]] bool HasDiagnosticKind(const mockfakegen::GenerationPolicyDecision& decision,
										 mockfakegen::GenerationPolicyDiagnosticKind kind)
	{
		for (const auto& diagnostic : decision.diagnostics)
		{
			if (diagnostic.kind == kind)
			{
				return true;
			}
		}
		return false;
	}

	void BestEffortWritesGeneratedOutputAndReport()
	{
		TempTree tree;
		const std::vector classes = {ServiceClass(true)};
		const std::vector<mockfakegen::Diagnostic> parse_diagnostics;
		const std::vector<mockfakegen::GeneratedCompileDiagnostic> validation_diagnostics;

		const auto decision = mockfakegen::EvaluateGenerationPolicy(
			BestEffortConfig(),
			mockfakegen::GenerationPolicyInput{
				.classes = classes,
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(decision.exit_code == 0, "best-effort unsupported output should exit zero");
		Expect(decision.write_outputs, "best-effort unsupported output should be written");
		Expect(decision.publish_generated_files,
			   "best-effort unsupported output may publish generated files");
		Expect(decision.has_unsupported_items, "unsupported item should be recorded");
		Expect(!decision.class_link_readiness[0].link_ready,
			   "unsupported class should not be link-ready");
		Expect(HasDiagnosticKind(decision,
								 mockfakegen::GenerationPolicyDiagnosticKind::UnsupportedItem),
			   "unsupported diagnostic should be distinct");

		mockfakegen::OutputWriterOptions output_options;
		output_options.output_dir = tree.root();
		output_options.overwrite = true;
		const auto write_result = mockfakegen::WriteGeneratedFiles(
			output_options, mockfakegen::GenerateMockFakeProject(classes));
		Expect(write_result.ok(), "best-effort generated files should be writable");
		Expect(std::filesystem::exists(tree.root() / "MockService.h"),
			   "best-effort should write generated mock header");
		const auto report = ReadText(tree.root() / "generation_report.md");
		Expect(Contains(report, "sample::Service::Convert"),
			   "best-effort report should include unsupported item");
		const auto manifest = ReadText(tree.root() / "manifest.json");
		Expect(Contains(manifest, "\"link_ready\": false"),
			   "manifest should mark incomplete fake as not link-ready");
	}

	void StrictUnsupportedReturnsNonZero()
	{
		const std::vector classes = {ServiceClass(true)};
		const std::vector<mockfakegen::Diagnostic> parse_diagnostics;
		const std::vector<mockfakegen::GeneratedCompileDiagnostic> validation_diagnostics;

		const auto decision = mockfakegen::EvaluateGenerationPolicy(
			StrictConfig(),
			mockfakegen::GenerationPolicyInput{
				.classes = classes,
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(decision.exit_code != 0, "strict unsupported output should be non-zero");
		Expect(decision.has_unsupported_items, "strict should record unsupported item");
		Expect(HasDiagnosticKind(decision,
								 mockfakegen::GenerationPolicyDiagnosticKind::UnsupportedItem),
			   "strict unsupported diagnostic should be distinct");
	}

	void ParseFailureSuppressesOutput()
	{
		const std::vector<mockfakegen::ClassModel> classes;
		mockfakegen::Diagnostic parse_error;
		parse_error.severity = mockfakegen::DiagnosticSeverity::Error;
		parse_error.code = mockfakegen::DiagnosticCode::ParseError;
		parse_error.message = "failed to parse header";
		const std::vector parse_diagnostics = {parse_error};
		const std::vector<mockfakegen::GeneratedCompileDiagnostic> validation_diagnostics;

		const auto decision = mockfakegen::EvaluateGenerationPolicy(
			BestEffortConfig(),
			mockfakegen::GenerationPolicyInput{
				.classes = classes,
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(decision.exit_code != 0, "parse failure should be non-zero");
		Expect(!decision.write_outputs, "parse failure should suppress generated output");
		Expect(decision.has_parse_failure, "parse failure should be recorded");
		Expect(
			HasDiagnosticKind(decision, mockfakegen::GenerationPolicyDiagnosticKind::ParseFailure),
			"parse diagnostic should be distinct");
	}

	void ValidationFailureIsNonZeroAndSuppressesPublish()
	{
		const std::vector classes = {ServiceClass(false)};
		const std::vector<mockfakegen::Diagnostic> parse_diagnostics;
		mockfakegen::GeneratedCompileDiagnostic validation_error;
		validation_error.message = "generated output compile validation failed.";
		validation_error.command = "c++ -std=c++23 -c FakeService.cpp";
		validation_error.stderr_summary = "FakeService.cpp:1: error";
		const std::vector validation_diagnostics = {validation_error};

		const auto decision = mockfakegen::EvaluateGenerationPolicy(
			BestEffortConfig(),
			mockfakegen::GenerationPolicyInput{
				.classes = classes,
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(decision.exit_code != 0, "validation failure should be non-zero");
		Expect(!decision.write_outputs, "validation failure should suppress generated output");
		Expect(!decision.publish_generated_files,
			   "validation failure should suppress publishable generated files");
		Expect(decision.emit_manifest, "validation failure should still allow manifest emission");
		Expect(decision.emit_report, "validation failure should still allow report emission");
		Expect(decision.has_validation_failure, "validation failure should be recorded");
		Expect(HasDiagnosticKind(decision,
								 mockfakegen::GenerationPolicyDiagnosticKind::ValidationFailure),
			   "validation diagnostic should be distinct");
		Expect(!decision.diagnostics.back().command.empty(),
			   "validation policy diagnostic should retain command");
		Expect(!decision.diagnostics.back().stderr_summary.empty(),
			   "validation policy diagnostic should retain stderr summary");
	}

	void FailurePolicyMatrixCoversPublicationAndReports()
	{
		const auto best_effort = BestEffortConfig();
		const auto strict = StrictConfig();

		const auto best_effort_unsupported = mockfakegen::EvaluateFailurePolicy(
			best_effort, mockfakegen::GenerationFailureKind::UnsupportedItem);
		Expect(best_effort_unsupported.exit_code == 0,
			   "best-effort unsupported items should exit zero");
		Expect(best_effort_unsupported.publish_generated_files,
			   "best-effort unsupported items may publish generated files");
		Expect(best_effort_unsupported.emit_manifest && best_effort_unsupported.emit_report,
			   "best-effort unsupported items should emit manifest and report");

		const auto strict_unsupported = mockfakegen::EvaluateFailurePolicy(
			strict, mockfakegen::GenerationFailureKind::UnsupportedItem);
		Expect(strict_unsupported.exit_code != 0, "strict unsupported items should fail");
		Expect(strict_unsupported.publish_generated_files,
			   "strict unsupported items still keep diagnostics artifacts publishable");

		const auto parse_failure = mockfakegen::EvaluateFailurePolicy(
			best_effort, mockfakegen::GenerationFailureKind::ParseFailure);
		Expect(parse_failure.exit_code != 0, "parse failure should fail");
		Expect(!parse_failure.publish_generated_files,
			   "parse failure should not publish generated files");
		Expect(parse_failure.emit_manifest && parse_failure.emit_report,
			   "parse failure should still allow diagnostic artifacts");

		const auto validation_failure = mockfakegen::EvaluateFailurePolicy(
			best_effort, mockfakegen::GenerationFailureKind::CompileValidationFailure);
		Expect(validation_failure.exit_code != 0, "compile validation failure should fail");
		Expect(!validation_failure.publish_generated_files,
			   "compile validation failure should not publish generated files");

		const auto ket_contamination = mockfakegen::EvaluateFailurePolicy(
			best_effort, mockfakegen::GenerationFailureKind::KetContamination);
		Expect(ket_contamination.exit_code != 0, "ket contamination should fail");
		Expect(!ket_contamination.publish_generated_files,
			   "ket-contaminated output should not be published");
	}

	void DefaultReturnRejectsReferenceAndNonDefaultConstructibleReturns()
	{
		auto class_model = ServiceClass(false);
		class_model.mock_methods = {ReferenceReturnMethod(), NonDefaultConstructibleReturnMethod()};
		class_model.fake_methods = class_model.mock_methods;
		const std::vector classes = {class_model};
		const std::vector<mockfakegen::Diagnostic> parse_diagnostics;
		const std::vector<mockfakegen::GeneratedCompileDiagnostic> validation_diagnostics;

		const auto decision = mockfakegen::EvaluateGenerationPolicy(
			DefaultReturnConfig(),
			mockfakegen::GenerationPolicyInput{
				.classes = classes,
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(decision.exit_code != 0, "default-return incompatibility should fail");
		Expect(!decision.publish_generated_files,
			   "fallback-incompatible output should not be published");
		Expect(decision.has_policy_failure, "fallback incompatibility should be policy failure");
		Expect(HasDiagnosticKind(
				   decision, mockfakegen::GenerationPolicyDiagnosticKind::FallbackIncompatibility),
			   "fallback incompatibility diagnostic should be distinct");
		Expect(Contains(decision.diagnostics[0].message, "cannot return a reference"),
			   "reference return diagnostic should be explicit");
		Expect(Contains(decision.diagnostics[1].message, "default-constructible"),
			   "non-default-constructible return diagnostic should be explicit");
		Expect(!decision.class_link_readiness[0].link_ready,
			   "fallback-incompatible class should not be link-ready");
	}

	void ThrowFallbackRejectsNoexceptMethods()
	{
		auto class_model = ServiceClass(false);
		class_model.mock_methods = {NoexceptReturnMethod()};
		class_model.fake_methods = class_model.mock_methods;
		const std::vector classes = {class_model};
		const std::vector<mockfakegen::Diagnostic> parse_diagnostics;
		const std::vector<mockfakegen::GeneratedCompileDiagnostic> validation_diagnostics;

		const auto decision = mockfakegen::EvaluateGenerationPolicy(
			ThrowFallbackConfig(),
			mockfakegen::GenerationPolicyInput{
				.classes = classes,
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(decision.exit_code != 0, "throw fallback on noexcept should fail");
		Expect(HasDiagnosticKind(
				   decision, mockfakegen::GenerationPolicyDiagnosticKind::FallbackIncompatibility),
			   "throw/noexcept diagnostic should be distinct");
		Expect(Contains(decision.diagnostics[0].message, "noexcept"),
			   "throw/noexcept diagnostic should mention noexcept");
		Expect(!decision.class_link_readiness[0].link_ready,
			   "throw/noexcept class should not be link-ready");
	}
} // namespace

int main()
{
	BestEffortWritesGeneratedOutputAndReport();
	StrictUnsupportedReturnsNonZero();
	ParseFailureSuppressesOutput();
	ValidationFailureIsNonZeroAndSuppressesPublish();
	FailurePolicyMatrixCoversPublicationAndReports();
	DefaultReturnRejectsReferenceAndNonDefaultConstructibleReturns();
	ThrowFallbackRejectsNoexceptMethods();
	return 0;
}
