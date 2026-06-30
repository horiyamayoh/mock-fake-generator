#include "CompilationResolver.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include <clang/Frontend/ASTUnit.h>
#include <clang/Frontend/TextDiagnosticBuffer.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

#include "clang/ClassExtractor.h"

namespace mockfakegen
{
	namespace
	{
		struct ParsedTranslationUnit
		{
			std::filesystem::path source_path;
			std::vector<std::string> compile_args;
			std::string parse_command;
			std::vector<ClangParseDiagnostic> diagnostics;
			std::unique_ptr<clang::ASTUnit> ast;
		};

		struct ClassObservation
		{
			std::string fingerprint;
			HeaderParseAttempt attempt;
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

		[[nodiscard]] std::string ReadFileText(const std::filesystem::path& path)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
			{
				return {};
			}

			std::ostringstream buffer;
			buffer << stream.rdbuf();
			return buffer.str();
		}

		[[nodiscard]] std::string JoinCommand(std::filesystem::path executable,
											  const std::vector<std::string>& args)
		{
			std::string command = std::move(executable).generic_string();
			for (const auto& arg : args)
			{
				command += ' ';
				command += arg;
			}
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

		void AddResolverDiagnostic(std::vector<CompilationResolverDiagnostic>& diagnostics,
								   DiagnosticSeverity severity,
								   CompilationResolverDiagnosticCode code,
								   std::filesystem::path header_path,
								   std::filesystem::path translation_unit,
								   std::string message)
		{
			diagnostics.push_back(CompilationResolverDiagnostic{
				.severity = severity,
				.code = code,
				.header_path = std::move(header_path),
				.translation_unit = std::move(translation_unit),
				.message = std::move(message),
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
				: AbsoluteNormalized(std::filesystem::path(command.Directory) / argument_path);
			return absolute_argument == AbsoluteNormalized(command.Filename);
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

		[[nodiscard]] std::vector<std::string>
		SanitizeCompileCommandArgs(const clang::tooling::CompileCommand& command)
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
			result.source_path = AbsoluteNormalized(command.Filename);
			result.compile_args = SanitizeCompileCommandArgs(command);
			result.parse_command = JoinCommand(result.source_path, result.compile_args);

			const auto source = ReadFileText(result.source_path);
			if (source.empty())
			{
				result.diagnostics.push_back(ClangParseDiagnostic{
					.severity = ClangDiagnosticSeverity::Error,
					.message = "translation unit could not be read or is empty",
				});
				return result;
			}

			clang::TextDiagnosticBuffer diagnostic_buffer;
			result.ast = clang::tooling::buildASTFromCodeWithArgs(
				source,
				result.compile_args,
				result.source_path.generic_string(),
				"mockfakegen-clang-tool",
				std::make_shared<clang::PCHContainerOperations>(),
				clang::tooling::getClangStripDependencyFileAdjuster(),
				clang::tooling::FileContentMappings(),
				&diagnostic_buffer);

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
			for (const auto& method : class_model.mock_methods)
			{
				value += "\nmethod:";
				value += MethodFingerprint(method);
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

			for (auto& class_model : extraction.classes)
			{
				class_model.source_header = header;
				const auto key = ClassKey(class_model);
				const auto fingerprint = ClassFingerprint(class_model);
				const auto [iterator, inserted] =
					observations.emplace(key,
										 ClassObservation{
											 .fingerprint = fingerprint,
											 .attempt = attempt,
										 });

				if (inserted)
				{
					result.project.classes.push_back(std::move(class_model));
					continue;
				}

				if (iterator->second.fingerprint != fingerprint)
				{
					const auto message = "class " + class_model.qualified_name +
						" has different declarations across compile configurations.";
					AddResolverDiagnostic(result.diagnostics,
										  DiagnosticSeverity::Error,
										  CompilationResolverDiagnosticCode::CompileConfigConflict,
										  header.absolute_path,
										  attempt.translation_unit,
										  message);
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
								 return AbsoluteNormalized(lhs.Filename).generic_string() <
									 AbsoluteNormalized(rhs.Filename).generic_string();
							 });
			return commands;
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
				const auto source_parent = AbsoluteNormalized(command.Filename).parent_path();
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
					best_args = SanitizeCompileCommandArgs(command);
				}
			}

			if (best_args.has_value())
			{
				return *best_args;
			}

			return BuildSyntheticTuFallbackArgs(project_root);
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
				const auto code = parsed.ast == nullptr
					? CompilationResolverDiagnosticCode::TranslationUnitReadFailure
					: CompilationResolverDiagnosticCode::RealTuParseFailure;
				AddResolverDiagnostic(result.diagnostics,
									  DiagnosticSeverity::Warning,
									  code,
									  {},
									  parsed.source_path,
									  "real translation unit parse failed: " +
										  parsed.source_path.generic_string());
				continue;
			}

			for (auto& header : result.project.headers)
			{
				auto real_header = header;
				real_header.parsed_by_real_tu = true;
				const auto extraction = ExtractClassDefinitionsFromAst(
					*parsed.ast,
					real_header,
					ClassExtractionOptions{
						.fake_special_members = options.fake_special_members,
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
				AddResolverDiagnostic(result.diagnostics,
									  DiagnosticSeverity::Error,
									  CompilationResolverDiagnosticCode::SyntheticTuParseFailure,
									  synthetic_header.absolute_path,
									  {},
									  "synthetic TU parse failed: " +
										  synthetic_header.absolute_path.generic_string());
				continue;
			}

			const auto extraction = ExtractClassDefinitionsFromAst(
				*synthetic.ast,
				synthetic_header,
				ClassExtractionOptions{
					.fake_special_members = options.fake_special_members,
				});
			MergeExtraction(result, observations, synthetic_header, std::move(attempt), extraction);
		}

		SortProjectModel(result.project);
		return result;
	}
} // namespace mockfakegen
