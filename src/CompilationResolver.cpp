#include "CompilationResolver.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <clang/Frontend/ASTUnit.h>
#include <clang/Frontend/TextDiagnosticBuffer.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/StringRef.h>

#include "clang/ClassExtractor.h"

namespace mockfakegen
{
	namespace
	{
		struct ParsedTranslationUnit
		{
			std::filesystem::path source_path;
			std::filesystem::path command_directory;
			std::vector<std::string> compile_args;
			std::vector<std::string> tool_args;
			std::string parse_command;
			bool read_failure = false;
			std::vector<ClangParseDiagnostic> diagnostics;
			std::unique_ptr<clang::ASTUnit> ast;
		};

		struct ClassObservation
		{
			std::string fingerprint;
			HeaderParseAttempt attempt;
			SourceRange source_range;
		};

		class SingleCompileCommandDatabase final : public clang::tooling::CompilationDatabase
		{
		  public:
			explicit SingleCompileCommandDatabase(clang::tooling::CompileCommand command)
				: command_(std::move(command))
			{
			}

			[[nodiscard]] std::vector<clang::tooling::CompileCommand>
			getCompileCommands(llvm::StringRef /*file_path*/) const override
			{
				return {command_};
			}

			[[nodiscard]] std::vector<std::string> getAllFiles() const override
			{
				return {command_.Filename};
			}

		  private:
			clang::tooling::CompileCommand command_;
		};

		[[nodiscard]] std::filesystem::path AbsoluteNormalized(const std::filesystem::path& path)
		{
			std::error_code absolute_error;
			auto absolute = std::filesystem::absolute(path, absolute_error);
			if (absolute_error)
			{
				absolute = path;
			}

			std::error_code canonical_error;
			const auto canonical = std::filesystem::weakly_canonical(absolute, canonical_error);
			if (!canonical_error)
			{
				return canonical.lexically_normal();
			}

			auto normalized = absolute.lexically_normal();
			if (normalized.has_relative_path() && normalized.filename().empty())
			{
				normalized = normalized.parent_path();
			}
			return normalized;
		}

		[[nodiscard]] std::filesystem::path
		CommandDirectory(const clang::tooling::CompileCommand& command)
		{
			return AbsoluteNormalized(std::filesystem::path(command.Directory));
		}

		[[nodiscard]] std::filesystem::path
		CommandRelativePath(const clang::tooling::CompileCommand& command,
							const std::filesystem::path& path)
		{
			if (path.is_absolute())
			{
				return AbsoluteNormalized(path);
			}
			return AbsoluteNormalized(CommandDirectory(command) / path);
		}

		[[nodiscard]] std::string JoinCommand(const std::filesystem::path& directory,
											  const std::filesystem::path& executable,
											  const std::vector<std::string>& args,
											  const std::filesystem::path& source_path)
		{
			std::string command = "cd ";
			command += directory.generic_string();
			command += " && ";
			command += executable.empty() ? std::string("clang++") : executable.generic_string();
			for (const auto& arg : args)
			{
				command += ' ';
				command += arg;
			}
			command += ' ';
			command += source_path.generic_string();
			return command;
		}

		void AppendDiagnostics(std::vector<ClangParseDiagnostic>& diagnostics,
							   ClangDiagnosticSeverity severity,
							   clang::TextDiagnosticBuffer::const_iterator begin,
							   clang::TextDiagnosticBuffer::const_iterator end)
		{
			for (auto iterator = begin; iterator != end; ++iterator)
			{
				diagnostics.push_back(ClangParseDiagnostic{
					.severity = severity,
					.message = iterator->second,
				});
			}
		}

		[[nodiscard]] bool HasErrorDiagnostic(const std::vector<ClangParseDiagnostic>& diagnostics)
		{
			return std::any_of(diagnostics.begin(),
							   diagnostics.end(),
							   [](const auto& diagnostic)
							   {
								   return diagnostic.severity == ClangDiagnosticSeverity::Error;
							   });
		}

		[[nodiscard]] std::string ToString(ClangDiagnosticSeverity severity)
		{
			switch (severity)
			{
				case ClangDiagnosticSeverity::Error:
					return "error";
				case ClangDiagnosticSeverity::Warning:
					return "warning";
				case ClangDiagnosticSeverity::Note:
					return "note";
			}

			return "unknown";
		}

		[[nodiscard]] std::string
		ClangDiagnosticsSummary(const std::vector<ClangParseDiagnostic>& diagnostics)
		{
			std::string summary;
			for (const auto& diagnostic : diagnostics)
			{
				if (!summary.empty())
				{
					summary += '\n';
				}
				summary += ToString(diagnostic.severity);
				summary += ": ";
				summary += diagnostic.message;
			}
			return summary;
		}

		void AddResolverDiagnostic(std::vector<CompilationResolverDiagnostic>& diagnostics,
								   DiagnosticSeverity severity,
								   CompilationResolverDiagnosticCode code,
								   std::filesystem::path header_path,
								   std::filesystem::path translation_unit,
								   std::string message,
								   std::string command = {},
								   std::string stderr_summary = {})
		{
			diagnostics.push_back(CompilationResolverDiagnostic{
				.severity = severity,
				.code = code,
				.header_path = std::move(header_path),
				.translation_unit = std::move(translation_unit),
				.message = std::move(message),
				.command = std::move(command),
				.stderr_summary = std::move(stderr_summary),
			});
		}

		void AddProjectDiagnostic(ProjectModel& project,
								  DiagnosticSeverity severity,
								  std::string message)
		{
			project.diagnostics.push_back(Diagnostic{
				.severity = severity,
				.code = DiagnosticCode::ParseError,
				.source_range = {},
				.message = std::move(message),
			});
		}

		[[nodiscard]] bool IsSourcePathArgument(const std::string& argument,
												const clang::tooling::CompileCommand& command)
		{
			if (argument == command.Filename)
			{
				return true;
			}

			const auto argument_path = std::filesystem::path(argument);
			if (argument_path.empty() ||
				(argument_path.is_relative() && argument.find('/') == std::string::npos))
			{
				return false;
			}

			const auto absolute_argument = argument_path.is_absolute()
				? AbsoluteNormalized(argument_path)
				: CommandRelativePath(command, argument_path);
			return absolute_argument == CommandRelativePath(command, command.Filename);
		}

		[[nodiscard]] bool HasStdArg(const std::vector<std::string>& args)
		{
			return std::any_of(args.begin(),
							   args.end(),
							   [](const auto& arg)
							   {
								   return arg == "-std" || arg.starts_with("-std=");
							   });
		}

		[[nodiscard]] bool IsRelativePathLike(const std::string& value)
		{
			if (value.empty() || value.starts_with('='))
			{
				return false;
			}
			return !std::filesystem::path(value).is_absolute();
		}

		[[nodiscard]] std::string
		ResolveCommandPathArgument(const clang::tooling::CompileCommand& command,
								   const std::string& value)
		{
			if (!IsRelativePathLike(value))
			{
				return value;
			}
			return CommandRelativePath(command, value).generic_string();
		}

		[[nodiscard]] bool IsSeparatePathOption(const std::string& arg)
		{
			return arg == "-I" || arg == "-iquote" || arg == "-isystem" || arg == "-idirafter" ||
				arg == "-iframework" || arg == "-F" || arg == "-include" || arg == "-imacros" ||
				arg == "-include-pch" || arg == "-ivfsoverlay" || arg == "-isysroot" ||
				arg == "--sysroot" || arg == "-resource-dir";
		}

		[[nodiscard]] std::optional<std::string>
		RewriteJoinedPathOption(const clang::tooling::CompileCommand& command,
								const std::string& arg)
		{
			const auto rewrite_after_prefix =
				[&](std::string_view prefix) -> std::optional<std::string>
			{
				if (!arg.starts_with(prefix) || arg.size() == prefix.size())
				{
					return std::nullopt;
				}
				const auto value = arg.substr(prefix.size());
				return std::string(prefix) + ResolveCommandPathArgument(command, value);
			};

			if (auto rewritten = rewrite_after_prefix("-I"); rewritten.has_value())
			{
				return rewritten;
			}
			if (auto rewritten = rewrite_after_prefix("-F"); rewritten.has_value())
			{
				return rewritten;
			}
			if (auto rewritten = rewrite_after_prefix("-isystem"); rewritten.has_value())
			{
				return rewritten;
			}

			const auto rewrite_after_equals =
				[&](std::string_view prefix) -> std::optional<std::string>
			{
				if (!arg.starts_with(prefix))
				{
					return std::nullopt;
				}
				const auto value = arg.substr(prefix.size());
				return std::string(prefix) + ResolveCommandPathArgument(command, value);
			};
			if (auto rewritten = rewrite_after_equals("--sysroot="); rewritten.has_value())
			{
				return rewritten;
			}
			if (auto rewritten = rewrite_after_equals("-fmodule-map-file="); rewritten.has_value())
			{
				return rewritten;
			}
			if (auto rewritten = rewrite_after_equals("-fmodules-cache-path=");
				rewritten.has_value())
			{
				return rewritten;
			}
			if (auto rewritten = rewrite_after_equals("-fprebuilt-module-path=");
				rewritten.has_value())
			{
				return rewritten;
			}

			return std::nullopt;
		}

		[[nodiscard]] std::vector<std::string>
		SanitizeCompileCommandArgs(const clang::tooling::CompileCommand& command,
								   bool resolve_path_arguments)
		{
			std::vector<std::string> args;
			for (std::size_t index = 1U; index < command.CommandLine.size(); ++index)
			{
				const auto& arg = command.CommandLine[index];
				if (arg == "-c" || IsSourcePathArgument(arg, command))
				{
					continue;
				}
				if (arg == "-o" || arg == "-MF" || arg == "-MT" || arg == "-MQ")
				{
					++index;
					continue;
				}
				if (arg.starts_with("-o") && arg.size() > 2U)
				{
					continue;
				}
				if (arg == "-MD" || arg == "-MMD" || arg == "-M" || arg == "-MM")
				{
					continue;
				}
				if (arg.starts_with("--driver-mode="))
				{
					continue;
				}

				if (IsSeparatePathOption(arg) && index + 1U < command.CommandLine.size())
				{
					args.push_back(arg);
					++index;
					args.push_back(resolve_path_arguments ? ResolveCommandPathArgument(
																command, command.CommandLine[index])
														  : command.CommandLine[index]);
					continue;
				}
				if (resolve_path_arguments)
				{
					if (auto rewritten = RewriteJoinedPathOption(command, arg);
						rewritten.has_value())
					{
						args.push_back(std::move(*rewritten));
						continue;
					}
				}
				args.push_back(arg);
			}

			if (!HasStdArg(args))
			{
				args.push_back("-std=c++23");
			}

			return args;
		}

		[[nodiscard]] ParsedTranslationUnit
		ParseTranslationUnit(const clang::tooling::CompileCommand& command)
		{
			ParsedTranslationUnit result;
			result.source_path = CommandRelativePath(command, command.Filename);
			result.command_directory = CommandDirectory(command);
			result.tool_args = SanitizeCompileCommandArgs(command, false);
			result.compile_args = SanitizeCompileCommandArgs(command, true);
			const auto executable = command.CommandLine.empty()
				? std::filesystem::path("clang++")
				: std::filesystem::path(command.CommandLine.front());
			result.parse_command = JoinCommand(
				result.command_directory, executable, result.tool_args, result.source_path);

			std::error_code status_error;
			const auto status = std::filesystem::status(result.source_path, status_error);
			if (status_error || !std::filesystem::is_regular_file(status))
			{
				result.read_failure = true;
				result.diagnostics.push_back(ClangParseDiagnostic{
					.severity = ClangDiagnosticSeverity::Error,
					.message = "translation unit could not be read",
				});
				return result;
			}

			std::vector<std::string> command_line;
			command_line.reserve(result.tool_args.size() + 2U);
			command_line.push_back(executable.generic_string());
			command_line.insert(
				command_line.end(), result.tool_args.begin(), result.tool_args.end());
			command_line.push_back(result.source_path.generic_string());

			clang::TextDiagnosticBuffer diagnostic_buffer;
			const SingleCompileCommandDatabase database(
				clang::tooling::CompileCommand(result.command_directory.string(),
											   result.source_path.generic_string(),
											   std::move(command_line),
											   ""));
			clang::tooling::ClangTool tool(
				database,
				std::vector<std::string>{result.source_path.generic_string()},
				std::make_shared<clang::PCHContainerOperations>());
			tool.setDiagnosticConsumer(&diagnostic_buffer);
			tool.setPrintErrorMessage(false);

			std::vector<std::unique_ptr<clang::ASTUnit>> asts;
			const auto exit_code = tool.buildASTs(asts);
			if (!asts.empty())
			{
				result.ast = std::move(asts.front());
			}

			AppendDiagnostics(result.diagnostics,
							  ClangDiagnosticSeverity::Error,
							  diagnostic_buffer.err_begin(),
							  diagnostic_buffer.err_end());
			AppendDiagnostics(result.diagnostics,
							  ClangDiagnosticSeverity::Warning,
							  diagnostic_buffer.warn_begin(),
							  diagnostic_buffer.warn_end());
			AppendDiagnostics(result.diagnostics,
							  ClangDiagnosticSeverity::Note,
							  diagnostic_buffer.note_begin(),
							  diagnostic_buffer.note_end());
			if (exit_code != 0 && result.diagnostics.empty())
			{
				result.diagnostics.push_back(ClangParseDiagnostic{
					.severity = ClangDiagnosticSeverity::Error,
					.message = "ClangTool failed without a diagnostic",
				});
			}

			return result;
		}

		[[nodiscard]] bool ParseSucceeded(const ParsedTranslationUnit& parsed)
		{
			return parsed.ast != nullptr && !HasErrorDiagnostic(parsed.diagnostics);
		}

		[[nodiscard]] std::filesystem::path HeaderKey(const HeaderModel& header)
		{
			return AbsoluteNormalized(header.absolute_path);
		}

		[[nodiscard]] std::string ClassKey(const ClassModel& class_model)
		{
			return HeaderKey(class_model.source_header).generic_string() +
				"::" + class_model.qualified_name;
		}

		[[nodiscard]] std::string MethodFingerprint(const MethodModel& method)
		{
			std::string value = method.signature_for_report;
			value += "|";
			value += method.return_type_spelling;
			value += "|";
			value += method.gmock_return_type_spelling;
			value += method.is_static ? "|static" : "|member";
			value += method.is_const ? "|const" : "|mutable";
			value += method.is_noexcept ? "|noexcept" : "|maythrow";
			value += "|ref=" + std::to_string(static_cast<int>(method.ref_qualifier));
			for (const auto& parameter : method.parameters)
			{
				value += "|param:";
				value += parameter.type_spelling;
				value += ":";
				value += parameter.gmock_type_spelling;
			}
			return value;
		}

		[[nodiscard]] std::string ClassFingerprint(const ClassModel& class_model)
		{
			std::string value = class_model.qualified_name;
			value += class_model.interface_mock ? "\nmode:interface" : "\nmode:link";
			for (const auto& method : class_model.mock_methods)
			{
				value += "\nmethod:";
				value += MethodFingerprint(method);
			}
			for (const auto& static_data : class_model.static_data_members)
			{
				value += "\nstatic_data:";
				value += static_data.signature_for_report;
				value += "|";
				value += static_data.type_spelling;
			}
			for (const auto& item : class_model.unsupported_items)
			{
				value += "\nunsupported:";
				value += item.kind;
				value += ":";
				value += item.member_signature;
			}
			return value;
		}

		[[nodiscard]] SourceRange PrimarySourceRange(const ClassModel& class_model)
		{
			if (!class_model.mock_methods.empty())
			{
				return class_model.mock_methods.front().source_range;
			}
			if (!class_model.fake_methods.empty())
			{
				return class_model.fake_methods.front().source_range;
			}
			if (!class_model.static_data_members.empty())
			{
				return class_model.static_data_members.front().source_range;
			}
			if (!class_model.unsupported_items.empty())
			{
				return class_model.unsupported_items.front().source_range;
			}

			return SourceRange{
				.begin =
					SourceLocation{
						.file = class_model.source_header.absolute_path,
					},
				.end =
					SourceLocation{
						.file = class_model.source_header.absolute_path,
					},
			};
		}

		[[nodiscard]] std::string SourceLocationSummary(const SourceRange& range)
		{
			std::string summary = range.begin.file.generic_string();
			if (range.begin.line != 0U)
			{
				summary += ':';
				summary += std::to_string(range.begin.line);
				if (range.begin.column != 0U)
				{
					summary += ':';
					summary += std::to_string(range.begin.column);
				}
			}
			return summary;
		}

		[[nodiscard]] std::string UnsupportedFingerprint(const UnsupportedItem& unsupported)
		{
			return unsupported.kind + "|" + unsupported.member_signature + "|" +
				unsupported.source_range.begin.file.generic_string() + "|" +
				std::to_string(unsupported.source_range.begin.line) + "|" +
				std::to_string(unsupported.source_range.begin.column);
		}

		void MergeTopLevelUnsupportedItems(ProjectModel& project,
										   std::vector<UnsupportedItem> unsupported_items)
		{
			for (auto& unsupported : unsupported_items)
			{
				const auto fingerprint = UnsupportedFingerprint(unsupported);
				const auto exists =
					std::any_of(project.unsupported_items.begin(),
								project.unsupported_items.end(),
								[&fingerprint](const auto& existing)
								{
									return UnsupportedFingerprint(existing) == fingerprint;
								});
				if (!exists)
				{
					project.unsupported_items.push_back(std::move(unsupported));
				}
			}
		}

		[[nodiscard]] bool HasExtractedContent(const ClassExtractionResult& extraction)
		{
			return !extraction.classes.empty() || !extraction.unsupported_items.empty() ||
				!extraction.diagnostics.empty();
		}

		void MergeExtraction(CompilationResolveResult& result,
							 std::map<std::string, ClassObservation>& observations,
							 HeaderModel header,
							 HeaderParseAttempt attempt,
							 ClassExtractionResult extraction)
		{
			for (auto& diagnostic : extraction.diagnostics)
			{
				result.project.diagnostics.push_back(std::move(diagnostic));
			}
			MergeTopLevelUnsupportedItems(result.project, std::move(extraction.unsupported_items));

			for (auto& class_model : extraction.classes)
			{
				class_model.source_header = header;
				const auto key = ClassKey(class_model);
				const auto fingerprint = ClassFingerprint(class_model);
				const auto source_range = PrimarySourceRange(class_model);
				const auto [iterator, inserted] =
					observations.emplace(key,
										 ClassObservation{
											 .fingerprint = fingerprint,
											 .attempt = attempt,
											 .source_range = source_range,
										 });

				if (inserted)
				{
					result.project.classes.push_back(std::move(class_model));
					continue;
				}

				if (iterator->second.fingerprint != fingerprint)
				{
					const auto message = "class " + class_model.qualified_name +
						" has different declarations across compile configurations; first "
						"command: " +
						iterator->second.attempt.parse_command +
						"; conflicting command: " + attempt.parse_command +
						"; first source: " + SourceLocationSummary(iterator->second.source_range) +
						"; conflicting source: " + SourceLocationSummary(source_range) + ".";
					const auto command_summary =
						"first: " + iterator->second.attempt.parse_command +
						"\nconflicting: " + attempt.parse_command;
					AddResolverDiagnostic(result.diagnostics,
										  DiagnosticSeverity::Error,
										  CompilationResolverDiagnosticCode::CompileConfigConflict,
										  header.absolute_path,
										  attempt.translation_unit,
										  message,
										  command_summary);
					AddProjectDiagnostic(result.project, DiagnosticSeverity::Error, message);
				}
			}
		}

		[[nodiscard]] std::vector<clang::tooling::CompileCommand>
		LoadCompileCommands(const CompilationResolverOptions& options,
							CompilationResolveResult& result)
		{
			const auto build_path = AbsoluteNormalized(options.build_path);
			const auto database_path = build_path / "compile_commands.json";
			if (!std::filesystem::exists(database_path))
			{
				AddResolverDiagnostic(
					result.diagnostics,
					DiagnosticSeverity::Warning,
					CompilationResolverDiagnosticCode::CompileDatabaseNotFound,
					{},
					{},
					"compile_commands.json was not found; using synthetic TU fallback.");
				return {};
			}

			std::string load_error;
			const auto database = clang::tooling::CompilationDatabase::loadFromDirectory(
				build_path.string(), load_error);
			if (database == nullptr)
			{
				AddResolverDiagnostic(result.diagnostics,
									  DiagnosticSeverity::Warning,
									  CompilationResolverDiagnosticCode::CompileDatabaseLoadFailure,
									  {},
									  {},
									  "failed to load compile_commands.json: " + load_error);
				return {};
			}

			auto commands = database->getAllCompileCommands();
			std::stable_sort(commands.begin(),
							 commands.end(),
							 [](const auto& lhs, const auto& rhs)
							 {
								 return CommandRelativePath(lhs, lhs.Filename).generic_string() <
									 CommandRelativePath(rhs, rhs.Filename).generic_string();
							 });
			return commands;
		}

		void AppendProjectRootInclude(std::vector<std::string>& args,
									  const std::filesystem::path& project_root)
		{
			const auto include_arg = "-I" + project_root.generic_string();
			const auto has_joined_include =
				std::find(args.begin(), args.end(), include_arg) != args.end();
			const auto has_separate_include =
				std::adjacent_find(args.begin(),
								   args.end(),
								   [&project_root](const auto& lhs, const auto& rhs)
								   {
									   return lhs == "-I" && rhs == project_root.generic_string();
								   }) != args.end();
			if (!has_joined_include && !has_separate_include)
			{
				args.push_back(include_arg);
			}
		}

		[[nodiscard]] std::vector<std::string>
		NearestCompileArgs(const HeaderModel& header,
						   const std::vector<clang::tooling::CompileCommand>& commands,
						   const std::filesystem::path& project_root)
		{
			std::optional<std::vector<std::string>> best_args;
			std::size_t best_score = 0U;
			const auto header_parent = AbsoluteNormalized(header.absolute_path).parent_path();

			for (const auto& command : commands)
			{
				const auto source_parent =
					CommandRelativePath(command, command.Filename).parent_path();
				std::size_t score = 0U;
				auto header_iterator = header_parent.begin();
				auto source_iterator = source_parent.begin();
				while (header_iterator != header_parent.end() &&
					   source_iterator != source_parent.end() &&
					   *header_iterator == *source_iterator)
				{
					++score;
					++header_iterator;
					++source_iterator;
				}

				if (!best_args.has_value() || score > best_score)
				{
					best_score = score;
					best_args = SanitizeCompileCommandArgs(command, true);
				}
			}

			if (best_args.has_value())
			{
				auto args = *best_args;
				AppendProjectRootInclude(args, project_root);
				return args;
			}

			return BuildSyntheticTuFallbackArgs(project_root);
		}

		void AppendUniqueValidationArgs(std::vector<std::string>& validation_args,
										const std::vector<std::string>& compile_args)
		{
			for (const auto& arg : compile_args)
			{
				const auto exists =
					std::find(validation_args.begin(), validation_args.end(), arg) !=
					validation_args.end();
				if (!exists)
				{
					validation_args.push_back(arg);
				}
			}
		}

	} // namespace

	bool CompilationResolveResult::ok() const noexcept
	{
		return std::none_of(diagnostics.begin(),
							diagnostics.end(),
							[](const auto& diagnostic)
							{
								return diagnostic.severity == DiagnosticSeverity::Error;
							});
	}

	std::string ToString(HeaderParseMode mode)
	{
		switch (mode)
		{
			case HeaderParseMode::RealTu:
				return "real-tu";
			case HeaderParseMode::SyntheticTu:
				return "synthetic-tu";
		}

		return "unknown";
	}

	CompilationResolveResult ResolveCompilation(const CompilationResolverOptions& options)
	{
		CompilationResolveResult result;
		result.project.headers = options.headers;
		std::map<std::string, ClassObservation> observations;

		const auto project_root = AbsoluteNormalized(options.project_root);
		const auto commands = LoadCompileCommands(options, result);

		for (const auto& command : commands)
		{
			auto parsed = ParseTranslationUnit(command);
			if (!ParseSucceeded(parsed))
			{
				const auto code = parsed.read_failure
					? CompilationResolverDiagnosticCode::TranslationUnitReadFailure
					: CompilationResolverDiagnosticCode::RealTuParseFailure;
				for (const auto& header : result.project.headers)
				{
					auto failed_header = header;
					result.parse_attempts.push_back(HeaderParseAttempt{
						.header = failed_header,
						.mode = HeaderParseMode::RealTu,
						.translation_unit = parsed.source_path,
						.compile_args = parsed.compile_args,
						.parse_command = parsed.parse_command,
						.success = false,
						.diagnostics = parsed.diagnostics,
					});
				}
				AddResolverDiagnostic(result.diagnostics,
									  DiagnosticSeverity::Warning,
									  code,
									  {},
									  parsed.source_path,
									  "real translation unit parse failed: " +
										  parsed.source_path.generic_string(),
									  parsed.parse_command,
									  ClangDiagnosticsSummary(parsed.diagnostics));
				continue;
			}
			AppendUniqueValidationArgs(result.validation_args, parsed.compile_args);

			for (auto& header : result.project.headers)
			{
				auto real_header = header;
				real_header.parsed_by_real_tu = true;
				const auto extraction = ExtractClassDefinitionsFromAst(
					*parsed.ast,
					real_header,
					ClassExtractionOptions{
						.fake_special_members = options.fake_special_members,
						.fake_static_data = options.fake_static_data,
						.interface_mock = options.interface_mock,
					});
				if (!HasExtractedContent(extraction))
				{
					continue;
				}

				header.parsed_by_real_tu = true;
				auto attempt = HeaderParseAttempt{
					.header = real_header,
					.mode = HeaderParseMode::RealTu,
					.translation_unit = parsed.source_path,
					.compile_args = parsed.compile_args,
					.parse_command = parsed.parse_command,
					.success = true,
					.diagnostics = parsed.diagnostics,
				};
				result.parse_attempts.push_back(attempt);
				MergeExtraction(result, observations, real_header, std::move(attempt), extraction);
			}
		}

		for (auto& header : result.project.headers)
		{
			if (header.parsed_by_real_tu)
			{
				continue;
			}

			auto synthetic_header = header;
			synthetic_header.parsed_by_synthetic_tu = true;
			const auto compile_args = NearestCompileArgs(synthetic_header, commands, project_root);
			auto synthetic = ParseHeaderWithSyntheticTu({
				.header_path = synthetic_header.absolute_path,
				.project_root = project_root,
				.compile_args = compile_args,
			});

			header.parsed_by_synthetic_tu = true;
			auto attempt = HeaderParseAttempt{
				.header = synthetic_header,
				.mode = HeaderParseMode::SyntheticTu,
				.translation_unit = {},
				.compile_args = synthetic.compile_args,
				.parse_command = "synthetic-tu " + synthetic.synthetic_code,
				.success = synthetic.success,
				.diagnostics = synthetic.diagnostics,
			};
			result.parse_attempts.push_back(attempt);

			if (!synthetic.success || synthetic.ast == nullptr)
			{
				const auto synthetic_command = "synthetic-tu " + synthetic.synthetic_code;
				AddResolverDiagnostic(result.diagnostics,
									  DiagnosticSeverity::Error,
									  CompilationResolverDiagnosticCode::SyntheticTuParseFailure,
									  synthetic_header.absolute_path,
									  {},
									  "synthetic TU parse failed: " +
										  synthetic_header.absolute_path.generic_string(),
									  synthetic_command,
									  ClangDiagnosticsSummary(synthetic.diagnostics));
				continue;
			}
			AppendUniqueValidationArgs(result.validation_args, synthetic.compile_args);

			const auto extraction = ExtractClassDefinitionsFromAst(
				*synthetic.ast,
				synthetic_header,
				ClassExtractionOptions{
					.fake_special_members = options.fake_special_members,
					.fake_static_data = options.fake_static_data,
					.interface_mock = options.interface_mock,
				});
			MergeExtraction(result, observations, synthetic_header, std::move(attempt), extraction);
		}

		SortProjectModel(result.project);
		return result;
	}
} // namespace mockfakegen
