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
		method.access = mockfakegen::AccessKind::Public;
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
		Expect(decision.has_unsupported_items, "unsupported item should be recorded");
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

	void ValidationFailureIsNonZeroAndKeepsOutput()
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
		Expect(decision.write_outputs,
			   "validation failure should keep generated diagnostics output");
		Expect(decision.has_validation_failure, "validation failure should be recorded");
		Expect(HasDiagnosticKind(decision,
								 mockfakegen::GenerationPolicyDiagnosticKind::ValidationFailure),
			   "validation diagnostic should be distinct");
		Expect(!decision.diagnostics.back().command.empty(),
			   "validation policy diagnostic should retain command");
		Expect(!decision.diagnostics.back().stderr_summary.empty(),
			   "validation policy diagnostic should retain stderr summary");
	}
} // namespace

int main()
{
	BestEffortWritesGeneratedOutputAndReport();
	StrictUnsupportedReturnsNonZero();
	ParseFailureSuppressesOutput();
	ValidationFailureIsNonZeroAndKeepsOutput();
	return 0;
}
