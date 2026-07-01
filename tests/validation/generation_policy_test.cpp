#include <algorithm>
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

	[[nodiscard]] mockfakegen::UnsupportedItem UnsupportedClassTemplate()
	{
		mockfakegen::UnsupportedItem item;
		item.reason_code = mockfakegen::UnsupportedReasonCode::ClassTemplate;
		item.kind = "class_template";
		item.class_name = "Box";
		item.name = "Box";
		item.member_signature = "Box";
		item.reason = "class template is not supported by link replacement fake generation";
		item.suggested_action = "exclude it or provide a hand-authored mock";
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

	[[nodiscard]] mockfakegen::ClassModel NotLinkReadyServiceClass()
	{
		auto class_model = ServiceClass(false);
		class_model.link_ready = false;
		class_model.link_readiness_reasons = {"requires hand-authored adapter"};
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

	struct ExpectedPolicy
	{
		mockfakegen::GenerationFailureKind kind;
		int best_effort_exit = 1;
		int strict_exit = 1;
		bool write_outputs = false;
		bool publish_generated_files = false;
		bool emit_manifest = true;
		bool emit_report = true;
	};

	[[nodiscard]] ExpectedPolicy Policy(mockfakegen::GenerationFailureKind kind,
										int best_effort_exit = 1,
										int strict_exit = 1,
										bool publish_generated_files = false,
										bool emit_manifest = true,
										bool emit_report = true)
	{
		return ExpectedPolicy{
			.kind = kind,
			.best_effort_exit = best_effort_exit,
			.strict_exit = strict_exit,
			.write_outputs = publish_generated_files,
			.publish_generated_files = publish_generated_files,
			.emit_manifest = emit_manifest,
			.emit_report = emit_report,
		};
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
				.unsupported_items = {},
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(decision.exit_code == 0, "best-effort unsupported output should exit zero");
		Expect(decision.write_outputs, "best-effort unsupported output should be written");
		Expect(decision.publish_generated_files,
			   "best-effort unsupported output may publish generated files");
		Expect(decision.emit_manifest, "best-effort unsupported output should emit manifest");
		Expect(decision.emit_report, "best-effort unsupported output should emit report");
		Expect(decision.has_unsupported_items, "unsupported item should be recorded");
		Expect(decision.has_link_readiness_failure,
			   "unsupported item should make the class not link-ready");
		Expect(!decision.class_link_readiness[0].link_ready,
			   "unsupported class should not be link-ready");
		Expect(Contains(decision.class_link_readiness[0].reasons[0], "unsupported items remain"),
			   "unsupported item should be preserved as a link-readiness reason");
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
				.unsupported_items = {},
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(decision.exit_code != 0, "strict unsupported output should be non-zero");
		Expect(decision.has_unsupported_items, "strict should record unsupported item");
		Expect(HasDiagnosticKind(decision,
								 mockfakegen::GenerationPolicyDiagnosticKind::UnsupportedItem),
			   "strict unsupported diagnostic should be distinct");
	}

	void TopLevelUnsupportedInfluencesPolicy()
	{
		const std::vector<mockfakegen::ClassModel> classes;
		const std::vector unsupported_items = {UnsupportedClassTemplate()};
		const std::vector<mockfakegen::Diagnostic> parse_diagnostics;
		const std::vector<mockfakegen::GeneratedCompileDiagnostic> validation_diagnostics;

		const auto best_effort_decision = mockfakegen::EvaluateGenerationPolicy(
			BestEffortConfig(),
			mockfakegen::GenerationPolicyInput{
				.classes = classes,
				.unsupported_items = unsupported_items,
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(best_effort_decision.exit_code == 0,
			   "best-effort top-level unsupported should remain zero");
		Expect(best_effort_decision.has_unsupported_items,
			   "top-level unsupported should be recorded");
		Expect(HasDiagnosticKind(best_effort_decision,
								 mockfakegen::GenerationPolicyDiagnosticKind::UnsupportedItem),
			   "top-level unsupported diagnostic should be distinct");

		const auto strict_decision = mockfakegen::EvaluateGenerationPolicy(
			StrictConfig(),
			mockfakegen::GenerationPolicyInput{
				.classes = classes,
				.unsupported_items = unsupported_items,
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(strict_decision.exit_code != 0, "strict top-level unsupported should be non-zero");
		Expect(strict_decision.has_unsupported_items,
			   "strict top-level unsupported should be recorded");
	}

	void LinkReadinessInfluencesPolicyDecision()
	{
		const std::vector classes = {NotLinkReadyServiceClass()};
		const std::vector<mockfakegen::Diagnostic> parse_diagnostics;
		const std::vector<mockfakegen::GeneratedCompileDiagnostic> validation_diagnostics;

		const auto best_effort_decision = mockfakegen::EvaluateGenerationPolicy(
			BestEffortConfig(),
			mockfakegen::GenerationPolicyInput{
				.classes = classes,
				.unsupported_items = {},
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(best_effort_decision.exit_code == 0,
			   "best-effort link-readiness failure should remain diagnosable");
		Expect(best_effort_decision.write_outputs,
			   "best-effort link-readiness failure should keep writing diagnostic output");
		Expect(best_effort_decision.publish_generated_files,
			   "best-effort link-readiness failure may publish diagnostic fake output");
		Expect(best_effort_decision.emit_manifest,
			   "best-effort link-readiness failure should emit manifest");
		Expect(best_effort_decision.emit_report,
			   "best-effort link-readiness failure should emit report");
		Expect(best_effort_decision.has_link_readiness_failure,
			   "link-readiness failure should be recorded");
		Expect(best_effort_decision.has_policy_failure,
			   "link-readiness failure should count as policy failure");
		Expect(!best_effort_decision.class_link_readiness[0].link_ready,
			   "class should be marked not link-ready");
		Expect(Contains(best_effort_decision.class_link_readiness[0].reasons[0],
						"requires hand-authored adapter"),
			   "link-readiness reason should be preserved");

		const auto strict_decision = mockfakegen::EvaluateGenerationPolicy(
			StrictConfig(),
			mockfakegen::GenerationPolicyInput{
				.classes = classes,
				.unsupported_items = {},
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(strict_decision.exit_code != 0, "strict link-readiness failure should be non-zero");
		Expect(HasDiagnosticKind(strict_decision,
								 mockfakegen::GenerationPolicyDiagnosticKind::LinkReadinessFailure),
			   "link-readiness diagnostic should be distinct");
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
				.unsupported_items = {},
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(decision.exit_code != 0, "parse failure should be non-zero");
		Expect(!decision.write_outputs, "parse failure should suppress generated output");
		Expect(!decision.publish_generated_files,
			   "parse failure should suppress publishable generated files");
		Expect(decision.emit_manifest, "parse failure should still allow manifest emission");
		Expect(decision.emit_report, "parse failure should still allow report emission");
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
				.unsupported_items = {},
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

		const std::vector expected_policies = {
			Policy(mockfakegen::GenerationFailureKind::ParseFailure),
			Policy(mockfakegen::GenerationFailureKind::UnsupportedItem, 0, 1, true),
			Policy(mockfakegen::GenerationFailureKind::WriteFailure, 1, 1, false, false),
			Policy(mockfakegen::GenerationFailureKind::FormatFailure),
			Policy(mockfakegen::GenerationFailureKind::KetContamination),
			Policy(mockfakegen::GenerationFailureKind::CompileValidationFailure),
			Policy(mockfakegen::GenerationFailureKind::LinkValidationFailure),
			Policy(mockfakegen::GenerationFailureKind::FallbackIncompatibility),
			Policy(mockfakegen::GenerationFailureKind::LinkReadinessFailure, 0, 1, true),
		};

		for (const auto& expected : expected_policies)
		{
			const auto best_effort_policy =
				mockfakegen::EvaluateFailurePolicy(best_effort, expected.kind);
			const auto strict_policy = mockfakegen::EvaluateFailurePolicy(strict, expected.kind);

			Expect(best_effort_policy.exit_code == expected.best_effort_exit,
				   "best-effort failure policy exit should match matrix");
			Expect(strict_policy.exit_code == expected.strict_exit,
				   "strict failure policy exit should match matrix");
			Expect(best_effort_policy.write_outputs == expected.write_outputs,
				   "best-effort write policy should match matrix");
			Expect(strict_policy.write_outputs == expected.write_outputs,
				   "strict write policy should match matrix");
			Expect(best_effort_policy.publish_generated_files == expected.publish_generated_files,
				   "best-effort publish policy should match matrix");
			Expect(strict_policy.publish_generated_files == expected.publish_generated_files,
				   "strict publish policy should match matrix");
			Expect(best_effort_policy.emit_manifest == expected.emit_manifest,
				   "best-effort manifest policy should match matrix");
			Expect(strict_policy.emit_manifest == expected.emit_manifest,
				   "strict manifest policy should match matrix");
			Expect(best_effort_policy.emit_report == expected.emit_report,
				   "best-effort report policy should match matrix");
			Expect(strict_policy.emit_report == expected.emit_report,
				   "strict report policy should match matrix");
		}

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

	void MixedFailuresUseMostRestrictivePublication()
	{
		const std::vector classes = {ServiceClass(true)};
		mockfakegen::Diagnostic parse_error;
		parse_error.severity = mockfakegen::DiagnosticSeverity::Error;
		parse_error.code = mockfakegen::DiagnosticCode::ParseError;
		parse_error.message = "failed to parse header";
		const std::vector parse_diagnostics = {parse_error};
		mockfakegen::GeneratedCompileDiagnostic validation_error;
		validation_error.message = "generated output compile validation failed.";
		const std::vector validation_diagnostics = {validation_error};

		const auto decision = mockfakegen::EvaluateGenerationPolicy(
			BestEffortConfig(),
			mockfakegen::GenerationPolicyInput{
				.classes = classes,
				.unsupported_items = {},
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});

		Expect(decision.exit_code != 0, "mixed hard failures should be non-zero");
		Expect(!decision.publish_generated_files,
			   "mixed hard failures should suppress generated files");
		Expect(!decision.write_outputs, "mixed hard failures should suppress publishable outputs");
		Expect(decision.emit_manifest, "mixed hard failures should still emit manifest");
		Expect(decision.emit_report, "mixed hard failures should still emit report");
		Expect(decision.has_parse_failure, "mixed failure should record parse failure");
		Expect(decision.has_unsupported_items, "mixed failure should record unsupported items");
		Expect(decision.has_validation_failure, "mixed failure should record validation failure");
		Expect(decision.has_link_readiness_failure,
			   "mixed failure should record link-readiness failure");
	}

	void CMakeFragmentOmitsNotLinkReadyFakeSources()
	{
		auto ready = ServiceClass(false);
		ready.name = "ReadyService";
		ready.qualified_name = "sample::ReadyService";
		ready.mock_name = "MockReadyService";
		ready.mock_header_name = "MockReadyService.h";
		ready.fake_source_name = "FakeReadyService.cpp";
		auto not_ready = NotLinkReadyServiceClass();
		not_ready.name = "BlockedService";
		not_ready.qualified_name = "sample::BlockedService";
		not_ready.mock_name = "MockBlockedService";
		not_ready.mock_header_name = "MockBlockedService.h";
		not_ready.fake_source_name = "FakeBlockedService.cpp";
		const std::vector classes = {ready, not_ready};
		const auto files = mockfakegen::GenerateMockFakeProject(classes);
		const auto fragment =
			std::find_if(files.begin(),
						 files.end(),
						 [](const auto& file)
						 {
							 return file.relative_path == "CMakeLists.fragment.cmake";
						 });

		Expect(fragment != files.end(), "project should generate CMake fragment");
		Expect(Contains(fragment->content, "FakeReadyService.cpp"),
			   "link-ready service source should be listed");
		Expect(!Contains(fragment->content, "FakeBlockedService.cpp"),
			   "not-link-ready service source should not be listed");
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
				.unsupported_items = {},
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
				.unsupported_items = {},
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
	TopLevelUnsupportedInfluencesPolicy();
	LinkReadinessInfluencesPolicyDecision();
	ParseFailureSuppressesOutput();
	ValidationFailureIsNonZeroAndSuppressesPublish();
	FailurePolicyMatrixCoversPublicationAndReports();
	MixedFailuresUseMostRestrictivePublication();
	CMakeFragmentOmitsNotLinkReadyFakeSources();
	DefaultReturnRejectsReferenceAndNonDefaultConstructibleReturns();
	ThrowFallbackRejectsNoexceptMethods();
	return 0;
}
