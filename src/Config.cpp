#include "Config.h"

#include <algorithm>
#include <charconv>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "support/CliSupport.h"

namespace mockfakegen
{
	namespace
	{
		constexpr std::string_view kHelpOption = "--help";
		constexpr std::string_view kInputRootOption = "--input-root";
		constexpr std::string_view kOutputDirOption = "--output-dir";
		constexpr std::string_view kBuildPathOption = "--build-path";
		constexpr std::string_view kProjectRootOption = "--project-root";
		constexpr std::string_view kStdOption = "--std";
		constexpr std::string_view kConfigOption = "--config";
		constexpr std::string_view kHeaderExtensionOption = "--header-extension";
		constexpr std::string_view kHeaderFilterOption = "--header-filter";
		constexpr std::string_view kExcludeOption = "--exclude";
		constexpr std::string_view kClassFilterOption = "--class-filter";
		constexpr std::string_view kAccessOption = "--access";
		constexpr std::string_view kIncludeStructOption = "--include-struct";
		constexpr std::string_view kRegistryModeOption = "--registry-mode";
		constexpr std::string_view kFallbackPolicyOption = "--fallback-policy";
		constexpr std::string_view kMockNamespaceModeOption = "--mock-namespace-mode";
		constexpr std::string_view kCollisionPolicyOption = "--collision-policy";
		constexpr std::string_view kFakeSpecialMembersOption = "--fake-special-members";
		constexpr std::string_view kFakeStaticDataOption = "--fake-static-data";
		constexpr std::string_view kInterfaceMockOption = "--interface-mock";
		constexpr std::string_view kIncludeDirOption = "--include-dir";
		constexpr std::string_view kDefineOption = "--define";
		constexpr std::string_view kExtraArgOption = "--extra-arg";
		constexpr std::string_view kDryRunOption = "--dry-run";
		constexpr std::string_view kOverwriteOption = "--overwrite";
		constexpr std::string_view kStrictOption = "--strict";
		constexpr std::string_view kBestEffortOption = "--best-effort";
		constexpr std::string_view kEmitAllMocksOption = "--emit-all-mocks";
		constexpr std::string_view kEmitManifestOption = "--emit-manifest";
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

		[[nodiscard]] bool IsPresenceFlagOption(std::string_view option) noexcept
		{
			return option == kHelpOption || option == kDryRunOption || option == kOverwriteOption ||
				option == kStrictOption || option == kBestEffortOption;
		}

		[[nodiscard]] bool IsRepeatableOption(std::string_view option) noexcept
		{
			return option == kExcludeOption || option == kIncludeDirOption ||
				option == kDefineOption || option == kExtraArgOption;
		}

		[[nodiscard]] bool IsDeferredValueOption(std::string_view option) noexcept
		{
			return option == kConfigOption || option == kHeaderFilterOption ||
				option == kExcludeOption || option == kClassFilterOption ||
				option == kIncludeDirOption || option == kDefineOption || option == kExtraArgOption;
		}

		[[nodiscard]] bool IsKnownOption(std::string_view option) noexcept
		{
			return option == kHelpOption || option == kInputRootOption ||
				option == kOutputDirOption || option == kBuildPathOption ||
				option == kProjectRootOption || option == kStdOption || option == kConfigOption ||
				option == kHeaderExtensionOption || option == kHeaderFilterOption ||
				option == kExcludeOption || option == kClassFilterOption ||
				option == kAccessOption || option == kIncludeStructOption ||
				option == kRegistryModeOption || option == kFallbackPolicyOption ||
				option == kMockNamespaceModeOption || option == kCollisionPolicyOption ||
				option == kFakeSpecialMembersOption || option == kFakeStaticDataOption ||
				option == kInterfaceMockOption || option == kIncludeDirOption ||
				option == kDefineOption || option == kExtraArgOption || option == kDryRunOption ||
				option == kOverwriteOption || option == kStrictOption ||
				option == kBestEffortOption || option == kEmitAllMocksOption ||
				option == kEmitManifestOption || option == kEmitCMakeFragmentOption ||
				option == kFormatStyleOption || option == kValidateOption || option == kJobsOption;
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

		void AddDeferredError(std::vector<ConfigError>& errors,
							  std::string_view option,
							  std::string_view reason)
		{
			AddError(errors,
					 ConfigErrorCode::DeferredOption,
					 option,
					 std::string(option) + " is deferred: " + std::string(reason));
		}

		[[nodiscard]] bool MarkOptionSeen(std::vector<std::string>& seen_options,
										  std::vector<ConfigError>& errors,
										  std::string_view option)
		{
			if (IsRepeatableOption(option))
			{
				return true;
			}

			const auto already_seen = std::find(seen_options.begin(), seen_options.end(), option);
			if (already_seen != seen_options.end())
			{
				AddError(errors,
						 ConfigErrorCode::DuplicateOption,
						 option,
						 std::string(option) + " was provided more than once.");
				return false;
			}

			seen_options.emplace_back(option);
			return true;
		}

		[[nodiscard]] std::optional<std::string>
		ReadOptionValue(std::span<const std::string> arguments,
						std::size_t& index,
						std::string_view option,
						const std::optional<std::string>& inline_value,
						std::vector<ConfigError>& errors)
		{
			if (inline_value.has_value())
			{
				return inline_value;
			}

			const auto value_index = index + 1U;
			if (value_index >= arguments.size() || StartsWithOptionPrefix(arguments[value_index]))
			{
				AddError(errors,
						 ConfigErrorCode::MissingOptionValue,
						 option,
						 std::string(option) + " requires a value.");
				return std::nullopt;
			}

			index = value_index;
			return arguments[value_index];
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

		[[nodiscard]] std::optional<std::filesystem::path> NormalizePathValue(
			std::vector<ConfigError>& errors, std::string_view option, std::string_view value)
		{
			if (value.empty())
			{
				AddError(errors,
						 ConfigErrorCode::InvalidOptionValue,
						 option,
						 std::string(option) + " must not be empty.");
				return std::nullopt;
			}

			std::error_code absolute_error;
			const auto absolute =
				std::filesystem::absolute(std::filesystem::path(value), absolute_error);
			if (absolute_error)
			{
				AddError(errors,
						 ConfigErrorCode::InvalidOptionValue,
						 option,
						 "failed to normalize path for " + std::string(option) + ": " +
							 absolute_error.message());
				return std::nullopt;
			}

			auto normalized = absolute.lexically_normal();
			if (normalized.has_relative_path() && normalized.filename().empty())
			{
				normalized = normalized.parent_path();
			}

			return normalized;
		}

		[[nodiscard]] bool IsSameOrUnder(const std::filesystem::path& path,
										 const std::filesystem::path& directory)
		{
			if (directory.empty())
			{
				return false;
			}

			auto path_iterator = path.begin();
			const auto path_end = path.end();
			for (auto directory_iterator = directory.begin(); directory_iterator != directory.end();
				 ++directory_iterator)
			{
				if (path_iterator == path_end || *path_iterator != *directory_iterator)
				{
					return false;
				}
				++path_iterator;
			}

			return true;
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
		std::optional<std::filesystem::path> build_path;
		std::optional<std::filesystem::path> project_root;
		std::vector<std::string> seen_options;
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

			if (IsPresenceFlagOption(option))
			{
				if (inline_value.has_value())
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 option,
							 option + " does not accept a value.");
					continue;
				}

				if (!MarkOptionSeen(seen_options, result.errors, option))
				{
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

			const auto should_apply = MarkOptionSeen(seen_options, result.errors, option);
			const auto value =
				ReadOptionValue(arguments, index, option, inline_value, result.errors);
			if (!value.has_value() || !should_apply)
			{
				continue;
			}

			if (IsDeferredValueOption(option))
			{
				AddDeferredError(result.errors, option, "support is not implemented yet.");
			}
			else if (option == kInputRootOption)
			{
				input_root = NormalizePathValue(result.errors, option, *value);
			}
			else if (option == kOutputDirOption)
			{
				output_dir = NormalizePathValue(result.errors, option, *value);
			}
			else if (option == kBuildPathOption)
			{
				build_path = NormalizePathValue(result.errors, option, *value);
			}
			else if (option == kProjectRootOption)
			{
				project_root = NormalizePathValue(result.errors, option, *value);
			}
			else if (option == kStdOption)
			{
				if (*value != "c++23")
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kStdOption,
							 "--std must be c++23.");
					continue;
				}

				config.standard = *value;
			}
			else if (option == kHeaderExtensionOption)
			{
				if (*value != ".h")
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kHeaderExtensionOption,
							 "--header-extension must be .h.");
					continue;
				}

				config.header_extension = *value;
			}
			else if (option == kAccessOption)
			{
				if (*value == "public")
				{
					config.access = AccessPolicy::Public;
				}
				else if (*value == "protected" || *value == "private")
				{
					AddDeferredError(result.errors,
									 kAccessOption,
									 "protected/private member generation is deferred.");
				}
				else
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kAccessOption,
							 "--access must be public, protected, or private.");
				}
			}
			else if (option == kIncludeStructOption)
			{
				const auto parsed_include_struct = ParseBoolValue(*value);
				if (!parsed_include_struct.has_value())
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kIncludeStructOption,
							 "--include-struct must be true or false.");
					continue;
				}
				if (*parsed_include_struct)
				{
					AddDeferredError(result.errors,
									 kIncludeStructOption,
									 "struct declaration generation is deferred.");
					continue;
				}

				config.include_struct = false;
			}
			else if (option == kRegistryModeOption)
			{
				if (*value == "thread-local")
				{
					config.registry_mode = RegistryMode::ThreadLocal;
				}
				else if (*value == "global-mutex")
				{
					config.registry_mode = RegistryMode::GlobalMutex;
				}
				else if (*value == "shared-owner")
				{
					config.registry_mode = RegistryMode::SharedOwner;
				}
				else
				{
					AddError(
						result.errors,
						ConfigErrorCode::InvalidOptionValue,
						kRegistryModeOption,
						"--registry-mode must be thread-local, global-mutex, or shared-owner.");
				}
			}
			else if (option == kFallbackPolicyOption)
			{
				if (*value == "abort")
				{
					config.fallback_policy = FallbackPolicy::Abort;
				}
				else if (*value == "default-return" || *value == "throw" ||
						 *value == "compile-error")
				{
					AddDeferredError(result.errors,
									 kFallbackPolicyOption,
									 "fallback policy '" + *value + "' is deferred.");
				}
				else
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kFallbackPolicyOption,
							 "--fallback-policy must be abort, default-return, throw, or "
							 "compile-error.");
				}
			}
			else if (option == kMockNamespaceModeOption)
			{
				if (*value != "same-as-product")
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kMockNamespaceModeOption,
							 "--mock-namespace-mode must be same-as-product.");
					continue;
				}

				config.mock_namespace_mode = MockNamespaceMode::SameAsProduct;
			}
			else if (option == kCollisionPolicyOption)
			{
				if (*value != "qualified-filename")
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kCollisionPolicyOption,
							 "--collision-policy must be qualified-filename.");
					continue;
				}

				config.collision_policy = CollisionPolicy::QualifiedFilename;
			}
			else if (option == kFakeSpecialMembersOption)
			{
				const auto parsed = ParseBoolValue(*value);
				if (!parsed.has_value())
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kFakeSpecialMembersOption,
							 "--fake-special-members must be true or false.");
					continue;
				}
				if (*parsed)
				{
					config.fake_special_members = true;
				}
			}
			else if (option == kFakeStaticDataOption)
			{
				const auto parsed = ParseBoolValue(*value);
				if (!parsed.has_value())
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kFakeStaticDataOption,
							 "--fake-static-data must be true or false.");
					continue;
				}
				if (*parsed)
				{
					config.fake_static_data = true;
				}
			}
			else if (option == kInterfaceMockOption)
			{
				const auto parsed = ParseBoolValue(*value);
				if (!parsed.has_value())
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kInterfaceMockOption,
							 "--interface-mock must be true or false.");
					continue;
				}
				if (*parsed)
				{
					config.interface_mock = true;
				}
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
			else if (option == kEmitManifestOption)
			{
				const auto parsed_emit_manifest = ParseBoolValue(*value);
				if (!parsed_emit_manifest.has_value())
				{
					AddError(result.errors,
							 ConfigErrorCode::InvalidOptionValue,
							 kEmitManifestOption,
							 "--emit-manifest must be true or false.");
					continue;
				}

				config.emit_manifest = *parsed_emit_manifest;
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

		if (!build_path.has_value() && !result.help_requested)
		{
			AddError(result.errors,
					 ConfigErrorCode::MissingRequiredOption,
					 kBuildPathOption,
					 MissingRequiredMessage(kBuildPathOption));
		}

		if (!project_root.has_value() && !result.help_requested)
		{
			AddError(result.errors,
					 ConfigErrorCode::MissingRequiredOption,
					 kProjectRootOption,
					 MissingRequiredMessage(kProjectRootOption));
		}

		if (input_root.has_value() && project_root.has_value() &&
			!IsSameOrUnder(*input_root, *project_root))
		{
			AddError(result.errors,
					 ConfigErrorCode::InvalidOptionValue,
					 kInputRootOption,
					 "--input-root must be the same as or under --project-root.");
		}

		if (input_root.has_value())
		{
			config.input_root = *input_root;
		}
		if (output_dir.has_value())
		{
			config.output_dir = *output_dir;
		}
		if (build_path.has_value())
		{
			config.build_path = *build_path;
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
			" --input-root <path> --output-dir <path> --build-path <path> "
			"--project-root <path> [options]\n"
			"\n"
			"Required path options:\n"
			"  --input-root <path>     Root directory to scan for .h files.\n"
			"  --output-dir <path>     Directory where generated files will be written.\n"
			"  --build-path <path>     Directory containing compile_commands.json.\n"
			"  --project-root <path>   Base directory for project-relative paths.\n"
			"\n"
			"Options:\n"
			"  --help                  Show this help and exit.\n"
			"  --std <value>           c++23 only.\n"
			"  --header-extension <ext> .h only.\n"
			"  --access <policy>       public only; protected/private are deferred.\n"
			"  --registry-mode <mode>  thread-local, global-mutex, or shared-owner.\n"
			"  --fallback-policy <p>   abort; other policies are deferred.\n"
			"  --mock-namespace-mode <mode> same-as-product.\n"
			"  --collision-policy <policy> qualified-filename.\n"
			"  --fake-special-members <bool> Generate safe constructor/destructor fakes.\n"
			"  --fake-static-data <bool> Generate safe static data member definitions.\n"
			"  --interface-mock <bool> Generate inheritance-based interface mocks.\n"
			"  --dry-run               Resolve config without writing generated files.\n"
			"  --overwrite             Allow replacing existing generated files.\n"
			"  --strict                Fail when unsupported input is encountered.\n"
			"  --best-effort           Generate supported output and report unsupported input.\n"
			"  --emit-all-mocks <bool> Generate AllMocks.h when true.\n"
			"  --emit-manifest <bool>  Generate manifest.json when true.\n"
			"  --emit-cmake-fragment <bool> Generate CMakeLists.fragment.cmake when true.\n"
			"  --format-style <style>  file, llvm, google, or none.\n"
			"  --validate <mode>       none, syntax, or compile.\n"
			"  --jobs <N>              Positive worker count.\n"
			"\n"
			"Deferred options are recognized but fail with a diagnostic until their "
			"own component is implemented.\n";
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

} // namespace mockfakegen
