#include "HeaderScanner.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <system_error>

namespace mockfakegen
{
	namespace
	{
		void AddDiagnostic(std::vector<HeaderScanDiagnostic>& diagnostics,
						   HeaderScanDiagnosticCode code,
						   std::filesystem::path path,
						   std::string message)
		{
			diagnostics.push_back(HeaderScanDiagnostic{code, std::move(path), std::move(message)});
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
			std::error_code relative_error;
			const auto relative = std::filesystem::relative(header, project_root, relative_error);
			if (!relative_error)
			{
				return relative.lexically_normal();
			}

			return header.filename();
		}

		[[nodiscard]] HeaderCandidate MakeCandidate(const std::filesystem::path& header_path,
													const std::filesystem::path& project_root)
		{
			const auto absolute_path = AbsoluteNormalized(header_path);
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
			if (!entry.is_regular_file(error))
			{
				return false;
			}
			if (error)
			{
				return false;
			}

			return entry.path().extension() == ".h";
		}
	} // namespace

	HeaderScanResult ScanHeaders(const HeaderScannerOptions& options)
	{
		HeaderScanResult result;

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
				AddDiagnostic(result.diagnostics,
							  HeaderScanDiagnosticCode::InputRootDoesNotExist,
							  input_root,
							  "input root does not exist.");
				return result;
			}

			AddDiagnostic(result.diagnostics,
						  HeaderScanDiagnosticCode::FilesystemError,
						  input_root,
						  "failed to inspect input root: " + status_error.message());
			return result;
		}

		if (!std::filesystem::exists(input_status))
		{
			AddDiagnostic(result.diagnostics,
						  HeaderScanDiagnosticCode::InputRootDoesNotExist,
						  input_root,
						  "input root does not exist.");
			return result;
		}

		if (!std::filesystem::is_directory(input_status))
		{
			AddDiagnostic(result.diagnostics,
						  HeaderScanDiagnosticCode::InputRootIsNotDirectory,
						  input_root,
						  "input root is not a directory.");
			return result;
		}

		std::error_code iterator_error;
		auto iterator = std::filesystem::recursive_directory_iterator(input_root, iterator_error);
		const auto end = std::filesystem::recursive_directory_iterator();
		if (iterator_error)
		{
			AddDiagnostic(result.diagnostics,
						  HeaderScanDiagnosticCode::FilesystemError,
						  input_root,
						  "failed to start input root traversal: " + iterator_error.message());
			return result;
		}

		while (iterator != end)
		{
			const auto entry = *iterator;
			const auto entry_path = entry.path();

			std::error_code type_error;
			const auto is_directory = entry.is_directory(type_error);
			if (type_error)
			{
				AddDiagnostic(result.diagnostics,
							  HeaderScanDiagnosticCode::FilesystemError,
							  entry_path,
							  "failed to inspect path type: " + type_error.message());
			}
			else if (is_directory && IsSameOrUnder(entry_path, output_dir))
			{
				iterator.disable_recursion_pending();
			}
			else if (!IsSameOrUnder(entry_path, output_dir))
			{
				std::error_code header_error;
				if (IsHeaderFile(entry, header_error))
				{
					result.headers.push_back(MakeCandidate(entry_path, project_root));
				}
				else if (header_error)
				{
					AddDiagnostic(result.diagnostics,
								  HeaderScanDiagnosticCode::FilesystemError,
								  entry_path,
								  "failed to inspect file: " + header_error.message());
				}
			}

			iterator.increment(iterator_error);
			if (iterator_error)
			{
				AddDiagnostic(result.diagnostics,
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
