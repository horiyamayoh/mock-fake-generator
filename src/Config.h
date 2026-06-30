#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FormatStyle.h"

namespace mockfakegen
{
	enum class ConfigErrorCode
	{
		MissingRequiredOption,
		MissingOptionValue,
		InvalidOptionValue,
		UnknownOption,
		UnexpectedArgument,
		ConflictingOption,
	};

	struct ConfigError
	{
		ConfigErrorCode code;
		std::string option;
		std::string message;
	};

	struct Config
	{
		std::filesystem::path input_root;
		std::filesystem::path output_dir;
		std::filesystem::path project_root;
		bool dry_run = false;
		bool overwrite = false;
		bool strict = false;
		bool best_effort = true;
		bool emit_all_mocks = true;
		FormatStyleKind format_style = FormatStyleKind::File;
		int jobs = 1;
	};

	struct ConfigParseResult
	{
		std::optional<Config> config;
		std::vector<ConfigError> errors;
		bool help_requested = false;
		std::string program_name;

		[[nodiscard]] bool ok() const noexcept
		{
			return config.has_value() && errors.empty() && !help_requested;
		}
	};

	[[nodiscard]] ConfigParseResult ParseConfig(std::span<const std::string> arguments);
	[[nodiscard]] ConfigParseResult ParseConfigFromArgv(int argc, const char* const* argv);

	[[nodiscard]] std::string BuildUsage(std::string_view program_name);
	void PrintConfigErrors(std::ostream& out, std::span<const ConfigError> errors);

	int RunCli(int argc, const char* const* argv, std::ostream& out, std::ostream& err);
} // namespace mockfakegen
