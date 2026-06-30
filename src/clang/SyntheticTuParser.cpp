#include "clang/SyntheticTuParser.h"

#include <algorithm>
#include <string>
#include <system_error>

#include <clang/Frontend/TextDiagnosticBuffer.h>
#include <clang/Tooling/Tooling.h>

namespace mockfakegen
{
	namespace
	{
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

		[[nodiscard]] std::string IncludeSpelling(const std::filesystem::path& header_path,
												  const std::filesystem::path& project_root)
		{
			std::error_code relative_error;
			const auto relative =
				std::filesystem::relative(header_path, project_root, relative_error);
			if (!relative_error)
			{
				return relative.lexically_normal().generic_string();
			}

			return header_path.filename().generic_string();
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
	} // namespace

	std::string BuildSyntheticTuCode(std::string include_spelling)
	{
		return "#include \"" + include_spelling + "\"\n";
	}

	std::vector<std::string> BuildSyntheticTuFallbackArgs(const std::filesystem::path& project_root)
	{
		return {
			"-std=c++23",
			"-I" + AbsoluteNormalized(project_root).generic_string(),
		};
	}

	SyntheticTuParseResult ParseHeaderWithSyntheticTu(const SyntheticTuParseOptions& options)
	{
		SyntheticTuParseResult result;
		const auto header_path = AbsoluteNormalized(options.header_path);
		const auto project_root = AbsoluteNormalized(options.project_root);
		const auto include_spelling = IncludeSpelling(header_path, project_root);

		result.header = HeaderParseRecord{
			.header_path = header_path,
			.include_spelling = include_spelling,
			.parsed_by_synthetic_tu = true,
		};
		result.compile_args = options.compile_args.empty()
			? BuildSyntheticTuFallbackArgs(project_root)
			: options.compile_args;
		result.synthetic_code = BuildSyntheticTuCode(include_spelling);

		clang::TextDiagnosticBuffer diagnostic_buffer;
		result.ast = clang::tooling::buildASTFromCodeWithArgs(
			result.synthetic_code,
			result.compile_args,
			"mockfakegen_synthetic_tu.cpp",
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

		result.success = result.ast != nullptr && !HasErrorDiagnostic(result.diagnostics);
		return result;
	}
} // namespace mockfakegen
