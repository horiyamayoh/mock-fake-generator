#include "generator/GeneratedFormatter.h"

#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include <clang/Format/Format.h>
#include <clang/Tooling/Core/Replacement.h>
#include <llvm/Support/Error.h>

namespace mockfakegen
{
	namespace
	{
		[[nodiscard]] bool IsCxxGeneratedFile(GeneratedFileKind kind) noexcept
		{
			switch (kind)
			{
				case GeneratedFileKind::RuntimeHeader:
				case GeneratedFileKind::MockHeader:
				case GeneratedFileKind::FakeSource:
				case GeneratedFileKind::AllMocksHeader:
					return true;
				case GeneratedFileKind::CMakeFragment:
				case GeneratedFileKind::Manifest:
				case GeneratedFileKind::Report:
					return false;
			}

			return false;
		}

		[[nodiscard]] std::string ClangStyleName(FormatStyleKind style)
		{
			switch (style)
			{
				case FormatStyleKind::File:
					return "file";
				case FormatStyleKind::Llvm:
					return "LLVM";
				case FormatStyleKind::Google:
					return "Google";
				case FormatStyleKind::None:
					break;
			}

			return "none";
		}

		[[nodiscard]] std::filesystem::path FormatLookupPath(const GeneratedFormatOptions& options,
															 const GeneratedFile& file)
		{
			if (options.style_search_root.empty())
			{
				return file.relative_path;
			}
			return options.style_search_root / file.relative_path;
		}

		void
		AddDiagnostic(GeneratedFormatResult& result, const GeneratedFile& file, std::string message)
		{
			result.diagnostics.push_back(GeneratedFormatDiagnostic{
				.path = file.relative_path,
				.message = std::move(message),
			});
		}

		[[nodiscard]] GeneratedFile NormalizedCopy(const GeneratedFile& file, std::string content)
		{
			return MakeGeneratedFile(
				file.relative_path, std::move(content), file.kind, file.source_class);
		}

		[[nodiscard]] std::string FormatOneFile(const GeneratedFormatOptions& options,
												const GeneratedFile& file,
												GeneratedFormatResult& result)
		{
			if (file.content.size() > std::numeric_limits<unsigned>::max())
			{
				AddDiagnostic(result, file, "generated file is too large to format.");
				return file.content;
			}

			const auto lookup_path = FormatLookupPath(options, file);
			const auto lookup_path_text = lookup_path.generic_string();
			auto style = clang::format::getStyle(
				ClangStyleName(options.style), lookup_path_text, "LLVM", file.content);
			if (!style)
			{
				AddDiagnostic(result,
							  file,
							  "failed to resolve clang-format style: " +
								  llvm::toString(style.takeError()));
				return file.content;
			}

			auto code = file.content;
			const auto sort_range_length = static_cast<unsigned>(code.size());
			const std::vector<clang::tooling::Range> sort_ranges = {
				clang::tooling::Range(0U, sort_range_length),
			};
			const auto include_replacements =
				clang::format::sortIncludes(*style, code, sort_ranges, lookup_path_text);
			auto include_sorted = clang::tooling::applyAllReplacements(code, include_replacements);
			if (!include_sorted)
			{
				AddDiagnostic(result,
							  file,
							  "failed to apply clang-format include sorting: " +
								  llvm::toString(include_sorted.takeError()));
				return file.content;
			}

			code = std::move(*include_sorted);
			const auto format_range_length = static_cast<unsigned>(code.size());
			const std::vector<clang::tooling::Range> format_ranges = {
				clang::tooling::Range(0U, format_range_length),
			};
			bool incomplete_format = false;
			const auto replacements = clang::format::reformat(
				*style, code, format_ranges, lookup_path_text, &incomplete_format);
			if (incomplete_format)
			{
				AddDiagnostic(result, file, "clang-format could not completely format this file.");
				return file.content;
			}

			auto formatted = clang::tooling::applyAllReplacements(code, replacements);
			if (!formatted)
			{
				AddDiagnostic(result,
							  file,
							  "failed to apply clang-format replacements: " +
								  llvm::toString(formatted.takeError()));
				return file.content;
			}

			return std::move(*formatted);
		}
	} // namespace

	GeneratedFormatResult FormatGeneratedFiles(const GeneratedFormatOptions& options,
											   std::span<const GeneratedFile> files)
	{
		GeneratedFormatResult result;
		result.files.reserve(files.size());

		for (const auto& file : files)
		{
			if (options.style == FormatStyleKind::None || !IsCxxGeneratedFile(file.kind))
			{
				result.files.push_back(NormalizedCopy(file, file.content));
				continue;
			}

			result.files.push_back(NormalizedCopy(file, FormatOneFile(options, file, result)));
		}

		SortGeneratedFiles(result.files);
		return result;
	}
} // namespace mockfakegen
