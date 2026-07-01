#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iosfwd>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "CompilationResolver.h"
#include "Config.h"
#include "HeaderScanner.h"
#include "diagnostics/RunDiagnostic.h"
#include "generator/CodeGenerator.h"
#include "generator/GeneratedFormatter.h"
#include "model/GeneratedFile.h"
#include "model/ProjectModel.h"
#include "output/OutputWriter.h"
#include "validation/GeneratedCompileValidator.h"
#include "validation/GenerationPolicy.h"

namespace mockfakegen
{
	namespace
	{
		[[nodiscard]] std::string_view ToString(OutputWriteStatus status) noexcept
		{
			switch (status)
			{
				case OutputWriteStatus::Planned:
					return "planned";
				case OutputWriteStatus::Written:
					return "written";
				case OutputWriteStatus::Unchanged:
					return "unchanged";
				case OutputWriteStatus::SkippedExisting:
					return "skipped-existing";
				case OutputWriteStatus::Failed:
					return "failed";
			}

			return "unknown";
		}

		[[nodiscard]] std::string_view ToString(GenerationPolicyDiagnosticKind kind) noexcept
		{
			switch (kind)
			{
				case GenerationPolicyDiagnosticKind::ParseFailure:
					return "parse_failure";
				case GenerationPolicyDiagnosticKind::UnsupportedItem:
					return "unsupported_item";
				case GenerationPolicyDiagnosticKind::WriteFailure:
					return "write_failure";
				case GenerationPolicyDiagnosticKind::FormatFailure:
					return "format_failure";
				case GenerationPolicyDiagnosticKind::KetContamination:
					return "ket_contamination";
				case GenerationPolicyDiagnosticKind::ValidationFailure:
					return "validation_failure";
				case GenerationPolicyDiagnosticKind::FallbackIncompatibility:
					return "fallback_incompatibility";
				case GenerationPolicyDiagnosticKind::LinkReadinessFailure:
					return "link_readiness_failure";
			}

			return "unknown";
		}

		[[nodiscard]] bool IsError(DiagnosticSeverity severity) noexcept
		{
			return severity == DiagnosticSeverity::Error;
		}

		[[nodiscard]] bool IsPublishableGeneratedKind(GeneratedFileKind kind) noexcept
		{
			switch (kind)
			{
				case GeneratedFileKind::RuntimeHeader:
				case GeneratedFileKind::MockHeader:
				case GeneratedFileKind::FakeSource:
				case GeneratedFileKind::AllMocksHeader:
				case GeneratedFileKind::CMakeFragment:
					return true;
				case GeneratedFileKind::Manifest:
				case GeneratedFileKind::Report:
					return false;
			}

			return false;
		}

		[[nodiscard]] std::vector<HeaderModel>
		ToHeaderModels(std::span<const HeaderCandidate> candidates)
		{
			std::vector<HeaderModel> headers;
			headers.reserve(candidates.size());
			for (const auto& candidate : candidates)
			{
				headers.push_back(HeaderModel{
					.absolute_path = candidate.absolute_path,
					.project_relative_path = candidate.project_relative_path,
					.include_spelling = candidate.include_spelling,
				});
			}
			return headers;
		}

		[[nodiscard]] std::vector<std::filesystem::path> SplitPathList(std::string_view text)
		{
			std::vector<std::filesystem::path> paths;
			std::size_t begin = 0U;
			while (begin <= text.size())
			{
				const auto pipe = text.find('|', begin);
				const auto colon = text.find(':', begin);
				const auto end = std::min(pipe == std::string_view::npos ? text.size() : pipe,
										  colon == std::string_view::npos ? text.size() : colon);
				const auto part = text.substr(begin, end - begin);
				if (!part.empty())
				{
					paths.emplace_back(part);
				}
				if (end == text.size())
				{
					break;
				}
				begin = end + 1U;
			}
			return paths;
		}

		void AppendUniquePath(std::vector<std::filesystem::path>& paths, std::filesystem::path path)
		{
			if (path.empty())
			{
				return;
			}
			path = path.lexically_normal();
			const auto text = path.generic_string();
			const auto exists = std::any_of(paths.begin(),
											paths.end(),
											[&text](const auto& existing)
											{
												return existing.generic_string() == text;
											});
			if (!exists)
			{
				paths.push_back(std::move(path));
			}
		}

		[[nodiscard]] std::vector<std::filesystem::path>
		ValidationIncludeDirs(const Config& config, std::span<const HeaderModel> headers)
		{
			std::vector<std::filesystem::path> include_dirs;
			AppendUniquePath(include_dirs, config.input_root);
			AppendUniquePath(include_dirs, config.project_root);
			for (const auto& header : headers)
			{
				AppendUniquePath(include_dirs, header.absolute_path.parent_path());
			}

			if (const char* const env = std::getenv("MOCKFAKEGEN_GMOCK_INCLUDE_DIRS");
				env != nullptr)
			{
				for (auto path : SplitPathList(env))
				{
					AppendUniquePath(include_dirs, std::move(path));
				}
			}
			return include_dirs;
		}

		[[nodiscard]] std::filesystem::path ValidationCompiler()
		{
			if (const char* const env = std::getenv("MOCKFAKEGEN_CXX_COMPILER"); env != nullptr)
			{
				return env;
			}
			return "c++";
		}

		[[nodiscard]] std::vector<std::filesystem::path> ValidationLinkFiles()
		{
			std::vector<std::filesystem::path> link_files;
			if (const char* const env = std::getenv("MOCKFAKEGEN_GMOCK_LINK_FILES"); env != nullptr)
			{
				for (auto path : SplitPathList(env))
				{
					AppendUniquePath(link_files, std::move(path));
				}
			}
			return link_files;
		}

		[[nodiscard]] RunDiagnostic ToRunDiagnostic(const ConfigError& diagnostic)
		{
			RunDiagnostic result;
			result.severity = DiagnosticSeverity::Error;
			result.component = "config";
			result.code = std::string(ConfigErrorCodeName(diagnostic.code));
			result.kind = diagnostic.option;
			result.message = diagnostic.message;
			result.suggested_action = "fix the command line option and rerun mockfakegen";
			return result;
		}

		[[nodiscard]] RunDiagnostic ToRunDiagnostic(const HeaderScanDiagnostic& diagnostic)
		{
			RunDiagnostic result;
			switch (diagnostic.severity)
			{
				case HeaderScanDiagnosticSeverity::Info:
					result.severity = DiagnosticSeverity::Info;
					break;
				case HeaderScanDiagnosticSeverity::Warning:
					result.severity = DiagnosticSeverity::Warning;
					break;
				case HeaderScanDiagnosticSeverity::Error:
					result.severity = DiagnosticSeverity::Error;
					break;
			}
			result.component = "scanner";
			switch (diagnostic.code)
			{
				case HeaderScanDiagnosticCode::InputRootDoesNotExist:
					result.code = "scanner_input_root_missing";
					result.kind = "input_root_missing";
					break;
				case HeaderScanDiagnosticCode::InputRootIsNotDirectory:
					result.code = "scanner_input_root_not_directory";
					result.kind = "input_root_not_directory";
					break;
				case HeaderScanDiagnosticCode::FilesystemError:
					result.code = "scanner_filesystem_error";
					result.kind = "filesystem_error";
					break;
				case HeaderScanDiagnosticCode::InvalidHeaderFilter:
					result.code = "scanner_invalid_header_filter";
					result.kind = "invalid_header_filter";
					break;
				case HeaderScanDiagnosticCode::SkippedGeneratedOutput:
					result.code = "scanner_skipped_generated_output";
					result.kind = "skipped_generated_output";
					break;
				case HeaderScanDiagnosticCode::SkippedExcludedPath:
					result.code = "scanner_skipped_excluded_path";
					result.kind = "skipped_excluded_path";
					break;
				case HeaderScanDiagnosticCode::SkippedSymlinkPath:
					result.code = "scanner_skipped_symlink_path";
					result.kind = "skipped_symlink_path";
					break;
			}
			result.path = diagnostic.path;
			result.message = diagnostic.message;
			result.suggested_action = "check --input-root, --project-root, and filesystem access";
			return result;
		}

		[[nodiscard]] std::string
		CompilationResolverCodeName(CompilationResolverDiagnosticCode code)
		{
			switch (code)
			{
				case CompilationResolverDiagnosticCode::CompileDatabaseNotFound:
					return "compile_database_not_found";
				case CompilationResolverDiagnosticCode::CompileDatabaseLoadFailure:
					return "compile_database_load_failure";
				case CompilationResolverDiagnosticCode::TranslationUnitReadFailure:
					return "translation_unit_read_failure";
				case CompilationResolverDiagnosticCode::RealTuParseFailure:
					return "real_tu_parse_failure";
				case CompilationResolverDiagnosticCode::SyntheticTuParseFailure:
					return "synthetic_tu_parse_failure";
				case CompilationResolverDiagnosticCode::CompileConfigConflict:
					return "compile_config_conflict";
			}

			return "unknown";
		}

		[[nodiscard]] RunDiagnostic ToRunDiagnostic(const CompilationResolverDiagnostic& diagnostic)
		{
			RunDiagnostic result;
			result.severity = diagnostic.severity;
			result.component = "clang";
			result.code = CompilationResolverCodeName(diagnostic.code);
			result.kind = "compilation_resolver";
			result.path = diagnostic.header_path.empty() ? diagnostic.translation_unit
														 : diagnostic.header_path;
			result.source_range.begin.file = diagnostic.header_path;
			result.message = diagnostic.message;
			result.suggested_action = "inspect compile_commands.json or the synthetic TU fallback";
			result.command = diagnostic.command.empty()
				? diagnostic.translation_unit.generic_string()
				: diagnostic.command;
			result.stderr_summary = diagnostic.stderr_summary;
			return result;
		}

		[[nodiscard]] RunDiagnostic ToRunDiagnostic(const Diagnostic& diagnostic)
		{
			RunDiagnostic result;
			result.severity = diagnostic.severity;
			result.component = "clang";
			result.code = "extractor";
			result.kind = std::to_string(static_cast<int>(diagnostic.code));
			result.path = diagnostic.source_range.begin.file;
			result.source_range = diagnostic.source_range;
			result.message = diagnostic.message;
			result.suggested_action = "inspect the unsupported item details in the report";
			return result;
		}

		[[nodiscard]] RunDiagnostic ToRunDiagnostic(const GeneratedFormatDiagnostic& diagnostic)
		{
			RunDiagnostic result;
			result.severity = DiagnosticSeverity::Error;
			result.component = "formatter";
			result.code = "format_failure";
			result.kind = "clang_format";
			result.path = diagnostic.path;
			result.message = diagnostic.message;
			result.suggested_action =
				"fix generated C++ spelling or choose a different format style";
			return result;
		}

		[[nodiscard]] RunDiagnostic ToRunDiagnostic(const GeneratedCompileDiagnostic& diagnostic)
		{
			RunDiagnostic result;
			const auto stage = std::string(ToString(diagnostic.stage));
			result.severity = DiagnosticSeverity::Error;
			result.component = "validation";
			result.code = stage + "_validation_failure";
			result.kind = stage;
			result.path = diagnostic.source_path;
			result.message = diagnostic.message;
			result.suggested_action = diagnostic.stage == GeneratedCompileValidationStage::Link
				? "rerun the recorded linker command and fix the generated link inputs"
				: "rerun the recorded compiler command and fix the generated input";
			result.command = diagnostic.command;
			result.stderr_summary = diagnostic.stderr_summary;
			result.validation_artifact_path = diagnostic.validation_artifact_path;
			return result;
		}

		[[nodiscard]] RunDiagnostic ToRunDiagnostic(const GenerationPolicyDiagnostic& diagnostic)
		{
			RunDiagnostic result;
			result.severity = diagnostic.kind == GenerationPolicyDiagnosticKind::UnsupportedItem ||
					diagnostic.kind == GenerationPolicyDiagnosticKind::LinkReadinessFailure
				? DiagnosticSeverity::Warning
				: DiagnosticSeverity::Error;
			result.component = "policy";
			result.code = "generation_policy";
			result.kind = std::string(ToString(diagnostic.kind));
			result.message = diagnostic.message;
			result.suggested_action = "inspect policy inputs and the referenced generated class";
			result.command = diagnostic.command;
			result.stderr_summary = diagnostic.stderr_summary;
			return result;
		}

		[[nodiscard]] RunDiagnostic ToRunDiagnostic(const OutputWriteDiagnostic& diagnostic)
		{
			RunDiagnostic result;
			result.severity = diagnostic.severity;
			result.component = "writer";
			result.code = diagnostic.code;
			result.kind = diagnostic.kind;
			result.path = diagnostic.path;
			result.message = diagnostic.message;
			result.suggested_action = "check --output-dir, --overwrite, and filesystem permissions";
			return result;
		}

		void AppendRunDiagnostics(std::vector<RunDiagnostic>& out,
								  std::span<const HeaderScanDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				out.push_back(ToRunDiagnostic(diagnostic));
			}
		}

		void AppendRunDiagnostics(std::vector<RunDiagnostic>& out,
								  std::span<const CompilationResolverDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				out.push_back(ToRunDiagnostic(diagnostic));
			}
		}

		void AppendRunDiagnostics(std::vector<RunDiagnostic>& out,
								  std::span<const Diagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				if (diagnostic.code == DiagnosticCode::UnsupportedConstruct)
				{
					continue;
				}
				out.push_back(ToRunDiagnostic(diagnostic));
			}
		}

		void AppendRunDiagnostics(std::vector<RunDiagnostic>& out,
								  std::span<const GeneratedFormatDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				out.push_back(ToRunDiagnostic(diagnostic));
			}
		}

		void AppendRunDiagnostics(std::vector<RunDiagnostic>& out,
								  std::span<const GeneratedCompileDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				out.push_back(ToRunDiagnostic(diagnostic));
			}
		}

		void AppendRunDiagnostics(std::vector<RunDiagnostic>& out,
								  std::span<const GenerationPolicyDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				out.push_back(ToRunDiagnostic(diagnostic));
			}
		}

		void AppendRunDiagnostics(std::vector<RunDiagnostic>& out,
								  std::span<const OutputWriteDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				out.push_back(ToRunDiagnostic(diagnostic));
			}
		}

		void AppendRunDiagnostics(std::vector<RunDiagnostic>& out,
								  std::span<const RunDiagnostic> diagnostics)
		{
			out.insert(out.end(), diagnostics.begin(), diagnostics.end());
		}

		[[nodiscard]] std::vector<RunDiagnostic>
		ToRunDiagnostics(std::span<const ConfigError> diagnostics)
		{
			std::vector<RunDiagnostic> result;
			result.reserve(diagnostics.size());
			for (const auto& diagnostic : diagnostics)
			{
				result.push_back(ToRunDiagnostic(diagnostic));
			}
			return result;
		}

		[[nodiscard]] std::vector<RunCommand>
		ToRunCommands(std::span<const GeneratedCompileCommandResult> commands)
		{
			std::vector<RunCommand> run_commands;
			run_commands.reserve(commands.size());
			for (const auto& command : commands)
			{
				run_commands.push_back(RunCommand{
					.source_path = command.source_path,
					.command = command.command,
					.exit_code = command.exit_code,
				});
			}
			return run_commands;
		}

		void PrintRunDiagnostics(std::ostream& err, std::span<const RunDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : SortedRunDiagnostics(diagnostics))
			{
				err << ToString(diagnostic.severity) << " [" << diagnostic.component << "]";
				const auto path =
					diagnostic.path.empty() ? diagnostic.source_range.begin.file : diagnostic.path;
				if (!path.empty())
				{
					err << " " << path.generic_string();
					if (diagnostic.source_range.begin.line != 0U)
					{
						err << ':' << diagnostic.source_range.begin.line;
						if (diagnostic.source_range.begin.column != 0U)
						{
							err << ':' << diagnostic.source_range.begin.column;
						}
					}
				}
				if (!diagnostic.member.empty())
				{
					err << " " << diagnostic.member;
				}
				err << ": " << diagnostic.message << '\n';
				if (!diagnostic.command.empty())
				{
					err << "  command: " << diagnostic.command << '\n';
				}
				if (!diagnostic.stderr_summary.empty())
				{
					err << "  stderr:\n" << diagnostic.stderr_summary << '\n';
				}
			}
		}

		void PrintOutputSummary(std::ostream& out, const OutputWriteResult& result)
		{
			for (const auto& file : result.files)
			{
				out << "mockfakegen: " << ToString(file.status) << " " << file.path.generic_string()
					<< '\n';
			}
		}

		[[nodiscard]] Diagnostic ToParseDiagnostic(const CompilationResolverDiagnostic& diagnostic)
		{
			Diagnostic result;
			result.severity = diagnostic.severity;
			result.code = DiagnosticCode::ParseError;
			result.source_range.begin.file = diagnostic.header_path;
			result.message = diagnostic.message;
			return result;
		}

		[[nodiscard]] std::vector<Diagnostic>
		PolicyParseDiagnostics(const CompilationResolveResult& result)
		{
			std::vector<Diagnostic> diagnostics = result.project.diagnostics;
			for (const auto& diagnostic : result.diagnostics)
			{
				if (IsError(diagnostic.severity))
				{
					diagnostics.push_back(ToParseDiagnostic(diagnostic));
				}
			}
			return diagnostics;
		}

		[[nodiscard]] std::vector<GeneratedFile>
		FilesSelectedByPolicy(std::span<const GeneratedFile> files,
							  const GenerationPolicyDecision& decision)
		{
			std::vector<GeneratedFile> selected;
			for (const auto& file : files)
			{
				if (IsPublishableGeneratedKind(file.kind))
				{
					if (decision.publish_generated_files)
					{
						selected.push_back(file);
					}
					continue;
				}
				if (file.kind == GeneratedFileKind::Manifest && decision.emit_manifest)
				{
					selected.push_back(file);
				}
				else if (file.kind == GeneratedFileKind::Report && decision.emit_report)
				{
					selected.push_back(file);
				}
			}
			SortGeneratedFiles(selected);
			return selected;
		}

		[[nodiscard]] std::vector<GeneratedFile>
		AppendDiagnosticArtifacts(std::span<const GeneratedFile> files,
								  std::span<const ClassModel> classes,
								  const GenerationReportMetadata& metadata,
								  bool emit_manifest)
		{
			std::vector<GeneratedFile> result(files.begin(), files.end());
			if (emit_manifest)
			{
				result.push_back(GenerateManifestJson(classes, metadata));
			}
			result.push_back(GenerateGenerationReport(classes, metadata));
			SortGeneratedFiles(result);
			return result;
		}
	} // namespace

	int RunCli(int argc, const char* const* argv, std::ostream& out, std::ostream& err)
	{
		const auto result = ParseConfigFromArgv(argc, argv);
		if (result.help_requested && result.errors.empty())
		{
			out << BuildUsage(result.program_name);
			return 0;
		}

		if (!result.errors.empty())
		{
			const auto diagnostics = ToRunDiagnostics(result.errors);
			PrintRunDiagnostics(err, diagnostics);
			err << '\n' << BuildUsage(result.program_name);

			if (result.config.has_value())
			{
				const std::vector<ClassModel> no_classes;
				const auto diagnostic_files =
					AppendDiagnosticArtifacts({},
											  no_classes,
											  GenerationReportMetadata{
												  .diagnostics = diagnostics,
												  .validation_commands = {},
												  .registry_mode = result.config->registry_mode,
												  .fallback_policy = result.config->fallback_policy,
											  },
											  result.config->emit_manifest);
				const auto write_result = WriteGeneratedFiles(
					OutputWriterOptions{
						.output_dir = result.config->output_dir,
						.dry_run = result.config->dry_run,
						.overwrite = result.config->overwrite,
					},
					diagnostic_files);
				PrintOutputSummary(out, write_result);
			}
			return 2;
		}

		const auto& config = *result.config;
		const auto scan_result = ScanHeaders(HeaderScannerOptions{
			.input_root = config.input_root,
			.project_root = config.project_root,
			.output_dir = config.output_dir,
			.header_filter = config.header_filter,
			.exclude_globs = config.exclude_globs,
		});
		std::vector<RunDiagnostic> run_diagnostics;
		AppendRunDiagnostics(run_diagnostics, scan_result.diagnostics);
		PrintRunDiagnostics(err, run_diagnostics);
		if (!scan_result.ok())
		{
			const std::vector<ClassModel> no_classes;
			const auto diagnostic_files =
				AppendDiagnosticArtifacts({},
										  no_classes,
										  GenerationReportMetadata{
											  .diagnostics = run_diagnostics,
											  .validation_commands = {},
											  .registry_mode = config.registry_mode,
											  .fallback_policy = config.fallback_policy,
										  },
										  config.emit_manifest);
			const auto write_result = WriteGeneratedFiles(
				OutputWriterOptions{
					.output_dir = config.output_dir,
					.dry_run = config.dry_run,
					.overwrite = config.overwrite,
				},
				diagnostic_files);
			PrintOutputSummary(out, write_result);
			return 1;
		}

		auto headers = ToHeaderModels(scan_result.headers);
		out << "mockfakegen: scanned " << headers.size() << " header(s)\n";

		auto resolve_result = ResolveCompilation(CompilationResolverOptions{
			.project_root = config.project_root,
			.build_path = config.build_path,
			.headers = headers,
			.fake_special_members = config.fake_special_members,
			.fake_static_data = config.fake_static_data,
			.interface_mock = config.interface_mock,
		});
		AppendRunDiagnostics(run_diagnostics, resolve_result.diagnostics);
		AppendRunDiagnostics(run_diagnostics, resolve_result.project.diagnostics);
		PrintRunDiagnostics(err, run_diagnostics);

		auto generated_files =
			GenerateMockFakeProject(resolve_result.project.classes,
									ProjectGenerationOptions{
										.registry_mode = config.registry_mode,
										.fallback_policy = config.fallback_policy,
										.emit_all_mocks = config.emit_all_mocks,
										.emit_cmake_fragment = config.emit_cmake_fragment,
										.emit_manifest = false,
										.emit_report = false,
										.interface_mock = config.interface_mock,
									});

		const auto format_result = FormatGeneratedFiles(
			GeneratedFormatOptions{
				.style = config.format_style,
				.style_search_root = config.project_root,
			},
			generated_files);
		AppendRunDiagnostics(run_diagnostics, format_result.diagnostics);
		PrintRunDiagnostics(err, run_diagnostics);
		if (!format_result.ok())
		{
			const auto diagnostic_files =
				AppendDiagnosticArtifacts({},
										  resolve_result.project.classes,
										  GenerationReportMetadata{
											  .diagnostics = run_diagnostics,
											  .validation_commands = {},
											  .registry_mode = config.registry_mode,
											  .fallback_policy = config.fallback_policy,
										  },
										  config.emit_manifest);
			const auto write_result = WriteGeneratedFiles(
				OutputWriterOptions{
					.output_dir = config.output_dir,
					.dry_run = config.dry_run,
					.overwrite = config.overwrite,
				},
				diagnostic_files);
			PrintOutputSummary(out, write_result);
			return 1;
		}

		const auto validation_result = ValidateGeneratedOutputCompile(
			GeneratedCompileValidationOptions{
				.mode = config.validate,
				.compiler = ValidationCompiler(),
				.include_dirs = ValidationIncludeDirs(config, resolve_result.project.headers),
				.link_files = ValidationLinkFiles(),
				.extra_args = resolve_result.validation_args,
				.command_timeout = config.validation_timeout,
				.keep_failed_artifacts = config.validation_keep_artifacts,
				.artifact_dir = config.validation_artifact_dir,
			},
			format_result.files);
		AppendRunDiagnostics(run_diagnostics, validation_result.diagnostics);

		const auto parse_diagnostics = PolicyParseDiagnostics(resolve_result);
		const auto policy_decision = EvaluateGenerationPolicy(
			config,
			GenerationPolicyInput{
				.classes = resolve_result.project.classes,
				.unsupported_items = resolve_result.project.unsupported_items,
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_result.diagnostics,
			});
		const auto unsupported_diagnostics =
			BuildUnsupportedItemDiagnostics(resolve_result.project.classes);
		const auto top_level_unsupported_diagnostics =
			BuildUnsupportedItemDiagnostics(resolve_result.project.unsupported_items);
		AppendRunDiagnostics(run_diagnostics, unsupported_diagnostics);
		AppendRunDiagnostics(run_diagnostics, top_level_unsupported_diagnostics);
		AppendRunDiagnostics(run_diagnostics, policy_decision.diagnostics);
		PrintRunDiagnostics(err, run_diagnostics);

		const auto final_files = AppendDiagnosticArtifacts(
			format_result.files,
			resolve_result.project.classes,
			GenerationReportMetadata{
				.diagnostics = run_diagnostics,
				.validation_commands = ToRunCommands(validation_result.commands),
				.registry_mode = config.registry_mode,
				.fallback_policy = config.fallback_policy,
			},
			config.emit_manifest);
		const auto selected_files = FilesSelectedByPolicy(final_files, policy_decision);
		auto write_result = WriteGeneratedFiles(
			OutputWriterOptions{
				.output_dir = config.output_dir,
				.dry_run = config.dry_run,
				.overwrite = config.overwrite,
			},
			selected_files);
		std::vector<RunDiagnostic> writer_diagnostics;
		AppendRunDiagnostics(writer_diagnostics, write_result.diagnostics);
		PrintRunDiagnostics(err, writer_diagnostics);
		AppendRunDiagnostics(run_diagnostics, writer_diagnostics);
		PrintOutputSummary(out, write_result);

		if (!write_result.ok() && !config.dry_run)
		{
			const auto diagnostic_report = GenerateGenerationReport(
				resolve_result.project.classes,
				GenerationReportMetadata{
					.diagnostics = run_diagnostics,
					.validation_commands = ToRunCommands(validation_result.commands),
					.registry_mode = config.registry_mode,
					.fallback_policy = config.fallback_policy,
				});
			const auto report_write_result = WriteGeneratedFiles(
				OutputWriterOptions{
					.output_dir = config.output_dir,
					.dry_run = false,
					.overwrite = config.overwrite,
				},
				std::span<const GeneratedFile>(&diagnostic_report, 1U));
			std::vector<RunDiagnostic> report_writer_diagnostics;
			AppendRunDiagnostics(report_writer_diagnostics, report_write_result.diagnostics);
			PrintRunDiagnostics(err, report_writer_diagnostics);
			PrintOutputSummary(out, report_write_result);
		}

		out << "mockfakegen: extracted " << resolve_result.project.classes.size()
			<< " class(es), generated " << selected_files.size() << " file(s)\n";
		if (validation_result.skipped)
		{
			out << "mockfakegen: validation skipped\n";
		}
		else
		{
			out << "mockfakegen: validation commands " << validation_result.commands.size() << '\n';
		}

		if (!write_result.ok())
		{
			return std::max(policy_decision.exit_code, 1);
		}
		return policy_decision.exit_code;
	}
} // namespace mockfakegen
