#include "HeaderScanner.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace mockfakegen
{
	namespace
	{
		void AddDiagnostic(std::vector<HeaderScanDiagnostic>& diagnostics,
						   HeaderScanDiagnosticSeverity severity,
						   HeaderScanDiagnosticCode code,
						   std::filesystem::path path,
						   std::string message)
		{
			diagnostics.push_back(HeaderScanDiagnostic{
				.severity = severity,
				.code = code,
				.path = std::move(path),
				.message = std::move(message),
			});
		}

		void AddError(std::vector<HeaderScanDiagnostic>& diagnostics,
					  HeaderScanDiagnosticCode code,
					  std::filesystem::path path,
					  std::string message)
		{
			AddDiagnostic(diagnostics,
						  HeaderScanDiagnosticSeverity::Error,
						  code,
						  std::move(path),
						  std::move(message));
		}

		void AddInfo(std::vector<HeaderScanDiagnostic>& diagnostics,
					 HeaderScanDiagnosticCode code,
					 std::filesystem::path path,
					 std::string message)
		{
			AddDiagnostic(diagnostics,
						  HeaderScanDiagnosticSeverity::Info,
						  code,
						  std::move(path),
						  std::move(message));
		}

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

			return absolute.lexically_normal();
		}

		[[nodiscard]] std::filesystem::path
		AbsoluteLexicallyNormalized(const std::filesystem::path& path)
		{
			std::error_code absolute_error;
			auto absolute = std::filesystem::absolute(path, absolute_error);
			if (absolute_error)
			{
				absolute = path;
			}
			return absolute.lexically_normal();
		}

		[[nodiscard]] std::optional<std::filesystem::path>
		CanonicalNormalized(const std::filesystem::path& path, std::error_code& error)
		{
			error.clear();
			auto canonical = std::filesystem::canonical(path, error);
			if (error)
			{
				return std::nullopt;
			}
			return canonical.lexically_normal();
		}

		[[nodiscard]] bool IsSameOrUnder(const std::filesystem::path& path,
										 const std::filesystem::path& directory)
		{
			if (directory.empty())
			{
				return false;
			}

			const auto normalized_path = AbsoluteNormalized(path);
			const auto normalized_directory = AbsoluteNormalized(directory);

			const auto path_end = normalized_path.end();
			const auto directory_end = normalized_directory.end();
			const auto [directory_iterator, path_iterator] = std::mismatch(
				normalized_directory.begin(), directory_end, normalized_path.begin(), path_end);

			return directory_iterator == directory_end &&
				(path_iterator != normalized_path.begin() ||
				 normalized_directory == normalized_path);
		}

		[[nodiscard]] std::filesystem::path
		RelativeToProject(const std::filesystem::path& header,
						  const std::filesystem::path& project_root)
		{
			const auto relative = AbsoluteLexicallyNormalized(header).lexically_relative(
				AbsoluteLexicallyNormalized(project_root));
			if (!relative.empty())
			{
				return relative.lexically_normal();
			}

			return header.filename();
		}

		[[nodiscard]] HeaderCandidate MakeCandidate(const std::filesystem::path& header_path,
													const std::filesystem::path& project_root)
		{
			const auto absolute_path = AbsoluteLexicallyNormalized(header_path);
			const auto project_relative_path = RelativeToProject(absolute_path, project_root);
			return HeaderCandidate{
				.absolute_path = absolute_path,
				.project_relative_path = project_relative_path,
				.include_spelling = project_relative_path.generic_string(),
			};
		}

		[[nodiscard]] bool HeaderCandidateLess(const HeaderCandidate& lhs,
											   const HeaderCandidate& rhs)
		{
			const auto lhs_relative = lhs.project_relative_path.generic_string();
			const auto rhs_relative = rhs.project_relative_path.generic_string();
			if (lhs_relative != rhs_relative)
			{
				return lhs_relative < rhs_relative;
			}

			return lhs.absolute_path.generic_string() < rhs.absolute_path.generic_string();
		}

		[[nodiscard]] bool IsHeaderFile(const std::filesystem::directory_entry& entry,
										std::error_code& error)
		{
			error.clear();
			if (!std::filesystem::is_regular_file(entry.path(), error))
			{
				return false;
			}
			if (error)
			{
				return false;
			}

			return entry.path().extension() == ".h";
		}

		[[nodiscard]] bool IsCommonBuildDirectoryName(std::string_view name)
		{
			return name == "build" || name == "out" || name == ".git" || name == ".cache" ||
				name.starts_with("cmake-build-");
		}

		[[nodiscard]] bool IsBuiltinExcludedComponent(std::string_view name)
		{
			return name == "third_party" || name == "external" || IsCommonBuildDirectoryName(name);
		}

		[[nodiscard]] bool
		HasBuiltinExcludedComponent(const std::filesystem::path& project_relative_path)
		{
			for (const auto& part : project_relative_path)
			{
				if (IsBuiltinExcludedComponent(part.generic_string()))
				{
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool IsKnownGeneratedHeaderName(const std::filesystem::path& path)
		{
			const auto name = path.filename().generic_string();
			return name == "AllMocks.h" || name == "MockFakeRuntime.h";
		}

		[[nodiscard]] bool Exists(const std::filesystem::path& path)
		{
			std::error_code exists_error;
			return std::filesystem::exists(path, exists_error) && !exists_error;
		}

		[[nodiscard]] bool DirectoryLooksLikeGeneratedOutput(const std::filesystem::path& path)
		{
			return Exists(path / "MockFakeRuntime.h") || Exists(path / "AllMocks.h") ||
				(Exists(path / "manifest.json") && Exists(path / "generation_report.md")) ||
				Exists(path / "CMakeLists.fragment.cmake");
		}

		[[nodiscard]] std::string ReadPrefix(const std::filesystem::path& path)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
			{
				return {};
			}

			constexpr std::streamsize kMaxMarkerBytes = 8192;
			std::string text(static_cast<std::size_t>(kMaxMarkerBytes), '\0');
			stream.read(text.data(), kMaxMarkerBytes);
			text.resize(static_cast<std::size_t>(stream.gcount()));
			return text;
		}

		[[nodiscard]] bool FileHasGeneratedMarker(const std::filesystem::path& path)
		{
			const auto text = ReadPrefix(path);
			return text.find("Generated by mockfakegen") != std::string::npos ||
				text.find("generated by mockfakegen") != std::string::npos;
		}

		[[nodiscard]] bool LooksLikeGeneratedMockHeader(const std::filesystem::path& path)
		{
			const auto filename = path.filename().generic_string();
			if (!filename.starts_with("Mock") || path.extension() != ".h")
			{
				return false;
			}

			const auto text = ReadPrefix(path);
			return text.find("<gmock/gmock.h>") != std::string::npos &&
				text.find("MockFakeRuntime.h") != std::string::npos;
		}

		[[nodiscard]] bool IsGeneratedHeaderFile(const std::filesystem::path& path)
		{
			return IsKnownGeneratedHeaderName(path) || FileHasGeneratedMarker(path) ||
				LooksLikeGeneratedMockHeader(path);
		}

		[[nodiscard]] bool IsRegexMetaCharacter(char character) noexcept
		{
			switch (character)
			{
				case '.':
				case '^':
				case '$':
				case '+':
				case '(':
				case ')':
				case '[':
				case ']':
				case '{':
				case '}':
				case '|':
				case '\\':
					return true;
				default:
					return false;
			}
		}

		[[nodiscard]] std::string GlobToRegex(std::string_view pattern)
		{
			std::string regex;
			regex.reserve(pattern.size() * 2U + 2U);
			regex += '^';
			for (std::size_t index = 0U; index < pattern.size(); ++index)
			{
				const auto character = pattern[index];
				if (character == '*')
				{
					if (index + 1U < pattern.size() && pattern[index + 1U] == '*')
					{
						regex += ".*";
						++index;
					}
					else
					{
						regex += "[^/]*";
					}
				}
				else if (character == '?')
				{
					regex += "[^/]";
				}
				else
				{
					if (IsRegexMetaCharacter(character))
					{
						regex += '\\';
					}
					regex += character;
				}
			}
			regex += '$';
			return regex;
		}

		[[nodiscard]] bool GlobMatches(std::string_view pattern, std::string_view path)
		{
			try
			{
				return std::regex_match(std::string(path), std::regex(GlobToRegex(pattern)));
			}
			catch (const std::regex_error&)
			{
				return false;
			}
		}

		[[nodiscard]] bool MatchesConfiguredExclude(std::string_view relative_path,
													const std::vector<std::string>& exclude_globs)
		{
			for (const auto& glob : exclude_globs)
			{
				if (GlobMatches(glob, relative_path))
				{
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] std::optional<std::regex>
		CompileHeaderFilter(const HeaderScannerOptions& options,
							std::vector<HeaderScanDiagnostic>& diagnostics)
		{
			if (!options.header_filter.has_value())
			{
				return std::nullopt;
			}
			try
			{
				return std::regex(*options.header_filter);
			}
			catch (const std::regex_error& error)
			{
				AddError(diagnostics,
						 HeaderScanDiagnosticCode::InvalidHeaderFilter,
						 options.input_root,
						 "invalid --header-filter regex: " + std::string(error.what()));
				return std::nullopt;
			}
		}

		[[nodiscard]] bool MatchesHeaderFilter(const std::optional<std::regex>& header_filter,
											   std::string_view relative_path)
		{
			if (!header_filter.has_value())
			{
				return true;
			}
			return std::regex_search(std::string(relative_path), *header_filter);
		}
	} // namespace

	HeaderScanResult ScanHeaders(const HeaderScannerOptions& options)
	{
		HeaderScanResult result;
		const auto header_filter = CompileHeaderFilter(options, result.diagnostics);
		if (!result.ok())
		{
			return result;
		}

		const auto input_root = AbsoluteNormalized(options.input_root);
		const auto project_root = AbsoluteNormalized(options.project_root);
		const auto output_dir = AbsoluteNormalized(options.output_dir);

		std::error_code status_error;
		const auto input_status = std::filesystem::status(input_root, status_error);
		if (status_error)
		{
			if (status_error == std::errc::no_such_file_or_directory ||
				status_error == std::errc::not_a_directory)
			{
				AddError(result.diagnostics,
						 HeaderScanDiagnosticCode::InputRootDoesNotExist,
						 input_root,
						 "input root does not exist.");
				return result;
			}

			AddError(result.diagnostics,
					 HeaderScanDiagnosticCode::FilesystemError,
					 input_root,
					 "failed to inspect input root: " + status_error.message());
			return result;
		}

		if (!std::filesystem::exists(input_status))
		{
			AddError(result.diagnostics,
					 HeaderScanDiagnosticCode::InputRootDoesNotExist,
					 input_root,
					 "input root does not exist.");
			return result;
		}

		if (!std::filesystem::is_directory(input_status))
		{
			AddError(result.diagnostics,
					 HeaderScanDiagnosticCode::InputRootIsNotDirectory,
					 input_root,
					 "input root is not a directory.");
			return result;
		}

		std::set<std::filesystem::path> visited_directories;
		std::error_code input_canonical_error;
		if (const auto canonical_input = CanonicalNormalized(input_root, input_canonical_error);
			canonical_input.has_value())
		{
			visited_directories.insert(*canonical_input);
		}

		std::error_code iterator_error;
		auto iterator = std::filesystem::recursive_directory_iterator(
			input_root,
			std::filesystem::directory_options::follow_directory_symlink,
			iterator_error);
		const auto end = std::filesystem::recursive_directory_iterator();
		if (iterator_error)
		{
			AddError(result.diagnostics,
					 HeaderScanDiagnosticCode::FilesystemError,
					 input_root,
					 "failed to start input root traversal: " + iterator_error.message());
			return result;
		}

		while (iterator != end)
		{
			const auto entry = *iterator;
			const auto entry_path = entry.path();

			std::error_code symlink_error;
			const auto is_symlink = entry.is_symlink(symlink_error);
			if (symlink_error)
			{
				AddError(result.diagnostics,
						 HeaderScanDiagnosticCode::FilesystemError,
						 entry_path,
						 "failed to inspect symlink status: " + symlink_error.message());
			}
			else
			{
				bool should_skip_entry = false;
				bool is_directory = false;
				if (is_symlink)
				{
					std::error_code target_error;
					const auto target = CanonicalNormalized(entry_path, target_error);
					if (!target.has_value())
					{
						iterator.disable_recursion_pending();
						should_skip_entry = true;
						AddInfo(result.diagnostics,
								HeaderScanDiagnosticCode::SkippedSymlinkPath,
								entry_path,
								"skipped symlink path because target could not be resolved: " +
									target_error.message());
					}
					else if (!IsSameOrUnder(*target, project_root))
					{
						iterator.disable_recursion_pending();
						should_skip_entry = true;
						AddInfo(result.diagnostics,
								HeaderScanDiagnosticCode::SkippedSymlinkPath,
								entry_path,
								"skipped symlink path outside project root.");
					}
					else
					{
						std::error_code target_type_error;
						is_directory = std::filesystem::is_directory(*target, target_type_error);
						if (target_type_error)
						{
							iterator.disable_recursion_pending();
							should_skip_entry = true;
							AddError(result.diagnostics,
									 HeaderScanDiagnosticCode::FilesystemError,
									 entry_path,
									 "failed to inspect symlink target type: " +
										 target_type_error.message());
						}
						else if (is_directory && !visited_directories.insert(*target).second)
						{
							iterator.disable_recursion_pending();
							should_skip_entry = true;
							AddInfo(
								result.diagnostics,
								HeaderScanDiagnosticCode::SkippedSymlinkPath,
								entry_path,
								"skipped symlink directory because target was already visited.");
						}
					}
				}
				else
				{
					std::error_code type_error;
					is_directory = entry.is_directory(type_error);
					if (type_error)
					{
						should_skip_entry = true;
						AddError(result.diagnostics,
								 HeaderScanDiagnosticCode::FilesystemError,
								 entry_path,
								 "failed to inspect path type: " + type_error.message());
					}
					else if (is_directory)
					{
						std::error_code directory_canonical_error;
						if (const auto directory =
								CanonicalNormalized(entry_path, directory_canonical_error);
							directory.has_value())
						{
							visited_directories.insert(*directory);
						}
					}
				}

				if (!should_skip_entry && is_directory && IsSameOrUnder(entry_path, output_dir))
				{
					iterator.disable_recursion_pending();
					AddInfo(result.diagnostics,
							HeaderScanDiagnosticCode::SkippedGeneratedOutput,
							entry_path,
							"skipped configured output directory.");
				}
				else if (!should_skip_entry && is_directory &&
						 DirectoryLooksLikeGeneratedOutput(entry_path))
				{
					iterator.disable_recursion_pending();
					AddInfo(result.diagnostics,
							HeaderScanDiagnosticCode::SkippedGeneratedOutput,
							entry_path,
							"skipped directory containing mockfakegen generated output markers.");
				}
				else if (!should_skip_entry)
				{
					const auto absolute_entry_path = AbsoluteLexicallyNormalized(entry_path);
					const auto relative = RelativeToProject(absolute_entry_path, project_root);
					const auto input_relative = RelativeToProject(absolute_entry_path, input_root);
					const auto relative_path = relative.generic_string();
					if (HasBuiltinExcludedComponent(input_relative))
					{
						if (is_directory)
						{
							iterator.disable_recursion_pending();
						}
						AddInfo(result.diagnostics,
								HeaderScanDiagnosticCode::SkippedExcludedPath,
								entry_path,
								"skipped built-in excluded path.");
					}
					else if (MatchesConfiguredExclude(relative_path, options.exclude_globs))
					{
						if (is_directory)
						{
							iterator.disable_recursion_pending();
						}
						AddInfo(result.diagnostics,
								HeaderScanDiagnosticCode::SkippedExcludedPath,
								entry_path,
								"skipped path matching configured exclude.");
					}
					else if (!IsSameOrUnder(entry_path, output_dir))
					{
						std::error_code header_error;
						if (IsHeaderFile(entry, header_error))
						{
							if (IsGeneratedHeaderFile(entry_path))
							{
								AddInfo(result.diagnostics,
										HeaderScanDiagnosticCode::SkippedGeneratedOutput,
										entry_path,
										"skipped mockfakegen generated header.");
							}
							else if (!MatchesHeaderFilter(header_filter, relative_path))
							{
								AddInfo(
									result.diagnostics,
									HeaderScanDiagnosticCode::SkippedExcludedPath,
									entry_path,
									"skipped header because it does not match --header-filter.");
							}
							else
							{
								result.headers.push_back(MakeCandidate(entry_path, project_root));
							}
						}
						else if (header_error)
						{
							AddError(result.diagnostics,
									 HeaderScanDiagnosticCode::FilesystemError,
									 entry_path,
									 "failed to inspect file: " + header_error.message());
						}
					}
				}
			}

			iterator.increment(iterator_error);
			if (iterator_error)
			{
				AddError(result.diagnostics,
						 HeaderScanDiagnosticCode::FilesystemError,
						 entry_path,
						 "failed to continue traversal: " + iterator_error.message());
				iterator_error.clear();
			}
		}

		std::sort(result.headers.begin(), result.headers.end(), HeaderCandidateLess);
		return result;
	}
} // namespace mockfakegen
