#include "output/OutputWriter.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace mockfakegen
{
	namespace
	{
		constexpr std::string_view kStagingDirectoryName = ".mockfakegen-staging";

		struct PathValidationResult
		{
			bool ok = false;
			std::filesystem::path normalized_path;
			std::string message;
		};

		struct PlanEntry
		{
			std::size_t result_index = 0U;
			const GeneratedFile* file = nullptr;
			std::filesystem::path relative_path;
			std::filesystem::path output_path;
			std::filesystem::path staged_path;
			std::filesystem::path backup_path;
			bool publish = false;
			bool had_existing = false;
			bool backed_up = false;
			bool published = false;
		};

		void AddDiagnostic(OutputWriteResult& result,
						   std::filesystem::path path,
						   std::string code,
						   std::string kind,
						   std::string message)
		{
			result.diagnostics.push_back(OutputWriteDiagnostic{
				.severity = DiagnosticSeverity::Error,
				.code = std::move(code),
				.kind = std::move(kind),
				.path = std::move(path),
				.message = std::move(message),
			});
		}

		void AddFileResult(OutputWriteResult& result,
						   std::filesystem::path path,
						   GeneratedFileKind kind,
						   OutputWriteStatus status)
		{
			result.files.push_back(OutputFileResult{
				.path = std::move(path),
				.kind = kind,
				.status = status,
			});
		}

		[[nodiscard]] bool IsParentReference(const std::filesystem::path& component)
		{
			return component == "..";
		}

		[[nodiscard]] bool IsCurrentReference(const std::filesystem::path& component)
		{
			return component == ".";
		}

		[[nodiscard]] bool HasRootEscape(const std::filesystem::path& path)
		{
			return path.is_absolute() || path.has_root_path() || path.has_root_name() ||
				path.has_root_directory();
		}

		[[nodiscard]] PathValidationResult
		ValidateGeneratedPath(const std::filesystem::path& raw_path)
		{
			if (raw_path.empty() || raw_path.generic_string().empty())
			{
				return PathValidationResult{
					.ok = false,
					.normalized_path = {},
					.message = "generated output path is empty.",
				};
			}

			if (HasRootEscape(raw_path))
			{
				return PathValidationResult{
					.ok = false,
					.normalized_path = {},
					.message =
						"generated output path must be relative and must not contain a root name.",
				};
			}

			for (const auto& component : raw_path)
			{
				if (IsParentReference(component))
				{
					return PathValidationResult{
						.ok = false,
						.normalized_path = {},
						.message = "generated output path must not contain '..' traversal.",
					};
				}
			}

			const auto normalized_path = raw_path.lexically_normal();
			if (normalized_path.empty() || IsCurrentReference(normalized_path) ||
				normalized_path.filename().empty())
			{
				return PathValidationResult{
					.ok = false,
					.normalized_path = {},
					.message = "generated output path must name a file.",
				};
			}

			if (HasRootEscape(normalized_path))
			{
				return PathValidationResult{
					.ok = false,
					.normalized_path = {},
					.message = "normalized generated output path escapes the output directory.",
				};
			}

			bool first_component = true;
			for (const auto& component : normalized_path)
			{
				if (IsParentReference(component))
				{
					return PathValidationResult{
						.ok = false,
						.normalized_path = {},
						.message = "normalized generated output path escapes the output directory.",
					};
				}
				if (first_component && component == kStagingDirectoryName)
				{
					return PathValidationResult{
						.ok = false,
						.normalized_path = {},
						.message = "generated output path uses a reserved writer staging path.",
					};
				}
				first_component = false;
			}

			return PathValidationResult{
				.ok = true,
				.normalized_path = normalized_path,
				.message = {},
			};
		}

		[[nodiscard]] bool IsSameOrInsideRoot(const std::filesystem::path& root,
											  const std::filesystem::path& path)
		{
			const auto relative = path.lexically_relative(root);
			if (relative.empty() || relative.has_root_path())
			{
				return false;
			}
			if (IsCurrentReference(relative))
			{
				return true;
			}
			return std::none_of(relative.begin(), relative.end(), IsParentReference);
		}

		[[nodiscard]] bool IsInsideRoot(const std::filesystem::path& root,
										const std::filesystem::path& path)
		{
			const auto relative = path.lexically_relative(root);
			if (relative.empty() || IsCurrentReference(relative) || relative.has_root_path())
			{
				return false;
			}
			return std::none_of(relative.begin(), relative.end(), IsParentReference);
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

		[[nodiscard]] bool WriteTextFile(const std::filesystem::path& path,
										 const std::string& content,
										 std::string& message)
		{
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				message = "failed to open staged output file for writing.";
				return false;
			}

			stream.write(content.data(), static_cast<std::streamsize>(content.size()));
			if (!stream)
			{
				message = "failed to write staged output file.";
				return false;
			}

			stream.flush();
			if (!stream)
			{
				message = "failed to flush staged output file.";
				return false;
			}

			stream.close();
			if (!stream)
			{
				message = "failed to close staged output file.";
				return false;
			}

			return true;
		}

		void MarkPublishEntriesFailed(OutputWriteResult& result, std::vector<PlanEntry>& plan)
		{
			for (auto& entry : plan)
			{
				if (entry.publish)
				{
					result.files[entry.result_index].status = OutputWriteStatus::Failed;
				}
			}
		}

		[[nodiscard]] std::string SourceClassKey(const PlanEntry& entry)
		{
			if (entry.file == nullptr || !entry.file->source_class.has_value())
			{
				return {};
			}
			return entry.file->source_class->qualified_name;
		}

		void SuppressBlockedSourceClasses(OutputWriteResult& result,
										  std::vector<PlanEntry>& plan,
										  const std::set<std::string>& blocked_source_classes)
		{
			if (blocked_source_classes.empty())
			{
				return;
			}

			for (auto& entry : plan)
			{
				const auto source_class_key = SourceClassKey(entry);
				if (source_class_key.empty() ||
					blocked_source_classes.find(source_class_key) == blocked_source_classes.end())
				{
					continue;
				}

				entry.publish = false;
				auto& status = result.files[entry.result_index].status;
				if (status != OutputWriteStatus::SkippedExisting &&
					status != OutputWriteStatus::Unchanged)
				{
					status = OutputWriteStatus::Failed;
				}
			}
		}

		void BlockSourceClassIfPresent(const PlanEntry& entry,
									   std::set<std::string>& blocked_source_classes)
		{
			const auto source_class_key = SourceClassKey(entry);
			if (!source_class_key.empty())
			{
				blocked_source_classes.insert(source_class_key);
			}
		}

		[[nodiscard]] std::filesystem::path AbsoluteOutputRoot(const OutputWriterOptions& options,
															   OutputWriteResult& result)
		{
			if (options.output_dir.empty())
			{
				AddDiagnostic(result,
							  options.output_dir,
							  "output_path_invalid",
							  "empty_output_dir",
							  "output directory is empty.");
				return {};
			}

			std::error_code absolute_error;
			auto output_root = std::filesystem::absolute(options.output_dir, absolute_error);
			if (absolute_error)
			{
				AddDiagnostic(result,
							  options.output_dir,
							  "output_path_invalid",
							  "output_dir_normalization",
							  "failed to normalize output directory: " + absolute_error.message());
				return {};
			}

			return output_root.lexically_normal();
		}

		[[nodiscard]] std::vector<PlanEntry> BuildPathPlan(const OutputWriterOptions& options,
														   std::span<const GeneratedFile> files,
														   const std::filesystem::path& output_root,
														   OutputWriteResult& result)
		{
			std::vector<PlanEntry> plan;
			plan.reserve(files.size());
			std::set<std::string> output_paths;
			for (const auto& file : files)
			{
				const auto result_index = result.files.size();
				AddFileResult(result, file.relative_path, file.kind, OutputWriteStatus::Failed);

				const auto path_result = ValidateGeneratedPath(file.relative_path);
				if (!path_result.ok)
				{
					const auto diagnostic_path =
						file.relative_path.empty() ? output_root : file.relative_path;
					AddDiagnostic(result,
								  diagnostic_path,
								  "output_path_invalid",
								  "generated_path",
								  path_result.message);
					continue;
				}

				const auto output_path =
					(output_root / path_result.normalized_path).lexically_normal();
				result.files[result_index].path = output_path;
				if (!IsInsideRoot(output_root, output_path))
				{
					AddDiagnostic(result,
								  output_path,
								  "output_path_invalid",
								  "normalized_path",
								  "normalized generated output path escapes the output directory.");
					continue;
				}

				const auto [_, inserted] = output_paths.insert(output_path.generic_string());
				if (!inserted)
				{
					AddDiagnostic(result,
								  output_path,
								  "output_path_invalid",
								  "duplicate_output_path",
								  "multiple generated files resolve to the same output path.");
					continue;
				}

				if (options.dry_run)
				{
					result.files[result_index].status = OutputWriteStatus::Planned;
				}

				plan.push_back(PlanEntry{
					.result_index = result_index,
					.file = &file,
					.relative_path = path_result.normalized_path,
					.output_path = output_path,
					.staged_path = {},
					.backup_path = {},
					.publish = false,
					.had_existing = false,
					.backed_up = false,
					.published = false,
				});
			}
			return plan;
		}

		[[nodiscard]] bool CreateOutputRoot(const std::filesystem::path& output_root,
											std::span<const PlanEntry> plan,
											OutputWriteResult& result)
		{
			std::error_code create_error;
			std::filesystem::create_directories(output_root, create_error);
			if (!create_error)
			{
				return true;
			}

			AddDiagnostic(result,
						  output_root,
						  "output_directory_failure",
						  "create_output_dir",
						  "failed to create output directory: " + create_error.message());
			for (const auto& entry : plan)
			{
				result.files[entry.result_index].status = OutputWriteStatus::Failed;
			}
			return false;
		}

		[[nodiscard]] bool CanonicalPathInsideRoot(const std::filesystem::path& canonical_root,
												   const std::filesystem::path& path,
												   OutputWriteResult& result)
		{
			auto existing_path = path;
			while (!existing_path.empty())
			{
				std::error_code status_error;
				const auto status = std::filesystem::symlink_status(existing_path, status_error);
				if (status_error && status.type() != std::filesystem::file_type::not_found)
				{
					AddDiagnostic(result,
								  existing_path,
								  "output_path_invalid",
								  "canonical_path",
								  "failed to inspect output path inside output directory: " +
									  status_error.message());
					return false;
				}

				if (status.type() != std::filesystem::file_type::not_found)
				{
					break;
				}

				const auto parent_path = existing_path.parent_path();
				if (parent_path == existing_path)
				{
					break;
				}
				existing_path = parent_path;
			}

			if (existing_path.empty())
			{
				AddDiagnostic(result,
							  path,
							  "output_path_invalid",
							  "canonical_path",
							  "failed to find an existing output path ancestor.");
				return false;
			}

			std::error_code canonical_error;
			const auto canonical_path =
				std::filesystem::weakly_canonical(existing_path, canonical_error);
			if (canonical_error)
			{
				AddDiagnostic(result,
							  existing_path,
							  "output_path_invalid",
							  "canonical_path",
							  "failed to resolve output path inside output directory: " +
								  canonical_error.message());
				return false;
			}

			if (!IsSameOrInsideRoot(canonical_root, canonical_path.lexically_normal()))
			{
				AddDiagnostic(result,
							  path,
							  "output_path_invalid",
							  "canonical_path_escape",
							  "output path resolves outside the output directory.");
				return false;
			}

			return true;
		}

		[[nodiscard]] bool PrepareOutputParents(const std::filesystem::path& output_root,
												std::vector<PlanEntry>& plan,
												OutputWriteResult& result)
		{
			std::error_code canonical_error;
			const auto canonical_root =
				std::filesystem::weakly_canonical(output_root, canonical_error).lexically_normal();
			if (canonical_error)
			{
				AddDiagnostic(result,
							  output_root,
							  "output_directory_failure",
							  "canonical_output_dir",
							  "failed to resolve output directory: " + canonical_error.message());
				MarkPublishEntriesFailed(result, plan);
				return false;
			}

			bool ok = true;
			for (auto& entry : plan)
			{
				const auto parent_path = entry.output_path.parent_path();
				if (!CanonicalPathInsideRoot(canonical_root, parent_path, result))
				{
					ok = false;
					continue;
				}

				std::error_code parent_error;
				std::filesystem::create_directories(parent_path, parent_error);
				if (parent_error)
				{
					AddDiagnostic(result,
								  parent_path,
								  "output_directory_failure",
								  "create_parent_dir",
								  "failed to create output parent directory: " +
									  parent_error.message());
					ok = false;
					continue;
				}

				if (!CanonicalPathInsideRoot(canonical_root, parent_path, result))
				{
					ok = false;
				}
			}

			if (!ok)
			{
				MarkPublishEntriesFailed(result, plan);
			}
			return ok;
		}

		[[nodiscard]] bool CheckExistingOutputs(const OutputWriterOptions& options,
												std::vector<PlanEntry>& plan,
												OutputWriteResult& result)
		{
			std::set<std::string> blocked_source_classes;
			for (auto& entry : plan)
			{
				std::error_code status_error;
				const auto status =
					std::filesystem::symlink_status(entry.output_path, status_error);
				if (status_error && status.type() != std::filesystem::file_type::not_found)
				{
					AddDiagnostic(result,
								  entry.output_path,
								  "output_conflict",
								  "inspect_existing",
								  "failed to inspect existing output file: " +
									  status_error.message());
					BlockSourceClassIfPresent(entry, blocked_source_classes);
					continue;
				}

				if (status.type() == std::filesystem::file_type::not_found)
				{
					entry.publish = true;
					continue;
				}

				if (std::filesystem::is_symlink(status))
				{
					AddDiagnostic(result,
								  entry.output_path,
								  "output_conflict",
								  "existing_symlink",
								  "output path already exists as a symbolic link.");
					BlockSourceClassIfPresent(entry, blocked_source_classes);
					continue;
				}

				if (!std::filesystem::is_regular_file(status))
				{
					AddDiagnostic(result,
								  entry.output_path,
								  "output_conflict",
								  "existing_non_regular",
								  "output path already exists and is not a regular file.");
					BlockSourceClassIfPresent(entry, blocked_source_classes);
					continue;
				}

				std::string existing_content;
				if (!ReadText(entry.output_path, existing_content))
				{
					AddDiagnostic(result,
								  entry.output_path,
								  "output_conflict",
								  "read_existing",
								  "failed to read existing output file.");
					BlockSourceClassIfPresent(entry, blocked_source_classes);
					continue;
				}

				if (existing_content == entry.file->content)
				{
					result.files[entry.result_index].status = OutputWriteStatus::Unchanged;
					continue;
				}

				if (!options.overwrite)
				{
					AddDiagnostic(result,
								  entry.output_path,
								  "output_conflict",
								  "existing_changed_file",
								  "output file already exists; pass --overwrite to replace it.");
					result.files[entry.result_index].status = OutputWriteStatus::SkippedExisting;
					BlockSourceClassIfPresent(entry, blocked_source_classes);
					continue;
				}

				entry.publish = true;
				entry.had_existing = true;
			}

			SuppressBlockedSourceClasses(result, plan, blocked_source_classes);
			return true;
		}

		[[nodiscard]] bool PrepareStagingRoot(const std::filesystem::path& staging_root,
											  OutputWriteResult& result)
		{
			std::error_code remove_error;
			std::filesystem::remove_all(staging_root, remove_error);
			if (remove_error)
			{
				AddDiagnostic(result,
							  staging_root,
							  "output_staging_failure",
							  "remove_stale_staging",
							  "failed to remove stale staging directory: " +
								  remove_error.message());
				return false;
			}

			std::error_code create_error;
			std::filesystem::create_directories(staging_root / "files", create_error);
			if (create_error)
			{
				AddDiagnostic(result,
							  staging_root,
							  "output_staging_failure",
							  "create_staging_dir",
							  "failed to create staging directory: " + create_error.message());
				return false;
			}

			return true;
		}

		void RemoveStagingRoot(const std::filesystem::path& staging_root)
		{
			std::error_code remove_error;
			std::filesystem::remove_all(staging_root, remove_error);
		}

		[[nodiscard]] bool StageFiles(const std::filesystem::path& staging_root,
									  std::vector<PlanEntry>& plan,
									  OutputWriteResult& result)
		{
			if (!PrepareStagingRoot(staging_root, result))
			{
				MarkPublishEntriesFailed(result, plan);
				return false;
			}

			bool ok = true;
			for (auto& entry : plan)
			{
				if (!entry.publish)
				{
					continue;
				}

				entry.staged_path = staging_root / "files" / entry.relative_path;
				std::error_code parent_error;
				std::filesystem::create_directories(entry.staged_path.parent_path(), parent_error);
				if (parent_error)
				{
					AddDiagnostic(result,
								  entry.staged_path.parent_path(),
								  "output_staging_failure",
								  "create_staged_parent",
								  "failed to create staged output parent directory: " +
									  parent_error.message());
					ok = false;
					continue;
				}

				std::string write_message;
				if (!WriteTextFile(entry.staged_path, entry.file->content, write_message))
				{
					AddDiagnostic(result,
								  entry.staged_path,
								  "output_staging_failure",
								  "write_staged_file",
								  write_message);
					ok = false;
				}
			}

			if (!ok)
			{
				MarkPublishEntriesFailed(result, plan);
				RemoveStagingRoot(staging_root);
				return false;
			}

			return true;
		}

		void RollbackPublishedFiles(std::vector<PlanEntry>& plan, OutputWriteResult& result)
		{
			for (auto entry = plan.rbegin(); entry != plan.rend(); ++entry)
			{
				if (!entry->publish)
				{
					continue;
				}

				if (entry->published)
				{
					std::error_code remove_error;
					std::filesystem::remove(entry->output_path, remove_error);
					if (remove_error)
					{
						AddDiagnostic(result,
									  entry->output_path,
									  "output_rollback_failure",
									  "remove_published_file",
									  "failed to remove published output during rollback: " +
										  remove_error.message());
					}
				}

				if (entry->backed_up)
				{
					std::error_code restore_error;
					std::filesystem::rename(entry->backup_path, entry->output_path, restore_error);
					if (restore_error)
					{
						AddDiagnostic(result,
									  entry->output_path,
									  "output_rollback_failure",
									  "restore_backup_file",
									  "failed to restore backup during rollback: " +
										  restore_error.message());
					}
				}
			}
		}

		[[nodiscard]] bool OutputPathStillPublishable(const PlanEntry& entry,
													  OutputWriteResult& result)
		{
			std::error_code exists_error;
			const auto exists = std::filesystem::exists(entry.output_path, exists_error);
			if (exists_error)
			{
				AddDiagnostic(result,
							  entry.output_path,
							  "output_publish_failure",
							  "inspect_before_publish",
							  "failed to inspect output path before publish: " +
								  exists_error.message());
				return false;
			}

			if (exists)
			{
				AddDiagnostic(result,
							  entry.output_path,
							  "output_publish_failure",
							  "unexpected_existing_file",
							  "output path appeared after conflict checks.");
				return false;
			}

			return true;
		}

		[[nodiscard]] bool PublishStagedFiles(const std::filesystem::path& staging_root,
											  std::vector<PlanEntry>& plan,
											  OutputWriteResult& result)
		{
			const auto backup_root = staging_root / "backup";
			for (auto& entry : plan)
			{
				if (!entry.publish)
				{
					continue;
				}

				if (entry.had_existing)
				{
					entry.backup_path = backup_root / entry.relative_path;
					std::error_code backup_parent_error;
					std::filesystem::create_directories(entry.backup_path.parent_path(),
														backup_parent_error);
					if (backup_parent_error)
					{
						AddDiagnostic(result,
									  entry.backup_path.parent_path(),
									  "output_publish_failure",
									  "create_backup_parent",
									  "failed to create backup parent directory: " +
										  backup_parent_error.message());
						MarkPublishEntriesFailed(result, plan);
						RollbackPublishedFiles(plan, result);
						return false;
					}

					std::error_code backup_error;
					std::filesystem::rename(entry.output_path, entry.backup_path, backup_error);
					if (backup_error)
					{
						AddDiagnostic(result,
									  entry.output_path,
									  "output_publish_failure",
									  "backup_existing_file",
									  "failed to move existing output into backup: " +
										  backup_error.message());
						MarkPublishEntriesFailed(result, plan);
						RollbackPublishedFiles(plan, result);
						return false;
					}
					entry.backed_up = true;
				}

				if (!OutputPathStillPublishable(entry, result))
				{
					MarkPublishEntriesFailed(result, plan);
					RollbackPublishedFiles(plan, result);
					return false;
				}

				std::error_code publish_error;
				std::filesystem::rename(entry.staged_path, entry.output_path, publish_error);
				if (publish_error)
				{
					AddDiagnostic(result,
								  entry.output_path,
								  "output_publish_failure",
								  "publish_staged_file",
								  "failed to publish staged output file: " +
									  publish_error.message());
					MarkPublishEntriesFailed(result, plan);
					RollbackPublishedFiles(plan, result);
					return false;
				}
				entry.published = true;
			}

			for (const auto& entry : plan)
			{
				if (entry.publish)
				{
					result.files[entry.result_index].status = OutputWriteStatus::Written;
				}
			}

			RemoveStagingRoot(staging_root);
			return true;
		}
	} // namespace

	OutputWriteResult WriteGeneratedFiles(const OutputWriterOptions& options,
										  std::span<const GeneratedFile> files)
	{
		OutputWriteResult result;
		const auto output_root = AbsoluteOutputRoot(options, result);
		if (output_root.empty())
		{
			for (const auto& file : files)
			{
				AddFileResult(result, file.relative_path, file.kind, OutputWriteStatus::Failed);
			}
			return result;
		}

		auto plan = BuildPathPlan(options, files, output_root, result);
		if (options.dry_run || !result.ok())
		{
			return result;
		}

		if (!CreateOutputRoot(output_root, plan, result))
		{
			return result;
		}

		if (!PrepareOutputParents(output_root, plan, result))
		{
			return result;
		}

		if (!CheckExistingOutputs(options, plan, result))
		{
			return result;
		}
		if (!result.ok())
		{
			MarkPublishEntriesFailed(result, plan);
			return result;
		}

		const auto has_files_to_publish = std::any_of(plan.begin(),
													  plan.end(),
													  [](const auto& entry)
													  {
														  return entry.publish;
													  });
		if (!has_files_to_publish)
		{
			return result;
		}

		const auto staging_root = output_root / kStagingDirectoryName;
		if (!StageFiles(staging_root, plan, result))
		{
			return result;
		}

		if (!PublishStagedFiles(staging_root, plan, result))
		{
			RemoveStagingRoot(staging_root);
			return result;
		}

		return result;
	}
} // namespace mockfakegen
