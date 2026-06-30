#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "Config.h"

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

	[[nodiscard]] std::filesystem::path ExpectedPath(const char* value)
	{
		std::error_code error;
		const auto absolute = std::filesystem::absolute(value, error);
		if (error)
		{
			return std::filesystem::path(value).lexically_normal();
		}

		auto normalized = absolute.lexically_normal();
		if (normalized.has_relative_path() && normalized.filename().empty())
		{
			normalized = normalized.parent_path();
		}

		return normalized;
	}

	[[nodiscard]] std::vector<std::string> ValidArgs()
	{
		return {
			"mockfakegen",
			"--input-root",
			"include",
			"--output-dir=generated",
			"--build-path",
			"build",
			"--project-root",
			".",
			"--dry-run",
			"--jobs",
			"3",
		};
	}

	void ParsesValidConfig()
	{
		const auto args = ValidArgs();
		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "valid config should parse");
		Expect(result.config->input_root == ExpectedPath("include"),
			   "input root should be resolved");
		Expect(result.config->output_dir == ExpectedPath("generated"),
			   "output dir should be resolved");
		Expect(result.config->build_path == ExpectedPath("build"), "build path should be resolved");
		Expect(result.config->project_root == ExpectedPath("."), "project root should be resolved");
		Expect(result.config->standard == "c++23", "standard should default to c++23");
		Expect(result.config->header_extension == ".h", "header extension should default to .h");
		Expect(result.config->access == mockfakegen::AccessPolicy::Public,
			   "access should default to public");
		Expect(result.config->registry_mode == mockfakegen::RegistryMode::ThreadLocal,
			   "registry mode should default to thread-local");
		Expect(result.config->fallback_policy == mockfakegen::FallbackPolicy::Abort,
			   "fallback policy should default to abort");
		Expect(result.config->mock_namespace_mode == mockfakegen::MockNamespaceMode::SameAsProduct,
			   "mock namespace mode should default to same-as-product");
		Expect(result.config->collision_policy == mockfakegen::CollisionPolicy::QualifiedFilename,
			   "collision policy should default to qualified-filename");
		Expect(result.config->dry_run, "dry-run should be true");
		Expect(!result.config->overwrite, "overwrite should default false");
		Expect(!result.config->strict, "strict should default false");
		Expect(result.config->best_effort, "best-effort should default true");
		Expect(!result.config->include_struct, "include-struct should default false");
		Expect(result.config->emit_all_mocks, "emit-all-mocks should default true");
		Expect(result.config->emit_manifest, "emit-manifest should default true");
		Expect(result.config->emit_cmake_fragment, "emit-cmake-fragment should default true");
		Expect(!result.config->fake_special_members, "fake-special-members should default false");
		Expect(!result.config->fake_static_data, "fake-static-data should default false");
		Expect(!result.config->interface_mock, "interface-mock should default false");
		Expect(result.config->format_style == mockfakegen::FormatStyleKind::File,
			   "format style should default to file");
		Expect(result.config->validate == mockfakegen::ValidationMode::Compile,
			   "validate should default to compile");
		Expect(result.config->jobs == 3, "jobs should parse as integer");
	}

	void ParsesEmitAllMocksFalse()
	{
		auto args = ValidArgs();
		args.push_back("--emit-all-mocks=false");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "emit-all-mocks false config should parse");
		Expect(!result.config->emit_all_mocks, "emit-all-mocks should parse false");
	}

	void ParsesEmitManifestFalse()
	{
		auto args = ValidArgs();
		args.push_back("--emit-manifest=false");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "emit-manifest false config should parse");
		Expect(!result.config->emit_manifest, "emit-manifest should parse false");
	}

	void ParsesEmitCMakeFragmentFalse()
	{
		auto args = ValidArgs();
		args.push_back("--emit-cmake-fragment=false");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "emit-cmake-fragment false config should parse");
		Expect(!result.config->emit_cmake_fragment, "emit-cmake-fragment should parse false");
	}

	void ParsesFormatStyleGoogle()
	{
		auto args = ValidArgs();
		args.push_back("--format-style=google");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "format-style google config should parse");
		Expect(result.config->format_style == mockfakegen::FormatStyleKind::Google,
			   "format-style should parse google");
	}

	void ParsesValidateNone()
	{
		auto args = ValidArgs();
		args.push_back("--validate=none");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "validate none config should parse");
		Expect(result.config->validate == mockfakegen::ValidationMode::None,
			   "validate should parse none");
	}

	void ReportsMissingRequiredOptions()
	{
		const std::vector<std::string> args{"mockfakegen", "--dry-run"};
		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "missing required options should fail");
		Expect(result.errors.size() == 4U, "four required path options should be reported");
		Expect(result.errors[0].option == "--input-root",
			   "input-root should be first missing option");
		Expect(result.errors[1].option == "--output-dir",
			   "output-dir should be second missing option");
		Expect(result.errors[2].option == "--build-path",
			   "build-path should be third missing option");
		Expect(result.errors[3].option == "--project-root",
			   "project-root should be fourth missing option");
	}

	void ReportsInvalidJobs()
	{
		auto args = ValidArgs();
		args.back() = "abc";

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "invalid jobs should fail");
		Expect(result.errors.size() == 1U, "invalid jobs should produce one error");
		Expect(result.errors[0].option == "--jobs", "invalid jobs should identify option");
		Expect(result.errors[0].message == "--jobs must be a positive integer.",
			   "invalid jobs diagnostic should be deterministic");
	}

	void ReportsInvalidEmitAllMocks()
	{
		auto args = ValidArgs();
		args.push_back("--emit-all-mocks=maybe");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "invalid emit-all-mocks should fail");
		Expect(result.errors.size() == 1U, "invalid emit-all-mocks should produce one error");
		Expect(result.errors[0].option == "--emit-all-mocks",
			   "invalid emit-all-mocks should identify option");
		Expect(result.errors[0].message == "--emit-all-mocks must be true or false.",
			   "invalid emit-all-mocks diagnostic should be deterministic");
	}

	void ReportsInvalidEmitCMakeFragment()
	{
		auto args = ValidArgs();
		args.push_back("--emit-cmake-fragment=maybe");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "invalid emit-cmake-fragment should fail");
		Expect(result.errors.size() == 1U, "invalid emit-cmake-fragment should produce one error");
		Expect(result.errors[0].option == "--emit-cmake-fragment",
			   "invalid emit-cmake-fragment should identify option");
		Expect(result.errors[0].message == "--emit-cmake-fragment must be true or false.",
			   "invalid emit-cmake-fragment diagnostic should be deterministic");
	}

	void ReportsInvalidFormatStyle()
	{
		auto args = ValidArgs();
		args.push_back("--format-style=webkit");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "invalid format-style should fail");
		Expect(result.errors.size() == 1U, "invalid format-style should produce one error");
		Expect(result.errors[0].option == "--format-style",
			   "invalid format-style should identify option");
		Expect(result.errors[0].message == "--format-style must be file, llvm, google, or none.",
			   "invalid format-style diagnostic should be deterministic");
	}

	void ReportsInvalidValidate()
	{
		auto args = ValidArgs();
		args.push_back("--validate=link");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "invalid validate should fail");
		Expect(result.errors.size() == 1U, "invalid validate should produce one error");
		Expect(result.errors[0].option == "--validate", "invalid validate should identify option");
		Expect(result.errors[0].message == "--validate must be none, syntax, or compile.",
			   "invalid validate diagnostic should be deterministic");
	}

	void ReportsStrictBestEffortConflict()
	{
		auto args = ValidArgs();
		args.push_back("--strict");
		args.push_back("--best-effort");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "strict and best-effort together should fail");
		Expect(result.errors.size() == 1U, "mode conflict should produce one error");
		Expect(result.errors[0].code == mockfakegen::ConfigErrorCode::ConflictingOption,
			   "mode conflict should use conflicting option code");
	}

	void ReportsDuplicateOptions()
	{
		auto args = ValidArgs();
		args.push_back("--jobs");
		args.push_back("4");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "duplicate singleton option should fail");
		Expect(result.errors.size() == 1U, "duplicate option should produce one error");
		Expect(result.errors[0].code == mockfakegen::ConfigErrorCode::DuplicateOption,
			   "duplicate option should use duplicate option code");
		Expect(result.errors[0].option == "--jobs", "duplicate option should identify option");
		Expect(result.errors[0].message == "--jobs was provided more than once.",
			   "duplicate option diagnostic should be deterministic");
	}

	void ParsesGlobalMutexRegistryMode()
	{
		auto args = ValidArgs();
		args.push_back("--registry-mode=global-mutex");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "global-mutex registry mode should parse");
		Expect(result.config->registry_mode == mockfakegen::RegistryMode::GlobalMutex,
			   "registry mode should be global-mutex");
	}

	void ReportsDeferredOptions()
	{
		auto args = ValidArgs();
		args.push_back("--registry-mode=shared-owner");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "deferred registry mode should fail");
		Expect(result.errors.size() == 1U, "deferred option should produce one error");
		Expect(result.errors[0].code == mockfakegen::ConfigErrorCode::DeferredOption,
			   "deferred option should use deferred option code");
		Expect(result.errors[0].option == "--registry-mode",
			   "deferred option should identify option");
		Expect(result.errors[0].message ==
				   "--registry-mode is deferred: registry mode 'shared-owner' is deferred.",
			   "deferred option diagnostic should be deterministic");
	}

	void ReportsDeferredWholeOption()
	{
		auto args = ValidArgs();
		args.push_back("--config=config.yml");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "deferred config file option should fail");
		Expect(result.errors.size() == 1U, "deferred config file should produce one error");
		Expect(result.errors[0].code == mockfakegen::ConfigErrorCode::DeferredOption,
			   "deferred config file should use deferred code");
		Expect(result.errors[0].option == "--config", "deferred config should identify option");
	}

	void ReportsInputRootOutsideProjectRoot()
	{
		const std::vector<std::string> args{
			"mockfakegen",
			"--input-root",
			"/outside/include",
			"--output-dir",
			"/project/generated",
			"--build-path",
			"/project/build",
			"--project-root",
			"/project",
		};

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "input-root outside project-root should fail");
		Expect(result.errors.size() == 1U, "path relationship should produce one error");
		Expect(result.errors[0].code == mockfakegen::ConfigErrorCode::InvalidOptionValue,
			   "path relationship should use invalid option value code");
		Expect(result.errors[0].option == "--input-root",
			   "path relationship diagnostic should identify input-root");
		Expect(result.errors[0].message ==
				   "--input-root must be the same as or under --project-root.",
			   "path relationship diagnostic should be deterministic");
	}

	void HelpDoesNotRequirePaths()
	{
		const std::vector<std::string> args{"mockfakegen", "--help"};
		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.help_requested, "help should be requested");
		Expect(result.errors.empty(), "help should not require path options");
		Expect(!result.config.has_value(), "help should not produce runtime config");
		Expect(mockfakegen::BuildUsage("mockfakegen").find("--input-root <path>") !=
				   std::string::npos,
			   "usage should include required options");
		Expect(mockfakegen::BuildUsage("mockfakegen").find("--build-path <path>") !=
				   std::string::npos,
			   "usage should include build path option");
	}

	void RunCliReturnsDeterministicExitCodes()
	{
		const char* const help_args[] = {"mockfakegen", "--help"};
		std::ostringstream help_out;
		std::ostringstream help_err;
		Expect(mockfakegen::RunCli(2, help_args, help_out, help_err) == 0,
			   "help should return success");
		Expect(help_err.str().empty(), "help should not write stderr");

		const char* const invalid_args[] = {"mockfakegen", "--jobs", "0"};
		std::ostringstream invalid_out;
		std::ostringstream invalid_err;
		Expect(mockfakegen::RunCli(3, invalid_args, invalid_out, invalid_err) == 2,
			   "invalid config should return 2");
		Expect(invalid_out.str().empty(), "invalid config should not write stdout");
		Expect(invalid_err.str().find("--jobs must be a positive integer.") != std::string::npos,
			   "invalid config should print jobs diagnostic");
	}
} // namespace

int main()
{
	ParsesValidConfig();
	ParsesEmitAllMocksFalse();
	ParsesEmitManifestFalse();
	ParsesEmitCMakeFragmentFalse();
	ParsesFormatStyleGoogle();
	ParsesValidateNone();
	ReportsMissingRequiredOptions();
	ReportsInvalidJobs();
	ReportsInvalidEmitAllMocks();
	ReportsInvalidEmitCMakeFragment();
	ReportsInvalidFormatStyle();
	ReportsInvalidValidate();
	ReportsStrictBestEffortConflict();
	ReportsDuplicateOptions();
	ParsesGlobalMutexRegistryMode();
	ReportsDeferredOptions();
	ReportsDeferredWholeOption();
	ReportsInputRootOutsideProjectRoot();
	HelpDoesNotRequirePaths();
	RunCliReturnsDeterministicExitCodes();
	return 0;
}
