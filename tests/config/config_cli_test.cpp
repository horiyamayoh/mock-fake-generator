#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

	[[nodiscard]] bool Contains(std::string_view text, std::string_view token)
	{
		return text.find(token) != std::string_view::npos;
	}

	class TempTree
	{
	  public:
		TempTree()
			: root_(std::filesystem::temp_directory_path() /
					("mockfakegen_config_cli_test_" + std::to_string(UniqueSuffix())))
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
		Expect(stream.good(), "file should be readable");
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		return buffer.str();
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
		Expect(!result.config->header_filter.has_value(), "header filter should default unset");
		Expect(result.config->exclude_globs.empty(), "exclude globs should default empty");
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
		Expect(result.config->include_dirs.empty(), "include dirs should default empty");
		Expect(result.config->defines.empty(), "defines should default empty");
		Expect(result.config->extra_args.empty(), "extra args should default empty");
		Expect(result.config->path_maps.empty(), "path maps should default empty");
		Expect(result.config->format_style == mockfakegen::FormatStyleKind::File,
			   "format style should default to file");
		Expect(result.config->validate == mockfakegen::ValidationMode::Compile,
			   "validate should default to compile");
		Expect(result.config->validation_timeout == std::chrono::seconds(30),
			   "validation timeout should default to 30 seconds");
		Expect(!result.config->validation_keep_artifacts,
			   "validation artifacts should default to cleanup");
		Expect(result.config->validation_artifact_dir.empty(),
			   "validation artifact dir should default empty");
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

	void ParsesValidateLink()
	{
		auto args = ValidArgs();
		args.push_back("--validate=link");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "validate link config should parse");
		Expect(result.config->validate == mockfakegen::ValidationMode::Link,
			   "validate should parse link");
	}

	void ParsesValidationControls()
	{
		auto args = ValidArgs();
		args.push_back("--validation-timeout-ms=250");
		args.push_back("--validation-keep-artifacts");
		args.push_back("--validation-artifact-dir=validation-artifacts");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "validation controls should parse");
		Expect(result.config->validation_timeout == std::chrono::milliseconds(250),
			   "validation timeout should parse milliseconds");
		Expect(result.config->validation_keep_artifacts,
			   "validation keep artifacts flag should parse");
		Expect(result.config->validation_artifact_dir == ExpectedPath("validation-artifacts"),
			   "validation artifact dir should normalize");
	}

	void ParsesScannerFilters()
	{
		auto args = ValidArgs();
		args.push_back("--header-filter");
		args.push_back("^include/public/.*\\.h$");
		args.push_back("--exclude");
		args.push_back("include/generated/**");
		args.push_back("--exclude=include/internal/**");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "scanner filters should parse");
		Expect(result.config->header_filter == "^include/public/.*\\.h$",
			   "header filter should be stored");
		const std::vector<std::string> expected{
			"include/generated/**",
			"include/internal/**",
		};
		Expect(result.config->exclude_globs == expected, "exclude globs should preserve order");
	}

	void ParsesCompilerRescueArgs()
	{
		auto args = ValidArgs();
		args.push_back("--include-dir");
		args.push_back("sdk/include");
		args.push_back("--include-dir=generated/include");
		args.push_back("--define");
		args.push_back("FEATURE=1");
		args.push_back("--define=SECOND_FEATURE");
		args.push_back("--extra-arg");
		args.push_back("-Wno-unknown-warning-option");
		args.push_back("--extra-arg");
		args.push_back("--target=x86_64-linux-gnu");
		args.push_back("--path-map");
		args.push_back("/workspace=container-host");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "compiler rescue args should parse");
		const std::vector<std::filesystem::path> expected_include_dirs{
			ExpectedPath("sdk/include"),
			ExpectedPath("generated/include"),
		};
		const std::vector<std::string> expected_defines{"FEATURE=1", "SECOND_FEATURE"};
		const std::vector<std::string> expected_extra_args{
			"-Wno-unknown-warning-option",
			"--target=x86_64-linux-gnu",
		};
		Expect(result.config->include_dirs == expected_include_dirs,
			   "include dirs should normalize and preserve order");
		Expect(result.config->defines == expected_defines, "defines should preserve order");
		Expect(result.config->extra_args == expected_extra_args,
			   "extra args should accept option-looking separate values");
		Expect(result.config->path_maps.size() == 1U, "path map should parse");
		Expect(result.config->path_maps[0].from == std::filesystem::path("/workspace"),
			   "path map source should preserve container prefix");
		Expect(result.config->path_maps[0].to == ExpectedPath("container-host"),
			   "path map destination should normalize host path");
	}

	void RejectsInvalidHeaderFilterRegex()
	{
		auto args = ValidArgs();
		args.push_back("--header-filter");
		args.push_back("[");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "invalid header filter regex should fail");
		Expect(result.errors.size() == 1U, "invalid header filter should produce one error");
		Expect(result.errors[0].code == mockfakegen::ConfigErrorCode::InvalidOptionValue,
			   "invalid header filter should use invalid option code");
		Expect(result.errors[0].option == "--header-filter",
			   "invalid header filter should identify option");
	}

	void UsageMentionsEveryPublicOption()
	{
		const auto usage = mockfakegen::BuildUsage("mockfakegen");
		const std::vector<std::string_view> options = {
			"--help",
			"--input-root",
			"--output-dir",
			"--build-path",
			"--project-root",
			"--std",
			"--config",
			"--header-extension",
			"--header-filter",
			"--exclude",
			"--class-filter",
			"--access",
			"--include-struct",
			"--registry-mode",
			"--fallback-policy",
			"--mock-namespace-mode",
			"--collision-policy",
			"--fake-special-members",
			"--fake-static-data",
			"--interface-mock",
			"--include-dir",
			"--define",
			"--extra-arg",
			"--path-map",
			"--dry-run",
			"--overwrite",
			"--strict",
			"--best-effort",
			"--emit-all-mocks",
			"--emit-manifest",
			"--emit-cmake-fragment",
			"--format-style",
			"--validate",
			"--validation-timeout-ms",
			"--validation-keep-artifacts",
			"--validation-artifact-dir",
			"--jobs",
		};

		for (const auto option : options)
		{
			Expect(Contains(usage, option), "usage should mention every public option");
		}
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
		args.push_back("--validate=object");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "invalid validate should fail");
		Expect(result.errors.size() == 1U, "invalid validate should produce one error");
		Expect(result.errors[0].option == "--validate", "invalid validate should identify option");
		Expect(result.errors[0].message == "--validate must be none, syntax, compile, or link.",
			   "invalid validate diagnostic should be deterministic");
	}

	void ReportsInvalidValidationTimeout()
	{
		auto args = ValidArgs();
		args.push_back("--validation-timeout-ms=0");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "invalid validation timeout should fail");
		Expect(result.errors.size() == 1U, "invalid validation timeout should produce one error");
		Expect(result.errors[0].option == "--validation-timeout-ms",
			   "invalid validation timeout should identify option");
		Expect(result.errors[0].message == "--validation-timeout-ms must be a positive integer.",
			   "invalid validation timeout diagnostic should be deterministic");
	}

	void ReportsInvalidBoolSpelling()
	{
		auto args = ValidArgs();
		args.push_back("--emit-all-mocks=1");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "numeric bool spelling should fail");
		Expect(result.errors.size() == 1U, "invalid bool should produce one error");
		Expect(result.errors[0].code == mockfakegen::ConfigErrorCode::InvalidOptionValue,
			   "invalid bool should use invalid option value code");
		Expect(result.errors[0].option == "--emit-all-mocks",
			   "invalid bool should identify option");
		Expect(result.errors[0].message == "--emit-all-mocks must be true or false.",
			   "invalid bool diagnostic should be deterministic");
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

	void ReportsPresenceFlagValue()
	{
		auto args = ValidArgs();
		args.push_back("--dry-run=true");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "presence flag with value should fail");
		Expect(result.errors.size() == 1U, "presence flag value should produce one error");
		Expect(result.errors[0].code == mockfakegen::ConfigErrorCode::InvalidOptionValue,
			   "presence flag value should use invalid option value code");
		Expect(result.errors[0].option == "--dry-run",
			   "presence flag value should identify option");
		Expect(result.errors[0].message == "--dry-run does not accept a value.",
			   "presence flag value diagnostic should be deterministic");
	}

	void ReportsUnknownOption()
	{
		auto args = ValidArgs();
		args.push_back("--unknown-option");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "unknown option should fail");
		Expect(result.errors.size() == 1U, "unknown option should produce one error");
		Expect(result.errors[0].code == mockfakegen::ConfigErrorCode::UnknownOption,
			   "unknown option should use unknown option code");
		Expect(result.errors[0].option == "--unknown-option",
			   "unknown option should identify option");
		Expect(result.errors[0].message == "unknown option: --unknown-option",
			   "unknown option diagnostic should be deterministic");
	}

	void ReportsBareDoubleDashPositionals()
	{
		auto args = ValidArgs();
		args.push_back("--");
		args.push_back("--jobs");
		args.push_back("5");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "arguments after bare double dash should be positional errors");
		Expect(result.errors.size() == 2U, "bare double dash should expose both positionals");
		Expect(result.errors[0].code == mockfakegen::ConfigErrorCode::UnexpectedArgument,
			   "post-terminator option-looking token should be positional");
		Expect(result.errors[0].message == "unexpected positional argument: --jobs",
			   "post-terminator option diagnostic should be deterministic");
		Expect(result.errors[1].message == "unexpected positional argument: 5",
			   "post-terminator value diagnostic should be deterministic");
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

	void ParsesSharedOwnerRegistryMode()
	{
		auto args = ValidArgs();
		args.push_back("--registry-mode=shared-owner");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "shared-owner registry mode should parse");
		Expect(result.config->registry_mode == mockfakegen::RegistryMode::SharedOwner,
			   "registry mode should be shared-owner");
	}

	void ParsesFakeSpecialMembersTrue()
	{
		auto args = ValidArgs();
		args.push_back("--fake-special-members=true");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "fake-special-members true should parse");
		Expect(result.config->fake_special_members, "fake-special-members should parse true");
	}

	void ParsesFakeStaticDataTrue()
	{
		auto args = ValidArgs();
		args.push_back("--fake-static-data=true");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "fake-static-data true should parse");
		Expect(result.config->fake_static_data, "fake-static-data should parse true");
	}

	void ParsesInterfaceMockTrue()
	{
		auto args = ValidArgs();
		args.push_back("--interface-mock=true");

		const auto result = mockfakegen::ParseConfig(args);

		Expect(result.ok(), "interface-mock true should parse");
		Expect(result.config->interface_mock, "interface-mock should parse true");
	}

	void ParsesFallbackPolicies()
	{
		auto default_return_args = ValidArgs();
		default_return_args.push_back("--fallback-policy=default-return");
		const auto default_return_result = mockfakegen::ParseConfig(default_return_args);
		Expect(default_return_result.ok(), "default-return fallback policy should parse");
		Expect(default_return_result.config->fallback_policy ==
				   mockfakegen::FallbackPolicy::DefaultReturn,
			   "default-return fallback policy should be selected");

		auto throw_args = ValidArgs();
		throw_args.push_back("--fallback-policy=throw");
		const auto throw_result = mockfakegen::ParseConfig(throw_args);
		Expect(throw_result.ok(), "throw fallback policy should parse");
		Expect(throw_result.config->fallback_policy == mockfakegen::FallbackPolicy::Throw,
			   "throw fallback policy should be selected");
	}

	void ReportsRemovedCompileErrorFallbackPolicy()
	{
		auto args = ValidArgs();
		args.push_back("--fallback-policy=compile-error");

		const auto result = mockfakegen::ParseConfig(args);
		Expect(!result.ok(), "removed compile-error fallback policy should fail");
		Expect(result.errors.size() == 1U, "removed fallback policy should produce one error");
		Expect(result.errors[0].code == mockfakegen::ConfigErrorCode::InvalidOptionValue,
			   "removed fallback policy should use invalid value code");
		Expect(result.errors[0].option == "--fallback-policy",
			   "removed fallback policy should identify option");
		Expect(result.errors[0].message ==
				   "--fallback-policy must be abort, default-return, or throw.",
			   "removed fallback policy diagnostic should be deterministic");
	}

	void ReportsDeferredDesignOptions()
	{
		struct Case
		{
			const char* option;
			const char* value;
		};
		const std::vector<Case> cases = {
			{"--class-filter", "Hoge"},
			{"--include-struct", "true"},
			{"--access", "private"},
		};

		for (const auto& item : cases)
		{
			auto args = ValidArgs();
			args.push_back(item.option);
			args.push_back(item.value);

			const auto result = mockfakegen::ParseConfig(args);

			Expect(!result.ok(), "deferred design option should fail");
			Expect(result.errors.size() == 1U, "deferred design option should produce one error");
			Expect(result.errors[0].code == mockfakegen::ConfigErrorCode::DeferredOption,
				   "deferred design option should use deferred code");
			Expect(result.errors[0].option == item.option,
				   "deferred design option should identify option");
			Expect(Contains(result.errors[0].message, "is deferred"),
				   "deferred design option should say deferred");
		}
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

	void ReportsSymlinkedInputRootEscapingProjectRoot()
	{
		TempTree tree;
		const auto project_root = tree.root() / "project";
		const auto outside_root = tree.root() / "outside";
		const auto input_root = project_root / "include";
		std::filesystem::create_directories(project_root);
		std::filesystem::create_directories(outside_root);
		std::error_code symlink_error;
		std::filesystem::create_directory_symlink(outside_root, input_root, symlink_error);
		if (symlink_error)
		{
			return;
		}

		const std::vector<std::string> args{
			"mockfakegen",
			"--input-root",
			input_root.string(),
			"--output-dir",
			(project_root / "generated").string(),
			"--build-path",
			(project_root / "build").string(),
			"--project-root",
			project_root.string(),
		};

		const auto result = mockfakegen::ParseConfig(args);

		Expect(!result.ok(), "symlinked input-root escaping project-root should fail");
		Expect(result.errors.size() == 1U, "canonical path relationship should produce one error");
		Expect(result.errors[0].code == mockfakegen::ConfigErrorCode::InvalidOptionValue,
			   "canonical path relationship should use invalid option value code");
		Expect(result.errors[0].option == "--input-root",
			   "canonical path relationship should identify input-root");
		Expect(result.errors[0].message ==
				   "--input-root must resolve to the same as or under --project-root.",
			   "canonical path relationship diagnostic should be deterministic");
	}

	void RunCliRejectsSymlinkedInputRootEscapeBeforeScanning()
	{
		TempTree tree;
		const auto project_root = tree.root() / "project";
		const auto outside_root = tree.root() / "outside";
		const auto input_root = project_root / "include";
		const auto output_dir = project_root / "generated";
		const auto build_path = project_root / "build";
		std::filesystem::create_directories(project_root);
		std::filesystem::create_directories(outside_root);
		std::filesystem::create_directories(build_path);
		std::error_code symlink_error;
		std::filesystem::create_directory_symlink(outside_root, input_root, symlink_error);
		if (symlink_error)
		{
			return;
		}

		const std::vector<std::string> args{
			"mockfakegen",
			"--input-root",
			input_root.string(),
			"--output-dir",
			output_dir.string(),
			"--build-path",
			build_path.string(),
			"--project-root",
			project_root.string(),
			"--validate",
			"none",
			"--format-style",
			"none",
		};
		std::vector<const char*> argv;
		argv.reserve(args.size());
		for (const auto& arg : args)
		{
			argv.push_back(arg.c_str());
		}
		std::ostringstream out;
		std::ostringstream err;

		const auto exit_code =
			mockfakegen::RunCli(static_cast<int>(argv.size()), argv.data(), out, err);

		Expect(exit_code == 2, "symlinked input-root escape should fail at config phase");
		Expect(Contains(err.str(),
						"--input-root must resolve to the same as or under --project-root."),
			   "canonical input-root error should be printed");
		Expect(!std::filesystem::exists(output_dir / "MockEscaped.h"),
			   "escaped header should not be generated");
		Expect(std::filesystem::exists(output_dir / "manifest.json"),
			   "config error manifest should be written");
		const auto manifest = ReadText(output_dir / "manifest.json");
		Expect(Contains(manifest, "\"component\": \"config\""),
			   "manifest should record config failure");
		Expect(!Contains(manifest, "\"component\": \"scanner\""),
			   "manifest should not include scanner diagnostics after config failure");
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

	void RunCliHelpWithErrorsPrintsErrorsAndUsage()
	{
		const char* const args[] = {"mockfakegen", "--help", "--jobs", "0"};
		std::ostringstream out;
		std::ostringstream err;

		const auto exit_code = mockfakegen::RunCli(4, args, out, err);

		Expect(exit_code == 2, "help with invalid option should return config failure");
		Expect(out.str().empty(), "help with errors should not write usage to stdout");
		Expect(Contains(err.str(), "--jobs must be a positive integer."),
			   "help with errors should print diagnostic");
		Expect(Contains(err.str(), "Usage:"), "help with errors should print usage to stderr");
	}

	void RunCliConfigErrorsEmitDiagnosticArtifacts()
	{
		TempTree tree;
		const auto project_root = tree.root() / "product";
		const auto input_root = project_root / "include";
		const auto output_dir = tree.root() / "generated";
		const auto build_path = tree.root() / "build";
		std::filesystem::create_directories(input_root);
		std::filesystem::create_directories(build_path);
		const std::vector<std::string> args = {
			"mockfakegen",
			"--input-root",
			input_root.string(),
			"--output-dir",
			output_dir.string(),
			"--build-path",
			build_path.string(),
			"--project-root",
			project_root.string(),
			"--config",
			"mockfakegen.yml",
		};
		std::vector<const char*> argv;
		argv.reserve(args.size());
		for (const auto& arg : args)
		{
			argv.push_back(arg.c_str());
		}
		std::ostringstream out;
		std::ostringstream err;

		const auto exit_code =
			mockfakegen::RunCli(static_cast<int>(argv.size()), argv.data(), out, err);

		Expect(exit_code == 2, "deferred config option should fail at config phase");
		Expect(Contains(err.str(), "--config is deferred"),
			   "deferred config option should be printed");
		Expect(std::filesystem::exists(output_dir / "manifest.json"),
			   "config error manifest should be written");
		Expect(std::filesystem::exists(output_dir / "generation_report.md"),
			   "config error report should be written");
		const auto manifest = ReadText(output_dir / "manifest.json");
		const auto report = ReadText(output_dir / "generation_report.md");
		Expect(Contains(manifest, "\"component\": \"config\""),
			   "manifest should include config diagnostic component");
		Expect(Contains(manifest, "\"code\": \"deferred_option\""),
			   "manifest should include stable deferred option code");
		Expect(Contains(report, "deferred_option"),
			   "report should include stable deferred option code");
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
	ParsesValidateLink();
	ParsesValidationControls();
	ParsesScannerFilters();
	ParsesCompilerRescueArgs();
	RejectsInvalidHeaderFilterRegex();
	UsageMentionsEveryPublicOption();
	ReportsMissingRequiredOptions();
	ReportsInvalidJobs();
	ReportsInvalidEmitAllMocks();
	ReportsInvalidEmitCMakeFragment();
	ReportsInvalidFormatStyle();
	ReportsInvalidValidate();
	ReportsInvalidValidationTimeout();
	ReportsInvalidBoolSpelling();
	ReportsStrictBestEffortConflict();
	ReportsDuplicateOptions();
	ReportsPresenceFlagValue();
	ReportsUnknownOption();
	ReportsBareDoubleDashPositionals();
	ParsesGlobalMutexRegistryMode();
	ParsesSharedOwnerRegistryMode();
	ParsesFakeSpecialMembersTrue();
	ParsesFakeStaticDataTrue();
	ParsesInterfaceMockTrue();
	ParsesFallbackPolicies();
	ReportsRemovedCompileErrorFallbackPolicy();
	ReportsDeferredDesignOptions();
	ReportsDeferredWholeOption();
	ReportsInputRootOutsideProjectRoot();
	ReportsSymlinkedInputRootEscapingProjectRoot();
	RunCliRejectsSymlinkedInputRootEscapeBeforeScanning();
	HelpDoesNotRequirePaths();
	RunCliHelpWithErrorsPrintsErrorsAndUsage();
	RunCliConfigErrorsEmitDiagnosticArtifacts();
	RunCliReturnsDeterministicExitCodes();
	return 0;
}
