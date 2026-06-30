#include "output/OutputWriter.h"

#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace mockfakegen
{
	namespace
	{
		struct AtomicWriteResult
		{
			bool ok = false;
			OutputWriteStatus failure_status = OutputWriteStatus::Failed;
			std::string message;
		};

		[[nodiscard]] AtomicWriteResult AtomicWriteSuccess()
		{
			return AtomicWriteResult{
				.ok = true,
				.failure_status = OutputWriteStatus::Failed,
				.message = {},
			};
		}

		[[nodiscard]] AtomicWriteResult
		AtomicWriteFailure(std::string message,
						   OutputWriteStatus status = OutputWriteStatus::Failed)
		{
			return AtomicWriteResult{
				.ok = false,
				.failure_status = status,
				.message = std::move(message),
			};
		}

		void
		AddDiagnostic(OutputWriteResult& result, std::filesystem::path path, std::string message)
		{
			result.diagnostics.push_back(
				OutputWriteDiagnostic{.path = std::move(path), .message = std::move(message)});
		}

		void AddFileResult(OutputWriteResult& result,
						   const std::filesystem::path& path,
						   GeneratedFileKind kind,
						   OutputWriteStatus status)
		{
			result.files.push_back(OutputFileResult{.path = path, .kind = kind, .status = status});
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

		[[nodiscard]] std::filesystem::path
		TemporaryPathFor(const std::filesystem::path& output_path)
		{
			const auto temporary_name = "." + output_path.filename().string() + ".mockfakegen.tmp";
			return output_path.parent_path() / temporary_name;
		}

		void RemoveTemporaryFile(const std::filesystem::path& path)
		{
			std::error_code remove_error;
			std::filesystem::remove(path, remove_error);
		}

		[[nodiscard]] AtomicWriteResult PrepareTemporaryPath(const std::filesystem::path& path)
		{
			std::error_code status_error;
			const auto status = std::filesystem::symlink_status(path, status_error);
			if (status.type() == std::filesystem::file_type::not_found)
			{
				return AtomicWriteSuccess();
			}

			if (status_error)
			{
				return AtomicWriteFailure("failed to inspect temporary output path: " +
										  status_error.message());
			}

			if (!std::filesystem::exists(status))
			{
				return AtomicWriteSuccess();
			}

			if (std::filesystem::is_directory(status))
			{
				return AtomicWriteSuccess();
			}

			if (!std::filesystem::is_regular_file(status))
			{
				return AtomicWriteFailure(
					"temporary output path already exists and is not a regular file.");
			}

			std::error_code remove_error;
			std::filesystem::remove(path, remove_error);
			if (remove_error)
			{
				return AtomicWriteFailure("failed to remove stale temporary output file: " +
										  remove_error.message());
			}

			return AtomicWriteSuccess();
		}

		[[nodiscard]] AtomicWriteResult WriteTemporaryFile(const std::filesystem::path& path,
														   const std::string& content)
		{
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				return AtomicWriteFailure("failed to open temporary output file for writing.");
			}

			stream.write(content.data(), static_cast<std::streamsize>(content.size()));
			if (!stream)
			{
				RemoveTemporaryFile(path);
				return AtomicWriteFailure("failed to write temporary output file.");
			}

			stream.flush();
			if (!stream)
			{
				RemoveTemporaryFile(path);
				return AtomicWriteFailure("failed to flush temporary output file.");
			}

			stream.close();
			if (!stream)
			{
				RemoveTemporaryFile(path);
				return AtomicWriteFailure("failed to close temporary output file.");
			}

			return AtomicWriteSuccess();
		}

		[[nodiscard]] AtomicWriteResult
		ReplaceWithTemporaryFile(const std::filesystem::path& output_path,
								 const std::filesystem::path& temporary_path,
								 bool allow_replace)
		{
			if (!allow_replace)
			{
				std::error_code exists_error;
				const auto exists = std::filesystem::exists(output_path, exists_error);
				if (exists_error)
				{
					RemoveTemporaryFile(temporary_path);
					return AtomicWriteFailure("failed to inspect output file before rename: " +
											  exists_error.message());
				}

				if (exists)
				{
					RemoveTemporaryFile(temporary_path);
					return AtomicWriteFailure(
						"output file already exists before rename; pass --overwrite to replace it.",
						OutputWriteStatus::SkippedExisting);
				}
			}

			std::error_code rename_error;
			std::filesystem::rename(temporary_path, output_path, rename_error);
			if (rename_error)
			{
				RemoveTemporaryFile(temporary_path);
				return AtomicWriteFailure("failed to rename temporary output file into place: " +
										  rename_error.message());
			}

			return AtomicWriteSuccess();
		}

		[[nodiscard]] AtomicWriteResult
		WriteTextAtomically(const std::filesystem::path& output_path,
							const std::string& content,
							bool allow_replace)
		{
			const auto temporary_path = TemporaryPathFor(output_path);
			const auto prepare_result = PrepareTemporaryPath(temporary_path);
			if (!prepare_result.ok)
			{
				return prepare_result;
			}

			const auto write_result = WriteTemporaryFile(temporary_path, content);
			if (!write_result.ok)
			{
				return write_result;
			}

			return ReplaceWithTemporaryFile(output_path, temporary_path, allow_replace);
		}
	} // namespace

	OutputWriteResult WriteGeneratedFiles(const OutputWriterOptions& options,
										  std::span<const GeneratedFile> files)
	{
		OutputWriteResult result;

		if (options.dry_run)
		{
			for (const auto& file : files)
			{
				AddFileResult(result,
							  options.output_dir / file.relative_path,
							  file.kind,
							  OutputWriteStatus::Planned);
			}
			return result;
		}

		std::error_code create_error;
		std::filesystem::create_directories(options.output_dir, create_error);
		if (create_error)
		{
			AddDiagnostic(result,
						  options.output_dir,
						  "failed to create output directory: " + create_error.message());
			for (const auto& file : files)
			{
				AddFileResult(result,
							  options.output_dir / file.relative_path,
							  file.kind,
							  OutputWriteStatus::Failed);
			}
			return result;
		}

		for (const auto& file : files)
		{
			const auto output_path = options.output_dir / file.relative_path;

			std::error_code parent_error;
			std::filesystem::create_directories(output_path.parent_path(), parent_error);
			if (parent_error)
			{
				AddDiagnostic(result,
							  output_path,
							  "failed to create parent directory: " + parent_error.message());
				AddFileResult(result, output_path, file.kind, OutputWriteStatus::Failed);
				continue;
			}

			std::error_code exists_error;
			const auto exists = std::filesystem::exists(output_path, exists_error);
			if (exists_error)
			{
				AddDiagnostic(result,
							  output_path,
							  "failed to inspect existing output file: " + exists_error.message());
				AddFileResult(result, output_path, file.kind, OutputWriteStatus::Failed);
				continue;
			}

			if (exists)
			{
				std::string existing_content;
				if (ReadText(output_path, existing_content) && existing_content == file.content)
				{
					AddFileResult(result, output_path, file.kind, OutputWriteStatus::Unchanged);
					continue;
				}

				if (!options.overwrite)
				{
					AddDiagnostic(result,
								  output_path,
								  "output file already exists; pass --overwrite to replace it.");
					AddFileResult(
						result, output_path, file.kind, OutputWriteStatus::SkippedExisting);
					continue;
				}
			}

			const auto write_result =
				WriteTextAtomically(output_path, file.content, options.overwrite);
			if (!write_result.ok)
			{
				AddDiagnostic(result, output_path, write_result.message);
				AddFileResult(result, output_path, file.kind, write_result.failure_status);
				continue;
			}

			AddFileResult(result, output_path, file.kind, OutputWriteStatus::Written);
		}

		return result;
	}
} // namespace mockfakegen
