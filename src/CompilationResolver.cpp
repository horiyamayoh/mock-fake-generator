#include "CompilationResolver.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
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
#include <llvm/TargetParser/Host.h>

#include "clang/ClassExtractor.h"

namespace mockfakegen
{
	namespace
	{
		struct ParsedTranslationUnit
		{
			std::filesystem::path source_path;
			std::filesystem::path command_directory;
			std::filesystem::path compiler;
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

		struct PathMapApplication
		{
			std::filesystem::path path;
			bool mapped = false;
			std::filesystem::path from;
			std::filesystem::path to;
		};

		struct CompileCommandPathObservation
		{
			std::string option;
			std::filesystem::path original;
			std::filesystem::path rewritten;
			bool mapped = false;
			bool must_exist = true;
			bool directory_expected = false;
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

		[[nodiscard]] std::filesystem::path LexicallyAbsolute(const std::filesystem::path& path)
		{
			std::error_code absolute_error;
			auto absolute =
				path.is_absolute() ? path : std::filesystem::absolute(path, absolute_error);
			if (absolute_error)
			{
				absolute = path;
			}
			auto normalized = absolute.lexically_normal();
			if (normalized.has_relative_path() && normalized.filename().empty())
			{
				normalized = normalized.parent_path();
			}
			return normalized;
		}

		[[nodiscard]] bool IsSameOrUnderPath(const std::filesystem::path& path,
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

		[[nodiscard]] std::size_t PathComponentCount(const std::filesystem::path& path)
		{
			return static_cast<std::size_t>(std::distance(path.begin(), path.end()));
		}

		[[nodiscard]] std::filesystem::path
		RelativeSuffixAfterPrefix(const std::filesystem::path& path,
								  const std::filesystem::path& prefix)
		{
			auto path_iterator = path.begin();
			for (auto prefix_iterator = prefix.begin(); prefix_iterator != prefix.end();
				 ++prefix_iterator)
			{
				++path_iterator;
			}

			std::filesystem::path suffix;
			for (; path_iterator != path.end(); ++path_iterator)
			{
				suffix /= *path_iterator;
			}
			return suffix;
		}

		[[nodiscard]] PathMapApplication
		ApplyPathMapsDetailed(const std::filesystem::path& path,
							  const std::vector<PathMapEntry>& path_maps)
		{
			const auto candidate = LexicallyAbsolute(path);
			const PathMapEntry* best_match = nullptr;
			std::filesystem::path best_from;
			std::size_t best_component_count = 0U;
			for (const auto& path_map : path_maps)
			{
				const auto from = LexicallyAbsolute(path_map.from);
				if (!IsSameOrUnderPath(candidate, from))
				{
					continue;
				}

				const auto component_count = PathComponentCount(from);
				if (best_match == nullptr || component_count > best_component_count)
				{
					best_match = &path_map;
					best_from = from;
					best_component_count = component_count;
				}
			}

			if (best_match == nullptr)
			{
				return PathMapApplication{
					.path = AbsoluteNormalized(candidate),
					.mapped = false,
					.from = {},
					.to = {},
				};
			}

			const auto suffix = RelativeSuffixAfterPrefix(candidate, best_from);
			return PathMapApplication{
				.path = AbsoluteNormalized(best_match->to / suffix),
				.mapped = true,
				.from = best_from,
				.to = best_match->to,
			};
		}

		[[nodiscard]] std::filesystem::path
		ApplyPathMaps(const std::filesystem::path& path, const std::vector<PathMapEntry>& path_maps)
		{
			return ApplyPathMapsDetailed(path, path_maps).path;
		}

		[[nodiscard]] std::filesystem::path
		CommandDirectory(const clang::tooling::CompileCommand& command,
						 const std::vector<PathMapEntry>& path_maps)
		{
			return ApplyPathMaps(std::filesystem::path(command.Directory), path_maps);
		}

		[[nodiscard]] std::filesystem::path
		CommandRelativePath(const clang::tooling::CompileCommand& command,
							const std::filesystem::path& path,
							const std::vector<PathMapEntry>& path_maps)
		{
			if (path.is_absolute())
			{
				return ApplyPathMaps(path, path_maps);
			}
			return AbsoluteNormalized(CommandDirectory(command, path_maps) / path);
		}

		[[nodiscard]] std::filesystem::path
		CommandCompilerPath(const clang::tooling::CompileCommand& command,
							const std::vector<PathMapEntry>& path_maps)
		{
			auto compiler = command.CommandLine.empty()
				? std::filesystem::path("clang++")
				: std::filesystem::path(command.CommandLine.front());
			if (compiler.empty() || compiler.parent_path().empty())
			{
				return compiler;
			}
			if (compiler.is_absolute())
			{
				return ApplyPathMaps(compiler, path_maps);
			}
			return AbsoluteNormalized(CommandDirectory(command, path_maps) / compiler);
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

		[[nodiscard]] std::string LowercaseAscii(std::string text)
		{
			for (auto& character : text)
			{
				character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
			}
			return text;
		}

		[[nodiscard]] std::string Basename(const std::filesystem::path& path)
		{
			return path.filename().generic_string();
		}

		[[nodiscard]] bool IsCompilerWrapperName(std::string_view name) noexcept
		{
			return name == "ccache" || name == "sccache" || name == "distcc" || name == "icecc";
		}

		[[nodiscard]] bool PathExists(const std::filesystem::path& path)
		{
			std::error_code error;
			return std::filesystem::exists(path, error);
		}

		[[nodiscard]] bool PathIsDirectory(const std::filesystem::path& path)
		{
			std::error_code error;
			return std::filesystem::is_directory(path, error);
		}

		[[nodiscard]] bool PathIsRegularFile(const std::filesystem::path& path)
		{
			std::error_code error;
			return std::filesystem::is_regular_file(path, error);
		}

		[[nodiscard]] bool IsKnownSystemRoot(const std::filesystem::path& path)
		{
			return IsSameOrUnderPath(path, std::filesystem::path("/usr")) ||
				IsSameOrUnderPath(path, std::filesystem::path("/lib")) ||
				IsSameOrUnderPath(path, std::filesystem::path("/opt"));
		}

		[[nodiscard]] bool IsUnderProjectOrBuild(const std::filesystem::path& path,
												 const CompilationResolverOptions& options)
		{
			const auto project_root = AbsoluteNormalized(options.project_root);
			const auto build_path = AbsoluteNormalized(options.build_path);
			return IsSameOrUnderPath(path, project_root) || IsSameOrUnderPath(path, build_path);
		}

		[[nodiscard]] bool ShouldRequirePath(std::string_view option) noexcept
		{
			return option != "-fmodules-cache-path";
		}

		[[nodiscard]] bool ShouldExpectDirectory(std::string_view option) noexcept
		{
			return option == "directory" || option == "-I" || option == "-iquote" ||
				option == "-isystem" || option == "-idirafter" || option == "-iframework" ||
				option == "-F" || option == "-isysroot" || option == "--sysroot" ||
				option == "--gcc-toolchain" || option == "-resource-dir" ||
				option == "-fmodules-cache-path" || option == "-fprebuilt-module-path";
		}

		[[nodiscard]] std::optional<std::filesystem::path>
		PathWithCaseMismatch(const std::filesystem::path& path)
		{
			if (path.empty())
			{
				return std::nullopt;
			}

			std::filesystem::path current;
			std::filesystem::path mismatched;
			for (const auto& component : path)
			{
				if (component == path.root_name() || component == path.root_directory())
				{
					current /= component;
					mismatched /= component;
					continue;
				}

				const auto parent = current.empty() ? std::filesystem::path(".") : current;
				std::error_code directory_error;
				if (!std::filesystem::is_directory(parent, directory_error))
				{
					return std::nullopt;
				}

				bool exact_match = false;
				std::optional<std::filesystem::path> case_fold_match;
				for (std::filesystem::directory_iterator entry(parent, directory_error), end;
					 !directory_error && entry != end;
					 entry.increment(directory_error))
				{
					const auto filename = entry->path().filename();
					if (filename == component)
					{
						exact_match = true;
						break;
					}
					if (LowercaseAscii(filename.generic_string()) ==
						LowercaseAscii(component.generic_string()))
					{
						case_fold_match = filename;
					}
				}

				if (!exact_match && case_fold_match.has_value())
				{
					mismatched /= *case_fold_match;
					return mismatched;
				}
				current /= component;
				mismatched /= component;
			}

			return std::nullopt;
		}

		void
		AddPathCaseCollisionDiagnostics(std::vector<CompilationResolverDiagnostic>& diagnostics,
										const std::filesystem::path& root)
		{
			if (root.empty() || !PathIsDirectory(root))
			{
				return;
			}

			std::map<std::string, std::filesystem::path> first_seen;
			std::set<std::string> reported;
			std::error_code iterator_error;
			for (std::filesystem::recursive_directory_iterator
					 entry(root,
						   std::filesystem::directory_options::skip_permission_denied,
						   iterator_error),
				 end;
				 !iterator_error && entry != end;
				 entry.increment(iterator_error))
			{
				const auto relative = entry->path().lexically_relative(root).generic_string();
				const auto key = LowercaseAscii(relative);
				const auto [iterator, inserted] = first_seen.emplace(key, entry->path());
				if (inserted || iterator->second == entry->path() || reported.contains(key))
				{
					continue;
				}
				reported.insert(key);
				AddResolverDiagnostic(
					diagnostics,
					DiagnosticSeverity::Warning,
					CompilationResolverDiagnosticCode::CompileDatabaseCaseFoldCollision,
					{},
					entry->path(),
					"case-fold collision under compile database root: " +
						iterator->second.generic_string() + " and " +
						entry->path().generic_string(),
					{},
					{});
			}
		}

		void AddSymlinkDiagnostics(std::vector<CompilationResolverDiagnostic>& diagnostics,
								   const CompileCommandPathObservation& observation,
								   const CompilationResolverOptions& options)
		{
			const auto symlink_path = observation.original.is_absolute()
				? LexicallyAbsolute(observation.original)
				: observation.rewritten;
			std::error_code symlink_error;
			const auto status = std::filesystem::symlink_status(symlink_path, symlink_error);
			if (symlink_error || status.type() == std::filesystem::file_type::not_found)
			{
				return;
			}
			if (status.type() != std::filesystem::file_type::symlink)
			{
				return;
			}

			std::error_code target_error;
			const auto target = std::filesystem::weakly_canonical(symlink_path, target_error);
			if (target_error || target.empty())
			{
				AddResolverDiagnostic(diagnostics,
									  DiagnosticSeverity::Error,
									  CompilationResolverDiagnosticCode::CompileDatabaseSymlinkRisk,
									  {},
									  symlink_path,
									  "compile database path contains a broken symlink for " +
										  observation.option + ": " + symlink_path.generic_string(),
									  {},
									  target_error.message());
				return;
			}

			if (!IsUnderProjectOrBuild(target, options))
			{
				AddResolverDiagnostic(diagnostics,
									  DiagnosticSeverity::Warning,
									  CompilationResolverDiagnosticCode::CompileDatabaseSymlinkRisk,
									  {},
									  symlink_path,
									  "compile database path symlink for " + observation.option +
										  " resolves outside --project-root and --build-path: " +
										  symlink_path.generic_string() + " -> " +
										  target.generic_string(),
									  {},
									  {});
			}
		}

		[[nodiscard]] bool IsSourcePathArgument(const std::string& argument,
												const clang::tooling::CompileCommand& command,
												const std::vector<PathMapEntry>& path_maps)
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
				? ApplyPathMaps(argument_path, path_maps)
				: CommandRelativePath(command, argument_path, path_maps);
			return absolute_argument == CommandRelativePath(command, command.Filename, path_maps);
		}

		[[nodiscard]] bool IsSeparateStandardOption(std::string_view arg) noexcept
		{
			return arg == "-std" || arg == "--std";
		}

		[[nodiscard]] bool IsJoinedStandardOption(std::string_view arg) noexcept
		{
			return arg.starts_with("-std=") || arg.starts_with("--std=");
		}

		void NormalizeCxx23StandardArgs(std::vector<std::string>& args)
		{
			std::vector<std::string> normalized;
			normalized.reserve(args.size() + 1U);
			for (std::size_t index = 0U; index < args.size(); ++index)
			{
				const auto& arg = args[index];
				if (IsSeparateStandardOption(arg))
				{
					if (index + 1U < args.size())
					{
						++index;
					}
					continue;
				}
				if (IsJoinedStandardOption(arg))
				{
					continue;
				}
				normalized.push_back(arg);
			}
			normalized.push_back("-std=c++23");
			args = std::move(normalized);
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
								   const std::string& value,
								   const std::vector<PathMapEntry>& path_maps,
								   bool resolve_relative_paths)
		{
			if (!IsRelativePathLike(value))
			{
				const auto value_path = std::filesystem::path(value);
				if (value_path.is_absolute())
				{
					return ApplyPathMaps(value_path, path_maps).generic_string();
				}
				return value;
			}
			if (!resolve_relative_paths)
			{
				return value;
			}
			return CommandRelativePath(command, value, path_maps).generic_string();
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
								const std::string& arg,
								const std::vector<PathMapEntry>& path_maps,
								bool resolve_relative_paths)
		{
			const auto rewrite_after_prefix =
				[&](std::string_view prefix) -> std::optional<std::string>
			{
				if (!arg.starts_with(prefix) || arg.size() == prefix.size())
				{
					return std::nullopt;
				}
				const auto value = arg.substr(prefix.size());
				return std::string(prefix) +
					ResolveCommandPathArgument(command, value, path_maps, resolve_relative_paths);
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
				return std::string(prefix) +
					ResolveCommandPathArgument(command, value, path_maps, resolve_relative_paths);
			};
			if (auto rewritten = rewrite_after_equals("--sysroot="); rewritten.has_value())
			{
				return rewritten;
			}
			if (auto rewritten = rewrite_after_equals("--gcc-toolchain="); rewritten.has_value())
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

		[[nodiscard]] std::optional<std::pair<std::string, std::string>>
		SplitJoinedPathOption(const std::string& arg)
		{
			const auto split_after_prefix =
				[&](std::string_view prefix) -> std::optional<std::pair<std::string, std::string>>
			{
				if (!arg.starts_with(prefix) || arg.size() == prefix.size())
				{
					return std::nullopt;
				}
				return std::pair{std::string(prefix), arg.substr(prefix.size())};
			};

			for (const auto prefix : {"-I", "-F", "-isystem"})
			{
				if (auto result = split_after_prefix(prefix); result.has_value())
				{
					return result;
				}
			}

			const auto split_after_equals =
				[&](std::string_view prefix,
					std::string_view option) -> std::optional<std::pair<std::string, std::string>>
			{
				if (!arg.starts_with(prefix))
				{
					return std::nullopt;
				}
				return std::pair{std::string(option), arg.substr(prefix.size())};
			};

			if (auto result = split_after_equals("--sysroot=", "--sysroot"); result.has_value())
			{
				return result;
			}
			if (auto result = split_after_equals("--gcc-toolchain=", "--gcc-toolchain");
				result.has_value())
			{
				return result;
			}
			if (auto result = split_after_equals("-fmodule-map-file=", "-fmodule-map-file");
				result.has_value())
			{
				return result;
			}
			if (auto result = split_after_equals("-fmodules-cache-path=", "-fmodules-cache-path");
				result.has_value())
			{
				return result;
			}
			if (auto result =
					split_after_equals("-fprebuilt-module-path=", "-fprebuilt-module-path");
				result.has_value())
			{
				return result;
			}

			return std::nullopt;
		}

		[[nodiscard]] CompileCommandPathObservation
		MakePathObservation(const clang::tooling::CompileCommand& command,
							std::string option,
							const std::filesystem::path& value,
							const std::vector<PathMapEntry>& path_maps)
		{
			CompileCommandPathObservation observation;
			observation.option = std::move(option);
			observation.original = value;
			observation.must_exist = ShouldRequirePath(observation.option);
			observation.directory_expected = ShouldExpectDirectory(observation.option);

			if (value.is_absolute())
			{
				const auto mapped = ApplyPathMapsDetailed(value, path_maps);
				observation.rewritten = mapped.path;
				observation.mapped = mapped.mapped;
				return observation;
			}

			observation.rewritten = CommandRelativePath(command, value, path_maps);
			return observation;
		}

		[[nodiscard]] std::vector<CompileCommandPathObservation>
		ObserveCompileCommandPaths(const clang::tooling::CompileCommand& command,
								   const CompilationResolverOptions& options)
		{
			std::vector<CompileCommandPathObservation> observations;
			const auto add_observation = [&](std::string option,
											 const std::filesystem::path& value,
											 bool must_exist,
											 bool directory_expected)
			{
				auto observation =
					MakePathObservation(command, std::move(option), value, options.path_maps);
				observation.must_exist = must_exist;
				observation.directory_expected = directory_expected;
				observations.push_back(std::move(observation));
			};

			add_observation("directory", command.Directory, true, true);
			add_observation("file", command.Filename, true, false);

			for (std::size_t index = 1U; index < command.CommandLine.size(); ++index)
			{
				const auto& arg = command.CommandLine[index];
				if (arg == "-o" || arg == "-MF" || arg == "-MT" || arg == "-MQ")
				{
					++index;
					continue;
				}
				if (arg.starts_with("-o") && arg.size() > 2U)
				{
					continue;
				}
				if (arg == "-MD" || arg == "-MMD" || arg == "-M" || arg == "-MM" || arg == "-c" ||
					IsSourcePathArgument(arg, command, options.path_maps))
				{
					continue;
				}

				if (IsSeparatePathOption(arg) && index + 1U < command.CommandLine.size())
				{
					++index;
					observations.push_back(MakePathObservation(
						command, arg, command.CommandLine[index], options.path_maps));
					continue;
				}
				if (auto joined = SplitJoinedPathOption(arg); joined.has_value())
				{
					observations.push_back(MakePathObservation(
						command, joined->first, joined->second, options.path_maps));
				}
			}

			return observations;
		}

		[[nodiscard]] bool IsFileLikeOption(std::string_view option) noexcept
		{
			return option == "file" || option == "-include" || option == "-imacros" ||
				option == "-include-pch" || option == "-ivfsoverlay" ||
				option == "-fmodule-map-file";
		}

		[[nodiscard]] bool HasSystemContextArg(const std::vector<std::string>& args)
		{
			for (std::size_t index = 0U; index < args.size(); ++index)
			{
				const auto& arg = args[index];
				if (arg == "-resource-dir" || arg == "-isysroot" || arg == "--sysroot")
				{
					return true;
				}
				if (arg == "--gcc-toolchain" && index + 1U < args.size())
				{
					return true;
				}
				if (arg.starts_with("--sysroot=") || arg.starts_with("--gcc-toolchain="))
				{
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] std::optional<std::string>
		TargetTripleArg(const std::vector<std::string>& args)
		{
			for (std::size_t index = 0U; index < args.size(); ++index)
			{
				const auto& arg = args[index];
				if ((arg == "-target" || arg == "--target") && index + 1U < args.size())
				{
					return args[index + 1U];
				}
				if (arg.starts_with("-target="))
				{
					return arg.substr(std::string_view("-target=").size());
				}
				if (arg.starts_with("--target="))
				{
					return arg.substr(std::string_view("--target=").size());
				}
			}
			return std::nullopt;
		}

		void
		AddCompileCommandPathDiagnostics(std::vector<CompilationResolverDiagnostic>& diagnostics,
										 const CompileCommandPathObservation& observation,
										 const CompilationResolverOptions& options)
		{
			if (observation.mapped)
			{
				AddResolverDiagnostic(diagnostics,
									  DiagnosticSeverity::Info,
									  CompilationResolverDiagnosticCode::CompileDatabasePathMapped,
									  {},
									  observation.rewritten,
									  "compile database path mapped for " + observation.option +
										  ": " + observation.original.generic_string() + " -> " +
										  observation.rewritten.generic_string(),
									  {},
									  {});
			}

			if (observation.original.is_absolute() && !observation.mapped)
			{
				const auto original = LexicallyAbsolute(observation.original);
				if (!IsUnderProjectOrBuild(original, options) && !IsKnownSystemRoot(original))
				{
					AddResolverDiagnostic(
						diagnostics,
						DiagnosticSeverity::Warning,
						CompilationResolverDiagnosticCode::CompileDatabaseUnmappedAbsolutePath,
						{},
						original,
						"compile database absolute path for " + observation.option +
							" was not remapped: " + original.generic_string(),
						{},
						{});
				}
			}

			if (const auto mismatch = PathWithCaseMismatch(observation.rewritten);
				mismatch.has_value())
			{
				AddResolverDiagnostic(
					diagnostics,
					DiagnosticSeverity::Warning,
					CompilationResolverDiagnosticCode::CompileDatabasePathCaseMismatch,
					{},
					observation.rewritten,
					"compile database path casing does not match the filesystem for " +
						observation.option + ": " + observation.rewritten.generic_string() +
						" differs from " + mismatch->generic_string(),
					{},
					{});
			}

			AddSymlinkDiagnostics(diagnostics, observation, options);

			if (!observation.must_exist)
			{
				return;
			}

			if (!PathExists(observation.rewritten))
			{
				AddResolverDiagnostic(
					diagnostics,
					DiagnosticSeverity::Error,
					CompilationResolverDiagnosticCode::CompileDatabaseMappedPathMissing,
					{},
					observation.rewritten,
					"compile database path for " + observation.option +
						" does not exist after path mapping: " +
						observation.rewritten.generic_string(),
					{},
					{});
				return;
			}

			if (observation.directory_expected && !PathIsDirectory(observation.rewritten))
			{
				AddResolverDiagnostic(
					diagnostics,
					DiagnosticSeverity::Error,
					CompilationResolverDiagnosticCode::CompileDatabaseMappedPathMissing,
					{},
					observation.rewritten,
					"compile database path for " + observation.option +
						" is expected to be a directory: " + observation.rewritten.generic_string(),
					{},
					{});
			}
			else if (IsFileLikeOption(observation.option) &&
					 !PathIsRegularFile(observation.rewritten))
			{
				AddResolverDiagnostic(
					diagnostics,
					DiagnosticSeverity::Error,
					CompilationResolverDiagnosticCode::CompileDatabaseMappedPathMissing,
					{},
					observation.rewritten,
					"compile database path for " + observation.option +
						" is expected to be a regular file: " +
						observation.rewritten.generic_string(),
					{},
					{});
			}
		}

		void AddCompileCommandPreflightDiagnostics(
			std::vector<CompilationResolverDiagnostic>& diagnostics,
			const clang::tooling::CompileCommand& command,
			const CompilationResolverOptions& options)
		{
			const auto compiler = CommandCompilerPath(command, options.path_maps);
			const auto compiler_name =
				command.CommandLine.empty() ? std::string{} : Basename(command.CommandLine.front());
			if (IsCompilerWrapperName(compiler_name))
			{
				AddResolverDiagnostic(
					diagnostics,
					DiagnosticSeverity::Warning,
					CompilationResolverDiagnosticCode::CompileDatabaseCompilerWrapper,
					{},
					CommandRelativePath(command, command.Filename, options.path_maps),
					"compile database compiler appears to be a wrapper: " + compiler_name +
						". Use an explicit compiler override if wrapper paths are not available.",
					{},
					{});
			}
			else if (!compiler.empty() && compiler.has_parent_path() && !PathExists(compiler))
			{
				AddResolverDiagnostic(
					diagnostics,
					DiagnosticSeverity::Warning,
					CompilationResolverDiagnosticCode::CompileDatabaseCompilerMissing,
					{},
					compiler,
					"compile database compiler path does not exist after path mapping: " +
						compiler.generic_string(),
					{},
					{});
			}

			for (const auto& observation : ObserveCompileCommandPaths(command, options))
			{
				AddCompileCommandPathDiagnostics(diagnostics, observation, options);
			}

			const auto target = TargetTripleArg(command.CommandLine);
			if (target.has_value() && *target != llvm::sys::getDefaultTargetTriple())
			{
				const auto context_note = HasSystemContextArg(command.CommandLine)
					? std::string(
						  "verify that mapped sysroot/resource-dir/gcc-toolchain paths exist.")
					: std::string("no sysroot/resource-dir/gcc-toolchain path was provided.");
				AddResolverDiagnostic(
					diagnostics,
					DiagnosticSeverity::Warning,
					CompilationResolverDiagnosticCode::CompileDatabaseTargetMismatch,
					{},
					CommandRelativePath(command, command.Filename, options.path_maps),
					"compile database target triple `" + *target + "` differs from host triple `" +
						llvm::sys::getDefaultTargetTriple() + "`; " + context_note,
					{},
					{});
			}

			if (!options.path_maps.empty() && !HasSystemContextArg(command.CommandLine))
			{
				AddResolverDiagnostic(
					diagnostics,
					DiagnosticSeverity::Info,
					CompilationResolverDiagnosticCode::CompileDatabaseSystemContextAssumption,
					{},
					CommandRelativePath(command, command.Filename, options.path_maps),
					"compile database was path-mapped without explicit sysroot/resource-dir/"
					"gcc-toolchain paths; producer and consumer system headers may differ.",
					{},
					{});
			}
		}

		[[nodiscard]] std::vector<std::string>
		SanitizeCompileCommandArgs(const clang::tooling::CompileCommand& command,
								   bool resolve_path_arguments,
								   const std::vector<PathMapEntry>& path_maps)
		{
			std::vector<std::string> args;
			for (std::size_t index = 1U; index < command.CommandLine.size(); ++index)
			{
				const auto& arg = command.CommandLine[index];
				if (arg == "-c" || IsSourcePathArgument(arg, command, path_maps))
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
					args.push_back(
						resolve_path_arguments
							? ResolveCommandPathArgument(
								  command, command.CommandLine[index], path_maps, true)
							: ResolveCommandPathArgument(
								  command, command.CommandLine[index], path_maps, false));
					continue;
				}
				if (auto rewritten =
						RewriteJoinedPathOption(command, arg, path_maps, resolve_path_arguments);
					rewritten.has_value())
				{
					args.push_back(std::move(*rewritten));
					continue;
				}
				args.push_back(arg);
			}

			return args;
		}

		void AppendExtraCompilerArgs(std::vector<std::string>& args,
									 const CompilationResolverOptions& options)
		{
			for (const auto& include_dir : options.extra_include_dirs)
			{
				args.push_back("-I");
				args.push_back(AbsoluteNormalized(include_dir).generic_string());
			}
			args.insert(args.end(), options.extra_args.begin(), options.extra_args.end());
		}

		[[nodiscard]] ParsedTranslationUnit
		ParseTranslationUnit(const clang::tooling::CompileCommand& command,
							 const CompilationResolverOptions& options)
		{
			ParsedTranslationUnit result;
			result.source_path = CommandRelativePath(command, command.Filename, options.path_maps);
			result.command_directory = CommandDirectory(command, options.path_maps);
			result.tool_args = SanitizeCompileCommandArgs(command, false, options.path_maps);
			result.compile_args = SanitizeCompileCommandArgs(command, true, options.path_maps);
			AppendExtraCompilerArgs(result.tool_args, options);
			AppendExtraCompilerArgs(result.compile_args, options);
			NormalizeCxx23StandardArgs(result.tool_args);
			NormalizeCxx23StandardArgs(result.compile_args);
			const auto executable = CommandCompilerPath(command, options.path_maps);
			result.compiler = executable;
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
					result.validation_arg_sets.push_back(GeneratedSourceCompileArgs{
						.qualified_name = class_model.qualified_name,
						.source_header = class_model.source_header.include_spelling,
						.compiler = attempt.compiler,
						.args = attempt.compile_args,
					});
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
			std::stable_sort(
				commands.begin(),
				commands.end(),
				[&options](const auto& lhs, const auto& rhs)
				{
					return CommandRelativePath(lhs, lhs.Filename, options.path_maps)
							   .generic_string() <
						CommandRelativePath(rhs, rhs.Filename, options.path_maps).generic_string();
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
						   const std::filesystem::path& project_root,
						   const CompilationResolverOptions& options)
		{
			std::optional<std::vector<std::string>> best_args;
			std::size_t best_score = 0U;
			const auto header_parent = AbsoluteNormalized(header.absolute_path).parent_path();

			for (const auto& command : commands)
			{
				const auto source_parent =
					CommandRelativePath(command, command.Filename, options.path_maps).parent_path();
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
					best_args = SanitizeCompileCommandArgs(command, true, options.path_maps);
				}
			}

			if (best_args.has_value())
			{
				auto args = *best_args;
				AppendProjectRootInclude(args, project_root);
				AppendExtraCompilerArgs(args, options);
				NormalizeCxx23StandardArgs(args);
				return args;
			}

			auto args = BuildSyntheticTuFallbackArgs(project_root);
			AppendExtraCompilerArgs(args, options);
			NormalizeCxx23StandardArgs(args);
			return args;
		}

		void AppendUniqueValidationArgs(std::vector<std::string>& validation_args,
										const std::vector<std::string>& compile_args)
		{
			for (std::size_t index = 0U; index < compile_args.size(); ++index)
			{
				const auto& arg = compile_args[index];
				if (IsSeparatePathOption(arg) && index + 1U < compile_args.size())
				{
					const auto& value = compile_args[index + 1U];
					const auto exists =
						std::adjacent_find(validation_args.begin(),
										   validation_args.end(),
										   [&arg, &value](const auto& lhs, const auto& rhs)
										   {
											   return lhs == arg && rhs == value;
										   }) != validation_args.end();
					if (!exists)
					{
						validation_args.push_back(arg);
						validation_args.push_back(value);
					}
					++index;
					continue;
				}

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
		if (!options.path_maps.empty())
		{
			AddPathCaseCollisionDiagnostics(result.diagnostics, project_root);
			AddPathCaseCollisionDiagnostics(result.diagnostics,
											AbsoluteNormalized(options.build_path));
		}

		for (const auto& command : commands)
		{
			AddCompileCommandPreflightDiagnostics(result.diagnostics, command, options);
			auto parsed = ParseTranslationUnit(command, options);
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
						.compiler = parsed.compiler,
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
					.compiler = parsed.compiler,
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
			const auto compile_args =
				NearestCompileArgs(synthetic_header, commands, project_root, options);
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
				.compiler = {},
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
