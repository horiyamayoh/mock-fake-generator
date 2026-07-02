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
#include "validation/GeneratedOutputCheck.h"
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

		[[nodiscard]] bool CanWriteConfigErrorArtifacts(const ConfigParseResult& result)
		{
			if (!result.config.has_value())
			{
				return false;
			}
			return std::none_of(result.errors.begin(),
								result.errors.end(),
								[](const auto& error)
								{
									return error.option == "--output-dir";
								});
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

		[[nodiscard]] std::string DefineCompilerArg(std::string_view value)
		{
			if (value.starts_with("-D"))
			{
				return std::string(value);
			}
			return "-D" + std::string(value);
		}

		[[nodiscard]] std::vector<std::string> ConfigCompilerArgs(const Config& config)
		{
			std::vector<std::string> args;
			args.reserve(config.defines.size() + config.extra_args.size());
			for (const auto& define : config.defines)
			{
				args.push_back(DefineCompilerArg(define));
			}
			args.insert(args.end(), config.extra_args.begin(), config.extra_args.end());
			return args;
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
				case CompilationResolverDiagnosticCode::CompileDatabasePathMapped:
					return "compile_database_path_mapped";
				case CompilationResolverDiagnosticCode::CompileDatabaseUnmappedAbsolutePath:
					return "compile_database_unmapped_absolute_path";
				case CompilationResolverDiagnosticCode::CompileDatabaseMappedPathMissing:
					return "compile_database_mapped_path_missing";
				case CompilationResolverDiagnosticCode::CompileDatabaseCompilerMissing:
					return "compile_database_compiler_missing";
				case CompilationResolverDiagnosticCode::CompileDatabaseCompilerWrapper:
					return "compile_database_compiler_wrapper";
				case CompilationResolverDiagnosticCode::CompileDatabaseTargetMismatch:
					return "compile_database_target_mismatch";
				case CompilationResolverDiagnosticCode::CompileDatabaseSystemContextAssumption:
					return "compile_database_system_context_assumption";
				case CompilationResolverDiagnosticCode::CompileDatabasePathCaseMismatch:
					return "compile_database_path_case_mismatch";
				case CompilationResolverDiagnosticCode::CompileDatabaseCaseFoldCollision:
					return "compile_database_case_fold_collision";
				case CompilationResolverDiagnosticCode::CompileDatabaseSymlinkRisk:
					return "compile_database_symlink_risk";
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
			result.source_range = diagnostic.source_range;
			if (result.source_range.begin.file.empty())
			{
				result.source_range.begin.file = result.path;
				result.source_range.end.file = result.path;
			}
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

		[[nodiscard]] bool IsSameOrUnderPath(const std::filesystem::path& path,
											 const std::filesystem::path& root)
		{
			if (root.empty())
			{
				return false;
			}
			const auto relative = path.lexically_relative(root);
			if (relative.empty() || relative.has_root_path())
			{
				return false;
			}
			if (relative == ".")
			{
				return true;
			}
			return std::none_of(relative.begin(),
								relative.end(),
								[](const auto& component)
								{
									return component == "..";
								});
		}

		[[nodiscard]] std::filesystem::path
		RedactValidationPath(const std::filesystem::path& path,
							 const std::filesystem::path& artifact_root)
		{
			constexpr std::string_view kPlaceholder = "<validation-artifacts>";
			const auto normalized_root = artifact_root.lexically_normal();
			const auto normalized_path = path.lexically_normal();
			if (!IsSameOrUnderPath(normalized_path, normalized_root))
			{
				return path;
			}
			const auto relative = normalized_path.lexically_relative(normalized_root);
			if (relative == ".")
			{
				return std::filesystem::path(kPlaceholder);
			}
			return std::filesystem::path(kPlaceholder) / relative;
		}

		void ReplaceAll(std::string& text, std::string_view from, std::string_view to)
		{
			if (from.empty())
			{
				return;
			}

			std::size_t offset = 0U;
			while (offset <= text.size())
			{
				const auto position = text.find(from, offset);
				if (position == std::string::npos)
				{
					break;
				}
				text.replace(position, from.size(), to);
				offset = position + to.size();
			}
		}

		[[nodiscard]] std::string
		RedactValidationCommand(std::string command, const std::filesystem::path& artifact_root)
		{
			if (artifact_root.empty())
			{
				return command;
			}
			const auto normalized_root = artifact_root.lexically_normal();
			const auto generic_root = normalized_root.generic_string();
			ReplaceAll(command, generic_root, "<validation-artifacts>");
			const auto native_root = normalized_root.string();
			if (native_root != generic_root)
			{
				ReplaceAll(command, native_root, "<validation-artifacts>");
			}
			return command;
		}

		[[nodiscard]] std::vector<RunCommand>
		ToRunCommands(std::span<const GeneratedCompileCommandResult> commands,
					  const std::filesystem::path& artifact_root)
		{
			std::vector<RunCommand> run_commands;
			run_commands.reserve(commands.size());
			for (const auto& command : commands)
			{
				run_commands.push_back(RunCommand{
					.source_path = RedactValidationPath(command.source_path, artifact_root),
					.command = RedactValidationCommand(command.command, artifact_root),
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
				if (!diagnostic.code.empty())
				{
					err << " [" << diagnostic.code << "]";
				}
				if (!diagnostic.kind.empty())
				{
					err << " [" << diagnostic.kind << "]";
				}
				const auto path = !diagnostic.source_range.begin.file.empty() &&
						diagnostic.source_range.begin.line != 0U
					? diagnostic.source_range.begin.file
					: diagnostic.path.empty() ? diagnostic.source_range.begin.file
											  : diagnostic.path;
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

		void PrintWriterDiagnostics(std::ostream& err, const OutputWriteResult& write_result)
		{
			std::vector<RunDiagnostic> writer_diagnostics;
			AppendRunDiagnostics(writer_diagnostics, write_result.diagnostics);
			PrintRunDiagnostics(err, writer_diagnostics);
		}

		void PrintOutputSummary(std::ostream& out, const OutputWriteResult& result)
		{
			for (const auto& file : result.files)
			{
				out << "mockfakegen: " << ToString(file.status) << " " << file.path.generic_string()
					<< '\n';
			}
		}

		[[nodiscard]] bool WroteGeneratedFileKind(const OutputWriteResult& result,
												  GeneratedFileKind kind)
		{
			return std::any_of(result.files.begin(),
							   result.files.end(),
							   [kind](const auto& file)
							   {
								   return file.kind == kind &&
									   file.status == OutputWriteStatus::Written;
							   });
		}

		[[nodiscard]] GeneratedFilePublicationStatus
		ToPublicationStatus(OutputWriteStatus status) noexcept
		{
			switch (status)
			{
				case OutputWriteStatus::Planned:
					return GeneratedFilePublicationStatus::Planned;
				case OutputWriteStatus::Written:
					return GeneratedFilePublicationStatus::Written;
				case OutputWriteStatus::Unchanged:
					return GeneratedFilePublicationStatus::Unchanged;
				case OutputWriteStatus::SkippedExisting:
					return GeneratedFilePublicationStatus::SkippedExisting;
				case OutputWriteStatus::Failed:
					return GeneratedFilePublicationStatus::Failed;
			}

			return GeneratedFilePublicationStatus::Failed;
		}

		[[nodiscard]] std::string SourceClassName(const GeneratedFile& file)
		{
			if (!file.source_class.has_value())
			{
				return {};
			}
			return file.source_class->qualified_name;
		}

		[[nodiscard]] bool SamePublicationTarget(const GeneratedFile& lhs, const GeneratedFile& rhs)
		{
			return lhs.kind == rhs.kind &&
				lhs.relative_path.generic_string() == rhs.relative_path.generic_string() &&
				SourceClassName(lhs) == SourceClassName(rhs);
		}

		[[nodiscard]] GeneratedFilePublication
		MakeFilePublication(const GeneratedFile& file, GeneratedFilePublicationStatus status)
		{
			return GeneratedFilePublication{
				.kind = file.kind,
				.path = file.relative_path,
				.source_class = SourceClassName(file),
				.status = status,
			};
		}

		[[nodiscard]] std::vector<GeneratedFilePublication>
		BuildFilePublications(std::span<const GeneratedFile> generated_files,
							  std::span<const GeneratedFile> selected_files,
							  const OutputWriteResult* write_result)
		{
			std::vector<GeneratedFilePublication> publications;
			publications.reserve(generated_files.size());
			std::vector<bool> selected_used(selected_files.size(), false);
			for (const auto& file : generated_files)
			{
				auto status = GeneratedFilePublicationStatus::SuppressedByPolicy;
				for (std::size_t index = 0U; index < selected_files.size(); ++index)
				{
					if (selected_used[index] || !SamePublicationTarget(file, selected_files[index]))
					{
						continue;
					}

					selected_used[index] = true;
					status = GeneratedFilePublicationStatus::Selected;
					if (write_result != nullptr && index < write_result->files.size())
					{
						status = ToPublicationStatus(write_result->files[index].status);
					}
					break;
				}
				publications.push_back(MakeFilePublication(file, status));
			}
			return publications;
		}

		[[nodiscard]] Diagnostic ToParseDiagnostic(const CompilationResolverDiagnostic& diagnostic)
		{
			Diagnostic result;
			result.severity = diagnostic.severity;
			result.code = DiagnosticCode::ParseError;
			result.source_range = diagnostic.source_range;
			if (result.source_range.begin.file.empty())
			{
				const auto path = diagnostic.header_path.empty() ? diagnostic.translation_unit
																 : diagnostic.header_path;
				result.source_range.begin.file = path;
				result.source_range.end.file = path;
			}
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

		[[nodiscard]] bool IsParentReference(const std::filesystem::path& component)
		{
			return component == "..";
		}

		[[nodiscard]] bool IsCurrentReference(const std::filesystem::path& component)
		{
			return component == ".";
		}

		[[nodiscard]] bool HasRootEscape(const std::filesystem::path& path)
		{
			return path.is_absolute() || path.has_root_path() || path.has_root_name() ||
				path.has_root_directory();
		}

		[[nodiscard]] bool NormalizeGeneratedRelativePath(const std::filesystem::path& raw_path,
														  std::filesystem::path& normalized_path)
		{
			if (raw_path.empty() || raw_path.generic_string().empty() || HasRootEscape(raw_path))
			{
				return false;
			}

			normalized_path = raw_path.lexically_normal();
			if (normalized_path.empty() || IsCurrentReference(normalized_path) ||
				normalized_path.filename().empty() || HasRootEscape(normalized_path))
			{
				return false;
			}

			return std::none_of(normalized_path.begin(), normalized_path.end(), IsParentReference);
		}

		void RemoveStaleSuppressedGeneratedFiles(const Config& config,
												 std::span<const GeneratedFile> files,
												 std::ostream& out)
		{
			if (config.dry_run || !config.overwrite)
			{
				return;
			}

			for (const auto& file : files)
			{
				if (!IsPublishableGeneratedKind(file.kind))
				{
					continue;
				}

				std::filesystem::path relative_path;
				if (!NormalizeGeneratedRelativePath(file.relative_path, relative_path))
				{
					continue;
				}

				const auto output_path = (config.output_dir / relative_path).lexically_normal();
				std::error_code remove_error;
				const auto removed = std::filesystem::remove(output_path, remove_error);
				if (removed && !remove_error)
				{
					out << "mockfakegen: removed stale " << output_path.generic_string() << '\n';
				}
			}
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

		[[nodiscard]] bool IsGeneratedUnsupportedReadinessReason(std::string_view reason) noexcept
		{
			return reason.starts_with("unsupported items remain");
		}

		void ApplyPolicyLinkReadiness(std::vector<ClassModel>& classes,
									  std::span<const ClassLinkReadiness> readiness)
		{
			const auto count = std::min(classes.size(), readiness.size());
			for (std::size_t index = 0U; index < count; ++index)
			{
				auto& class_model = classes[index];
				const auto& class_readiness = readiness[index];
				class_model.link_ready = class_readiness.link_ready;
				class_model.link_readiness_reasons.clear();
				for (const auto& reason : class_readiness.reasons)
				{
					if (!class_model.unsupported_items.empty() &&
						IsGeneratedUnsupportedReadinessReason(reason))
					{
						continue;
					}
					class_model.link_readiness_reasons.push_back(reason);
				}
			}
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

			if (CanWriteConfigErrorArtifacts(result))
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
				PrintWriterDiagnostics(err, write_result);
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
		std::size_t printed_diagnostic_count = 0U;
		const auto print_new_run_diagnostics = [&err, &run_diagnostics, &printed_diagnostic_count]()
		{
			PrintRunDiagnostics(
				err,
				std::span<const RunDiagnostic>(run_diagnostics).subspan(printed_diagnostic_count));
			printed_diagnostic_count = run_diagnostics.size();
		};
		AppendRunDiagnostics(run_diagnostics, scan_result.diagnostics);
		print_new_run_diagnostics();
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
			PrintWriterDiagnostics(err, write_result);
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
			.extra_include_dirs = config.include_dirs,
			.extra_args = ConfigCompilerArgs(config),
			.path_maps = config.path_maps,
		});
		AppendRunDiagnostics(run_diagnostics, resolve_result.diagnostics);
		AppendRunDiagnostics(run_diagnostics, resolve_result.project.diagnostics);
		print_new_run_diagnostics();

		auto report_classes = ResolveGeneratedClassFilenames(resolve_result.project.classes);
		auto generated_files =
			GenerateMockFakeProject(report_classes,
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
		print_new_run_diagnostics();
		if (!format_result.ok())
		{
			const auto diagnostic_files = AppendDiagnosticArtifacts(
				{},
				report_classes,
				GenerationReportMetadata{
					.diagnostics = run_diagnostics,
					.validation_commands = {},
					.unsupported_items = resolve_result.project.unsupported_items,
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
			PrintWriterDiagnostics(err, write_result);
			PrintOutputSummary(out, write_result);
			return 1;
		}

		const auto generated_output_check_result =
			CheckGeneratedOutputForKetTokens(format_result.files);
		const auto generated_output_token_diagnostics =
			BuildGeneratedOutputTokenDiagnostics(generated_output_check_result.diagnostics);
		AppendRunDiagnostics(run_diagnostics, generated_output_token_diagnostics);

		const auto validation_result = ValidateGeneratedOutputCompile(
			GeneratedCompileValidationOptions{
				.mode = config.validate,
				.compiler = ValidationCompiler(),
				.include_dirs = ValidationIncludeDirs(config, resolve_result.project.headers),
				.link_files = ValidationLinkFiles(),
				.extra_args = ConfigCompilerArgs(config),
				.source_args = resolve_result.validation_arg_sets,
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
				.classes = report_classes,
				.unsupported_items = resolve_result.project.unsupported_items,
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_result.diagnostics,
				.generated_output_token_diagnostics = generated_output_check_result.diagnostics,
			});
		ApplyPolicyLinkReadiness(report_classes, policy_decision.class_link_readiness);
		const auto unsupported_diagnostics = BuildUnsupportedItemDiagnostics(report_classes);
		const auto top_level_unsupported_diagnostics =
			BuildUnsupportedItemDiagnostics(resolve_result.project.unsupported_items);
		AppendRunDiagnostics(run_diagnostics, unsupported_diagnostics);
		AppendRunDiagnostics(run_diagnostics, top_level_unsupported_diagnostics);
		AppendRunDiagnostics(run_diagnostics, policy_decision.diagnostics);
		print_new_run_diagnostics();

		auto report_metadata = GenerationReportMetadata{
			.diagnostics = run_diagnostics,
			.validation_commands =
				ToRunCommands(validation_result.commands, validation_result.artifact_root),
			.unsupported_items = resolve_result.project.unsupported_items,
			.registry_mode = config.registry_mode,
			.fallback_policy = config.fallback_policy,
			.validation_mode = std::string(ToString(validation_result.mode)),
			.validation_link_strategy = std::string(ToString(validation_result.link_strategy)),
			.validation_link_input_count = validation_result.link_input_count,
		};
		auto final_files = AppendDiagnosticArtifacts(
			format_result.files, report_classes, report_metadata, config.emit_manifest);
		auto selected_files = FilesSelectedByPolicy(final_files, policy_decision);
		if (!policy_decision.publish_generated_files)
		{
			report_metadata.file_publications =
				BuildFilePublications(final_files, selected_files, nullptr);
			final_files = AppendDiagnosticArtifacts(
				format_result.files, report_classes, report_metadata, config.emit_manifest);
			selected_files = FilesSelectedByPolicy(final_files, policy_decision);
			RemoveStaleSuppressedGeneratedFiles(config, final_files, out);
		}
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
			const auto write_failure_policy =
				EvaluateFailurePolicy(config, GenerationFailureKind::WriteFailure);
			auto final_report_metadata = GenerationReportMetadata{
				.diagnostics = run_diagnostics,
				.validation_commands =
					ToRunCommands(validation_result.commands, validation_result.artifact_root),
				.unsupported_items = resolve_result.project.unsupported_items,
				.registry_mode = config.registry_mode,
				.fallback_policy = config.fallback_policy,
				.validation_mode = std::string(ToString(validation_result.mode)),
				.validation_link_strategy = std::string(ToString(validation_result.link_strategy)),
				.validation_link_input_count = validation_result.link_input_count,
			};
			final_report_metadata.file_publications =
				BuildFilePublications(final_files, selected_files, &write_result);

			std::vector<GeneratedFile> diagnostic_artifacts;
			if (config.emit_manifest && write_failure_policy.emit_manifest)
			{
				diagnostic_artifacts.push_back(
					GenerateManifestJson(report_classes, final_report_metadata));
			}
			if (write_failure_policy.emit_report)
			{
				diagnostic_artifacts.push_back(
					GenerateGenerationReport(report_classes, final_report_metadata));
			}

			for (const auto& artifact : diagnostic_artifacts)
			{
				const auto artifact_write_result = WriteGeneratedFiles(
					OutputWriterOptions{
						.output_dir = config.output_dir,
						.dry_run = false,
						.overwrite =
							config.overwrite || WroteGeneratedFileKind(write_result, artifact.kind),
					},
					std::span<const GeneratedFile>(&artifact, 1U));
				std::vector<RunDiagnostic> artifact_writer_diagnostics;
				AppendRunDiagnostics(artifact_writer_diagnostics,
									 artifact_write_result.diagnostics);
				PrintRunDiagnostics(err, artifact_writer_diagnostics);
				PrintOutputSummary(out, artifact_write_result);
			}
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
