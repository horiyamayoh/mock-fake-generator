#include "validation/GeneratedOutputCheck.h"

#include <fstream>
#include <sstream>
#include <system_error>

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
						for (const auto& token : ForbiddenGeneratedOutputKetTokens())
						{
							if (content.find(token) != std::string::npos)
							{
								AddDiagnostic(result,
											  path,
											  token,
											  "generated output contains forbidden token: " +
												  token);
							}
						}
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
