#include "validation/GeneratedOutputCheck.h"

#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

namespace mockfakegen
{
	namespace
	{
		void AddDiagnostic(GeneratedOutputCheckResult& result,
						   std::filesystem::path path,
						   std::string token,
						   std::string message)
		{
			result.diagnostics.push_back(GeneratedOutputTokenDiagnostic{
				.path = std::move(path),
				.token = std::move(token),
				.message = std::move(message),
			});
		}

		[[nodiscard]] bool ReadText(const std::filesystem::path& path, std::string& out)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
			{
				return false;
			}

			std::ostringstream buffer;
			buffer << stream.rdbuf();
			if (!stream.good() && !stream.eof())
			{
				return false;
			}

			out = buffer.str();
			return true;
		}

		[[nodiscard]] bool IsAsciiIdentifierCharacter(const char character) noexcept
		{
			return (character >= 'a' && character <= 'z') ||
				(character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
				character == '_';
		}

		[[nodiscard]] bool HasKetNamespaceUsage(std::string_view content) noexcept
		{
			constexpr std::string_view token = "ket::";
			auto position = content.find(token);
			while (position != std::string_view::npos)
			{
				if (position == 0U || !IsAsciiIdentifierCharacter(content[position - 1U]))
				{
					return true;
				}
				position = content.find(token, position + token.size());
			}
			return false;
		}

		void SkipSpaces(std::string_view line, std::size_t& position) noexcept
		{
			while (position < line.size() && (line[position] == ' ' || line[position] == '\t'))
			{
				++position;
			}
		}

		[[nodiscard]] std::string KetIncludeToken(std::string_view line)
		{
			std::size_t position = 0U;
			SkipSpaces(line, position);
			if (position >= line.size() || line[position] != '#')
			{
				return {};
			}
			++position;
			SkipSpaces(line, position);

			constexpr std::string_view include_directive = "include";
			if (line.substr(position, include_directive.size()) != include_directive)
			{
				return {};
			}
			position += include_directive.size();
			if (position < line.size() && IsAsciiIdentifierCharacter(line[position]))
			{
				return {};
			}
			SkipSpaces(line, position);
			if (position >= line.size() || (line[position] != '"' && line[position] != '<'))
			{
				return {};
			}

			const char opener = line[position];
			const char closer = opener == '"' ? '"' : '>';
			const auto target_begin = position + 1U;
			const auto target_end = line.find(closer, target_begin);
			if (target_end == std::string_view::npos)
			{
				return {};
			}

			const auto include_target = line.substr(target_begin, target_end - target_begin);
			constexpr std::string_view ket_header_prefix = "ket_";
			if (include_target.substr(0U, ket_header_prefix.size()) != ket_header_prefix)
			{
				return {};
			}

			std::string token = "#include ";
			token += opener;
			token += include_target;
			token += closer;
			return token;
		}

		[[nodiscard]] std::vector<std::string> KetIncludeTokens(std::string_view content)
		{
			std::vector<std::string> tokens;
			std::size_t line_begin = 0U;
			while (line_begin <= content.size())
			{
				const auto line_end = content.find('\n', line_begin);
				const auto line = line_end == std::string_view::npos
					? content.substr(line_begin)
					: content.substr(line_begin, line_end - line_begin);
				auto token = KetIncludeToken(line);
				if (!token.empty())
				{
					tokens.push_back(std::move(token));
				}
				if (line_end == std::string_view::npos)
				{
					break;
				}
				line_begin = line_end + 1U;
			}
			return tokens;
		}

		void CheckContentForKetTokens(GeneratedOutputCheckResult& result,
									  const std::filesystem::path& path,
									  std::string_view content)
		{
			if (HasKetNamespaceUsage(content))
			{
				AddDiagnostic(
					result, path, "ket::", "generated output contains forbidden token: ket::");
			}
			for (auto token : KetIncludeTokens(content))
			{
				AddDiagnostic(result,
							  path,
							  std::move(token),
							  "generated output includes a ket module header.");
			}
		}
	} // namespace

	const std::vector<std::string>& ForbiddenGeneratedOutputKetTokens()
	{
		static const std::vector<std::string> tokens{
			"ket::",
			"#include \"ket_",
			"#include <ket_",
		};
		return tokens;
	}

	GeneratedOutputCheckResult
	CheckGeneratedOutputForKetTokens(std::span<const GeneratedFile> files)
	{
		GeneratedOutputCheckResult result;
		for (const auto& file : files)
		{
			++result.checked_file_count;
			CheckContentForKetTokens(result, file.relative_path, file.content);
		}
		return result;
	}

	GeneratedOutputCheckResult
	CheckGeneratedOutputForKetTokens(const std::vector<std::filesystem::path>& roots)
	{
		GeneratedOutputCheckResult result;

		for (const auto& root : roots)
		{
			std::error_code status_error;
			const auto status = std::filesystem::status(root, status_error);
			if (status_error)
			{
				AddDiagnostic(result,
							  root,
							  {},
							  "failed to inspect generated output path: " + status_error.message());
				continue;
			}

			if (!std::filesystem::exists(status))
			{
				AddDiagnostic(result, root, {}, "generated output path does not exist.");
				continue;
			}

			if (!std::filesystem::is_directory(status))
			{
				AddDiagnostic(result, root, {}, "generated output path is not a directory.");
				continue;
			}

			std::size_t files_under_root = 0U;
			std::error_code iterator_error;
			auto iterator = std::filesystem::recursive_directory_iterator(root, iterator_error);
			const auto end = std::filesystem::recursive_directory_iterator();
			if (iterator_error)
			{
				AddDiagnostic(result,
							  root,
							  {},
							  "failed to traverse generated output path: " +
								  iterator_error.message());
				continue;
			}

			while (iterator != end)
			{
				const auto entry = *iterator;
				const auto path = entry.path();

				std::error_code type_error;
				const auto is_regular = entry.is_regular_file(type_error);
				if (type_error)
				{
					AddDiagnostic(result,
								  path,
								  {},
								  "failed to inspect generated output file: " +
									  type_error.message());
				}
				else if (is_regular)
				{
					++files_under_root;
					++result.checked_file_count;

					std::string content;
					if (!ReadText(path, content))
					{
						AddDiagnostic(result, path, {}, "failed to read generated output file.");
					}
					else
					{
						CheckContentForKetTokens(result, path, content);
					}
				}

				iterator.increment(iterator_error);
				if (iterator_error)
				{
					AddDiagnostic(result,
								  path,
								  {},
								  "failed to continue generated output traversal: " +
									  iterator_error.message());
					iterator_error.clear();
				}
			}

			if (files_under_root == 0U)
			{
				AddDiagnostic(result, root, {}, "generated output path contains no files.");
			}
		}

		return result;
	}
} // namespace mockfakegen
