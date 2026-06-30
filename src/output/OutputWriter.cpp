#include "output/OutputWriter.h"

#include <fstream>
#include <sstream>
#include <system_error>

namespace mockfakegen
{
	namespace
	{
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

		[[nodiscard]] bool WriteText(const std::filesystem::path& path, const std::string& content)
		{
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				return false;
			}

			stream << content;
			stream.close();
			return stream.good();
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

			if (!WriteText(output_path, file.content))
			{
				AddDiagnostic(result, output_path, "failed to write output file.");
				AddFileResult(result, output_path, file.kind, OutputWriteStatus::Failed);
				continue;
			}

			AddFileResult(result, output_path, file.kind, OutputWriteStatus::Written);
		}

		return result;
	}
} // namespace mockfakegen
