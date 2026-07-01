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
		[[nodiscard]] std::string_view ToString(DiagnosticSeverity severity) noexcept
		{
			switch (severity)
			{
				case DiagnosticSeverity::Info:
					return "info";
				case DiagnosticSeverity::Warning:
					return "warning";
				case DiagnosticSeverity::Error:
					return "error";
			}

			return "unknown";
		}

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

		void PrintHeaderScanDiagnostics(std::ostream& err,
										std::span<const HeaderScanDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				err << "error [scanner]";
				if (!diagnostic.path.empty())
				{
					err << " " << diagnostic.path.generic_string();
				}
				err << ": " << diagnostic.message << '\n';
			}
		}

		void PrintResolverDiagnostics(std::ostream& err,
									  std::span<const CompilationResolverDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				err << ToString(diagnostic.severity) << " [clang]";
				if (!diagnostic.header_path.empty())
				{
					err << " header=" << diagnostic.header_path.generic_string();
				}
				if (!diagnostic.translation_unit.empty())
				{
					err << " tu=" << diagnostic.translation_unit.generic_string();
				}
				err << ": " << diagnostic.message << '\n';
			}
		}

		void PrintProjectDiagnostics(std::ostream& err, std::span<const Diagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				err << ToString(diagnostic.severity) << " [extractor]";
				if (!diagnostic.source_range.begin.file.empty())
				{
					err << " " << diagnostic.source_range.begin.file.generic_string();
					if (diagnostic.source_range.begin.line != 0U)
					{
						err << ':' << diagnostic.source_range.begin.line;
						if (diagnostic.source_range.begin.column != 0U)
						{
							err << ':' << diagnostic.source_range.begin.column;
						}
					}
				}
				err << ": " << diagnostic.message << '\n';
			}
		}

		void PrintFormatDiagnostics(std::ostream& err,
									std::span<const GeneratedFormatDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				err << "error [formatter] " << diagnostic.path.generic_string() << ": "
					<< diagnostic.message << '\n';
			}
		}

		void PrintValidationDiagnostics(std::ostream& err,
										std::span<const GeneratedCompileDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				err << "error [validation] " << diagnostic.source_path.generic_string() << ": "
					<< diagnostic.message << '\n';
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

		void PrintPolicyDiagnostics(std::ostream& err,
									std::span<const GenerationPolicyDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				err << "warning [policy]: " << diagnostic.message << '\n';
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

		void PrintOutputDiagnostics(std::ostream& err,
									std::span<const OutputWriteDiagnostic> diagnostics)
		{
			for (const auto& diagnostic : diagnostics)
			{
				err << "error [writer] " << diagnostic.path.generic_string() << ": "
					<< diagnostic.message << '\n';
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
	} // namespace

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

		const auto& config = *result.config;
		const auto scan_result = ScanHeaders(HeaderScannerOptions{
			.input_root = config.input_root,
			.project_root = config.project_root,
			.output_dir = config.output_dir,
		});
		PrintHeaderScanDiagnostics(err, scan_result.diagnostics);
		if (!scan_result.ok())
		{
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
		PrintResolverDiagnostics(err, resolve_result.diagnostics);
		PrintProjectDiagnostics(err, resolve_result.project.diagnostics);

		auto generated_files =
			GenerateMockFakeProject(resolve_result.project.classes,
									ProjectGenerationOptions{
										.registry_mode = config.registry_mode,
										.emit_all_mocks = config.emit_all_mocks,
										.emit_cmake_fragment = config.emit_cmake_fragment,
										.emit_manifest = config.emit_manifest,
										.emit_report = true,
										.interface_mock = config.interface_mock,
									});

		const auto format_result = FormatGeneratedFiles(
			GeneratedFormatOptions{
				.style = config.format_style,
				.style_search_root = config.project_root,
			},
			generated_files);
		PrintFormatDiagnostics(err, format_result.diagnostics);
		if (!format_result.ok())
		{
			return 1;
		}

		const auto validation_result = ValidateGeneratedOutputCompile(
			GeneratedCompileValidationOptions{
				.mode = config.validate,
				.compiler = ValidationCompiler(),
				.include_dirs = ValidationIncludeDirs(config, resolve_result.project.headers),
				.extra_args = {},
			},
			format_result.files);
		PrintValidationDiagnostics(err, validation_result.diagnostics);

		const auto parse_diagnostics = PolicyParseDiagnostics(resolve_result);
		const auto policy_decision =
			EvaluateGenerationPolicy(config,
									 GenerationPolicyInput{
										 .classes = resolve_result.project.classes,
										 .parse_diagnostics = parse_diagnostics,
										 .validation_diagnostics = validation_result.diagnostics,
									 });
		PrintPolicyDiagnostics(err, policy_decision.diagnostics);

		const auto selected_files = FilesSelectedByPolicy(format_result.files, policy_decision);
		auto write_result = WriteGeneratedFiles(
			OutputWriterOptions{
				.output_dir = config.output_dir,
				.dry_run = config.dry_run,
				.overwrite = config.overwrite,
			},
			selected_files);
		PrintOutputDiagnostics(err, write_result.diagnostics);
		PrintOutputSummary(out, write_result);

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
