#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
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

	[[nodiscard]] std::vector<std::string> ValidArgs()
	{
		return {
			"mockfakegen",
			"--input-root",
			"include",
			"--output-dir=generated",
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
		Expect(result.config->input_root == "include", "input root should be resolved");
		Expect(result.config->output_dir == "generated", "output dir should be resolved");
		Expect(result.config->project_root == ".", "project root should be resolved");
		Expect(result.config->dry_run, "dry-run should be true");
		Expect(!result.config->overwrite, "overwrite should default false");
		Expect(!result.config->strict, "strict should default false");
		Expect(result.config->best_effort, "best-effort should default true");
		Expect(result.config->emit_all_mocks, "emit-all-mocks should default true");
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

	void ReportsMissingRequiredOptions()
	{
		const std::vector<std::string> args{"mockfakegen", "--dry-run"};
		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "missing required options should fail");
		Expect(result.errors.size() == 3U, "three required path options should be reported");
		Expect(result.errors[0].option == "--input-root",
			   "input-root should be first missing option");
		Expect(result.errors[1].option == "--output-dir",
			   "output-dir should be second missing option");
		Expect(result.errors[2].option == "--project-root",
			   "project-root should be third missing option");
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
	ReportsMissingRequiredOptions();
	ReportsInvalidJobs();
	ReportsInvalidEmitAllMocks();
	ReportsStrictBestEffortConflict();
	HelpDoesNotRequirePaths();
	RunCliReturnsDeterministicExitCodes();
	return 0;
}
