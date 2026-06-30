#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FormatStyle.h"
#include "ValidationMode.h"

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
		DuplicateOption,
		DeferredOption,
	};

	enum class AccessPolicy
	{
		Public,
	};

	enum class RegistryMode
	{
		ThreadLocal,
		GlobalMutex,
		SharedOwner,
	};

	enum class FallbackPolicy
	{
		Abort,
		DefaultReturn,
		Throw,
		CompileError,
	};

	enum class MockNamespaceMode
	{
		SameAsProduct,
	};

	enum class CollisionPolicy
	{
		QualifiedFilename,
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
		std::filesystem::path build_path;
		std::filesystem::path project_root;
		std::string standard = "c++23";
		std::string header_extension = ".h";
		AccessPolicy access = AccessPolicy::Public;
		RegistryMode registry_mode = RegistryMode::ThreadLocal;
		FallbackPolicy fallback_policy = FallbackPolicy::Abort;
		MockNamespaceMode mock_namespace_mode = MockNamespaceMode::SameAsProduct;
		CollisionPolicy collision_policy = CollisionPolicy::QualifiedFilename;
		bool dry_run = false;
		bool overwrite = false;
		bool strict = false;
		bool best_effort = true;
		bool include_struct = false;
		bool emit_all_mocks = true;
		bool emit_manifest = true;
		bool emit_cmake_fragment = true;
		bool fake_special_members = false;
		bool fake_static_data = false;
		bool interface_mock = false;
		FormatStyleKind format_style = FormatStyleKind::File;
		ValidationMode validate = ValidationMode::Compile;
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
