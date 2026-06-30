#include "Config.h"

#include <algorithm>
#include <charconv>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

#include "support/CliSupport.h"

namespace mockfakegen
{
	namespace
	{
		constexpr std::string_view kHelpOption = "--help";
		constexpr std::string_view kInputRootOption = "--input-root";
		constexpr std::string_view kOutputDirOption = "--output-dir";
		constexpr std::string_view kProjectRootOption = "--project-root";
		constexpr std::string_view kDryRunOption = "--dry-run";
		constexpr std::string_view kOverwriteOption = "--overwrite";
		constexpr std::string_view kStrictOption = "--strict";
		constexpr std::string_view kBestEffortOption = "--best-effort";
		constexpr std::string_view kEmitAllMocksOption = "--emit-all-mocks";
		constexpr std::string_view kEmitCMakeFragmentOption = "--emit-cmake-fragment";
		constexpr std::string_view kFormatStyleOption = "--format-style";
		constexpr std::string_view kValidateOption = "--validate";
		constexpr std::string_view kJobsOption = "--jobs";

		[[nodiscard]] int DefaultJobs() noexcept
		{
			const auto detected = std::thread::hardware_concurrency();
			if (detected == 0U)
			{
				return 1;
			}

			constexpr auto kMaxReasonableJobs = 1024U;
			const auto clamped = std::min(detected, kMaxReasonableJobs);
			return static_cast<int>(clamped);
		}

		[[nodiscard]] bool StartsWithOptionPrefix(std::string_view value) noexcept
		{
			return value.starts_with("--");
		}

		[[nodiscard]] std::string OptionWithInlineValueName(std::string_view argument)
		{
			const auto equals = argument.find('=');
			if (equals == std::string_view::npos)
			{
				return std::string(argument);
			}

			return std::string(argument.substr(0U, equals));
		}

		[[nodiscard]] bool IsKnownOption(std::string_view option) noexcept
		{
			return option == kHelpOption || option == kInputRootOption ||
				option == kOutputDirOption || option == kProjectRootOption ||
				option == kDryRunOption || option == kOverwriteOption || option == kStrictOption ||
				option == kBestEffortOption || option == kEmitAllMocksOption ||
				option == kEmitCMakeFragmentOption || option == kFormatStyleOption ||
				option == kValidateOption || option == kJobsOption;
		}

		[[nodiscard]] bool IsFlagOption(std::string_view option) noexcept
		{
			return option == kHelpOption || option == kDryRunOption || option == kOverwriteOption ||
				option == kStrictOption || option == kBestEffortOption;
		}

		[[nodiscard]] std::optional<std::string> InlineValue(std::string_view argument)
		{
			const auto equals = argument.find('=');
			if (equals == std::string_view::npos)
			{
				return std::nullopt;
			}

			return std::string(argument.substr(equals + 1U));
		}

		[[nodiscard]] std::string MissingRequiredMessage(std::string_view option)
		{
			return std::string(option) + " is required.";
		}

		void AddError(std::vector<ConfigError>& errors,
					  ConfigErrorCode code,
					  std::string_view option,
					  std::string message)
		{
			errors.push_back(ConfigError{code, std::string(option), std::move(message)});
		}

		[[nodiscard]] std::optional<int> ParseJobsValue(std::string_view text)
		{
			int value = 0;
			const auto* const begin = text.data();
			const auto* const end = begin + text.size();
			const auto [ptr, error] = std::from_chars(begin, end, value);
			if (error != std::errc{} || ptr != end || value <= 0)
			{
				return std::nullopt;
			}

			return value;
		}

		[[nodiscard]] std::optional<bool> ParseBoolValue(std::string_view text)
		{
			if (text == "true")
			{
				return true;
			}
			if (text == "false")
			{
				return false;
			}
			return std::nullopt;
		}

		[[nodiscard]] std::filesystem::path NormalizePath(std::string value)
		{
			return std::filesystem::path(std::move(value)).lexically_normal();
		}

		[[nodiscard]] std::string ProgramNameOrDefault(std::string_view program_name)
		{
			if (program_name.empty())
			{
				return "mockfakegen";
			}

			return std::string(program_name);
		}
	} // namespace

	ConfigParseResult ParseConfig(std::span<const std::string> arguments)
	{
		ConfigParseResult result;
		result.program_name = arguments.empty() ? std::string("mockfakegen")
												: ProgramNameOrDefault(arguments.front());

		Config config;
		config.jobs = DefaultJobs();

		std::optional<std::filesystem::path> input_root;
		std::optional<std::filesystem::path> output_dir;
		std::optional<std::filesystem::path> project_root;
		bool strict_seen = false;
		bool best_effort_seen = false;
		bool option_scanning_enabled = true;

		for (std::size_t index = 1U; index < arguments.size(); ++index)
		{
			const std::string_view argument = arguments[index];

			if (option_scanning_enabled && argument == "--")
			{
				option_scanning_enabled = false;
				continue;
			}

			if (!option_scanning_enabled || !StartsWithOptionPrefix(argument))
			{
				AddError(result.errors,
						 ConfigErrorCode::UnexpectedArgument,
						 {},
						 "unexpected positional argument: " + std::string(argument));
				continue;
			}

			const auto option = OptionWithInlineValueName(argument);
			const auto inline_value = InlineValue(argument);

			if (!IsKnownOption(option))
			{
				AddError(result.errors,
						 ConfigErrorCode::UnknownOption,
						 option,
						 "unknown option: " + option);
				continue;
			}

			if (IsFlagOption(option))
			{
				if (inline_value.has_value())
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 option,
							 option + " does not accept a value.");
					continue;
				}

				if (option == kHelpOption)
				{
					result.help_requested = true;
				}
				else if (option == kDryRunOption)
				{
					config.dry_run = true;
				}
				else if (option == kOverwriteOption)
				{
					config.overwrite = true;
				}
				else if (option == kStrictOption)
				{
					strict_seen = true;
					config.strict = true;
					config.best_effort = false;
				}
				else if (option == kBestEffortOption)
				{
					best_effort_seen = true;
					config.best_effort = true;
					config.strict = false;
				}

				continue;
			}

			std::optional<std::string> value = inline_value;
			if (!value.has_value())
			{
				const auto value_index = index + 1U;
				if (value_index >= arguments.size() ||
					StartsWithOptionPrefix(arguments[value_index]))
				{
					AddError(result.errors,
							 ConfigErrorCode::MissingOptionValue,
							 option,
							 option + " requires a value.");
					continue;
				}

				value = arguments[value_index];
				index = value_index;
			}

			if (option == kInputRootOption)
			{
				input_root = NormalizePath(*value);
			}
			else if (option == kOutputDirOption)
			{
				output_dir = NormalizePath(*value);
			}
			else if (option == kProjectRootOption)
			{
				project_root = NormalizePath(*value);
			}
			else if (option == kJobsOption)
			{
				const auto parsed_jobs = ParseJobsValue(*value);
				if (!parsed_jobs.has_value())
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kJobsOption,
							 "--jobs must be a positive integer.");
					continue;
				}

				config.jobs = *parsed_jobs;
			}
			else if (option == kEmitAllMocksOption)
			{
				const auto parsed_emit_all_mocks = ParseBoolValue(*value);
				if (!parsed_emit_all_mocks.has_value())
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kEmitAllMocksOption,
							 "--emit-all-mocks must be true or false.");
					continue;
				}

				config.emit_all_mocks = *parsed_emit_all_mocks;
			}
			else if (option == kEmitCMakeFragmentOption)
			{
				const auto parsed_emit_cmake_fragment = ParseBoolValue(*value);
				if (!parsed_emit_cmake_fragment.has_value())
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kEmitCMakeFragmentOption,
							 "--emit-cmake-fragment must be true or false.");
					continue;
				}

				config.emit_cmake_fragment = *parsed_emit_cmake_fragment;
			}
			else if (option == kFormatStyleOption)
			{
				const auto parsed_format_style = ParseFormatStyleKind(*value);
				if (!parsed_format_style.has_value())
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kFormatStyleOption,
							 "--format-style must be file, llvm, google, or none.");
					continue;
				}

				config.format_style = *parsed_format_style;
			}
			else if (option == kValidateOption)
			{
				const auto parsed_validate = ParseValidationMode(*value);
				if (!parsed_validate.has_value())
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kValidateOption,
							 "--validate must be none, syntax, or compile.");
					continue;
				}

				config.validate = *parsed_validate;
			}
		}

		if (strict_seen && best_effort_seen)
		{
			AddError(result.errors,
					 ConfigErrorCode::ConflictingOption,
					 kStrictOption,
					 "--strict and --best-effort are mutually exclusive.");
		}

		if (!input_root.has_value() && !result.help_requested)
		{
			AddError(result.errors,
					 ConfigErrorCode::MissingRequiredOption,
					 kInputRootOption,
					 MissingRequiredMessage(kInputRootOption));
		}

		if (!output_dir.has_value() && !result.help_requested)
		{
			AddError(result.errors,
					 ConfigErrorCode::MissingRequiredOption,
					 kOutputDirOption,
					 MissingRequiredMessage(kOutputDirOption));
		}

		if (!project_root.has_value() && !result.help_requested)
		{
			AddError(result.errors,
					 ConfigErrorCode::MissingRequiredOption,
					 kProjectRootOption,
					 MissingRequiredMessage(kProjectRootOption));
		}

		if (input_root.has_value())
		{
			config.input_root = *input_root;
		}
		if (output_dir.has_value())
		{
			config.output_dir = *output_dir;
		}
		if (project_root.has_value())
		{
			config.project_root = *project_root;
		}

		if (result.errors.empty() && !result.help_requested)
		{
			result.config = std::move(config);
		}

		return result;
	}

	ConfigParseResult ParseConfigFromArgv(int argc, const char* const* argv)
	{
		const auto arguments = support::CopyArgv(argc, argv);
		return ParseConfig(arguments);
	}

	std::string BuildUsage(std::string_view program_name)
	{
		const auto display_name = ProgramNameOrDefault(program_name);
		return "Usage:\n"
			   "  " +
			display_name +
			" --input-root <path> --output-dir <path> --project-root <path> [options]\n"
			"\n"
			"Options:\n"
			"  --help                 Show this help and exit.\n"
			"  --input-root <path>    Root directory to scan for .h files.\n"
			"  --output-dir <path>    Directory where generated files will be written.\n"
			"  --project-root <path>  Base directory for project-relative paths.\n"
			"  --dry-run              Resolve config without writing generated files.\n"
			"  --overwrite            Allow replacing existing generated files.\n"
			"  --strict               Fail when unsupported input is encountered.\n"
			"  --best-effort          Generate supported output and report unsupported input.\n"
			"  --emit-all-mocks <bool> Generate AllMocks.h when true.\n"
			"  --emit-cmake-fragment <bool> Generate CMakeLists.fragment.cmake when true.\n"
			"  --format-style <style> file, llvm, google, or none.\n"
			"  --validate <mode>      none, syntax, or compile.\n"
			"  --jobs <N>             Positive worker count.\n";
	}

	void PrintConfigErrors(std::ostream& out, std::span<const ConfigError> errors)
	{
		for (const auto& error : errors)
		{
			out << "error";
			if (!error.option.empty())
			{
				out << " [" << error.option << ']';
			}
			out << ": " << error.message << '\n';
		}
	}

	int RunCli(int argc, const char* const* argv, std::ostream& out, std::ostream& err)
	{
		const auto result = ParseConfigFromArgv(argc, argv);
		if (result.help_requested)
		{
			out << BuildUsage(result.program_name);
			return result.errors.empty() ? 0 : 2;
		}

		if (!result.errors.empty())
		{
			PrintConfigErrors(err, result.errors);
			err << '\n' << BuildUsage(result.program_name);
			return 2;
		}

		out << "mockfakegen: configuration resolved\n";
		return 0;
	}
} // namespace mockfakegen
